# 托盘功能实现说明

## 功能概述

JohnHotKeyMap 的系统托盘功能已完整实现，提供可视化的程序控制和状态显示。

## 实现细节

### 1. 核心组件

#### 托盘窗口 (tray.c)
- **隐藏窗口**: 创建消息窗口接收托盘事件
- **窗口过程**: 处理托盘图标消息和菜单命令
- **状态管理**: 维护启用/禁用状态

#### 托盘图标
- **正常状态**: 绿色图标 (icon.ico)
- **禁用状态**: 红色图标 (icon_disabled.ico)
- **工具提示**: 显示当前状态

#### 托盘菜单
- 启用/禁用 (带状态切换)
- 查看日志
- 关于
- 退出

### 2. 技术实现

#### Windows API 使用
```c
// 托盘图标结构
NOTIFYICONDATA nid;
nid.cbSize = sizeof(NOTIFYICONDATA);
nid.hWnd = hWnd;                    // 窗口句柄
nid.uID = 1;                        // 图标ID
nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
nid.uCallbackMessage = WM_TRAYICON; // 自定义消息
nid.hIcon = hIcon;                  // 图标句柄
strcpy(nid.szTip, "提示文本");      // 工具提示

// 添加托盘图标
Shell_NotifyIcon(NIM_ADD, &nid);

// 修改托盘图标
Shell_NotifyIcon(NIM_MODIFY, &nid);

// 删除托盘图标
Shell_NotifyIcon(NIM_DELETE, &nid);
```

#### 消息处理
```c
// 托盘消息
case WM_TRAYICON:
    if (lParam == WM_LBUTTONUP) {
        // 左键点击：切换状态
    }
    else if (lParam == WM_RBUTTONUP) {
        // 右键点击：显示菜单
    }
    break;

// 菜单命令
case WM_COMMAND:
    switch (LOWORD(wParam)) {
        case IDM_ENABLE:    // 启用
        case IDM_DISABLE:   // 禁用
        case IDM_SHOW_LOG:  // 查看日志
        case IDM_ABOUT:     // 关于
        case IDM_EXIT:      // 退出
    }
    break;
```

### 3. 资源文件

#### 资源定义 (resource.rc)
```rc
#define IDI_ICON_NORMAL    101
#define IDI_ICON_DISABLED  102

IDI_ICON_NORMAL    ICON    "icon.ico"
IDI_ICON_DISABLED  ICON    "icon_disabled.ico"
```

#### 图标文件
- **icon.ico**: 32x32 像素，绿色边框
- **icon_disabled.ico**: 32x32 像素，红色边框
- **格式**: Windows ICO 格式
- **颜色深度**: 32位 (BGRA)

### 4. 编译集成

#### 资源编译
```bash
# 编译资源文件
windres resources/resource.rc build/resource.o -O coff

# 链接到可执行文件
gcc -o hotkeymap.exe ... build/resource.o ...
```

#### 构建脚本
使用 `scripts/build_with_resources.bat` 自动编译资源和程序。

## 使用说明

### 启动程序

运行程序后，系统托盘（任务栏右下角）会出现图标：
- **绿色图标**: 程序已启用，键位映射生效
- **红色图标**: 程序已禁用，键位映射不生效

### 交互方式

#### 左键点击
- 切换启用/禁用状态
- 图标颜色随之改变
- 工具提示更新

#### 右键点击
显示菜单：
```
┌─────────────┐
│ 启用        │
│ 禁用        │
├─────────────┤
│ 查看日志    │
│ 关于        │
├─────────────┤
│ 退出        │
└─────────────┘
```

### 菜单功能

#### 启用/禁用
- 切换程序状态
- 启用：键位映射生效
- 禁用：键位映射失效，恢复原按键功能

#### 查看日志
- 打开日志文件
- 路径：`%APPDATA%\JohnHotKeyMap\logs\hotkeymap.log`
- 使用系统默认文本编辑器

#### 关于
- 显示程序信息
- 版本：1.0
- 作者：John
- 许可：MIT License

#### 退出
- 关闭程序
- 移除托盘图标
- 卸载键盘钩子

## 状态指示

### 图标颜色
| 颜色 | 状态 | 说明 |
|------|------|------|
| 绿色 | 启用 | CapsLock映射生效 |
| 红色 | 禁用 | CapsLock功能正常 |

### 工具提示
鼠标悬停在图标上显示：
- 启用：`JohnHotKeyMap - 启用`
- 禁用：`JohnHotKeyMap - 禁用`

## 技术特点

### 1. 轻量级实现
- 最小化系统资源占用
- 不创建可见窗口
- 仅在托盘显示图标

### 2. 用户友好
- 直观的状态指示
- 简单的交互方式
- 清晰的菜单结构

### 3. 稳定性
- 正确的资源管理
- 完善的错误处理
- 安全的清理流程

### 4. 兼容性
- 支持 Windows 7/8/10/11
- 兼容高DPI显示
- 适配不同主题

## 故障排除

### 托盘图标不显示

**可能原因**:
1. 资源文件未正确编译
2. 图标文件损坏
3. 系统托盘区域已满

**解决方案**:
1. 重新编译：`scripts\build_with_resources.bat`
2. 检查图标文件：`resources\icon.ico`
3. 清理系统托盘：重启资源管理器

### 图标显示异常

**可能原因**:
- 图标格式不正确
- 颜色深度不支持

**解决方案**:
- 使用标准ICO格式
- 确保32位颜色深度

### 菜单不响应

**可能原因**:
- 窗口消息循环异常
- 钩子冲突

**解决方案**:
- 重启程序
- 检查日志文件
- 关闭其他热键工具

## 开发说明

### 扩展菜单

在 `tray.c` 中添加新菜单项：

```c
// 1. 定义菜单ID (tray.h)
#define IDM_NEW_ITEM  1006

// 2. 添加菜单项 (TrayInit)
AppendMenuA(g_tray.hMenu, MF_STRING, IDM_NEW_ITEM, "新功能");

// 3. 处理命令 (TrayWndProc)
case IDM_NEW_ITEM:
    // 实现功能
    break;
```

### 自定义图标

替换图标文件：
1. 准备新的ICO文件
2. 替换 `resources/icon.ico` 和 `resources/icon_disabled.ico`
3. 重新编译资源

### 状态扩展

可以添加更多状态：
- 暂停状态
- 错误状态
- 配置加载状态

每种状态对应不同的图标和提示文本。

## 总结

托盘功能已完整实现，包括：
- ✅ 图标显示和状态指示
- ✅ 左键点击切换状态
- ✅ 右键菜单操作
- ✅ 资源文件嵌入
- ✅ 完善的清理机制

程序现在具有完整的图形界面控制功能，用户可以方便地通过系统托盘管理程序。
