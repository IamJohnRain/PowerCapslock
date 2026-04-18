#ifndef SCREENSHOT_FLOAT_H
#define SCREENSHOT_FLOAT_H

#include <windows.h>
#include <stdbool.h>
#include "screenshot.h"
#include "screenshot_ocr.h"

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

    // OCR 模式状态
    bool ocrMode;           // 是否为 OCR 模式
    OCRResults* ocrResults; // OCR 识别结果
    bool ocrInProgress;     // OCR 是否正在进行
    DWORD ocrSessionId;     // 当前 OCR session ID，用于验证
    int hoveredWordIndex;   // 当前悬停的词索引
    int anchorWordIndex;    // 选择锚点索引
    int activeWordIndex;    // 当前活动索引
    bool isSelectingText;   // 是否正在选择文本

    // 统一图片布局
    float imageScale;       // 图片缩放比例
    POINT imageOffset;      // 图片绘制偏移
    SIZE imageDrawSize;     // 图片绘制尺寸

    // 复制成功提示
    bool showCopiedToast;   // 是否显示复制成功提示
    DWORD toastStartTime;   // 提示开始时间
} FloatWindowContext;

// 初始化浮动窗口模块
bool ScreenshotFloatInit(void);

// 清理浮动窗口模块
void ScreenshotFloatCleanup(void);

// 显示浮动窗口（普通模式）
// image: 要显示的图像（会复制一份）
// x, y: 初始位置（-1 表示自动定位）
bool ScreenshotFloatShow(const ScreenshotImage* image, int x, int y);

// 显示浮动窗口（OCR 模式）
bool ScreenshotFloatShowOcr(const ScreenshotImage* image, int x, int y);

// 获取选中的文本
const char* ScreenshotFloatGetSelectedText(void);

// 清除文本选择
void ScreenshotFloatClearSelection(void);

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
