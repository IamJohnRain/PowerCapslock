#ifndef TRAY_H
#define TRAY_H

#include <windows.h>
#include <stdbool.h>

// 托盘消息定义
#define WM_TRAYICON (WM_USER + 1)

// 菜单命令ID
#define IDM_ENABLE       1001
#define IDM_DISABLE      1002
#define IDM_SHOW_LOG     1003
#define IDM_CONFIGURE    1006
#define IDM_ABOUT        1004
#define IDM_EXIT         1005

// 初始化系统托盘
// hInstance: 程序实例句柄
// 返回: 托盘窗口句柄，失败返回NULL
HWND TrayInit(HINSTANCE hInstance);

// 清理系统托盘
void TrayCleanup(void);

// 设置托盘状态
// enabled: true显示正常图标，false显示禁用图标
void TraySetState(bool enabled);

// 启动/停止 CapsLock 状态检测定时器
void TrayStartCapsLockMonitor(void);
void TrayStopCapsLockMonitor(void);

// 获取托盘窗口句柄
HWND TrayGetWindow(void);

// 显示全局提示框
// title: 提示框标题
// message: 提示框消息
void TrayShowToast(const char* title, const wchar_t* message);

#endif // TRAY_H
