#ifndef SCREENSHOT_FLOAT_H
#define SCREENSHOT_FLOAT_H

#include <windows.h>
#include <stdbool.h>
#include "screenshot.h"

// 浮动窗口状态
typedef struct {
    HWND hwnd;              // 浮动窗口句柄
    ScreenshotImage* image; // 显示的图像
    ScreenshotRect displayRect; // 显示位置和大小
    bool isPinned;          // 是否置顶
    float opacity;          // 透明度 (0.0-1.0)
    bool showToolbar;       // 是否显示工具栏
    bool isDragging;        // 是否正在拖拽
    POINT dragStart;        // 拖拽起点
} FloatWindowContext;

// 初始化浮动窗口模块
bool ScreenshotFloatInit(void);

// 清理浮动窗口模块
void ScreenshotFloatCleanup(void);

// 显示浮动窗口
// image: 要显示的图像（会复制一份）
// x, y: 初始位置（-1 表示自动定位）
bool ScreenshotFloatShow(const ScreenshotImage* image, int x, int y);

// 隐藏浮动窗口
void ScreenshotFloatHide(void);

// 检查浮动窗口是否可见
bool ScreenshotFloatIsVisible(void);

// 设置置顶状态
void ScreenshotFloatSetPinned(bool pinned);

// 设置透明度
void ScreenshotFloatSetOpacity(float opacity);

// 获取当前图像
const ScreenshotImage* ScreenshotFloatGetImage(void);

// 测试函数
int ScreenshotFloatTest(void);

#endif // SCREENSHOT_FLOAT_H
