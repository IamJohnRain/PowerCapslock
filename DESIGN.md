# PowerCapslock 技术设计方案

## 项目概述

Windows 平台下的键盘热键映射工具，将 CapsLock 键作为修饰键，实现组合键映射功能。

## 核心功能需求

### 键位映射

| 组合键 | 映射目标 | 说明 |
|--------|----------|------|
| CapsLock + h | 左方向键 | 光标左移 |
| CapsLock + j | 下方向键 | 光标下移 |
| CapsLock + k | 上方向键 | 光标上移 |
| CapsLock + l | 右方向键 | 光标右移 |
| CapsLock + i | Home 键 | 行首 |
| CapsLock + o | End 键 | 行尾 |
| CapsLock + u | PageDown | 下一页 |
| CapsLock + p | PageUp | 上一页 |
| CapsLock + 1 | F1 | 功能键 |
| CapsLock + 2 | F2 | 功能键 |
| CapsLock + 3 | F3 | 功能键 |
| CapsLock + 4 | F4 | 功能键 |
| CapsLock + 5 | F5 | 功能键 |
| CapsLock + 6 | F6 | 功能键 |
| CapsLock + 7 | F7 | 功能键 |
| CapsLock + 8 | F8 | 功能键 |
| CapsLock + 9 | F9 | 功能键 |
| CapsLock + 0 | F10 | 功能键 |
| CapsLock + - | F11 | 功能键 |
| CapsLock + = | F12 | 功能键 |

### 附加功能

1. **多语言键盘布局支持**：基于 Scan Code 识别物理按键位置，不受语言布局影响
2. **日志调试功能**：支持不同级别的日志输出，便于问题排查
3. **系统托盘图标**：显示程序状态，提供快捷操作菜单
4. **开机自动启动**：通过注册表实现
5. **配置文件支持**：JSON 格式配置，支持自定义映射

## 技术架构

### 核心技术选型

- **开发语言**: C11
- **开发环境**: Windows + MinGW-w64 或 MSVC
- **构建工具**: CMake
- **核心技术**: Windows Low-level Keyboard Hook (WH_KEYBOARD_LL)

### 系统架构

```
┌─────────────────────────────────────────┐
│         Windows 消息循环                 │
│    SetWindowsHookEx(WH_KEYBOARD_LL)     │
│    注册低级别键盘钩子                     │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│         键盘钩子回调函数                  │
│    LowLevelKeyboardProc                 │
│                                         │
│    1. 检测 CapsLock 按下                 │
│    2. 设置标志位 capslock_held = true   │
│    3. 拦截 CapsLock 事件                 │
│                                         │
│    4. 检测映射键 (h/j/k/l/i/o/u/p/1-9/0/-/=)  │
│       如果 capslock_held == true:        │
│          - 发送目标键事件                 │
│          - 拦截原按键                     │
│       否则:                             │
│          - 正常传递                       │
│                                         │
│    5. 检测 CapsLock 释放                  │
│       capslock_held = false              │
└─────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────┐
│         模拟键盘输入                     │
│    SendInput()                          │
│    发送目标 Virtual Key                  │
└─────────────────────────────────────────┘
```

### 模块设计

#### 1. 键盘钩子模块 (hook.c/h)

**职责**: 安装和管理低级别键盘钩子

**主要函数**:
```c
HHOOK HookInstall(void);           // 安装钩子
void HookUninstall(HHOOK hHook);   // 卸载钩子
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
void SendKeyInput(UINT vk, BOOL keyDown);
```

**关键逻辑**:
- 使用 `SetWindowsHookEx(WH_KEYBOARD_LL, ...)` 注册全局钩子
- 在回调函数中拦截 CapsLock 事件
- 当 CapsLock 按下时，检查后续按键是否在映射表中
- 使用 `SendInput()` 发送目标按键事件
- 通过返回 1 拦截原按键，通过 `CallNextHookEx()` 传递其他按键

#### 2. 键位映射模块 (keymap.c/h)

**职责**: 管理键位映射表

**数据结构**:
```c
typedef struct {
    WORD scanCode;          // 物理按键扫描码（多语言支持）
    UINT targetVk;          // 目标 Virtual Key
    const char* name;       // 映射名称（用于日志）
} KeyMapping;
```

**主要函数**:
```c
void KeymapInit(void);
KeyMapping* KeymapFindByScanCode(WORD scanCode);
void KeymapAddMapping(WORD scanCode, UINT targetVk, const char* name);
void KeymapClear(void);
void KeymapResetToDefaults(void);
```

**默认映射表**（基于美式键盘物理布局的 Scan Code）:
```c
// 方向键
{0x23, VK_LEFT,  "H->Left"},    // H
{0x24, VK_DOWN,  "J->Down"},    // J
{0x25, VK_UP,    "K->Up"},      // K
{0x26, VK_RIGHT, "L->Right"},   // L

// 功能键
{0x17, VK_HOME,      "I->Home"},     // I
{0x18, VK_END,       "O->End"},      // O
{0x16, VK_PRIOR,     "P->PageUp"},   // P
{0x15, VK_NEXT,      "U->PageDown"}, // U

// F键 (1-9,0,-,=)
{0x02, VK_F1,  "1->F1"},   // 1
{0x03, VK_F2,  "2->F2"},   // 2
// ... 以此类推
```

#### 3. 系统托盘模块 (tray.c/h)

**职责**: 管理系统托盘图标和菜单

**主要函数**:
```c
BOOL TrayInit(HINSTANCE hInstance);
void TrayCleanup(void);
void TraySetState(BOOL enabled);
void TrayShowMenu(void);
LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
```

**菜单项**:
- 启用/禁用（带勾选标记）
- 查看日志
- 关于
- 退出

**图标状态**:
- 正常图标：程序运行中
- 禁用图标：程序已暂停

#### 4. 日志模块 (logger.c/h)

**职责**: 日志记录和管理

**日志级别**:
```c
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;
```

**主要函数**:
```c
void LoggerInit(const char* logPath);
void LoggerCleanup(void);
void LogMessage(LogLevel level, const char* format, ...);
void LoggerSetLevel(LogLevel level);
```

**日志文件位置**: `%APPDATA%\PowerCapslock\logs\powercapslock.log`

#### 5. 配置模块 (config.c/h)

**职责**: 配置文件加载和解析

**数据结构**:
```c
typedef struct {
    char modifierKey[32];
    BOOL suppressOriginal;
    BOOL controlLed;
    BOOL startEnabled;
    LogLevel logLevel;
    BOOL logToFile;
    char keyboardLayout[32];
} Config;
```

**主要函数**:
```c
BOOL ConfigLoad(const char* path);
BOOL ConfigSave(const char* path);
void ConfigLoadDefaults(void);
const Config* ConfigGet(void);
```

**配置文件位置**: `%APPDATA%\PowerCapslock\config.json`

#### 6. 键盘布局模块 (keyboard_layout.c/h)

**职责**: 处理多语言键盘布局

**主要函数**:
```c
HKL KeyboardLayoutGetCurrent(void);
WORD KeyboardLayoutVkToScanCode(UINT vkCode);
UINT KeyboardLayoutScanCodeToVk(WORD scanCode);
char* KeyboardLayoutGetName(HKL hkl);
```

**关键技术**:
- 使用 `MapVirtualKeyEx()` 进行 VK <-> Scan Code 转换
- 基于 Scan Code 识别物理按键位置
- 支持动态布局切换检测

### 多语言键盘布局支持

**问题**: 不同语言布局下，同一个物理按键可能对应不同的字符

**解决方案**:
1. 使用 **Scan Code**（扫描码）识别物理按键位置
2. Scan Code 是硬件级别的编码，不受软件布局影响
3. 在钩子回调中获取 `KBDLLHOOKSTRUCT.scanCode`
4. 映射表基于 Scan Code 而非 VK Code

**示例**:
```c
// 美式键盘上 H 键的 Scan Code 是 0x23
// 无论切换到中文、德文还是法文布局
// 物理 H 键始终是 0x23

// 在钩子回调中
KBDLLHOOKSTRUCT *pKb = (KBDLLHOOKSTRUCT*)lParam;
WORD scanCode = pKb->scanCode;

// 查找映射
KeyMapping* mapping = KeymapFindByScanCode(scanCode);
```

### 全局状态管理

```c
typedef struct {
    BOOL enabled;           // 程序是否启用
    BOOL capslockHeld;      // CapsLock 是否按下
    HHOOK hHook;           // 键盘钩子句柄
    HWND hTrayWnd;         // 托盘窗口句柄
    HMENU hTrayMenu;       // 托盘菜单句柄
    HINSTANCE hInstance;   // 程序实例句柄
    Config config;         // 配置
} AppState;

AppState g_app = {0};
```

## 程序流程

### 启动流程

```
WinMain()
    │
    ├── LoggerInit()          // 初始化日志
    │
    ├── ConfigLoad()          // 加载配置
    │   └── 如果失败，使用默认配置
    │
    ├── KeymapInit()          // 初始化映射表
    │   └── 加载默认映射
    │
    ├── TrayInit()            // 初始化托盘
    │   ├── 创建隐藏窗口
    │   ├── 加载图标
    │   └── 创建菜单
    │
    ├── HookInstall()         // 安装键盘钩子
    │   └── SetWindowsHookEx()
    │
    └── 消息循环
        └── GetMessage() / DispatchMessage()
```

### 按键处理流程

```
键盘事件发生
    │
    ▼
LowLevelKeyboardProc()
    │
    ├── 检查 nCode
    │   └── 如果 < 0，直接 CallNextHookEx()
    │
    ├── 检查程序状态
    │   └── 如果 disabled，直接 CallNextHookEx()
    │
    ├── 解析按键信息
    │   ├── vkCode (Virtual Key)
    │   ├── scanCode (Scan Code)
    │   └── flags (按下/释放)
    │
    ├── 检查是否是 CapsLock
    │   │
    │   ├── 是 CapsLock 按下
    │   │   ├── 设置 capslockHeld = TRUE
    │   │   ├── 记录日志
    │   │   └── 返回 1 (拦截)
    │   │
    │   └── 是 CapsLock 释放
    │       ├── 设置 capslockHeld = FALSE
    │       ├── 记录日志
    │       └── 返回 1 (拦截)
    │
    ├── 检查是否在映射表中
    │   │
    │   ├── 是映射键且 capslockHeld == TRUE
    │   │   ├── 查找目标 VK
    │   │   ├── SendKeyInput(targetVk, TRUE)   // 按下
    │   │   ├── SendKeyInput(targetVk, FALSE)  // 释放
    │   │   ├── 记录日志
    │   │   └── 返回 1 (拦截原按键)
    │   │
    │   └── 不是映射键或 capslockHeld == FALSE
    │       └── CallNextHookEx() (正常传递)
    │
    └── 返回结果
```

### 托盘消息处理

```
TrayWndProc()
    │
    ├── WM_CREATE
    │   └── 添加托盘图标
    │
    ├── WM_DESTROY
    │   └── 移除托盘图标
    │
    ├── WM_TRAYICON (自定义消息)
    │   ├── 左键单击
    │   │   └── 切换 enabled 状态
    │   │       ├── enabled = !enabled
    │   │       └── TraySetState(enabled)
    │   │
    │   └── 右键单击
    │       └── 显示菜单
    │
    ├── WM_COMMAND (菜单命令)
    │   ├── IDM_ENABLE
    │   │   ├── enabled = TRUE
    │   │   └── TraySetState(TRUE)
    │   │
    │   ├── IDM_DISABLE
    │   │   ├── enabled = FALSE
    │   │   └── TraySetState(FALSE)
    │   │
    │   ├── IDM_SHOW_LOG
    │   │   └── ShellExecute(log文件)
    │   │
    │   ├── IDM_ABOUT
    │   │   └── MessageBox(关于信息)
    │   │
    │   └── IDM_EXIT
    │       └── PostQuitMessage()
    │
    └── DefWindowProc()
```

## 配置文件格式

### keymap.json

```json
{
    "version": "1.0",
    "modifier": {
        "key": "CAPSLOCK",
        "suppress_original": true,
        "control_led": false
    },
    "mappings": [
        {"from": "H", "to": "LEFT"},
        {"from": "J", "to": "DOWN"},
        {"from": "K", "to": "UP"},
        {"from": "L", "to": "RIGHT"},
        {"from": "I", "to": "HOME"},
        {"from": "O", "to": "END"},
        {"from": "U", "to": "PAGEDOWN"},
        {"from": "P", "to": "PAGEUP"},
        {"from": "1", "to": "F1"},
        {"from": "2", "to": "F2"},
        {"from": "3", "to": "F3"},
        {"from": "4", "to": "F4"},
        {"from": "5", "to": "F5"},
        {"from": "6", "to": "F6"},
        {"from": "7", "to": "F7"},
        {"from": "8", "to": "F8"},
        {"from": "9", "to": "F9"},
        {"from": "0", "to": "F10"},
        {"from": "MINUS", "to": "F11"},
        {"from": "EQUAL", "to": "F12"}
    ],
    "options": {
        "start_enabled": true,
        "log_level": "INFO",
        "log_to_file": true,
        "keyboard_layout": "auto"
    }
}
```

### 配置说明

- **modifier.key**: 修饰键名称（目前仅支持 CAPSLOCK）
- **modifier.suppress_original**: 是否屏蔽修饰键原功能
- **modifier.control_led**: 是否控制 LED 指示灯
- **mappings**: 键位映射列表
  - **from**: 源按键名称（基于美式键盘布局）
  - **to**: 目标按键名称
- **options.start_enabled**: 启动时是否自动启用
- **options.log_level**: 日志级别（DEBUG/INFO/WARN/ERROR）
- **options.log_to_file**: 是否写入日志文件
- **options.keyboard_layout**: 键盘布局（auto 表示自动检测）

### 支持的按键名称

**源按键 (from)**:
- 字母: A-Z
- 数字: 0-9
- 符号: MINUS, EQUAL, LBRACKET, RBRACKET, SEMICOLON, QUOTE, BACKSLASH, COMMA, PERIOD, SLASH, BACKQUOTE
- 功能键: F1-F24
- 控制键: ESCAPE, TAB, CAPSLOCK, LSHIFT, RSHIFT, LCTRL, RCTRL, LALT, RALT, SPACE, ENTER, BACKSPACE
- 导航键: INSERT, DELETE, HOME, END, PAGEUP, PAGEDOWN, UP, DOWN, LEFT, RIGHT

**目标按键 (to)**:
- 同上所有按键
- 多媒体键: VOLUMEUP, VOLUMEDOWN, MUTE, PLAY, STOP, NEXT, PREV

## 文件结构

```
PowerCapslock/
├── src/
│   ├── main.c              # 程序入口
│   ├── hook.c/.h           # 键盘钩子
│   ├── keymap.c/.h         # 键位映射
│   ├── tray.c/.h           # 系统托盘
│   ├── config.c/.h         # 配置管理
│   ├── logger.c/.h         # 日志系统
│   └── keyboard_layout.c/.h # 键盘布局
├── resources/
│   ├── icon.ico            # 托盘图标（正常状态）
│   ├── icon_disabled.ico   # 托盘图标（禁用状态）
│   └── resource.rc         # Windows 资源文件
├── config/
│   └── config.json         # 默认配置文件
├── build/
│   └── (构建输出)
├── CMakeLists.txt          # CMake 配置
├── build.bat               # Windows 构建脚本
├── install.bat             # 安装脚本
├── DESIGN.md               # 本设计文档
└── README.md               # 使用说明
```

## 构建系统

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(PowerCapslock C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/main.c
    src/hook.c
    src/keymap.c
    src/tray.c
    src/config.c
    src/logger.c
    src/keyboard_layout.c
    resources/resource.rc
)

# 可执行文件
add_executable(powercapslock WIN32 ${SOURCES})

# 链接库
target_link_libraries(powercapslock
    user32
    kernel32
    shell32
    comctl32
    gdi32
)

# 包含目录
target_include_directories(powercapslock PRIVATE src)

# 设置 Windows 子系统
set_target_properties(powercapslock PROPERTIES
    WIN32_EXECUTABLE TRUE
)
```

### 构建命令

```batch
# 创建构建目录
mkdir build
cd build

# 生成项目
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 编译
mingw32-make -j4
```

## 安装部署

### 安装脚本 (install.bat)

```batch
@echo off
setlocal

:: 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo 请以管理员身份运行此脚本
    pause
    exit /b 1
)

:: 设置安装目录
set "INSTALL_DIR=C:\Program Files\PowerCapslock"
set "CONFIG_DIR=%APPDATA%\PowerCapslock"

:: 创建目录
mkdir "%INSTALL_DIR%" 2>nul
mkdir "%CONFIG_DIR%\logs" 2>nul

:: 复制程序文件
copy "build\powercapslock.exe" "%INSTALL_DIR%\"
copy "resources\*.ico" "%INSTALL_DIR%\" 2>nul

:: 复制默认配置（如果不存在）
if not exist "%CONFIG_DIR%\config.json" (
    copy "config\config.json" "%CONFIG_DIR%\"
)

:: 添加到开机启动
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "PowerCapslock" ^
    /t REG_SZ ^
    /d "\"%INSTALL_DIR%\powercapslock.exe\"" ^
    /f

:: 创建开始菜单快捷方式
set "SHORTCUT_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\PowerCapslock"
mkdir "%SHORTCUT_DIR%" 2>nul

powershell -Command "$
WshShell = New-Object -ComObject WScript.Shell; $
Shortcut = $WshShell.CreateShortcut('%SHORTCUT_DIR%\PowerCapslock.lnk'); $
Shortcut.TargetPath = '%INSTALL_DIR%\powercapslock.exe'; $
Shortcut.WorkingDirectory = '%CONFIG_DIR%'; $
Shortcut.Save()"

echo 安装完成！
echo 程序将随 Windows 启动自动运行
echo 配置文件位置: %CONFIG_DIR%
pause
```

### 卸载脚本 (uninstall.bat)

```batch
@echo off
setlocal

:: 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo 请以管理员身份运行此脚本
    pause
    exit /b 1
)

:: 停止运行中的程序
taskkill /F /IM powercapslock.exe 2>nul

:: 移除开机启动
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" ^
    /v "PowerCapslock" ^
    /f 2>nul

:: 删除程序文件
set "INSTALL_DIR=C:\Program Files\PowerCapslock"
rmdir /S /Q "%INSTALL_DIR%" 2>nul

:: 删除开始菜单快捷方式
set "SHORTCUT_DIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\PowerCapslock"
rmdir /S /Q "%SHORTCUT_DIR%" 2>nul

echo 卸载完成！
echo 配置文件保留在: %APPDATA%\PowerCapslock
pause
```

## 潜在问题与解决方案

### 1. 杀毒软件拦截

**问题**: 键盘钩子可能被误判为键盘记录器

**解决方案**:
- 使用代码签名证书签名可执行文件
- 在安装说明中提示用户添加到白名单
- 提供开源代码供用户审计

### 2. 某些游戏不生效

**问题**: 部分游戏使用 DirectInput 或 Raw Input，绕过 Windows 消息队列

**解决方案**:
- 文档中说明此限制
- 提供替代方案：使用 AutoHotkey 或 Interception driver
- 考虑未来版本支持 Raw Input

### 3. 与其他热键工具冲突

**问题**: 多个程序同时注册键盘钩子可能导致冲突

**解决方案**:
- 使用 `CallNextHookEx()` 确保其他钩子也能收到事件
- 提供选项调整钩子优先级
- 在日志中记录冲突检测

### 4. 权限问题

**问题**: 安装钩子需要管理员权限

**解决方案**:
- 安装时请求管理员权限
- 运行时以普通用户权限运行（钩子一旦安装，不需要持续管理员权限）
- 使用 Windows 服务（可选，需要更多开发工作）

### 5. 性能问题

**问题**: 钩子处理可能导致输入延迟

**解决方案**:
- 保持钩子回调函数轻量
- 避免在回调中执行耗时操作
- 使用日志级别控制，避免频繁日志写入

## 开发计划

### 第一阶段：核心功能
1. 键盘钩子模块
2. 键位映射模块
3. 基本程序框架
4. 构建系统

### 第二阶段：增强功能
1. 系统托盘模块
2. 日志模块
3. 配置模块
4. 多语言布局支持

### 第三阶段：完善
1. 安装程序
2. 文档编写
3. 测试和优化
4. 发布

## 技术参考

### Windows API 参考

- **SetWindowsHookEx**: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexa
- **LowLevelKeyboardProc**: https://docs.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc
- **KBDLLHOOKSTRUCT**: https://docs.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-kbdllhookstruct
- **SendInput**: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput
- **MapVirtualKeyEx**: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mapvirtualkeyexa

### Scan Code 参考

基于 IBM PC 键盘扫描码集 1（Set 1）：

| 按键 | Scan Code |
|------|-----------|
| Esc | 0x01 |
| 1-9,0 | 0x02-0x0B |
| - = | 0x0C-0x0D |
| Q-P | 0x10-0x19 |
| A-L | 0x1E-0x26 |
| Z-M | 0x2C-0x32 |
| F1-F12 | 0x3B-0x44 |
| 方向键 | 0xE0 前缀 |

## 总结

本设计方案提供了一个完整的 Windows 键盘热键映射工具的技术实现方案，包括：

1. **核心架构**: 基于 Windows Low-level Keyboard Hook
2. **多语言支持**: 使用 Scan Code 识别物理按键
3. **完整功能**: 键位映射、系统托盘、日志、配置
4. **易于部署**: CMake 构建系统，提供安装脚本
5. **可扩展性**: 模块化设计，易于添加新功能

开发时应注意：
- 保持钩子回调轻量，避免性能问题
- 正确处理权限和 UAC
- 提供完善的错误处理和日志记录
- 遵循 Windows 开发最佳实践
