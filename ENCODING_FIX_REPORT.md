# 托盘菜单乱码问题修复报告

## 问题现象

托盘右键菜单显示乱码，中文字符无法正常显示。

## 根本原因

**编码不匹配**：程序使用UTF-8编码的中文文本，但Windows系统默认使用GBK/GB2312编码，导致字符解析错误。

### 具体表现

从截图分析，菜单项显示为：
```
錫 默
绂佺默
鏌ョ湅鏃ュ綍
鍏充簬
闂€鏍
```

这些是**UTF-8编码的中文被错误地用GBK解码**后产生的乱码。

### 还原后的正确内容

```
启用
禁用
查看日志
关于
退出
```

## 技术分析

### Windows API 编码问题

Windows提供两套API：
- **ANSI版本** (以A结尾): `MessageBoxA`, `AppendMenuA`, `Shell_NotifyIconA`
- **Unicode版本** (以W结尾): `MessageBoxW`, `AppendMenuW`, `Shell_NotifyIconW`

### 编码流程

#### 错误的方式 (ANSI API + UTF-8源码)
```c
// 源码文件: UTF-8编码
AppendMenuA(hMenu, MF_STRING, ID, "启用");

// 编译后: "启用" 的UTF-8字节: E5 90 AF E7 94 A8
// Windows ANSI API: 使用系统默认编码(GBK)解释
// GBK解码 E5 90 AF E7 94 A8 → "绂佺" (乱码)
```

#### 正确的方式 (Unicode API + 宽字符)
```c
// 使用宽字符字面量 (L前缀)
AppendMenuW(hMenu, MF_STRING, ID, L"启用");

// 编译后: "启用" 的UTF-16编码
// Windows Unicode API: 正确处理UTF-16
// 显示: "启用" (正确)
```

## 修复方案

### 1. 使用Unicode结构体

```c
// 修改前
NOTIFYICONDATA nid;

// 修改后
NOTIFYICONDATAW nid;  // 使用Unicode版本
```

### 2. 使用Unicode API

```c
// 修改前
AppendMenuA(hMenu, MF_STRING, IDM_ENABLE, "启用");
Shell_NotifyIcon(NIM_ADD, &nid);
MessageBoxA(hWnd, "关于", "标题", MB_OK);

// 修改后
AppendMenuW(hMenu, MF_STRING, IDM_ENABLE, L"启用");
Shell_NotifyIconW(NIM_ADD, &nid);
MessageBoxW(hWnd, L"关于", L"标题", MB_OK);
```

### 3. 使用宽字符字符串

```c
// 修改前
strcpy(nid.szTip, "JohnHotKeyMap - 启用");

// 修改后
wcscpy(nid.szTip, L"JohnHotKeyMap - 启用");
```

## 具体修改

### tray.c 文件修改

#### 1. 结构体定义
```c
typedef struct {
    HWND hWnd;
    HMENU hMenu;
    HICON hIconNormal;
    HICON hIconDisabled;
    NOTIFYICONDATAW nid;  // ← 改为Unicode版本
    bool enabled;
} TrayState;
```

#### 2. 菜单创建
```c
// 使用Unicode API和宽字符
g_tray.hMenu = CreatePopupMenu();
AppendMenuW(g_tray.hMenu, MF_STRING, IDM_ENABLE, L"启用");
AppendMenuW(g_tray.hMenu, MF_STRING, IDM_DISABLE, L"禁用");
AppendMenuW(g_tray.hMenu, MF_SEPARATOR, 0, NULL);
AppendMenuW(g_tray.hMenu, MF_STRING, IDM_SHOW_LOG, L"查看日志");
AppendMenuW(g_tray.hMenu, MF_STRING, IDM_ABOUT, L"关于");
AppendMenuW(g_tray.hMenu, MF_SEPARATOR, 0, NULL);
AppendMenuW(g_tray.hMenu, MF_STRING, IDM_EXIT, L"退出");
```

#### 3. 托盘图标操作
```c
// 添加图标
Shell_NotifyIconW(NIM_ADD, &g_tray.nid);

// 修改图标
Shell_NotifyIconW(NIM_MODIFY, &g_tray.nid);

// 删除图标
Shell_NotifyIconW(NIM_DELETE, &g_tray.nid);
```

#### 4. 工具提示文本
```c
// 使用宽字符复制函数
wcscpy(g_tray.nid.szTip, L"JohnHotKeyMap - 启用");
```

#### 5. 消息框
```c
MessageBoxW(hWnd,
    L"JohnHotKeyMap v1.0\n\n"
    L"Windows 键盘热键映射工具\n"
    L"将 CapsLock 改造为强大的修饰键\n\n"
    L"作者: John\n"
    L"许可: MIT License",
    L"关于 JohnHotKeyMap",
    MB_OK | MB_ICONINFORMATION);
```

## 编码知识

### 字符编码类型

| 编码 | 说明 | Windows API |
|------|------|-------------|
| ANSI | 单字节，系统默认编码 | `*A` 函数 |
| UTF-8 | 多字节，可变长度 | 需手动转换 |
| UTF-16 | 双字节，固定长度 | `*W` 函数 |

### 宽字符标记

```c
"文本"   // char[] - ANSI/UTF-8
L"文本"  // wchar_t[] - UTF-16
```

### 常用函数对比

| ANSI版本 | Unicode版本 | 说明 |
|----------|-------------|------|
| `strcpy` | `wcscpy` | 字符串复制 |
| `strlen` | `wcslen` | 字符串长度 |
| `strcmp` | `wcscmp` | 字符串比较 |
| `printf` | `wprintf` | 格式化输出 |

## 验证方法

### 1. 编译检查
```bash
gcc -o hotkeymap.exe ... -lshell32 ...
```

无错误输出表示编译成功。

### 2. 运行测试
```bash
build\hotkeymap.exe
```

### 3. 功能验证
1. 右键点击托盘图标
2. 检查菜单项是否正确显示中文
3. 应该看到：
   - 启用
   - 禁用
   - 查看日志
   - 关于
   - 退出

### 4. 日志检查
查看日志文件确认无错误：
```
%APPDATA%\JohnHotKeyMap\logs\hotkeymap.log
```

## 最佳实践

### 1. 优先使用Unicode API

在Windows程序开发中，推荐：
- ✅ 使用 `*W` 版本的API
- ✅ 使用 `wchar_t` 和 `L""` 字符串
- ✅ 使用 `NOTIFYICONDATAW` 等Unicode结构

### 2. 编译器设置

可以定义宏简化：
```c
#define UNICODE
#define _UNICODE
```

这样不带后缀的API会自动映射到Unicode版本：
```c
MessageBox → MessageBoxW
AppendMenu → AppendMenuW
```

### 3. 源码编码

确保源码文件保存为：
- UTF-8 with BOM (推荐)
- 或 UTF-16 LE

## 相关问题

### 问题1: 控制台输出乱码

**解决**:
```c
SetConsoleOutputCP(CP_UTF8);
```

### 问题2: 文件路径乱码

**解决**: 使用宽字符版本的文件API：
```c
CreateFileW(L"路径", ...);
fopen("路径", ...);  // 可能有问题
_wfopen(L"路径", ...);  // 推荐
```

### 问题3: 配置文件乱码

**解决**:
- 配置文件使用UTF-8编码
- 读取时转换为UTF-16
- 或使用JSON库处理编码

## 总结

### 修复要点

1. ✅ 使用 `NOTIFYICONDATAW` 结构
2. ✅ 使用 `Shell_NotifyIconW` API
3. ✅ 使用 `AppendMenuW` 创建菜单
4. ✅ 使用 `MessageBoxW` 显示消息
5. ✅ 使用 `L""` 宽字符字面量
6. ✅ 使用 `wcscpy` 复制宽字符字符串

### 预期结果

修复后，托盘菜单应该正确显示：
```
启用
禁用
─────────
查看日志
关于
─────────
退出
```

所有中文字符都能正常显示，不再有乱码。

---

**修复完成时间**: 2026-04-04
**修复版本**: v1.0-unicode
**编译器**: GCC 15.2.0 (MinGW-W64)
