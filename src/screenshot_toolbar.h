#ifndef SCREENSHOT_TOOLBAR_H
#define SCREENSHOT_TOOLBAR_H

#include <windows.h>
#include <stdbool.h>
#include "screenshot.h"

// 工具栏按钮类型
typedef enum {
    TOOLBAR_BTN_NONE = 0,
    TOOLBAR_BTN_SAVE,       // 保存
    TOOLBAR_BTN_COPY,       // 复制
    TOOLBAR_BTN_PIN,        // 置顶
    TOOLBAR_BTN_RECT,       // 矩形标注
    TOOLBAR_BTN_ARROW,      // 箭头标注
    TOOLBAR_BTN_PENCIL,     // 铅笔
    TOOLBAR_BTN_CIRCLE,     // 圆形标注
    TOOLBAR_BTN_TEXT,       // 插入文字
    TOOLBAR_BTN_OCR,        // OCR
    TOOLBAR_BTN_CLOSE,      // 关闭
    TOOLBAR_BTN_COUNT       // 按钮数量
} ToolbarButtonType;

// 工具栏按钮定义
typedef struct {
    ToolbarButtonType type;
    const wchar_t* tooltip;
    const char* icon;       // 图标字符（使用 Unicode 符号）
    bool enabled;
} ToolbarButton;

// 工具栏状态
typedef struct {
    HWND hwnd;              // 工具栏窗口句柄
    ScreenshotRect* selection;  // 关联的选区
    ToolbarButton buttons[TOOLBAR_BTN_COUNT];
    int buttonCount;
    int hoveredButton;      // 当前悬停按钮索引
    bool isVisible;
    bool isHorizontal;      // 是否水平布局
} ToolbarContext;

// 回调函数类型
typedef void (*ToolbarCallback)(ToolbarButtonType button, void* userData);

// 初始化工具栏模块
bool ScreenshotToolbarInit(void);

// 清理工具栏模块
void ScreenshotToolbarCleanup(void);

// 显示工具栏
// selection: 关联的选区
// callback: 按钮点击回调
// userData: 回调用户数据
bool ScreenshotToolbarShow(const ScreenshotRect* selection, ToolbarCallback callback, void* userData);

// 隐藏工具栏
void ScreenshotToolbarHide(void);

// 检查工具栏是否可见
bool ScreenshotToolbarIsVisible(void);

// 设置工具栏位置（根据选区自动计算）
void ScreenshotToolbarUpdatePosition(const ScreenshotRect* selection);

// 测试函数
int ScreenshotToolbarTest(void);

#endif // SCREENSHOT_TOOLBAR_H
