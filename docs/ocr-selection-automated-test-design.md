# OCR 文字选择功能自动化测试设计方案

## 文档目的

本文档是 `docs/ocr-selection-design-plan.md` 的配套测试设计方案，目标是让后续开发者或大模型在继续实现 OCR 文字选择功能时，可以完全通过自动化测试验证功能正确性，不依赖人工移动鼠标、人工观察高亮、人工复制粘贴。

本文强调测试的技术实现细节，包括需要新增的测试接口、可复用的纯函数、命令行测试入口、Python 编排脚本、Win32 消息驱动测试、剪贴板隔离策略、异步 OCR mock 策略和 CI 接入方式。

## 测试总目标

OCR 文字选择功能最终需要自动化覆盖以下能力：

- OCR 词级数据模型正确生成、释放和复制。
- 图片坐标、窗口坐标、缩放后的绘制坐标可以稳定互转。
- 鼠标悬停到词框时可以命中正确 OCR word。
- 鼠标拖选多个词时可以生成正确的选择区间。
- 从左到右、从右到左、跨行拖选都能得到正确文本。
- 复制文本时 UTF-8 到 UTF-16 转换正确，中文、英文、符号和换行不乱码。
- OCR 浮窗的 hover 高亮、选择高亮和复制提示可以被程序化验证。
- 普通挂起截图模式不受 OCR 模式影响。
- 异步 OCR 后端完成、失败、取消和窗口关闭场景不会崩溃或泄漏。
- 真实 OCR 后端接入后，可以用固定图片做可重复的 smoke test。

自动化测试必须满足：

- 不要求人工观察窗口。
- 不要求人工点击、拖动、键盘操作。
- 不依赖当前鼠标位置。
- 不依赖用户剪贴板已有内容。
- 不依赖网络。
- 知道 `powercapslock.exe` 是单例程序，每个测试用例启动前都必须检查并停止已有实例。
- 能在 Windows 本地和 GitHub Actions Windows runner 上执行。
- 失败时能输出结构化日志、截图或渲染结果，方便后续模型定位。

## 当前项目测试基础

当前项目已有命令行测试入口，集中在 `src/main.c` 中，例如：

- `--test-screenshot-all`
- `--test-screenshot-overlay`
- `--test-screenshot-toolbar`
- `--test-screenshot-float`
- `--test-screenshot-annotate`
- `--test-screenshot-ocr`
- `--test-config`

当前也已有 Python 测试编排脚本：

- `test/screenshot_function_test.py`

建议后续 OCR 文字选择测试延续这个风格：

- C 层提供细粒度测试函数，返回 `0` 表示通过，非 `0` 表示失败。
- `src/main.c` 增加新的 `--test-...` 参数。
- Python 脚本负责编排多个命令、收集 stdout/stderr、生成 Markdown 和 JSON 报告。
- 所有测试产物写入 `test/output/<suite>_<timestamp>/`。

## 单例进程约束

PowerCapslock 是单例模式程序。已有实例运行时，再启动 `powercapslock.exe --test-*` 可能出现以下问题：

- 新进程被已有实例拦截或提前退出，测试命令没有真正执行。
- 已有实例持有全局热键、剪贴板、窗口类、单例互斥量，导致 UI 测试结果不稳定。
- 已有实例锁住 `build/powercapslock.exe`，导致构建、覆盖或打包失败。
- 上一个失败测试残留窗口，影响下一个测试的 HWND、焦点、剪贴板和截图状态。

因此自动化测试必须遵守：

- 每条 `powercapslock.exe --test-*` 命令启动前，都要检查已有 `powercapslock.exe` 进程。
- 默认测试 runner 必须提供 `--stop-existing` 参数，并在测试环境中推荐始终开启。
- 如果发现已有实例且未传入 `--stop-existing`，测试必须失败并提示用户，不允许静默继续。
- 即使整套测试开始前已经清理过，单个测试命令前也要再次检查，因为上一个测试可能残留实例。
- 每条测试命令结束后也要检查一次实例残留；如有残留，先尝试正常关闭，再强制结束。
- 进程清理动作必须写入 `run.log`，包括 PID、可执行文件路径、命令行和处理结果。

推荐 Python runner 的执行模型：

```text
for test_case in test_cases:
    ensure_no_existing_instance(phase="before", test_case=test_case)
    run powercapslock.exe --test-...
    ensure_no_existing_instance(phase="after", test_case=test_case)
```

推荐默认行为：

- 本地开发：`python test/screenshot_ocr_selection_test.py --stop-existing`
- CI：始终使用 `--stop-existing`
- 手动排查：如果不希望自动杀进程，可以不传 `--stop-existing`，此时发现已有实例就立即失败。

### 进程发现策略

优先使用 PowerShell/CIM 获取完整进程信息，而不是只用 `tasklist`。原因是 `tasklist` 不包含可执行文件路径，无法判断实例来源。

推荐命令：

```powershell
Get-CimInstance Win32_Process -Filter "name = 'powercapslock.exe'" |
    Select-Object ProcessId, ExecutablePath, CommandLine
```

Python 中建议这样调用：

```python
def find_powercapslock_instances() -> list[dict[str, str]]:
    script = r"""
    Get-CimInstance Win32_Process -Filter "name = 'powercapslock.exe'" |
      Select-Object ProcessId, ExecutablePath, CommandLine |
      ConvertTo-Json -Compress
    """
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", script],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0 or not result.stdout.strip():
        return []
    data = json.loads(result.stdout)
    if isinstance(data, dict):
        data = [data]
    return data
```

### 进程停止策略

建议分两步停止已有实例：

1. 如果进程有主窗口，先尝试正常关闭。
2. 等待超时后仍存在，再强制结束。

参考实现：

```python
def stop_powercapslock_instances(instances: list[dict[str, str]], timeout_sec: float = 3.0) -> None:
    for item in instances:
        pid = int(item["ProcessId"])
        subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             f"$p = Get-Process -Id {pid} -ErrorAction SilentlyContinue; "
             f"if ($p -and $p.MainWindowHandle -ne 0) {{ $p.CloseMainWindow() | Out-Null }}"],
            capture_output=True,
            text=True,
        )

    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if not find_powercapslock_instances():
            return
        time.sleep(0.1)

    for item in find_powercapslock_instances():
        pid = int(item["ProcessId"])
        subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True, text=True)
```

如果项目的单例互斥量是全局的，即不同路径下的 `powercapslock.exe` 也会互相影响，那么测试 runner 必须停止所有名为 `powercapslock.exe` 的进程。如果后续改成只停止当前 build 目录中的测试实例，需要先确认单例互斥量不会跨路径冲突。

### 每条测试命令前后的固定流程

建议 `run_command()` 不直接 `subprocess.run()`，而是统一封装：

```python
def run_powercapslock_test(self, args: list[str], name: str, timeout: int = 20):
    self.ensure_no_existing_instance(phase="before", test_name=name)
    try:
        return subprocess.run(
            [str(self.exe), *args],
            cwd=str(self.build_dir),
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
    finally:
        self.ensure_no_existing_instance(phase="after", test_name=name)
```

`ensure_no_existing_instance()` 规则：

- 找不到实例：直接返回。
- 找到实例且 `--stop-existing=true`：记录实例信息，停止实例，确认已停止。
- 找到实例且 `--stop-existing=false`：抛出错误，报告 PID 和路径。
- 停止后仍有实例残留：测试失败，返回码为 `2`。

### 日志要求

每次清理实例都必须写入日志：

```text
[12:00:01] before ocr-layout: found powercapslock.exe pid=1234 path=D:\code\PowerCapslock\build\powercapslock.exe
[12:00:01] before ocr-layout: CloseMainWindow sent pid=1234
[12:00:04] before ocr-layout: taskkill /F pid=1234
[12:00:04] before ocr-layout: instance cleanup completed
```

如果没有发现实例，也建议在 verbose 模式下记录：

```text
[12:00:05] before ocr-layout: no existing powercapslock.exe
```

## 测试分层架构

建议把自动化测试拆为五层，越底层越确定，越上层越接近真实用户流程。

### 第 1 层：纯函数单元测试

目标是让核心逻辑脱离窗口、GDI、剪贴板、OCR 引擎，做到可快速验证。

覆盖对象：

- OCR word 数据构造和释放。
- 选择区间计算。
- 坐标映射。
- hit-test。
- 选中文本拼接。
- UTF-8 到 UTF-16 长度计算。

建议新增文件：

- `src/screenshot_ocr_selection.h`
- `src/screenshot_ocr_selection.c`

建议从 `src/screenshot_float.c` 中拆出的逻辑：

```c
typedef struct {
    float scale;
    POINT offset;
    SIZE drawSize;
} OcrImageLayout;

typedef struct {
    int anchorWordIndex;
    int activeWordIndex;
} OcrSelectionState;

bool OcrCalculateImageLayout(
    int imageWidth,
    int imageHeight,
    int clientWidth,
    int clientHeight,
    OcrImageLayout* outLayout);

POINT OcrImageToWindowPoint(const OcrImageLayout* layout, POINT imagePoint);
POINT OcrWindowToImagePoint(const OcrImageLayout* layout, POINT windowPoint);
RECT OcrImageToWindowRect(const OcrImageLayout* layout, RECT imageRect);

int OcrHitTestWordAtImagePoint(const OCRResults* results, POINT imagePoint);
int OcrHitTestWordAtWindowPoint(
    const OCRResults* results,
    const OcrImageLayout* layout,
    POINT windowPoint);

bool OcrGetSelectedWordRange(
    const OcrSelectionState* selection,
    int wordCount,
    int* outStart,
    int* outEnd);

char* OcrBuildSelectedTextUtf8(
    const OCRResults* results,
    const OcrSelectionState* selection);

int OcrMeasureUtf16LengthFromUtf8(const char* utf8Text);
```

验收要求：

- 这些函数不访问全局窗口状态。
- 这些函数不调用 `GetCursorPos`、`OpenClipboard`、`InvalidateRect`。
- 这些函数可以在无窗口环境下运行。
- 测试失败时输出输入参数、期望值和实际值。

### 第 2 层：模块级测试

目标是验证 `screenshot_ocr`、`screenshot_float` 等模块对纯函数的集成是否正确。

覆盖对象：

- `OCRRecognize()` mock 数据结构完整性。
- `ScreenshotFloatShow()` 普通模式不启用 OCR 状态。
- `ScreenshotFloatShowOcr()` OCR 模式可以加载 mock words。
- `ScreenshotFloatGetSelectedText()` 能返回选中文本。
- `ScreenshotFloatClearSelection()` 能清空选择。

建议新增测试专用接口，放在测试头文件中，避免污染正式 API：

- `src/screenshot_float_test.h`
- `src/screenshot_ocr_test.h`

示例：

```c
#ifdef POWERCAPSLOCK_TESTING
HWND ScreenshotFloatTestGetWindow(void);
bool ScreenshotFloatTestIsOcrMode(void);
int ScreenshotFloatTestGetHoveredWordIndex(void);
bool ScreenshotFloatTestSetSelection(int anchorWordIndex, int activeWordIndex);
bool ScreenshotFloatTestGetLayout(OcrImageLayout* outLayout);
bool ScreenshotFloatTestRenderToBitmap(const char* outputBmpPath);
bool ScreenshotFloatTestInjectOcrResults(const OCRResults* results);
#endif
```

实现约束：

- 测试接口必须放在 `#ifdef POWERCAPSLOCK_TESTING` 内。
- Release 构建可以不暴露这些接口。
- 如果当前项目只有一个 `powercapslock.exe` 目标，也可以默认编译测试接口，但函数名必须带 `Test`，且不被生产流程调用。

### 第 3 层：Win32 消息驱动 UI 测试

目标是用程序向窗口发送消息，验证真实窗口过程中的 hover、拖选、复制行为。

不要使用真实鼠标移动。推荐使用：

- `SendMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y))`
- `SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y))`
- `SendMessage(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y))`
- `SendMessage(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y))`
- 测试专用 copy 命令或直接调用 `ScreenshotFloatGetSelectedText()`

不推荐默认使用：

- `SendInput`
- `mouse_event`
- 全局快捷键
- 真实 `Ctrl+C`

原因是这些方式会受焦点、权限、输入法、远程桌面和 CI runner 状态影响。

建议新增测试消息：

```c
#define WM_APP_FLOAT_TEST_COPY_SELECTION (WM_APP + 201)
#define WM_APP_FLOAT_TEST_GET_STATE      (WM_APP + 202)
```

也可以更简单地不走 `WM_KEYDOWN`，直接在测试中调用：

```c
const char* selected = ScreenshotFloatGetSelectedText();
```

Win32 UI 测试的核心流程：

1. 创建固定尺寸测试图片，例如 `400x240`。
2. 注入固定 OCR words，或调用 mock `OCRRecognize()`。
3. 调用 `ScreenshotFloatShowOcr(image, 100, 100)`。
4. 获取浮窗 HWND。
5. 调用 `GetClientRect()`，计算 layout。
6. 把目标 word 的 image bounding box 转换为 window 坐标。
7. 发送 `WM_MOUSEMOVE` 到第一个词中心点。
8. 断言 `hoveredWordIndex == expectedIndex`。
9. 发送 `WM_LBUTTONDOWN` 到起始词中心点。
10. 发送 `WM_MOUSEMOVE` 到结束词中心点。
11. 发送 `WM_LBUTTONUP`。
12. 调用 `ScreenshotFloatGetSelectedText()`。
13. 断言文本完全匹配。
14. 调用 `ScreenshotFloatHide()` 清理窗口。

### 第 4 层：Python 端到端编排测试

目标是用一个脚本跑完整测试矩阵，生成机器可读和人可读报告。

建议新增：

- `test/screenshot_ocr_selection_test.py`

脚本职责：

- 检查 `build/powercapslock.exe` 是否存在。
- 在整套测试开始前检查并停止已有 `powercapslock.exe` 进程。
- 在每条 `powercapslock.exe --test-*` 命令启动前再次检查并停止已有实例。
- 在每条测试命令结束后检查是否残留实例，并在必要时停止。
- 运行所有 OCR selection 相关命令行测试。
- 为每个命令保存 stdout/stderr。
- 读取每个命令输出的 JSON 结果。
- 汇总 Markdown 报告和 JSON 报告。
- 将失败用例、输入参数、期望值、实际值写入报告。

建议输出目录：

```text
test/output/ocr_selection_YYYYMMDD_HHMMSS/
  run.log
  result.json
  report.md
  commands/
    ocr_model_stdout.txt
    ocr_model_stderr.txt
    layout_stdout.txt
    layout_stderr.txt
  artifacts/
    hover_render.bmp
    selection_render.bmp
```

脚本建议命令：

```powershell
python test/screenshot_ocr_selection_test.py --stop-existing
```

脚本返回码：

- `0`：全部通过。
- `1`：至少一个测试失败。
- `2`：测试环境错误，例如 exe 不存在、无法创建输出目录。

### 第 5 层：真实 OCR 后端 smoke test

真实 OCR 后端受系统组件、语言包、Tesseract 数据文件、字体渲染和 DPI 影响，不应作为第一阶段强制门禁。

建议策略：

- 默认 PR/本地回归使用 mock OCR。
- 真实 OCR 作为可选测试，需要显式参数开启。
- 如果后端依赖不存在，返回 skipped，不算失败。

建议命令：

```powershell
build\powercapslock.exe --test-ocr-real-smoke --fixture test\fixtures\ocr\hello_zh_en.png
python test/screenshot_ocr_selection_test.py --include-real-ocr
```

真实 OCR smoke test 验收不要要求逐字完全相等，建议：

- 归一化空白。
- 忽略大小写，或分别验证中文和英文关键 token。
- 要求至少命中指定关键词。
- 要求平均 confidence 大于阈值。
- 要求 word bounding box 数量大于 0。

## 需要新增的命令行测试入口

建议在 `src/main.c` 中新增以下参数。

### `--test-ocr-data-model`

目标：

- 验证 `OCRResults` 和 `OCRWord` 构造、复制、释放。
- 验证中文、英文、符号、换行字段能被正确保存。

核心断言：

- `wordCount` 等于预期。
- 每个 `text` 非空。
- 每个 `boundingBox` 合法，满足 `left < right`、`top < bottom`。
- `imageWidth/imageHeight` 与 fixture 匹配。
- `fullText` 与 words 拼接结果一致。
- 连续创建释放 1000 次无崩溃。

建议输出：

```json
{
  "suite": "ocr-data-model",
  "passed": true,
  "wordCount": 12,
  "fullText": "Hello World\r\nOCR Selection Test\r\n"
}
```

### `--test-ocr-layout`

目标：

- 验证图片在浮窗内缩放和居中时，坐标映射稳定。

测试用例：

- 图片 `400x300`，窗口 `400x300`，scale 应为 `1.0`，offset 为 `(0,0)`。
- 图片 `400x300`，窗口 `800x600`，scale 应为 `2.0`，offset 为 `(0,0)`。
- 图片 `400x300`，窗口 `800x800`，scale 应为 `2.0`，offset 为 `(0,100)`。
- 图片 `800x400`，窗口 `400x400`，scale 应为 `0.5`，offset 为 `(0,100)`。
- 图片 `300x600`，窗口 `600x300`，scale 应为 `0.5`，offset 为 `(225,0)`。

核心断言：

- image point 转 window point 再转回 image point，误差不超过 1 像素。
- image rect 转 window rect 后宽高等于原宽高乘 scale，允许 1 像素舍入误差。
- `scale > 0`。
- draw size 不超过 client size。

### `--test-ocr-hit-test`

目标：

- 验证 mouse point 到 OCR word 的命中检测。

测试用例：

- 点在 `Hello` 中心，命中 word `0`。
- 点在 `World` 中心，命中 word `1`。
- 点在两个词之间的空隙，返回 `-1`。
- 点在词框左上边界，命中该词。
- 点在词框右下边界，命中该词。
- 点在图片外，返回 `-1`。
- 在缩放窗口中，用 window 坐标命中同一个 word。

核心断言：

- 每个 fixture point 返回精确 word index。
- 缩放前后的命中结果一致。

### `--test-ocr-selection`

目标：

- 验证拖选状态和选中文本拼接。

测试用例：

- 单词选择：anchor=0、active=0，文本为 `Hello`。
- 正向同行选择：anchor=0、active=1，文本为 `Hello World`。
- 反向同行选择：anchor=1、active=0，文本仍为 `Hello World`。
- 跨行选择：anchor=0、active=4，文本包含 `Hello World\r\nOCR Selection Test`。
- 从中间词开始跨行：anchor=3、active=6，文本包含正确空格和换行。
- 清除选择后，`ScreenshotFloatGetSelectedText()` 返回 `NULL`。

核心断言：

- `start = min(anchor, active)`。
- `end = max(anchor, active)`。
- `isLineBreak` 产生 `\r\n`。
- 非换行词之间产生单个空格。
- 没有多余尾随空格。

### `--test-ocr-copy-text`

目标：

- 验证 UTF-8 到剪贴板 UTF-16 的转换逻辑。

默认测试不直接写系统剪贴板，而是测试可拆出的转换函数。

测试用例：

- `Hello World`
- `中文测试`
- `Hello 中文 123`
- `emoji 😀 test`
- `第一行\r\n第二行`
- 空字符串。
- 非法 UTF-8 输入。

核心断言：

- `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)` 对合法输入成功。
- 转换后的 UTF-16 文本与预期一致。
- 非法 UTF-8 返回失败，不写剪贴板。
- 计算长度包含结尾 `NUL`。

可选真实剪贴板测试：

```powershell
build\powercapslock.exe --test-ocr-copy-text --allow-clipboard
```

真实剪贴板测试需要：

- 读取并保存原剪贴板文本。
- 写入测试文本。
- 读取 `CF_UNICODETEXT` 验证。
- 测试结束尽量恢复原剪贴板文本。
- 如果无法打开剪贴板，最多重试 5 次，每次等待 50ms。

### `--test-ocr-float-ui`

目标：

- 验证浮窗 OCR 模式下真实窗口消息处理。

测试实现：

1. 初始化 `ScreenshotFloatInit()` 和 `OCRInit()`。
2. 创建固定测试图片。
3. 调用 `ScreenshotFloatShowOcr(image, 100, 100)`。
4. 获取 HWND。
5. 通过测试接口获取 layout。
6. 发送 hover 消息。
7. 断言 hovered word。
8. 发送拖选消息。
9. 断言 selected text。
10. 渲染窗口到 bitmap。
11. 对高亮区域采样验证颜色变化。
12. 调用 `ScreenshotFloatHide()` 和 cleanup。

推荐不要验证整张图像逐像素完全一致。原因是 GDI 字体、抗锯齿、DPI 可能造成微小差异。建议验证关键像素或区域平均色。

示例采样规则：

- hover word 中心区域至少 60% 像素不是原图背景色。
- selected word bounding box 内至少 60% 像素接近高亮色。
- 非 selected word 区域不应出现 selected 高亮色。

### `--test-ocr-async`

目标：

- 为后续真实 OCR 后台线程接入提供自动化门禁。

测试后端：

```c
typedef struct {
    DWORD delayMs;
    bool shouldFail;
    OCRResults* results;
} FakeOcrBackendConfig;
```

测试用例：

- 成功：后台线程延迟 50ms 返回 words，UI 收到完成消息后显示高亮能力。
- 失败：后台线程返回 NULL，窗口显示失败状态或保持可关闭，不崩溃。
- 取消：OCR 未完成时关闭窗口，后台完成后不得访问已销毁 HWND。
- 连续调用：连续打开两个 OCR 浮窗，旧请求结果不能覆盖新窗口状态。

核心断言：

- UI 状态只在 UI 线程更新。
- `PostMessage(hwnd, WM_APP_OCR_COMPLETE, ...)` 失败时能释放结果内存。
- `ScreenshotFloatHide()` 后没有残留 `ocrInProgress=true`。
- 连续 100 次打开关闭无崩溃。

### `--test-ocr-e2e-mock`

目标：

- 验证从固定图片到 mock OCR、浮窗拖选、复制文本的完整闭环。

流程：

1. 生成或读取固定测试图片。
2. 使用 mock OCR backend 返回固定 words。
3. 打开 OCR 浮窗。
4. 程序化拖选 `Hello` 到 `Test`。
5. 获取 selected text。
6. 验证文本为：

   ```text
   Hello World
   OCR Selection Test
   ```

7. 清理窗口和 OCR 结果。

这个测试应该成为 OCR 文字选择功能的主要回归入口。

### `--test-screenshot-ocr-selection-all`

目标：

- 一键运行 OCR selection 所有强制测试。

建议包含：

- `--test-ocr-data-model`
- `--test-ocr-layout`
- `--test-ocr-hit-test`
- `--test-ocr-selection`
- `--test-ocr-copy-text`
- `--test-ocr-float-ui`
- `--test-ocr-e2e-mock`

默认不包含：

- `--test-ocr-real-smoke`
- 需要真实剪贴板写入的测试。
- 需要长时间压力循环的测试。

## Fixture 设计

### Mock OCR words

建议固定一组 OCR words，覆盖多行、空格、换行、中英文和符号。

建议文件：

- `test/fixtures/ocr/mock_words_basic.json`

示例：

```json
{
  "imageWidth": 400,
  "imageHeight": 240,
  "words": [
    { "text": "Hello", "bbox": [50, 40, 120, 68], "confidence": 0.95, "lineBreak": false },
    { "text": "World", "bbox": [132, 40, 210, 68], "confidence": 0.94, "lineBreak": true },
    { "text": "OCR", "bbox": [50, 82, 100, 110], "confidence": 0.93, "lineBreak": false },
    { "text": "Selection", "bbox": [112, 82, 220, 110], "confidence": 0.92, "lineBreak": false },
    { "text": "Test", "bbox": [232, 82, 292, 110], "confidence": 0.91, "lineBreak": true },
    { "text": "中文", "bbox": [50, 124, 112, 152], "confidence": 0.90, "lineBreak": false },
    { "text": "测试", "bbox": [124, 124, 186, 152], "confidence": 0.89, "lineBreak": true }
  ]
}
```

如果不想引入 JSON 解析依赖，C 层可以直接构造 fixture，Python 层仍保留 JSON 作为文档化期望。

### 测试图片

有两种可选方案。

方案 A：运行时生成 bitmap。

- 优点：不需要提交二进制图片。
- 缺点：字体渲染可能受环境影响。
- 适合 mock OCR 交互测试，因为 OCR 结果来自 mock，不依赖真实识别。

方案 B：提交固定 PNG/BMP。

- 优点：真实 OCR smoke test 更稳定。
- 缺点：需要维护二进制 fixture。
- 适合真实 OCR 后端回归。

推荐：

- mock 交互测试使用运行时生成的简单背景图。
- 真实 OCR smoke test 使用固定图片。

建议生成图片规则：

- 尺寸固定 `400x240`。
- 背景纯白。
- word bbox 区域绘制浅灰边框，方便渲染调试。
- 文本使用常见字体，例如 `Segoe UI` 或 `Microsoft YaHei`。
- 即使字体不存在，mock OCR 测试也不失败，因为它只验证交互和坐标。

## Python 自动化脚本设计

建议新增脚本：

- `test/screenshot_ocr_selection_test.py`

核心结构：

```python
@dataclass
class CheckResult:
    name: str
    passed: bool
    details: str
    stdout_path: str | None = None
    stderr_path: str | None = None
    artifact_paths: list[str] = field(default_factory=list)

class OcrSelectionSuite:
    def ensure_ready(self) -> None: ...
    def find_existing_instances(self) -> list[dict[str, str]]: ...
    def stop_existing_instances(self, instances: list[dict[str, str]]) -> None: ...
    def ensure_no_existing_instance(self, phase: str, test_name: str) -> None: ...
    def run_command(self, args: list[str], name: str, timeout: int = 20) -> CompletedProcess[str]: ...
    def test_data_model(self) -> None: ...
    def test_layout(self) -> None: ...
    def test_hit_test(self) -> None: ...
    def test_selection(self) -> None: ...
    def test_copy_text(self) -> None: ...
    def test_float_ui(self) -> None: ...
    def test_e2e_mock(self) -> None: ...
    def write_reports(self) -> None: ...
```

建议参数：

```text
--stop-existing
--include-real-ocr
--allow-clipboard
--stress
--timeout <seconds>
--exe <path>
--output-dir <path>
```

报告 JSON 建议格式：

```json
{
  "suite": "screenshot-ocr-selection",
  "startedAt": "2026-04-18T23:59:00",
  "exe": "D:/code/PowerCapslock/build/powercapslock.exe",
  "passed": 7,
  "total": 7,
  "results": [
    {
      "name": "OCR layout",
      "passed": true,
      "details": "12 cases passed",
      "stdout": "commands/layout_stdout.txt",
      "stderr": "commands/layout_stderr.txt",
      "artifacts": []
    }
  ]
}
```

Markdown 报告建议包含：

- 总结。
- 每个测试命令。
- 返回码。
- 失败断言。
- 产物路径。
- 环境信息：Windows 版本、DPI、屏幕尺寸、当前分支、commit。

## C 层测试实现细节

### 断言宏

建议新增轻量断言宏，放在测试源文件顶部或 `src/test_utils.h`。

```c
#define TEST_ASSERT_TRUE(expr, msg) \
    do { \
        if (!(expr)) { \
            printf("ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, msg); \
            return 1; \
        } \
    } while (0)

#define TEST_ASSERT_INT_EQ(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            printf("ASSERT FAILED: %s:%d: %s expected=%d actual=%d\n", \
                   __FILE__, __LINE__, msg, (expected), (actual)); \
            return 1; \
        } \
    } while (0)
```

复杂测试建议不要遇到第一个失败就退出，而是累计 failures，最后统一返回。

### JSON 输出

初期可以只输出文本。为了 Python 更稳，建议每个命令支持可选 JSON：

```powershell
build\powercapslock.exe --test-ocr-layout --json test\output\ocr_layout.json
```

如果不想新增通用参数解析，可以先让 Python 从 stdout 解析 `PASSED` 和 `FAILED`。但长期推荐 JSON，因为后续模型更容易消费结构化结果。

### 测试图片创建

建议提供工具函数：

```c
ScreenshotImage* ScreenshotTestCreateSolidImage(int width, int height, COLORREF color);
ScreenshotImage* ScreenshotTestCreateOcrFixtureImage(void);
```

注意：

- `ScreenshotImage` 的像素格式必须与 `screenshot.c` 约定一致。
- 如果是 32-bit BGRA，写像素时要确认通道顺序。
- 每个测试结束必须调用 `ScreenshotImageFree()`。

### OCR fixture 创建

建议提供：

```c
OCRResults* OCRTestCreateBasicResults(void);
OCRResults* OCRTestCreateChineseResults(void);
OCRResults* OCRTestCloneResults(const OCRResults* source);
```

不要让测试复用生产 `OCRRecognize()` 的内部静态指针。每次测试都应分配独立结果，测试结束释放。

### 坐标舍入规则

坐标映射中 `float` 到 `int` 可能有舍入误差。测试应统一规则：

- 点坐标允许 `<= 1` 像素误差。
- 矩形宽高允许 `<= 1` 像素误差。
- hit-test 点应选 bbox 中心点，避免边界舍入影响。
- 边界测试单独覆盖 left/right/top/bottom 是否包含。

### 剪贴板测试隔离

默认 CI 不建议写系统剪贴板。必须写剪贴板时：

1. 调用 `OpenClipboard(hwnd)`。
2. 如果失败，最多重试 5 次。
3. 读取原 `CF_UNICODETEXT`，复制到进程内存。
4. `EmptyClipboard()`。
5. 写入测试文本。
6. 读取并断言。
7. 尝试恢复原文本。
8. `CloseClipboard()` 必须在所有路径执行。

如果恢复失败，测试报告中必须标记：

```text
WARNING: clipboard restore failed
```

## Win32 UI 自动化技术细节

### 消息泵

创建窗口后需要短暂 pump messages，确保 `WM_CREATE`、`WM_PAINT` 等消息处理完成。

建议工具函数：

```c
static void PumpMessagesFor(DWORD milliseconds) {
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < milliseconds) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(1);
    }
}
```

### 发送鼠标消息

建议封装：

```c
static void SendMouseMove(HWND hwnd, int x, int y, WPARAM keys) {
    SendMessage(hwnd, WM_MOUSEMOVE, keys, MAKELPARAM(x, y));
}

static void SendLeftDrag(HWND hwnd, POINT from, POINT to) {
    SendMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(from.x, from.y));
    SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(from.x, from.y));
    SendMessage(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(to.x, to.y));
    SendMessage(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(to.x, to.y));
}
```

注意：

- 使用 client 坐标，不是 screen 坐标。
- 不依赖 `SetCursorPos()`。
- 如果窗口逻辑调用 `GetCursorPos()` 处理拖动窗口，OCR 文字选择测试应确保从 word 区域开始拖选，避免走窗口拖动分支。

### 渲染验证

推荐新增：

```c
bool ScreenshotFloatTestRenderToImage(ScreenshotImage** outImage);
bool ScreenshotFloatTestSaveRender(const char* path);
```

实现方式：

- 获取窗口 client rect。
- 创建兼容 DC 和 bitmap。
- 调用内部绘制函数或发送 `WM_PRINTCLIENT`。
- 读取 bitmap 像素到 `ScreenshotImage`。

如果内部绘制函数是 static，可以提供测试 wrapper：

```c
#ifdef POWERCAPSLOCK_TESTING
bool ScreenshotFloatTestDrawToHdc(HDC hdc, RECT rect) {
    DrawFloatWindow(hdc, &g_float);
    return true;
}
#endif
```

验证策略：

- 不做整图精确比较。
- 对 word bbox 对应区域做采样。
- 计算区域平均色或非背景像素比例。
- 允许颜色差异阈值，例如每通道误差 `<= 20`。

## 异步 OCR 自动化测试细节

后续真实 OCR 接入时，建议先引入后端抽象：

```c
typedef struct {
    OCRResults* (*recognize)(const ScreenshotImage* image, void* userData);
    void* userData;
} OCRBackend;

void OCRSetBackendForTesting(const OCRBackend* backend);
void OCRResetBackendForTesting(void);
```

异步消息建议：

```c
#define WM_APP_OCR_COMPLETE (WM_APP + 101)
#define WM_APP_OCR_FAILED   (WM_APP + 102)
```

测试用例需要覆盖：

- backend 立即成功。
- backend 延迟成功。
- backend 返回失败。
- backend 运行中关闭窗口。
- backend 完成时 HWND 已销毁。
- 连续两次打开 OCR 浮窗，旧请求结果被丢弃。

为避免旧请求覆盖新状态，建议引入 request id：

```c
DWORD ocrRequestId;
```

后台线程完成时携带 request id，UI 线程只接受当前 request id 的结果。

测试断言：

- 旧 request id 的完成消息被忽略并释放内存。
- `ocrInProgress` 最终为 false。
- `ocrResults` 在成功时非空，失败时为空。
- `hoveredWordIndex` 初始为 `-1`。
- 窗口关闭后不再访问已释放 image。

## 分阶段自动化验收矩阵

本节列出的 `build\powercapslock.exe --test-*` 命令都是测试目标。实际执行时必须通过 Python runner 或等价包装器运行，确保每条命令前后都完成单例实例清理。

如果临时手工执行单条命令，建议先定义 PowerShell 包装函数：

```powershell
function Stop-PowerCapslockInstances {
    $instances = Get-CimInstance Win32_Process -Filter "name = 'powercapslock.exe'"
    foreach ($item in $instances) {
        Write-Host "Stopping powercapslock.exe pid=$($item.ProcessId) path=$($item.ExecutablePath)"
        Stop-Process -Id $item.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

function Invoke-PowerCapslockTest {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    Stop-PowerCapslockInstances
    & .\build\powercapslock.exe @Arguments
    $exitCode = $LASTEXITCODE
    Stop-PowerCapslockInstances

    if ($exitCode -ne 0) {
        throw "powercapslock.exe $($Arguments -join ' ') failed with exit code $exitCode"
    }
}
```

调用示例：

```powershell
Invoke-PowerCapslockTest @("--test-ocr-layout")
Invoke-PowerCapslockTest @("--test-ocr-float-ui", "--case", "hover")
```

### 阶段 0：基线确认

目标：

- 确认现有截图功能测试通过。

必须运行：

```powershell
cmake --build build
build\powercapslock.exe --test-screenshot-all
build\powercapslock.exe --test-screenshot-toolbar
build\powercapslock.exe --test-screenshot-annotate
build\powercapslock.exe --test-screenshot-float
build\powercapslock.exe --test-screenshot-ocr
```

验收：

- 所有命令返回 `0`。
- 没有残留测试窗口。
- 没有残留 `test_*.bmp` 文件，或脚本会自动清理。

### 阶段 1：OCR 词级数据模型与 mock 识别

目标：

- OCR 能返回稳定 words、bbox、confidence、lineBreak。

必须新增并运行：

```powershell
build\powercapslock.exe --test-ocr-data-model
build\powercapslock.exe --test-screenshot-ocr
```

验收：

- mock words 数量、文本、bbox 完全匹配 fixture。
- `fullText` 拼接正确。
- 连续创建释放无崩溃。

### 阶段 2：悬浮窗口 OCR 模式与统一坐标映射

目标：

- 普通浮窗和 OCR 浮窗状态隔离。
- 图片 layout 统一。

必须新增并运行：

```powershell
build\powercapslock.exe --test-ocr-layout
build\powercapslock.exe --test-screenshot-float
```

验收：

- 普通 `ScreenshotFloatShow()` 不进入 OCR 模式。
- `ScreenshotFloatShowOcr()` 进入 OCR 模式。
- 所有 layout case 通过。
- 坐标来回转换误差不超过 1 像素。

### 阶段 3：OCR hover 高亮与鼠标命中

目标：

- 鼠标移动到 word bbox 时命中正确 word。

必须新增并运行：

```powershell
build\powercapslock.exe --test-ocr-hit-test
build\powercapslock.exe --test-ocr-float-ui --case hover
```

验收：

- image 坐标 hit-test 通过。
- window 坐标 hit-test 通过。
- hover 状态从 `-1` 变为目标 word index。
- 鼠标移出文字区域后 hover 回到 `-1`。
- 渲染采样能检测到 hover 高亮。

### 阶段 4：拖选 OCR 文本与复制

目标：

- 拖选、跨行选择、反向选择、文本复制全部自动验证。

必须新增并运行：

```powershell
build\powercapslock.exe --test-ocr-selection
build\powercapslock.exe --test-ocr-copy-text
build\powercapslock.exe --test-ocr-float-ui --case drag-select
build\powercapslock.exe --test-ocr-e2e-mock
```

验收：

- 单词选择、同行选择、跨行选择、反向选择均通过。
- UTF-8 中文复制转换不乱码。
- 选区高亮渲染采样通过。
- 清除选择后不再返回文本。

### 阶段 5：OCR 入口接入截图流程

目标：

- 截图选区完成后可以通过指定入口进入 OCR 浮窗。
- OCR 使用原始选区图，不使用带标注图。

必须新增并运行：

```powershell
build\powercapslock.exe --test-ocr-e2e-mock --case manager-entry
build\powercapslock.exe --test-screenshot-annotate
```

验收：

- Manager 调用 OCR 入口时使用 `ScreenshotOverlayGetSelectionImage()`。
- 保存、复制、挂起仍使用各自既有逻辑。
- 标注后 OCR mock 输入图不包含标注结果，除非产品显式要求识别标注后图像。

### 阶段 6：异步 OCR 框架

目标：

- OCR 可以后台执行，UI 不阻塞。

必须新增并运行：

```powershell
build\powercapslock.exe --test-ocr-async
```

验收：

- 成功、失败、取消、连续请求都通过。
- 关闭窗口后后台完成不会崩溃。
- 旧请求不会覆盖新请求。
- 压力循环无崩溃。

### 阶段 7：真实 OCR 后端接入

目标：

- 真实 OCR 后端可用时能通过 smoke test。

可选运行：

```powershell
build\powercapslock.exe --test-ocr-real-smoke
python test\screenshot_ocr_selection_test.py --include-real-ocr
```

验收：

- 后端不可用时标记 skipped。
- 后端可用时识别固定图片中的关键词。
- 输出 word bbox。
- 输出 confidence。
- 不影响 mock 测试稳定性。

## CI 接入方案

建议在 GitHub Actions Windows job 中加入：

```yaml
name: Windows Tests

on:
  pull_request:
  push:
    branches: [ master ]

jobs:
  test-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
      - name: Build
        run: cmake --build build
      - name: Screenshot baseline tests
        run: python test/screenshot_function_test.py --stop-existing
      - name: OCR selection tests
        run: python test/screenshot_ocr_selection_test.py --stop-existing
      - name: Upload test reports
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: test-output
          path: test/output/
```

如果 CI runner 没有 MinGW 或项目依赖未预装，需要先沿用当前项目已有构建环境配置。此文档只定义测试目标，不强制改变构建工具链。

## Flaky 测试规避策略

### 不依赖真实鼠标和键盘

- 使用 `SendMessage` 发送 client 坐标消息。
- 不使用 `SetCursorPos`。
- 不使用全局热键触发截图。
- 不使用真实 `Ctrl+C` 作为默认测试路径。

### 控制窗口位置和大小

- 测试窗口使用固定坐标，例如 `(100, 100)`。
- 如果虚拟屏幕不包含该坐标，使用 `ClampWindowToVirtualScreen()` 后读取实际窗口 rect。
- 所有断言基于 client 坐标。

### 控制 DPI 和缩放

- 测试不要断言物理屏幕像素位置。
- 测试基于窗口 client rect 和 layout 计算。
- 坐标误差允许 1 像素。

### 控制颜色验证

- 不做整图 hash。
- 只做区域采样。
- 允许颜色阈值。
- 高亮验证优先检查“区域发生变化”，不是精确 RGB。

### 控制剪贴板

- 默认只测试转换函数。
- 真实剪贴板测试显式开启。
- 写剪贴板前后保存和恢复。

### 控制进程残留

Python 脚本启动前和每条测试命令前后都必须检查 `powercapslock.exe`。这是必须项，不是可选优化，因为 PowerCapslock 单例模式会让残留实例影响下一条测试命令。

如果传入 `--stop-existing`：

```powershell
taskkill /IM powercapslock.exe /F
```

上面的 `taskkill` 是兜底方式。正式 runner 应优先记录并按 PID 关闭：

```powershell
Get-CimInstance Win32_Process -Filter "name = 'powercapslock.exe'" |
    Select-Object ProcessId, ExecutablePath, CommandLine
```

如果没有传入 `--stop-existing`，不应擅自杀进程，而是报错提示，并在报告中列出发现的 PID、路径和命令行。

每条测试命令必须满足：

- before 阶段没有任何 `powercapslock.exe` 残留。
- after 阶段没有任何 `powercapslock.exe` 残留。
- 如果 after 阶段发现残留，即使命令返回码为 `0`，也应记录 warning；如果无法清理残留，应判定该测试失败。

## 失败诊断输出要求

每个失败用例至少输出：

- 测试名。
- 输入参数。
- 期望值。
- 实际值。
- 当前 commit。
- 当前窗口 client rect。
- 当前 image layout。
- 如果是 UI 渲染失败，保存渲染 bitmap。
- 如果是拖选失败，输出 anchor、active、selected range、selected text。

示例：

```text
FAILED: ocr drag select cross line
image=400x240 client=400x240 scale=1.000 offset=(0,0)
fromWord=0 toWord=4
expected="Hello World\r\nOCR Selection Test"
actual="Hello World OCR Selection Test"
render=artifacts/drag_select_cross_line.bmp
```

## 推荐文件改动清单

建议后续实现时按以下文件拆分：

- `src/screenshot_ocr_selection.h`
- `src/screenshot_ocr_selection.c`
- `src/screenshot_ocr_test.h`
- `src/screenshot_float_test.h`
- `src/main.c`
- `src/screenshot_ocr.c`
- `src/screenshot_float.c`
- `CMakeLists.txt`
- `test/screenshot_ocr_selection_test.py`
- `test/fixtures/ocr/mock_words_basic.json`
- `test/fixtures/ocr/README.md`

如果不想新增太多文件，最低限度也应做到：

- 把 layout、hit-test、selection text 拼接从 static 函数中拆出来。
- 给 `src/main.c` 增加 `--test-screenshot-ocr-selection-all`。
- 给 Python 增加一个统一跑测试的脚本。

## 推荐提交拆分

建议按以下顺序提交，方便回滚和后续模型接手：

1. 新增 OCR selection 纯函数和单元测试。
2. 新增 OCR fixture 和 Python 测试脚本。
3. 新增浮窗测试接口和 Win32 消息驱动测试。
4. 新增拖选、复制、渲染采样测试。
5. 新增异步 OCR fake backend 和异步测试。
6. 新增真实 OCR smoke test。
7. 接入 CI。

每个提交都应保证：

- `cmake --build build` 通过。
- 已有截图测试通过。
- 新增对应阶段测试通过。

## 最小自动化验收命令

当 OCR 文字选择核心功能完成后，最小验收命令应为：

```powershell
cmake --build build
python test\screenshot_ocr_selection_test.py --stop-existing
```

不推荐在最终验收时手工逐条直接运行 `build\powercapslock.exe --test-*`，除非每条命令前后都手动执行单例进程清理。推荐由 Python runner 统一编排，因为 runner 能保证每个测试用例前后都检查并停止已有实例。

如果任何命令失败，则不能认为 OCR 文字选择功能完成。

## 完成定义

OCR 文字选择功能达到可合并状态时，需要满足：

- 纯函数测试覆盖 layout、hit-test、selection、UTF-8/UTF-16 转换。
- Win32 消息驱动测试覆盖 hover 和拖选。
- Python 脚本可以一键跑完整测试并输出报告。
- Mock OCR e2e 测试通过。
- 普通截图保存、复制、挂起、标注测试仍通过。
- 真实 OCR 后端不可用时不会影响默认测试。
- 测试失败时有足够产物帮助定位。

这套测试方案的核心思想是：先把 OCR 文字选择拆成可验证的纯逻辑，再用窗口消息验证真实 UI 集成，最后才用真实 OCR 后端做 smoke test。这样既能覆盖用户体验，又能保持自动化测试稳定。
