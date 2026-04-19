#include "screenshot_ocr.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <process.h>

#ifdef USE_TESSERACT
/* Forward declaration for Tesseract backend */
OCRResults* OCRRecognizeTesseract(const ScreenshotImage* image);
#endif

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
    DWORD clientSessionId;
    HWND hwnd;
    UINT msg;
} WorkerArgs;

// 前向声明
static unsigned __stdcall OcrWorkerThread(void* arg);

BOOL OCRInit(void) {
    LOG_DEBUG("[%s] 开始初始化...", MODULE_NAME);

#ifdef USE_TESSERACT
    if (!OCRInitTesseract(NULL)) {
        LOG_WARN("[%s] Tesseract 初始化失败，使用 Mock OCR", MODULE_NAME);
    }
#else
    LOG_INFO("[%s] 模块初始化成功 (Mock OCR)", MODULE_NAME);
#endif

    g_ocrInitialized = TRUE;
    return TRUE;
}

void OCRCleanup(void) {
    LOG_DEBUG("[%s] 开始清理...", MODULE_NAME);
    OCRCancelAsync();

#ifdef USE_TESSERACT
    OCRCleanupTesseract();
#endif

    g_ocrInitialized = FALSE;
    LOG_INFO("[%s] 模块已清理", MODULE_NAME);
}

BOOL OCRRecognizeAsync(const ScreenshotImage* image, HWND hwnd, UINT msg, DWORD clientSessionId) {
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
    args->clientSessionId = clientSessionId;
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
    DWORD clientSessionId = args->clientSessionId;
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

    LOG_DEBUG("[%s] Worker 线程执行识别, sessionId=%lu", MODULE_NAME, sessionId);
    // 执行识别（mock 延迟模拟真实 OCR）
    Sleep(500);

    if (g_cancelOcr || sessionId != g_currentSessionId) {
        ScreenshotImageFree(image);
        LOG_DEBUG("[%s] Worker 线程已取消, sessionId=%lu", MODULE_NAME, sessionId);
        return 0;
    }

    int imageWidth = image->width;
    int imageHeight = image->height;
    OCRResults* results = OCRRecognize(image);
    ScreenshotImageFree(image);

    if (results == NULL) {
        results = (OCRResults*)calloc(1, sizeof(OCRResults));
        if (results == NULL) {
            LOG_ERROR("[%s] Worker 识别失败且无法分配空结果, sessionId=%lu", MODULE_NAME, sessionId);
            return 0;
        }
        results->imageWidth = imageWidth;
        results->imageHeight = imageHeight;
        LOG_WARN("[%s] Worker 未识别到文本，发送空 OCR 结果, sessionId=%lu", MODULE_NAME, sessionId);
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

    msgData->sessionId = clientSessionId;
    msgData->results = results;

    LOG_INFO("[%s] Worker 识别完成, 发送消息, sessionId=%lu, clientSessionId=%lu, wordCount=%d",
             MODULE_NAME, sessionId, clientSessionId, results->wordCount);

    if (!PostMessage(hwnd, msg, 0, (LPARAM)msgData)) {
        LOG_WARN("[%s] 发送 OCR 完成消息失败, hwnd=%p, error=%lu", MODULE_NAME, hwnd, GetLastError());
        OCRFreeResults(results);
        free(msgData);
    }

    return 0;
}

OCRResults* OCRRecognize(const ScreenshotImage* image) {
    OCRResults* results;

    if (!g_ocrInitialized || image == NULL) {
        LOG_ERROR("[%s] 识别失败: 模块未初始化或无效参数", MODULE_NAME);
        return NULL;
    }

    LOG_DEBUG("[%s] 开始识别: %dx%d", MODULE_NAME, image->width, image->height);

#ifdef USE_TESSERACT
    /* 使用 Tesseract OCR */
    LOG_DEBUG("[%s] 调用 OCRRecognizeTesseract...", MODULE_NAME);
    results = OCRRecognizeTesseract(image);
    LOG_DEBUG("[%s] OCRRecognizeTesseract 返回, results=%p", MODULE_NAME, results);
    if (results != NULL && results->wordCount > 0) {
        LOG_INFO("[%s] Tesseract 识别完成: %d words", MODULE_NAME, results->wordCount);
        return results;
    }
    LOG_WARN("[%s] Tesseract 识别失败或无结果", MODULE_NAME);
    /* 清理 Tesseract 结果 */
    if (results != NULL) {
        OCRFreeResults(results);
        results = NULL;
    }
#else
    results = NULL;
#endif

    /* 如果 Tesseract 未启用或未识别到文字，返回 NULL
     * UI 会显示"未识别到文字"提示用户
     */
    LOG_INFO("[%s] 未识别到文字或 OCR 不可用", MODULE_NAME);
    return NULL;
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
