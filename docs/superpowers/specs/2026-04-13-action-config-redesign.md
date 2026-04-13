# 快捷键配置交互优化设计

## 概述

优化 PowerCapslock 的快捷键配置功能，支持多种动作类型（按键映射、内置功能、执行命令），并采用键盘输入检测代替表单填写，提升用户体验。

## 目标

1. **键盘输入检测** - 用户直接按键完成配置，无需填写表单
2. **多动作类型支持** - 按键映射、内置功能、执行命令
3. **可插拔架构** - 内置功能可动态注册，便于后续扩展
4. **向后兼容** - 自动迁移现有按键映射配置

---

## 数据模型

### 配置文件结构

```json
{
  "actions": [
    {
      "trigger": "H",
      "type": "key_mapping",
      "param": "LEFT"
    },
    {
      "trigger": "A",
      "type": "builtin",
      "param": "voice_input"
    },
    {
      "trigger": "S",
      "type": "command",
      "param": "C:\\Tools\\screenshot.exe --region"
    }
  ]
}
```

### 枚举定义

```c
typedef enum {
    ACTION_TYPE_KEY_MAPPING,    // 按键映射
    ACTION_TYPE_BUILTIN,        // 内置功能
    ACTION_TYPE_COMMAND         // 执行命令
} ActionType;

typedef enum {
    BUILTIN_VOICE_INPUT,        // 语音输入
    BUILTIN_SCREENSHOT,         // 截图
    BUILTIN_TOGGLE_ENABLED,     // 启用/禁用切换
    BUILTIN_COUNT               // 边界值，用于扩展
} BuiltinAction;
```

### 统一动作结构

```c
typedef struct {
    char trigger[16];           // 触发键名称，如 "H", "A", "F1"
    ActionType type;            // 动作类型
    char param[256];            // 参数：按键名/内置功能名/命令路径
} Action;
```

### 向后兼容

加载配置时自动检测格式：
- 新格式：直接加载 `actions` 数组
- 旧格式：加载 `mappings` 数组并转换为 `actions` 格式，保存时写为新格式

---

## UI 交互设计

### 配置页面布局

```
┌─────────────────────────────────────────────────────────┐
│  快捷键配置                                              │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │  + 添加快捷键                                     │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │ CapsLock + H  →  按键映射: LEFT        [编辑][删除] │
│  │ CapsLock + A  →  内置功能: 语音输入    [编辑][删除] │
│  │ CapsLock + S  →  执行命令: screenshot  [编辑][删除] │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 添加/编辑流程

**步骤 1：捕获触发键**

显示 `CapsLock + [ 等待按键... ]`，用户按下任意键后显示具体按键名称。松开 CapsLock 可取消。

**步骤 2：选择动作类型（分类卡片）**

- 🎹 **按键映射** - 输出另一个按键
- ⚡ **内置功能** - 语音输入、截图、启用/禁用
- 💻 **执行命令** - 运行外部程序或脚本

**步骤 3：配置具体参数**

根据动作类型显示不同配置面板：
- 按键映射：捕获输出键 + 常用键快捷按钮
- 内置功能：单选列表选择功能
- 执行命令：路径输入框 + 浏览按钮 + 参数输入框

### 冲突处理

当用户尝试添加已存在的触发键时，显示冲突提示，提供"取消"和"覆盖原有绑定"选项。

---

## 后端架构

### 新增模块

```
src/
├── action.c / action.h       # 统一动作管理
├── action_builtin.c / .h     # 内置功能注册与执行
```

### 修改模块

```
src/
├── keymap.c / keymap.h       # 保留按键映射查询，从 action 模块获取
├── hook.c / hook.h           # 触发键检测 + 动作分发
├── config.c / config.h       # 加载/保存 actions 配置
├── config_dialog_webview.cpp # WebView2 消息处理
```

### 核心接口

**action.h - 统一动作管理**

```c
void ActionInit(void);
void ActionCleanup(void);

const Action* ActionFindByTrigger(WORD scanCode);
int ActionGetCount(void);
const Action* ActionGet(int index);

bool ActionAdd(const Action* action);
bool ActionUpdate(int index, const Action* action);
bool ActionDelete(int index);
void ActionResetToDefaults(void);

bool ActionExecute(const Action* action);
```

**action_builtin.h - 可插拔内置功能**

```c
typedef bool (*BuiltinHandler)(void);

void BuiltinRegister(const char* name, BuiltinHandler handler);
bool BuiltinExecute(const char* name);
const char** BuiltinGetList(int* count);
```

### 可插拔架构

内置功能通过注册机制动态添加：

```c
void BuiltinInit(void) {
    BuiltinRegister("voice_input", HandleVoiceInput);
    BuiltinRegister("screenshot", HandleScreenshot);
    BuiltinRegister("toggle_enabled", HandleToggleEnabled);
}
```

后续添加新功能只需注册新的处理器，无需修改核心逻辑。

---

## 前端实现

### WebView2 通信协议

**C → JS 事件**

| 事件名 | 数据 | 说明 |
|--------|------|------|
| `trigger_captured` | `{"key": "H", "displayName": "H"}` | 触发键捕获成功 |
| `output_key_captured` | `{"key": "LEFT", "displayName": "←"}` | 输出键捕获成功 |
| `builtin_list` | `["voice_input", "screenshot", ...]` | 内置功能列表 |
| `actions_loaded` | `[Action...]` | 现有动作列表 |

**JS → C 消息**

| 动作 | 参数 | 说明 |
|------|------|------|
| `start_capture` | `type: "trigger" | "output"` | 开始按键捕获 |
| `save_action` | `trigger, type, param` | 保存动作 |
| `delete_action` | `index` | 删除动作 |
| `browse_file` | - | 打开文件浏览对话框 |

### 按键捕获流程

1. JS 发送 `start_capture` 消息
2. C 设置捕获模式标志
3. Hook 层检测按键，发送捕获事件
4. JS 更新 UI 显示
5. 用户完成配置后发送 `save_action`

---

## 实现计划

### 阶段 1：后端基础

1. 创建 `action.c/h` 模块，实现统一动作管理
2. 创建 `action_builtin.c/h` 模块，实现可插拔注册机制
3. 修改 `config.c`，支持新配置格式加载/保存/迁移
4. 修改 `hook.c`，集成动作查询与执行

### 阶段 2：前端交互

1. 修改 `config_ui.html`，实现新的 UI 布局
2. 实现按键捕获交互流程
3. 实现分类卡片选择
4. 实现各动作类型的参数配置面板

### 阶段 3：集成测试

1. 测试配置加载/保存/迁移
2. 测试按键捕获准确性
3. 测试各动作类型执行
4. 测试冲突检测

---

## 风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| 旧配置迁移失败 | 迁移前备份，失败时保留原配置 |
| 按键捕获冲突 | 在 Hook 层优先处理捕获模式 |
| 内置功能执行失败 | 返回错误状态，UI 显示提示 |
| WebView2 通信延迟 | 使用异步消息队列，避免阻塞 |
