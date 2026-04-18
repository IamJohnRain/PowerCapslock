# OCR 文字选择功能设计与开发计划

## 文档目的

这份文档用于指导后续开发者或大模型继续设计、优化和实现截图 OCR 文字选择功能。文档不假设执行者了解当前会话历史，因此会明确说明当前代码现状、目标架构、阶段边界、具体改动点和验收方案。

本文推荐的路线是：先用 mock OCR 数据实现悬浮窗口中的文字选择体验，再接入真实 OCR 引擎。这样可以把用户交互风险和 OCR 引擎依赖风险拆开处理。

## 总体目标

在截图悬浮窗口中提供 OCR 文字识别与浏览器式文字选择能力，让用户可以对截图中的文字进行悬停、拖选、复制。

核心体验：

- 用户通过截图工具触发 OCR 模式后，截图以悬浮窗口形式展示。
- OCR 结果按词保存坐标，鼠标移动到文字区域时显示 I 形光标。
- 用户可以拖拽选择一个或多个词，选区以半透明高亮显示。
- 用户可以通过 `Ctrl+C` 或右键菜单复制选中的文字。
- 后续可以替换真实 OCR 引擎，而不改动悬浮窗口选择交互。

## 当前代码现状

相关模块：

- `src/screenshot_ocr.c/h`：目前是 OCR 占位实现，只返回整段 `fullText`，没有词级坐标。
- `src/screenshot_float.c/h`：目前负责挂起截图悬浮窗口，支持显示图片、拖拽窗口、右键菜单、双击关闭。
- `src/screenshot_manager.c`：截图工具栏动作中转层。当前 `TOOLBAR_BTN_OCR` 的 case 仍存在，但工具栏默认按钮列表里已经移除了 OCR 入口。
- `src/screenshot_toolbar.c/h`：截图工具栏按钮定义和绘制逻辑。近期已经新增颜色和文字字号按钮，工具栏宽度较之前更大。
- `src/screenshot_overlay.c/h`：负责截图选区、标注和导出图片。OCR 输入应优先从这里拿原始选区图像。

当前限制：

- OCR 没有真实识别，也没有 word bounding box。
- 悬浮窗口没有 OCR 模式和文字选择状态。
- 悬浮窗口当前绘制基本等价于“窗口大小等于图片大小”，但代码里已有窗口大小上限常量，未来一旦缩放就必须有统一 image layout。
- OCR 如果直接使用带标注后的截图，标注内容会干扰识别。

## 设计原则

1. 先实现可验证交互闭环，再接真实 OCR 引擎。

   第一阶段使用 mock OCR words 验证悬停、拖选、高亮、复制和右键菜单。真实 OCR 引擎留到后续阶段，避免 WinRT/Tesseract 构建和运行时依赖阻塞核心交互。

2. OCR 模式与普通挂起模式分离。

   普通 `ScreenshotFloatShow()` 继续保持“挂起截图”语义。新增 OCR 模式入口，例如 `ScreenshotFloatShowOcr()`，避免所有挂起截图都自动 OCR。

3. OCR 应使用原始截图。

   OCR 输入应优先使用 `ScreenshotOverlayGetSelectionImage()`，不是 `ScreenshotOverlayGetAnnotatedSelectionImage()`。用户画的箭头、文字、形状会影响识别结果。

4. OCR 结果只负责数据，文字选择只负责交互。

   `screenshot_ocr` 输出词级数据；`screenshot_float` 使用这些数据渲染 hover/selection，并负责复制。

5. UI 线程只接收 OCR 结果，不执行耗时识别。

   真实 OCR 接入后应在后台线程运行，完成后通过 `PostMessage` 回到悬浮窗口线程更新 UI 状态。

6. 坐标映射只允许有一个来源。

   图片绘制、OCR 框绘制、鼠标命中检测、选区高亮必须共用同一套 `CalculateImageLayout()` 结果，不能各自计算缩放和偏移。

## 推荐架构

### OCR 数据模型

建议将 `OCRResults` 从当前整段文本扩展为词级结果：

```c
typedef struct {
    char* text;
    RECT boundingBox;
    float confidence;
    BOOL isLineBreak;
} OCRWord;

typedef struct {
    OCRWord* words;
    int wordCount;
    char* fullText;
    int imageWidth;
    int imageHeight;
} OCRResults;
```

设计说明：

- `boundingBox` 使用原图像素坐标。
- `words` 按阅读顺序排列。
- `isLineBreak` 表示复制时该词后面应换行。
- `imageWidth/imageHeight` 用于校验 OCR 结果与当前图片是否匹配。
- `text` 建议统一使用 UTF-8 存储；复制到剪贴板时再转为 UTF-16。

### 悬浮窗口 OCR 状态

建议在 `FloatWindowContext` 中增加 OCR 模式状态：

```c
bool ocrMode;
OCRResults* ocrResults;
bool ocrInProgress;

int hoveredWordIndex;
int anchorWordIndex;
int activeWordIndex;
bool isSelectingText;

float imageScale;
POINT imageOffset;
SIZE imageDrawSize;

bool showCopiedToast;
DWORD toastStartTime;
```

关键点：

- 使用 `anchorWordIndex` 和 `activeWordIndex` 表示选择区间，不要在拖动过程中直接改写 start/end 作为锚点。
- 最终选区通过 `min(anchor, active)` 和 `max(anchor, active)` 动态计算。
- `imageScale/imageOffset/imageDrawSize` 必须由实际绘制图片的 layout 函数统一计算。
- `ocrMode=false` 时，悬浮窗口保持当前普通挂起截图行为。

### 新增入口 API

建议新增 OCR 专用浮窗入口：

```c
bool ScreenshotFloatShowOcr(const ScreenshotImage* image, int x, int y);
const char* ScreenshotFloatGetSelectedText(void);
void ScreenshotFloatClearSelection(void);
```

保留现有普通挂起入口：

```c
bool ScreenshotFloatShow(const ScreenshotImage* image, int x, int y);
```

这样普通挂起和 OCR 浮窗不会互相污染。

### Manager 入口调整

建议保留两条动作路径：

- 挂起：使用带标注截图或当前已有逻辑，调用 `ScreenshotFloatShow()`。
- OCR：使用原始选区截图，调用 `ScreenshotFloatShowOcr()`。

如果需要恢复 OCR 工具栏按钮，需要在 `screenshot_toolbar.c` 的默认按钮列表中重新加入 OCR 按钮；如果不恢复按钮，也可以后续增加快捷键或右键菜单入口。

## 面向后续模型的执行约束

后续模型实施时请遵守这些约束：

- 不要一次性接入真实 OCR、异步线程、悬浮窗口选择交互和工具栏入口。必须按阶段完成并验证。
- 不要让普通 `ScreenshotFloatShow()` 默认启用 OCR。
- 不要让 worker 线程直接修改 `g_float` 或调用 UI 绘制函数。
- 不要在 OCR 复制路径中假设 ASCII 文本。剪贴板必须输出 `CF_UNICODETEXT`。
- 不要用带标注的图片做 OCR 输入，除非用户明确选择“识别标注后图像”。
- 不要为了 OCR 入口破坏当前挂起、复制、保存、标注工具栏行为。
- 每个阶段完成后都要运行最小测试集并记录结果。

## 分阶段开发计划

### 阶段 0：准备与基线确认

#### 阶段目标

确认当前工作区、构建状态和截图相关测试状态，为后续分阶段改动建立基线。

#### 前置条件

- 当前仓库能正常构建。
- 明确当前工作区是否存在未提交改动。
- 确认是否需要先提交当前截图标注相关改动，避免与 OCR 功能混在一起。

#### 涉及文件

- 不应修改业务代码。
- 可以只读取：
  - `git status`
  - `CMakeLists.txt`
  - `src/screenshot_ocr.c/h`
  - `src/screenshot_float.c/h`
  - `src/screenshot_manager.c`
  - `src/screenshot_toolbar.c/h`

#### 具体操作

1. 查看工作区状态：

   ```powershell
   git status -sb
   ```

2. 构建项目：

   ```powershell
   cmake --build build
   ```

3. 运行当前截图相关测试：

   ```powershell
   build\powercapslock.exe --test-screenshot-all
   build\powercapslock.exe --test-screenshot-toolbar
   build\powercapslock.exe --test-screenshot-annotate
   build\powercapslock.exe --test-screenshot-float
   build\powercapslock.exe --test-screenshot-ocr
   ```

#### 验收方案

- 构建成功。
- 当前已有测试能通过，或明确记录失败项和原因。
- 没有测试残留的 `test_*.bmp` 文件。
- 没有残留运行中的 `powercapslock.exe` 锁住构建产物。

#### 风险与回滚

- 如果链接失败提示 `Permission denied`，通常是 `build\powercapslock.exe` 正在运行，先停掉进程再构建。
- 如果当前工作区已有未提交改动，不要回退；先记录改动来源和影响范围。

---

### 阶段 1：OCR 词级数据模型与 Mock 识别

#### 阶段目标

让 OCR 模块从“只返回整段文本”升级为“返回按阅读顺序排列的词级结果”，但暂不接真实 OCR 引擎。

这个阶段的产物用于驱动后续悬浮窗口选择交互。

#### 前置条件

- 阶段 0 构建基线已确认。
- 当前 `OCRResults` 的使用点已定位。

#### 涉及文件

- 修改：`src/screenshot_ocr.h`
- 修改：`src/screenshot_ocr.c`
- 可能修改：引用旧字段的调用方，例如 `src/screenshot_manager.c` 或测试代码

#### 数据结构改动点

将当前：

```c
typedef struct {
    char* text;
    int confidence;
    RECT boundingBox;
} OCRResult;

typedef struct {
    OCRResult* results;
    int count;
    char* fullText;
} OCRResults;
```

替换或迁移为：

```c
typedef struct {
    char* text;
    RECT boundingBox;
    float confidence;
    BOOL isLineBreak;
} OCRWord;

typedef struct {
    OCRWord* words;
    int wordCount;
    char* fullText;
    int imageWidth;
    int imageHeight;
} OCRResults;
```

#### 具体实现细节

1. `OCRRecognize()` 暂时返回 mock 数据。

   建议生成至少三行文本，每行多个词，覆盖：

   - 单行选择
   - 跨行选择
   - 末尾换行
   - 不同宽度单词

2. mock word 坐标使用原图坐标。

   示例：

   ```c
   words[0].boundingBox = (RECT){50, 50, 110, 76};
   words[1].boundingBox = (RECT){120, 50, 190, 76};
   ```

3. `fullText` 应和 `words/isLineBreak` 拼接结果一致。

4. `OCRFreeResults()` 必须释放：

   - 每个 `words[i].text`
   - `words`
   - `fullText`
   - `OCRResults`

5. 如果暂时不引入异步 API，本阶段不要声明 `OCRRecognizeAsync()`，避免 API 先承诺但没有稳定实现。

#### 验收方案

自动验收：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-ocr
build\powercapslock.exe --test-screenshot-all
```

预期：

- OCR 测试输出 mock fullText。
- 日志中能看到 `wordCount` 或等价识别完成信息。
- 截图全量测试不退化。

代码验收：

- 搜索旧字段：

  ```powershell
  Select-String -Path src\*.c,src\*.h -Pattern "results->count|results->results|OCRResult"
  ```

  预期：没有遗留旧结构使用，除非是兼容层且明确注释。

#### 手动验收

本阶段无 UI 改动，不需要手动 UI 验证。

#### 风险与回滚

- 风险：字段重命名导致编译失败。
- 回滚：只需回退 `screenshot_ocr.c/h` 和直接引用 OCRResults 的调用方。

---

### 阶段 2：悬浮窗口 OCR 模式与统一坐标映射

#### 阶段目标

让悬浮窗口支持 OCR 模式，并建立统一图片 layout。此阶段只需要持有 OCR 结果和映射坐标，不需要完成拖选复制。

#### 前置条件

- 阶段 1 已完成。
- `OCRRecognize()` 能返回 mock `OCRResults`。

#### 涉及文件

- 修改：`src/screenshot_float.h`
- 修改：`src/screenshot_float.c`
- 可能修改：`src/screenshot_manager.c`，但建议留到阶段 5 接入口

#### 结构改动点

在 `FloatWindowContext` 中增加：

```c
bool ocrMode;
OCRResults* ocrResults;
bool ocrInProgress;
int hoveredWordIndex;
int anchorWordIndex;
int activeWordIndex;
bool isSelectingText;
float imageScale;
POINT imageOffset;
SIZE imageDrawSize;
bool showCopiedToast;
DWORD toastStartTime;
```

#### API 改动点

在 `screenshot_float.h` 新增：

```c
bool ScreenshotFloatShowOcr(const ScreenshotImage* image, int x, int y);
const char* ScreenshotFloatGetSelectedText(void);
void ScreenshotFloatClearSelection(void);
```

#### 具体实现细节

1. 保留 `ScreenshotFloatShow()` 原行为。

   普通挂起窗口必须仍然：

   - 显示截图
   - 支持拖拽
   - 支持右键菜单
   - 支持双击关闭

2. 新增内部共用函数，例如：

   ```c
   static bool ShowFloatWindowInternal(const ScreenshotImage* image, int x, int y, bool ocrMode);
   ```

   `ScreenshotFloatShow()` 调用 `ocrMode=false`。

   `ScreenshotFloatShowOcr()` 调用 `ocrMode=true`。

3. 抽取统一 layout：

   ```c
   static void CalculateImageLayout(FloatWindowContext* ctx, RECT clientRect);
   static POINT ImageToWindowPoint(FloatWindowContext* ctx, POINT imagePoint);
   static POINT WindowToImagePoint(FloatWindowContext* ctx, POINT windowPoint);
   static RECT ImageToWindowRect(FloatWindowContext* ctx, RECT imageRect);
   ```

4. `DrawFloatWindow()` 必须用 `ctx->imageOffset/imageScale/imageDrawSize` 绘制图片。

   不允许绘制时一套缩放，命中检测时另一套缩放。

5. `ScreenshotFloatHide()` 和 `ScreenshotFloatCleanup()` 必须释放 `ocrResults`。

6. OCR 模式下本阶段可以同步调用 `OCRRecognize()` 获取 mock 结果。

   真实 OCR 接入前，允许同步 mock，因为 mock 不耗时。

#### 验收方案

自动验收：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-float
build\powercapslock.exe --test-screenshot-ocr
```

手动验收：

- 普通 `--test-screenshot-float` 窗口行为与改动前一致。
- 如果新增 OCR float 测试命令或临时调试入口，OCR 模式能正常显示同一张图片。
- 关闭窗口后没有崩溃。

代码验收：

- `ScreenshotFloatShow()` 不应自动设置 `ocrMode=true`。
- `ScreenshotFloatHide()` 和 `ScreenshotFloatCleanup()` 都释放 `ocrResults`。
- `DrawFloatWindow()`、word hit test 后续应共用同一 layout 函数。

#### 风险与回滚

- 风险：改动 `DrawFloatWindow()` 后影响普通挂起窗口显示尺寸。
- 回滚：可保留新增 OCR 字段，但回退绘制路径，先让普通 float 恢复稳定。

---

### 阶段 3：OCR Hover 高亮与鼠标命中

#### 阶段目标

OCR 模式下，用户移动鼠标到 OCR word 上时显示 I-beam 光标，并显示轻量 hover 高亮。

本阶段不做拖选复制，只做 hover。

#### 前置条件

- 阶段 2 已完成。
- OCR 模式窗口持有 mock `ocrResults`。
- 坐标映射函数可用。

#### 涉及文件

- 修改：`src/screenshot_float.c`

#### 具体改动点

1. 新增命中函数：

   ```c
   static int FindWordAtImagePoint(FloatWindowContext* ctx, POINT imagePoint);
   static int FindWordAtWindowPoint(FloatWindowContext* ctx, POINT windowPoint);
   ```

2. 新增高亮绘制函数：

   ```c
   static void DrawOcrOverlays(HDC hdc, FloatWindowContext* ctx);
   static void DrawWordHighlight(HDC hdc, FloatWindowContext* ctx, int wordIndex, COLORREF color, BYTE alpha);
   ```

3. 在 `DrawFloatWindow()` 图片绘制之后绘制 hover 高亮。

4. 在 `WM_MOUSEMOVE` 中更新 `hoveredWordIndex`。

5. 在 `WM_SETCURSOR` 中：

   - OCR 模式且命中 word：`IDC_IBEAM`
   - 其他情况：`IDC_ARROW`

#### 验收方案

自动验收：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-float
```

手动验收：

- OCR 模式下，鼠标移动到 mock word 上，光标变为 I-beam。
- 鼠标移出 word，光标恢复箭头。
- hover 高亮跟随 word 位置，不偏移。
- 普通挂起模式不显示 OCR hover，也不改变拖动行为。

代码验收：

- `FindWordAtWindowPoint()` 只能通过统一 layout 映射到 image point。
- hover 状态变化时调用 `InvalidateRect()`，避免高亮残留。
- 非 OCR 模式下不应执行 word hit test。

#### 风险与回滚

- 风险：hover 命中区域太大，影响窗口拖动。
- 回滚：先保留 OCR 数据，关闭 `WM_SETCURSOR` 和 hover 绘制。

---

### 阶段 4：拖选 OCR 文本与复制

#### 阶段目标

用户可以在 OCR 模式中拖选多个 word，并通过 `Ctrl+C` 或右键菜单复制选中的文字。

#### 前置条件

- 阶段 3 已完成。
- word hover 和坐标映射稳定。

#### 涉及文件

- 修改：`src/screenshot_float.c`
- 可能修改：`src/screenshot_float.h`

#### 具体改动点

1. 选择状态：

   ```c
   int anchorWordIndex;
   int activeWordIndex;
   bool isSelectingText;
   ```

2. 新增选择辅助函数：

   ```c
   static bool HasTextSelection(FloatWindowContext* ctx);
   static void GetSelectedWordRange(FloatWindowContext* ctx, int* start, int* end);
   static char* BuildSelectedTextUtf8(FloatWindowContext* ctx);
   static bool CopyUtf8TextToClipboard(HWND hwnd, const char* text);
   ```

3. 鼠标事件：

   - `WM_LBUTTONDOWN`：
     - OCR 模式且命中 word：开始文本选择，设置 anchor 和 active。
     - 未命中 word：清除文本选择，保留原窗口拖动逻辑。
   - `WM_MOUSEMOVE`：
     - `isSelectingText=true` 时更新 active word。
     - `isDragging=true` 时继续原窗口拖动。
   - `WM_LBUTTONUP`：
     - 结束文本选择或窗口拖动。

4. 键盘事件：

   - `WM_KEYDOWN` 中处理 `Ctrl+C`。
   - 有文本选区时复制文本。
   - 没有文本选区时可保持默认行为或复制图片，需明确产品决定。

5. 右键菜单：

   - 有文本选区时显示“复制文字”。
   - 没有文本选区时保留“复制图片”。
   - 也可同时提供“复制文字”和“复制图片”，但要禁用不可用项。

6. 复制编码：

   - `OCRWord.text` 按 UTF-8 处理。
   - `BuildSelectedTextUtf8()` 拼接 UTF-8。
   - `CopyUtf8TextToClipboard()` 转 `CF_UNICODETEXT`。
   - 必须先计算宽字符长度：

     ```c
     int wideLen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
     ```

#### 选区拼接规则

- 同一行词之间插入一个空格。
- 如果当前 word `isLineBreak=true`，插入 `\r\n`。
- 不在末尾额外追加空格。
- 如果 OCR 引擎后续提供标点独立 word，可在后续优化“标点前不加空格”。

#### 验收方案

自动验收：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-float
```

手动验收：

- 从左到右拖选多个词，高亮范围正确。
- 从右到左反向拖选，高亮范围正确。
- 跨行拖选时，每行 word 高亮正确。
- 文字区域拖选不会移动窗口。
- 空白区域拖动仍然移动窗口。
- `Ctrl+C` 后能粘贴出正确文本。
- 右键“复制文字”能复制正确文本。
- 复制成功 toast 或日志提示正常。

代码验收：

- 拖动时不能不断重写 anchor word。
- `BuildSelectedTextUtf8()` 由快捷键和右键菜单共用。
- `GlobalAlloc` 的内存交给 `SetClipboardData()` 后不再手动释放。

#### 风险与回滚

- 风险：鼠标选择和窗口拖动冲突。
- 回滚：保留 hover，关闭文本选择分支，窗口拖动恢复原逻辑。

---

### 阶段 5：OCR 入口接入截图流程

#### 阶段目标

从截图选区进入 OCR 悬浮窗口，让用户可以实际从截图流程触发 OCR 文字选择。

#### 前置条件

- 阶段 4 已完成。
- OCR 模式悬浮窗口通过 mock 数据可完整选择和复制。

#### 涉及文件

- 修改：`src/screenshot_manager.c`
- 可能修改：`src/screenshot_toolbar.c/h`
- 可能修改：`src/screenshot_overlay.h`，如果原始图获取 API 不足

#### 入口方案

方案 A：恢复 OCR 工具栏按钮。

- 优点：入口直观。
- 缺点：工具栏按钮变多，可能影响小选区下的工具栏布局。

方案 B：新增快捷键触发 OCR。

- 优点：不增加工具栏宽度。
- 缺点：可发现性较弱。

方案 C：将 OCR 放入二级菜单或更多按钮。

- 优点：工具栏更干净。
- 缺点：当前工具栏没有二级菜单基础，需要额外 UI。

推荐先使用方案 A 或 B，不建议本阶段引入复杂二级菜单。

#### 具体改动点

1. `TOOLBAR_BTN_OCR` 如果恢复入口，需要加入 `g_defaultButtons`。

2. `OnToolbarButton()` 的 OCR case 应使用原始截图：

   ```c
   image = ScreenshotOverlayGetSelectionImage();
   ```

   不要使用：

   ```c
   ScreenshotOverlayGetAnnotatedSelectionImage();
   ```

3. 获取屏幕坐标：

   ```c
   const ScreenshotRect* selection = ScreenshotOverlayGetSelection();
   screenSelection = SelectionToScreenRect(selection);
   ```

4. 隐藏 overlay 和 toolbar 后调用：

   ```c
   ScreenshotFloatShowOcr(image, screenSelection.x, screenSelection.y);
   ```

5. OCR 入口触发后需要设置：

   ```c
   g_active = false;
   ```

#### 验收方案

自动验收：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-all
build\powercapslock.exe --test-screenshot-toolbar
build\powercapslock.exe --test-screenshot-float
```

手动验收：

- 截图选区完成后可以触发 OCR 模式。
- OCR 浮窗出现的位置接近原截图选区位置。
- 先画标注再触发 OCR，OCR 输入不包含标注干扰。
- 普通复制、保存、挂起仍正常。
- ESC 或关闭 OCR 浮窗不会留下 overlay/toolbar。

代码验收：

- OCR case 不调用 `GetSelectionImageForAction()`，因为该函数返回带标注图。
- OCR 按钮如果加入工具栏，需要确认 tooltip、图标和 hit test 正常。

#### 风险与回滚

- 风险：恢复 OCR 按钮导致工具栏过宽。
- 回滚：保留内部 `ScreenshotFloatShowOcr()`，移除工具栏 OCR 按钮入口。

---

### 阶段 6：异步 OCR 框架

#### 阶段目标

在真实 OCR 接入前，先建立安全的异步识别框架，确保 OCR 不阻塞 UI，也不会在窗口关闭后写入失效状态。

#### 前置条件

- 阶段 5 已完成。
- OCR 模式入口可用。

#### 涉及文件

- 修改：`src/screenshot_ocr.h`
- 修改：`src/screenshot_ocr.c`
- 修改：`src/screenshot_float.c`

#### API 设计

建议增加：

```c
typedef void (*OCRCompleteCallback)(OCRResults* results, void* userData);

BOOL OCRRecognizeAsync(const ScreenshotImage* image, OCRCompleteCallback callback, void* userData);
void OCRCancelAsync(void);
```

但更推荐在 UI 层使用 `PostMessage`，而不是直接在 callback 中改 UI。

建议定义：

```c
#define WM_FLOAT_OCR_COMPLETE (WM_APP + 10)
```

#### 具体实现细节

1. `OCRRecognizeAsync()` 必须复制 `ScreenshotImage`。

   worker 线程不能持有 UI 传入的原始指针。

2. 为每次 OCR 创建 session id：

   ```c
   DWORD ocrSessionId;
   ```

3. worker 完成后把结果封装为 heap 对象：

   ```c
   typedef struct {
       DWORD sessionId;
       OCRResults* results;
   } FloatOcrCompleteMessage;
   ```

4. 通过 `PostMessage(hwnd, WM_FLOAT_OCR_COMPLETE, 0, (LPARAM)message)` 回 UI 线程。

5. UI 线程处理消息时：

   - 检查 hwnd 仍有效。
   - 检查 session id 是否匹配当前窗口。
   - 匹配则接管 results。
   - 不匹配则释放 results。

6. `ScreenshotFloatHide()`：

   - 标记当前 session 无效。
   - 不直接等待 worker 死锁 UI。
   - 后续旧消息到达时释放即可。

#### 验收方案

自动验收：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-ocr
build\powercapslock.exe --test-screenshot-float
```

手动验收：

- OCR 模式打开时 UI 不冻结。
- OCR 进行中关闭窗口不崩溃。
- 连续打开多个 OCR 浮窗，旧结果不会覆盖新窗口。
- OCR 失败时 UI 能正常显示图片窗口。

代码验收：

- worker 线程不直接访问 `g_float`。
- worker 线程持有自己的图片副本。
- 所有异步结果都有明确释放路径。

#### 风险与回滚

- 风险：线程生命周期和窗口生命周期交错导致 use-after-free。
- 回滚：保留同步 mock 识别路径，禁用 async 入口。

---

### 阶段 7：真实 OCR 后端接入

#### 阶段目标

将 mock OCR 替换或扩展为真实 OCR 后端，并输出统一的 `OCRWord` 结果。

#### 前置条件

- 阶段 6 已完成。
- 异步框架稳定。
- OCR 模式选择与复制体验已通过 mock 数据验证。

#### 后端选择

推荐先接一个后端，不建议同时接 Windows OCR 和 Tesseract。

方案 A：Windows.Media.OCR。

- 优点：系统能力，无需额外语言包分发。
- 缺点：C++/WinRT 与 MinGW 兼容性需要验证。

方案 B：Tesseract。

- 优点：跨系统 OCR 能力成熟，词级框输出直接。
- 缺点：dll、leptonica、tessdata 分发复杂，体积较大。

#### Windows OCR 实现建议

涉及文件：

- 新增：`src/screenshot_ocr_winrt.cpp`
- 修改：`CMakeLists.txt`
- 修改：`src/screenshot_ocr.c/h` 或引入后端桥接头

实现细节：

- 保持对外 C API 不变。
- 在 `.cpp` 文件内部使用 C++/WinRT。
- 后台线程中初始化 COM/WinRT。
- 将 `ScreenshotImage` 转成 Windows OCR 可识别的 bitmap。
- 将识别结果转换为 `OCRWord`。

验收：

- 英文截图能返回词级 bounding boxes。
- 中文截图能返回 UTF-8 文本并可复制。
- 无 OCR 语言包或系统不支持时返回失败，不崩溃。

#### Tesseract 实现建议

涉及文件：

- 可能新增：`src/screenshot_ocr_tesseract.c`
- 修改：`CMakeLists.txt`
- 修改：release 打包脚本或 release 文档

实现细节：

- 明确 tesseract/leptonica dll 路径。
- 明确 `tessdata` 分发路径和语言配置。
- 将 `ScreenshotImage` 转为 Tesseract 输入格式。
- 使用 word-level API 生成 `OCRWord`。

验收：

- 英文截图词级识别可用。
- 中文识别依赖 `chi_sim` 或相关语言包。
- release 包内包含必要 runtime 和模型数据。

#### 通用验收方案

自动验收：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-ocr
build\powercapslock.exe --test-screenshot-float
build\powercapslock.exe --test-screenshot-all
```

手动验收：

- 英文应用窗口截图可识别并选中文字。
- 中文应用窗口截图可识别并复制 Unicode 文本。
- OCR 失败时悬浮窗口仍可作为普通图片查看。
- 长文本截图不会明显卡 UI。

#### 风险与回滚

- 风险：真实 OCR 后端引入构建失败或发布包缺依赖。
- 回滚：保留 mock/disabled backend，关闭真实后端编译开关。

---

## 全局测试矩阵

每个阶段至少运行：

```powershell
cmake --build build
```

截图相关基础测试：

```powershell
build\powercapslock.exe --test-screenshot-all
build\powercapslock.exe --test-screenshot-toolbar
build\powercapslock.exe --test-screenshot-annotate
```

OCR/浮窗相关测试：

```powershell
build\powercapslock.exe --test-screenshot-ocr
build\powercapslock.exe --test-screenshot-float
```

测试后清理：

```powershell
Remove-Item -LiteralPath test_capture.bmp,test_save.bmp,test_annotate.bmp -ErrorAction SilentlyContinue
Get-Process | Where-Object { $_.ProcessName -like '*powercapslock*' }
```

如果存在测试残留进程，停止后再继续构建：

```powershell
Stop-Process -Id <pid>
```

## 手动验收清单

普通截图能力：

- 截图选区正常。
- 工具栏显示正常。
- 保存、复制、挂起正常。
- 标注矩形、箭头、画笔、圆形、文字正常。
- 文字标注颜色和字号仍正常。

普通悬浮窗口：

- 挂起图片显示正常。
- 空白处拖动窗口正常。
- 双击关闭正常。
- 右键菜单复制图片正常。

OCR 悬浮窗口：

- OCR 模式显示图片正常。
- OCR word hover 高亮准确。
- I-beam 光标只在 OCR word 上显示。
- 正向拖选和反向拖选都正确。
- 跨行选择复制时换行正确。
- `Ctrl+C` 复制文本正确。
- 右键复制文字正确。
- OCR 失败时不崩溃。

## 推荐提交拆分

建议按阶段拆提交，便于回滚和 review：

1. `feat: add OCR word result model and mock recognizer`
2. `feat: add OCR mode to screenshot float window`
3. `feat: render OCR word hover in float window`
4. `feat: implement OCR text selection and copy`
5. `feat: wire screenshot OCR entry to float window`
6. `feat: add async OCR recognition pipeline`
7. `feat: add real OCR backend`

如果当前工作区已有未提交截图标注改动，应先单独提交或记录，避免与 OCR 功能混杂。

## 回滚策略

- 阶段 1-4 不依赖真实 OCR，可以独立回滚。
- 阶段 5 如果入口有问题，可以先隐藏 OCR 按钮，保留内部 OCR 浮窗能力。
- 阶段 6 如果异步不稳定，可以回退到同步 mock 识别。
- 阶段 7 如果真实 OCR 后端不稳定，可以回退到 mock/disabled backend，不影响普通截图和挂起功能。

## 最终建议

不要直接执行原始计划中的“双引擎 OCR + 异步 + UI 选择”全量方案。更稳的路线是：

1. 先做 mock OCR 的 OCR 模式悬浮窗口。
2. 把词级选择、复制、坐标映射打磨稳定。
3. 再建立异步 OCR 框架。
4. 再选择一个真实 OCR 后端接入。
5. 最后再考虑双引擎 fallback、语言包配置和性能优化。

这条路径能最快验证用户体验，并把高风险的 OCR 引擎依赖隔离到后续阶段。
