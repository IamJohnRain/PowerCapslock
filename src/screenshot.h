#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <windows.h>
#include <stdbool.h>

// 截图图像结构
typedef struct {
    HBITMAP hBitmap;        // GDI 位图句柄
    int width;              // 图像宽度
    int height;             // 图像高度
    int stride;             // 每行字节数 (width * 4 for BGRA)
    BYTE* pixels;           // 像素数据（BGRA 格式）
    bool ownsData;          // 是否拥有像素数据内存
} ScreenshotImage;

// 矩形区域
typedef struct {
    int x;
    int y;
    int width;
    int height;
} ScreenshotRect;

// 图像保存格式
typedef enum {
    SCREENSHOT_FORMAT_PNG = 0,
    SCREENSHOT_FORMAT_JPEG = 1,
    SCREENSHOT_FORMAT_BMP = 2
} ScreenshotFormat;

// 初始化截图模块
bool ScreenshotInit(void);

// 清理截图模块
void ScreenshotCleanup(void);

// 截取屏幕指定区域
// x, y: 区域左上角坐标（屏幕坐标）
// width, height: 区域宽高
// 返回: 截图图像，失败返回 NULL
ScreenshotImage* ScreenshotCaptureRect(int x, int y, int width, int height);

// 截取整个屏幕
ScreenshotImage* ScreenshotCaptureScreen(void);

// 截取指定显示器
// monitor: 显示器句柄，NULL 表示主显示器
ScreenshotImage* ScreenshotCaptureMonitor(HMONITOR monitor);

// 截取所有显示器（虚拟桌面）
ScreenshotImage* ScreenshotCaptureAllMonitors(void);

// 创建空白图像
ScreenshotImage* ScreenshotImageCreate(int width, int height);

// 释放截图图像
void ScreenshotImageFree(ScreenshotImage* image);

// 复制图像
ScreenshotImage* ScreenshotImageDup(const ScreenshotImage* image);

// 裁剪图像
ScreenshotImage* ScreenshotImageCrop(const ScreenshotImage* image,
                                      int x, int y, int width, int height);

// 保存图像到文件
// path: 文件路径（UTF-8）
// format: 保存格式
// quality: JPEG 质量 (1-100)，其他格式忽略
bool ScreenshotSaveToFile(const ScreenshotImage* image, const char* path,
                          ScreenshotFormat format, int quality);

// 复制图像到剪贴板
bool ScreenshotCopyToClipboard(const ScreenshotImage* image);

// 从剪贴板获取图像
ScreenshotImage* ScreenshotGetFromClipboard(void);

// 获取虚拟桌面尺寸（所有显示器合并）
void ScreenshotGetVirtualScreenSize(int* x, int* y, int* width, int* height);

// 获取主显示器尺寸
void ScreenshotGetMainMonitorSize(int* width, int* height);

// 测试函数（用于命令行测试）
int ScreenshotTestCapture(int x, int y, int w, int h, const char* outputPath);
int ScreenshotTestSave(const char* outputPath);
int ScreenshotTestClipboard(void);
int ScreenshotTestAll(void);

#endif // SCREENSHOT_H
