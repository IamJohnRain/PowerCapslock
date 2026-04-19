#ifndef SCREENSHOT_OCR_H
#define SCREENSHOT_OCR_H

#include "screenshot.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char* text;
    RECT boundingBox;
    float confidence;
    BOOL isLineBreak;
} OCRWord;

typedef struct {
    OCRWord* words;
    int wordCount;
    char* fullText;
    int imageWidth;
    int imageHeight;
} OCRResults;

// 异步 OCR 消息封装
typedef struct {
    DWORD sessionId;
    OCRResults* results;
} OcrAsyncMessage;

// Async OCR API
BOOL OCRRecognizeAsync(const ScreenshotImage* image, HWND hwnd, UINT msg, DWORD clientSessionId);
void OCRCancelAsync(void);

BOOL OCRInit(void);
void OCRCleanup(void);
OCRResults* OCRRecognize(const ScreenshotImage* image);
void OCRFreeResults(OCRResults* results);
int OCRTest(void);

// Tesseract OCR 后端 API (需要 USE_TESSERACT 编译标志)
BOOL OCRInitTesseract(const char* tessdataPath);
void OCRCleanupTesseract(void);
OCRResults* OCRRecognizeTesseract(const ScreenshotImage* image);

#ifdef __cplusplus
}
#endif

#endif
