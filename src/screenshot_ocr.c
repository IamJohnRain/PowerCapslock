#include "screenshot_ocr.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <process.h>

#define MODULE_NAME "OCR"

static BOOL g_ocrInitialized = FALSE;
static volatile BOOL g_cancelOcr = FALSE;
static volatile DWORD g_currentSessionId = 0;

// 异步 OCR 消息
#define WM_FLOAT_OCR_COMPLETE (WM_APP + 10)

// Worker 线程参数
typedef struct {
    ScreenshotImage* image;
    DWORD sessionId;
    HWND hwnd;
    UINT msg;
} WorkerArgs;

// 前向声明
static unsigned __stdcall OcrWorkerThread(void* arg);

BOOL OCRInit(void) {
    LOG_DEBUG("[%s] 开始初始化...", MODULE_NAME);
    LOG_INFO("[%s] 模块初始化成功 (Tesseract 待集成)", MODULE_NAME);
    g_ocrInitialized = TRUE;
    return TRUE;
}

void OCRCleanup(void) {
    LOG_DEBUG("[%s] 开始清理...", MODULE_NAME);
    OCRCancelAsync();
    g_ocrInitialized = FALSE;
    LOG_INFO("[%s] 模块已清理", MODULE_NAME);
}

BOOL OCRRecognizeAsync(const ScreenshotImage* image, HWND hwnd, UINT msg) {
    if (!g_ocrInitialized || image == NULL || hwnd == NULL) {
        LOG_ERROR("[%s] 异步识别失败: 参数无效", MODULE_NAME);
        return FALSE;
    }

    // 取消之前的异步任务
    OCRCancelAsync();

    // 创建新的 session
    DWORD sessionId = ++g_currentSessionId;
    g_cancelOcr = FALSE;

    LOG_DEBUG("[%s] 开始异步识别, sessionId=%lu", MODULE_NAME, sessionId);

    // 复制图像数据供 worker 线程使用
    ScreenshotImage* imageCopy = ScreenshotImageDup(image);
    if (imageCopy == NULL) {
        LOG_ERROR("[%s] 复制图像失败", MODULE_NAME);
        return FALSE;
    }

    // 分配参数结构
    WorkerArgs* args = (WorkerArgs*)malloc(sizeof(WorkerArgs));
    if (args == NULL) {
        ScreenshotImageFree(imageCopy);
        LOG_ERROR("[%s] 分配参数失败", MODULE_NAME);
        return FALSE;
    }

    args->image = imageCopy;
    args->sessionId = sessionId;
    args->hwnd = hwnd;
    args->msg = msg;

    // 启动 worker 线程
    HANDLE hThread = (HANDLE)_beginthreadex(
        NULL, 0, OcrWorkerThread, args, 0, NULL);

    if (hThread == NULL) {
        ScreenshotImageFree(imageCopy);
        free(args);
        LOG_ERROR("[%s] 创建 worker 线程失败", MODULE_NAME);
        return FALSE;
    }

    CloseHandle(hThread);
    return TRUE;
}

void OCRCancelAsync(void) {
    g_cancelOcr = TRUE;
}

static unsigned __stdcall OcrWorkerThread(void* arg) {
    WorkerArgs* args = (WorkerArgs*)arg;
    ScreenshotImage* image = args->image;
    DWORD sessionId = args->sessionId;
    HWND hwnd = args->hwnd;
    UINT msg = args->msg;
    free(args);

    LOG_DEBUG("[%s] Worker 线程开始, sessionId=%lu", MODULE_NAME, sessionId);

    // 如果已取消，直接返回
    if (g_cancelOcr || sessionId != g_currentSessionId) {
        ScreenshotImageFree(image);
        LOG_DEBUG("[%s] Worker 线程已取消, sessionId=%lu", MODULE_NAME, sessionId);
        return 0;
    }

    // 执行识别（mock 延迟模拟真实 OCR）
    Sleep(500);

    if (g_cancelOcr || sessionId != g_currentSessionId) {
        ScreenshotImageFree(image);
        LOG_DEBUG("[%s] Worker 线程已取消, sessionId=%lu", MODULE_NAME, sessionId);
        return 0;
    }

    OCRResults* results = OCRRecognize(image);
    ScreenshotImageFree(image);

    if (results == NULL) {
        LOG_ERROR("[%s] Worker 识别失败, sessionId=%lu", MODULE_NAME, sessionId);
        return 0;
    }

    // 验证 session 仍然有效
    if (g_cancelOcr || sessionId != g_currentSessionId) {
        OCRFreeResults(results);
        LOG_DEBUG("[%s] Worker 线程已取消, sessionId=%lu", MODULE_NAME, sessionId);
        return 0;
    }

    // 通过 PostMessage 发送结果到 UI 线程
    OcrAsyncMessage* msgData = (OcrAsyncMessage*)malloc(sizeof(OcrAsyncMessage));
    if (msgData == NULL) {
        OCRFreeResults(results);
        LOG_ERROR("[%s] 分配消息失败", MODULE_NAME);
        return 0;
    }

    msgData->sessionId = sessionId;
    msgData->results = results;

    LOG_INFO("[%s] Worker 识别完成, 发送消息, sessionId=%lu, wordCount=%d",
             MODULE_NAME, sessionId, results->wordCount);

    PostMessage(hwnd, msg, 0, (LPARAM)msgData);

    return 0;
}

OCRResults* OCRRecognize(const ScreenshotImage* image) {
    OCRResults* results;

    if (!g_ocrInitialized || image == NULL) {
        LOG_ERROR("[%s] 识别失败: 模块未初始化或无效参数", MODULE_NAME);
        return NULL;
    }

    LOG_DEBUG("[%s] 开始识别: %dx%d", MODULE_NAME, image->width, image->height);

    results = (OCRResults*)malloc(sizeof(OCRResults));
    if (results == NULL) {
        LOG_ERROR("[%s] 分配结果内存失败", MODULE_NAME);
        return NULL;
    }

    memset(results, 0, sizeof(OCRResults));
    results->imageWidth = image->width;
    results->imageHeight = image->height;

    /* Mock OCR data - 3 lines of text for testing selection UI */
    results->wordCount = 12;
    results->words = (OCRWord*)malloc(results->wordCount * sizeof(OCRWord));
    if (results->words == NULL) {
        free(results);
        LOG_ERROR("[%s] 分配词数组内存失败", MODULE_NAME);
        return NULL;
    }

    /* Line 1: "Hello World" */
    results->words[0].text = strdup("Hello");
    results->words[0].boundingBox = (RECT){50, 50, 120, 76};
    results->words[0].confidence = 0.95f;
    results->words[0].isLineBreak = FALSE;

    results->words[1].text = strdup("World");
    results->words[1].boundingBox = (RECT){130, 50, 200, 76};
    results->words[1].confidence = 0.92f;
    results->words[1].isLineBreak = TRUE;

    /* Line 2: "OCR Selection Test" */
    results->words[2].text = strdup("OCR");
    results->words[2].boundingBox = (RECT){50, 86, 100, 112};
    results->words[2].confidence = 0.90f;
    results->words[2].isLineBreak = FALSE;

    results->words[3].text = strdup("Selection");
    results->words[3].boundingBox = (RECT){110, 86, 200, 112};
    results->words[3].confidence = 0.88f;
    results->words[3].isLineBreak = FALSE;

    results->words[4].text = strdup("Test");
    results->words[4].boundingBox = (RECT){210, 86, 260, 112};
    results->words[4].confidence = 0.93f;
    results->words[4].isLineBreak = TRUE;

    /* Line 3: "PowerCapslock" */
    results->words[5].text = strdup("Power");
    results->words[5].boundingBox = (RECT){50, 122, 130, 148};
    results->words[5].confidence = 0.91f;
    results->words[5].isLineBreak = FALSE;

    results->words[6].text = strdup("Capslock");
    results->words[6].boundingBox = (RECT){140, 122, 240, 148};
    results->words[6].confidence = 0.89f;
    results->words[6].isLineBreak = TRUE;

    /* Extra words for testing selection range */
    results->words[7].text = strdup("Select");
    results->words[7].boundingBox = (RECT){50, 158, 120, 184};
    results->words[7].confidence = 0.87f;
    results->words[7].isLineBreak = FALSE;

    results->words[8].text = strdup("multiple");
    results->words[8].boundingBox = (RECT){130, 158, 220, 184};
    results->words[8].confidence = 0.85f;
    results->words[8].isLineBreak = FALSE;

    results->words[9].text = strdup("words");
    results->words[9].boundingBox = (RECT){230, 158, 290, 184};
    results->words[9].confidence = 0.83f;
    results->words[9].isLineBreak = TRUE;

    results->words[10].text = strdup("Copy");
    results->words[10].boundingBox = (RECT){50, 194, 110, 220};
    results->words[10].confidence = 0.91f;
    results->words[10].isLineBreak = FALSE;

    results->words[11].text = strdup("text");
    results->words[11].boundingBox = (RECT){120, 194, 170, 220};
    results->words[11].confidence = 0.90f;
    results->words[11].isLineBreak = TRUE;

    /* Build fullText from words */
    {
        size_t len = 0;
        int i;
        for (i = 0; i < results->wordCount; i++) {
            len += strlen(results->words[i].text);
            if (results->words[i].isLineBreak) {
                len += 2; /* \r\n */
            } else if (i < results->wordCount - 1) {
                len += 1; /* space */
            }
        }
        results->fullText = (char*)malloc(len + 1);
        if (results->fullText) {
            char* p = results->fullText;
            for (i = 0; i < results->wordCount; i++) {
                strcpy(p, results->words[i].text);
                p += strlen(results->words[i].text);
                if (results->words[i].isLineBreak) {
                    *p++ = '\r';
                    *p++ = '\n';
                } else if (i < results->wordCount - 1) {
                    *p++ = ' ';
                }
            }
            *p = '\0';
        }
    }

    LOG_INFO("[%s] 识别完成, wordCount=%d", MODULE_NAME, results->wordCount);
    return results;
}

void OCRFreeResults(OCRResults* results) {
    if (results == NULL) {
        return;
    }
    if (results->words != NULL) {
        int i;
        for (i = 0; i < results->wordCount; i++) {
            if (results->words[i].text != NULL) {
                free(results->words[i].text);
            }
        }
        free(results->words);
    }
    if (results->fullText != NULL) {
        free(results->fullText);
    }
    free(results);
}

int OCRTest(void) {
    ScreenshotImage* captureImg;
    OCRResults* results;
    int result = 1;

    LOG_INFO("[%s测试] 开始测试...", MODULE_NAME);

    printf("========== PowerCapslock Screenshot OCR Test ==========\n");
    printf("Initializing OCR module...\n");
    fflush(stdout);

    if (!OCRInit()) {
        printf("ERROR: Failed to initialize OCR module\n");
        return 1;
    }

    if (!ScreenshotInit()) {
        printf("ERROR: Failed to initialize screenshot module\n");
        OCRCleanup();
        return 1;
    }

    captureImg = ScreenshotCaptureRect(0, 0, 400, 300);
    if (captureImg == NULL) {
        printf("ERROR: Failed to capture screenshot\n");
        ScreenshotCleanup();
        OCRCleanup();
        return 1;
    }

    printf("Running OCR recognition...\n");
    results = OCRRecognize(captureImg);
    if (results != NULL) {
        printf("OCR result: %s\n", results->fullText);
        OCRFreeResults(results);
        result = 0;
    } else {
        printf("ERROR: OCR recognition failed\n");
    }

    ScreenshotImageFree(captureImg);
    ScreenshotCleanup();
    OCRCleanup();

    printf("OCR test %s\n", result == 0 ? "PASSED" : "FAILED");
    LOG_INFO("[%s测试] 测试完成", MODULE_NAME);
    return result;
}
