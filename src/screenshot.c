#include "screenshot.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_initialized = false;

bool ScreenshotInit(void) {
    if (g_initialized) {
        LOG_DEBUG("[截图] 模块已初始化");
        return true;
    }

    LOG_DEBUG("[截图] 开始初始化...");
    g_initialized = true;
    LOG_INFO("[截图] 模块初始化成功");
    return true;
}

void ScreenshotCleanup(void) {
    if (!g_initialized) {
        return;
    }

    LOG_DEBUG("[截图] 开始清理...");
    g_initialized = false;
    LOG_INFO("[截图] 模块已清理");
}

ScreenshotImage* ScreenshotCaptureRect(int x, int y, int width, int height) {
    LOG_DEBUG("[截图] 开始截取区域: (%d, %d) %dx%d", x, y, width, height);

    if (width <= 0 || height <= 0) {
        LOG_ERROR("[截图] 无效的区域大小: %dx%d", width, height);
        return NULL;
    }

    // 获取屏幕 DC
    HDC hdcScreen = GetDC(NULL);
    if (hdcScreen == NULL) {
        LOG_ERROR("[截图] 获取屏幕 DC 失败");
        return NULL;
    }

    // 创建内存 DC
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (hdcMem == NULL) {
        LOG_ERROR("[截图] 创建内存 DC 失败");
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    // 创建位图
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    if (hBitmap == NULL) {
        LOG_ERROR("[截图] 创建位图失败");
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    // 选入位图
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // 复制屏幕内容
    BOOL result = BitBlt(hdcMem, 0, 0, width, height, hdcScreen, x, y, SRCCOPY);
    if (!result) {
        LOG_ERROR("[截图] BitBlt 失败: %d", GetLastError());
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    // 设置 BITMAPINFO 结构
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // 负值表示从上到下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    // 分配像素数据
    int stride = width * 4;
    BYTE* pixels = (BYTE*)malloc(height * stride);
    if (pixels == NULL) {
        LOG_ERROR("[截图] 分配像素数据失败");
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    // 获取像素数据
    result = GetDIBits(hdcMem, hBitmap, 0, height, pixels, &bmi, DIB_RGB_COLORS);
    if (!result) {
        LOG_ERROR("[截图] GetDIBits 失败: %d", GetLastError());
        free(pixels);
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    // 恢复旧位图
    SelectObject(hdcMem, hOldBitmap);

    // 清理
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // 创建图像结构
    ScreenshotImage* image = (ScreenshotImage*)malloc(sizeof(ScreenshotImage));
    if (image == NULL) {
        LOG_ERROR("[截图] 分配图像结构失败");
        free(pixels);
        DeleteObject(hBitmap);
        return NULL;
    }

    image->hBitmap = hBitmap;
    image->width = width;
    image->height = height;
    image->stride = stride;
    image->pixels = pixels;
    image->ownsData = true;

    LOG_INFO("[截图] 截图捕获成功: %dx%d", width, height);
    return image;
}

ScreenshotImage* ScreenshotCaptureScreen(void) {
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    return ScreenshotCaptureRect(0, 0, width, height);
}

ScreenshotImage* ScreenshotCaptureMonitor(HMONITOR monitor) {
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);

    if (monitor == NULL) {
        POINT pt = {0, 0};
        monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    }

    if (!GetMonitorInfo(monitor, &mi)) {
        LOG_ERROR("[截图] 获取显示器信息失败");
        return NULL;
    }

    int x = mi.rcMonitor.left;
    int y = mi.rcMonitor.top;
    int width = mi.rcMonitor.right - mi.rcMonitor.left;
    int height = mi.rcMonitor.bottom - mi.rcMonitor.top;

    return ScreenshotCaptureRect(x, y, width, height);
}

ScreenshotImage* ScreenshotCaptureAllMonitors(void) {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    return ScreenshotCaptureRect(x, y, width, height);
}

ScreenshotImage* ScreenshotImageCreate(int width, int height) {
    if (width <= 0 || height <= 0) {
        LOG_ERROR("[截图] 无效的图像大小: %dx%d", width, height);
        return NULL;
    }

    int stride = width * 4;
    BYTE* pixels = (BYTE*)calloc(height, stride);
    if (pixels == NULL) {
        LOG_ERROR("[截图] 分配像素数据失败");
        return NULL;
    }

    // 创建位图
    HDC hdc = GetDC(NULL);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, width, height);
    ReleaseDC(NULL, hdc);

    if (hBitmap == NULL) {
        LOG_ERROR("[截图] 创建位图失败");
        free(pixels);
        return NULL;
    }

    ScreenshotImage* image = (ScreenshotImage*)malloc(sizeof(ScreenshotImage));
    if (image == NULL) {
        LOG_ERROR("[截图] 分配图像结构失败");
        free(pixels);
        DeleteObject(hBitmap);
        return NULL;
    }

    image->hBitmap = hBitmap;
    image->width = width;
    image->height = height;
    image->stride = stride;
    image->pixels = pixels;
    image->ownsData = true;

    return image;
}

void ScreenshotImageFree(ScreenshotImage* image) {
    if (image == NULL) {
        return;
    }

    LOG_DEBUG("[截图] 释放图像: %dx%d", image->width, image->height);

    if (image->ownsData && image->pixels) {
        free(image->pixels);
    }

    if (image->hBitmap) {
        DeleteObject(image->hBitmap);
    }

    free(image);
}

ScreenshotImage* ScreenshotImageDup(const ScreenshotImage* image) {
    if (image == NULL) {
        return NULL;
    }

    ScreenshotImage* dup = ScreenshotImageCreate(image->width, image->height);
    if (dup == NULL) {
        return NULL;
    }

    memcpy(dup->pixels, image->pixels, image->height * image->stride);
    return dup;
}

ScreenshotImage* ScreenshotImageCrop(const ScreenshotImage* image,
                                      int x, int y, int width, int height) {
    if (image == NULL) {
        LOG_ERROR("[截图] 源图像为空");
        return NULL;
    }

    // 边界检查
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > image->width) { width = image->width - x; }
    if (y + height > image->height) { height = image->height - y; }

    if (width <= 0 || height <= 0) {
        LOG_ERROR("[截图] 裁剪区域无效");
        return NULL;
    }

    ScreenshotImage* cropped = ScreenshotImageCreate(width, height);
    if (cropped == NULL) {
        return NULL;
    }

    // 复制像素数据
    for (int row = 0; row < height; row++) {
        const BYTE* src = image->pixels + (y + row) * image->stride + x * 4;
        BYTE* dst = cropped->pixels + row * cropped->stride;
        memcpy(dst, src, width * 4);
    }

    return cropped;
}

// BMP 文件头结构 (不使用 windows.h 的定义以避免对齐问题)
#pragma pack(push, 1)
typedef struct {
    WORD bfType;
    DWORD bfSize;
    WORD bfReserved1;
    WORD bfReserved2;
    DWORD bfOffBits;
} BMPFILEHEADER;

typedef struct {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BMPINFOHEADER;
#pragma pack(pop)

bool ScreenshotSaveToFile(const ScreenshotImage* image, const char* path,
                          ScreenshotFormat format, int quality) {
    if (image == NULL || path == NULL) {
        LOG_ERROR("[截图] 图像或路径为空");
        return false;
    }

    LOG_DEBUG("[截图] 保存图像到: %s", path);

    // 检查文件扩展名
    const char* ext = strrchr(path, '.');
    bool saveAsBmp = (ext != NULL && _stricmp(ext, ".bmp") == 0);

    FILE* fp = fopen(path, "wb");
    if (fp == NULL) {
        LOG_ERROR("[截图] 无法创建文件: %s", path);
        return false;
    }

    if (saveAsBmp || format == SCREENSHOT_FORMAT_BMP) {
        // 保存为 BMP 格式
        int rowSize = (image->width * 4 + 3) & ~3;  // 每行对齐到 4 字节
        int padding = rowSize - image->width * 4;
        int imageSize = rowSize * image->height;
        int fileSize = sizeof(BMPFILEHEADER) + sizeof(BMPINFOHEADER) + imageSize;

        BMPFILEHEADER fileHeader = {0};
        fileHeader.bfType = 0x4D42;  // "BM"
        fileHeader.bfSize = fileSize;
        fileHeader.bfOffBits = sizeof(BMPFILEHEADER) + sizeof(BMPINFOHEADER);

        BMPINFOHEADER infoHeader = {0};
        infoHeader.biSize = sizeof(BMPINFOHEADER);
        infoHeader.biWidth = image->width;
        infoHeader.biHeight = image->height;  // 正值表示从下到上
        infoHeader.biPlanes = 1;
        infoHeader.biBitCount = 32;
        infoHeader.biCompression = 0;  // BI_RGB
        infoHeader.biSizeImage = imageSize;

        fwrite(&fileHeader, sizeof(fileHeader), 1, fp);
        fwrite(&infoHeader, sizeof(infoHeader), 1, fp);

        // 写入像素数据（从下到上，BGRA 格式）
        BYTE paddingBytes[4] = {0};
        for (int y = image->height - 1; y >= 0; y--) {
            fwrite(image->pixels + y * image->stride, image->width * 4, 1, fp);
            if (padding > 0) {
                fwrite(paddingBytes, padding, 1, fp);
            }
        }
    } else {
        // 默认保存为 BMP（简化实现，后续可添加 PNG 支持）
        // 使用 .bmp 扩展名替换原扩展名
        char bmpPath[MAX_PATH];
        strncpy(bmpPath, path, MAX_PATH - 1);
        char* dot = strrchr(bmpPath, '.');
        if (dot) {
            strcpy(dot, ".bmp");
        } else {
            strcat(bmpPath, ".bmp");
        }

        fclose(fp);
        remove(path);  // 删除可能已创建的空文件

        LOG_DEBUG("[截图] PNG 格式暂不支持，保存为 BMP: %s", bmpPath);
        return ScreenshotSaveToFile(image, bmpPath, SCREENSHOT_FORMAT_BMP, quality);
    }

    fclose(fp);
    LOG_INFO("[截图] 图像保存成功: %s", path);
    return true;
}

bool ScreenshotCopyToClipboard(const ScreenshotImage* image) {
    if (image == NULL) {
        LOG_ERROR("[截图] 图像为空");
        return false;
    }

    LOG_DEBUG("[截图] 复制图像到剪贴板");

    if (!OpenClipboard(NULL)) {
        LOG_ERROR("[截图] 打开剪贴板失败: %d", GetLastError());
        return false;
    }

    EmptyClipboard();

    // 创建 DIB
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = image->width;
    bmi.bmiHeader.biHeight = image->height;  // 正值表示从下到上
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    // 计算大小
    int dataSize = image->height * image->stride;
    int totalSize = sizeof(BITMAPINFOHEADER) + dataSize;

    // 分配全局内存
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, totalSize);
    if (hMem == NULL) {
        LOG_ERROR("[截图] 分配全局内存失败");
        CloseClipboard();
        return false;
    }

    void* pMem = GlobalLock(hMem);
    if (pMem == NULL) {
        LOG_ERROR("[截图] 锁定全局内存失败");
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    // 复制 BITMAPINFOHEADER
    memcpy(pMem, &bmi.bmiHeader, sizeof(BITMAPINFOHEADER));

    // 复制像素数据（需要翻转，因为剪贴板 DIB 是从下到上）
    BYTE* pPixels = (BYTE*)pMem + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < image->height; y++) {
        const BYTE* src = image->pixels + (image->height - 1 - y) * image->stride;
        BYTE* dst = pPixels + y * image->stride;
        memcpy(dst, src, image->stride);
    }

    GlobalUnlock(hMem);

    // 设置剪贴板数据
    if (SetClipboardData(CF_DIB, hMem) == NULL) {
        LOG_ERROR("[截图] 设置剪贴板数据失败: %d", GetLastError());
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();

    LOG_INFO("[截图] 图像已复制到剪贴板");
    return true;
}

ScreenshotImage* ScreenshotGetFromClipboard(void) {
    if (!IsClipboardFormatAvailable(CF_DIB)) {
        LOG_DEBUG("[截图] 剪贴板中没有图像数据");
        return NULL;
    }

    if (!OpenClipboard(NULL)) {
        LOG_ERROR("[截图] 打开剪贴板失败: %d", GetLastError());
        return NULL;
    }

    HGLOBAL hMem = GetClipboardData(CF_DIB);
    if (hMem == NULL) {
        LOG_ERROR("[截图] 获取剪贴板数据失败");
        CloseClipboard();
        return NULL;
    }

    void* pMem = GlobalLock(hMem);
    if (pMem == NULL) {
        LOG_ERROR("[截图] 锁定剪贴板数据失败");
        CloseClipboard();
        return NULL;
    }

    BITMAPINFOHEADER* pHeader = (BITMAPINFOHEADER*)pMem;
    int width = pHeader->biWidth;
    int height = pHeader->biHeight > 0 ? pHeader->biHeight : -pHeader->biHeight;
    int stride = width * 4;

    ScreenshotImage* image = ScreenshotImageCreate(width, height);
    if (image == NULL) {
        GlobalUnlock(hMem);
        CloseClipboard();
        return NULL;
    }

    // 复制像素数据
    BYTE* pPixels = (BYTE*)pMem + pHeader->biSize;
    for (int y = 0; y < height; y++) {
        BYTE* dst;
        if (pHeader->biHeight > 0) {
            // 从下到上
            dst = image->pixels + (height - 1 - y) * stride;
        } else {
            // 从上到下
            dst = image->pixels + y * stride;
        }
        const BYTE* src = pPixels + y * stride;
        memcpy(dst, src, stride);
    }

    GlobalUnlock(hMem);
    CloseClipboard();

    LOG_INFO("[截图] 从剪贴板获取图像: %dx%d", width, height);
    return image;
}

void ScreenshotGetVirtualScreenSize(int* x, int* y, int* width, int* height) {
    if (x) *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    if (y) *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    if (width) *width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    if (height) *height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

void ScreenshotGetMainMonitorSize(int* width, int* height) {
    if (width) *width = GetSystemMetrics(SM_CXSCREEN);
    if (height) *height = GetSystemMetrics(SM_CYSCREEN);
}

// 测试函数
int ScreenshotTestCapture(int x, int y, int w, int h, const char* outputPath) {
    LOG_INFO("[截图测试] 开始测试截图捕获...");

    if (!ScreenshotInit()) {
        LOG_ERROR("[截图测试] 初始化失败");
        return 1;
    }

    ScreenshotImage* image = ScreenshotCaptureRect(x, y, w, h);
    if (image == NULL) {
        LOG_ERROR("[截图测试] 截图捕获失败");
        ScreenshotCleanup();
        return 1;
    }

    printf("Captured: %dx%d\n", image->width, image->height);

    if (outputPath != NULL) {
        if (!ScreenshotSaveToFile(image, outputPath, SCREENSHOT_FORMAT_PNG, 90)) {
            LOG_ERROR("[截图测试] 保存失败");
            ScreenshotImageFree(image);
            ScreenshotCleanup();
            return 1;
        }
        printf("Saved to: %s\n", outputPath);
    }

    ScreenshotImageFree(image);
    ScreenshotCleanup();

    LOG_INFO("[截图测试] 测试通过");
    return 0;
}

int ScreenshotTestSave(const char* outputPath) {
    LOG_INFO("[截图测试] 开始测试保存功能...");

    if (!ScreenshotInit()) {
        LOG_ERROR("[截图测试] 初始化失败");
        return 1;
    }

    ScreenshotImage* image = ScreenshotCaptureRect(0, 0, 200, 200);
    if (image == NULL) {
        LOG_ERROR("[截图测试] 截图捕获失败");
        ScreenshotCleanup();
        return 1;
    }

    if (!ScreenshotSaveToFile(image, outputPath, SCREENSHOT_FORMAT_PNG, 90)) {
        LOG_ERROR("[截图测试] 保存失败");
        ScreenshotImageFree(image);
        ScreenshotCleanup();
        return 1;
    }

    printf("Saved to: %s\n", outputPath);

    ScreenshotImageFree(image);
    ScreenshotCleanup();

    LOG_INFO("[截图测试] 测试通过");
    return 0;
}

int ScreenshotTestClipboard(void) {
    LOG_INFO("[截图测试] 开始测试剪贴板功能...");

    if (!ScreenshotInit()) {
        LOG_ERROR("[截图测试] 初始化失败");
        return 1;
    }

    ScreenshotImage* image = ScreenshotCaptureRect(0, 0, 100, 100);
    if (image == NULL) {
        LOG_ERROR("[截图测试] 截图捕获失败");
        ScreenshotCleanup();
        return 1;
    }

    if (!ScreenshotCopyToClipboard(image)) {
        LOG_ERROR("[截图测试] 复制到剪贴板失败");
        ScreenshotImageFree(image);
        ScreenshotCleanup();
        return 1;
    }

    printf("Copied to clipboard: %dx%d\n", image->width, image->height);

    ScreenshotImageFree(image);
    ScreenshotCleanup();

    LOG_INFO("[截图测试] 测试通过");
    return 0;
}

int ScreenshotTestAll(void) {
    LOG_INFO("[截图测试] 开始全量测试...");

    int failed = 0;

    // 测试截图捕获
    printf("Testing capture...\n");
    if (ScreenshotTestCapture(0, 0, 200, 200, "test_capture.bmp") != 0) {
        printf("FAILED: capture test\n");
        failed++;
    } else {
        printf("PASSED: capture test\n");
    }

    // 测试保存
    printf("Testing save...\n");
    if (ScreenshotTestSave("test_save.bmp") != 0) {
        printf("FAILED: save test\n");
        failed++;
    } else {
        printf("PASSED: save test\n");
    }

    // 测试剪贴板
    printf("Testing clipboard...\n");
    if (ScreenshotTestClipboard() != 0) {
        printf("FAILED: clipboard test\n");
        failed++;
    } else {
        printf("PASSED: clipboard test\n");
    }

    printf("\n=== Test Summary ===\n");
    printf("Total: 3, Passed: %d, Failed: %d\n", 3 - failed, failed);

    return failed > 0 ? 1 : 0;
}
