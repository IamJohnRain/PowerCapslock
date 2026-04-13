#ifndef SCREENSHOT_OCR_H
#define SCREENSHOT_OCR_H

#include "screenshot.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char* text;
    int confidence;
    RECT boundingBox;
} OCRResult;

typedef struct {
    OCRResult* results;
    int count;
    char* fullText;
} OCRResults;

BOOL OCRInit(void);
void OCRCleanup(void);
OCRResults* OCRRecognize(const ScreenshotImage* image);
void OCRFreeResults(OCRResults* results);
int OCRTest(void);

#ifdef __cplusplus
}
#endif

#endif
