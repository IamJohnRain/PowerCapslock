/**
 * screenshot_ocr_tesseract.c
 *
 * Tesseract OCR backend using dynamic loading.
 *
 * This implementation dynamically loads the Tesseract DLL at runtime,
 * avoiding MinGW import library compatibility issues.
 */

#include "screenshot_ocr.h"
#include "logger.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_TESSERACT

#define MODULE_NAME "OCR"

/* Tesseract opaque types - forward declarations */
struct TessBaseAPI;
struct TessResultIterator;
struct TessPageIterator;
struct Pix;
typedef struct TessBaseAPI TessBaseAPI;
typedef struct TessResultIterator TessResultIterator;
typedef struct TessPageIterator TessPageIterator;

/* Tesseract enums from capi.h */
typedef enum TessOcrEngineMode {
    OEM_TESSERACT_ONLY = 0,
    OEM_LSTM_ONLY = 1,
    OEM_TESSERACT_LSTM_COMBINED = 2,
    OEM_DEFAULT = 3
} TessOcrEngineMode;

typedef enum TessPageSegMode {
    PSM_OSD_ONLY = 0,
    PSM_AUTO_OSD = 1,
    PSM_AUTO_ONLY = 2,
    PSM_AUTO = 3,
    PSM_SINGLE_COLUMN = 4,
    PSM_SINGLE_BLOCK_VERT_TEXT = 5,
    PSM_SINGLE_BLOCK = 6,
    PSM_SINGLE_LINE = 7,
    PSM_SINGLE_WORD = 8,
    PSM_CIRCLE_WORD = 9,
    PSM_SINGLE_CHAR = 10,
    PSM_SPARSE_TEXT = 11,
    PSM_SPARSE_TEXT_OSD = 12,
    PSM_RAW_LINE = 13,
    PSM_COUNT = 14
} TessPageSegMode;

typedef enum TessPageIteratorLevel {
    RIL_BLOCK = 0,
    RIL_PARA = 1,
    RIL_TEXTLINE = 2,
    RIL_WORD = 3,
    RIL_SYMBOL = 4
} TessPageIteratorLevel;

/* Tesseract function pointer types */
typedef TessBaseAPI* (*TessBaseAPICreate_t)(void);
typedef void (*TessBaseAPIDelete_t)(TessBaseAPI*);
typedef int (*TessBaseAPIInit3_t)(TessBaseAPI*, const char*, const char*);
typedef void (*TessBaseAPISetPageSegMode_t)(TessBaseAPI*, int);
typedef void (*TessBaseAPISetImage_t)(TessBaseAPI*, const unsigned char*, int, int, int, int);
typedef int (*TessBaseAPIRecognize_t)(TessBaseAPI*, void*);
typedef char* (*TessBaseAPIGetUTF8Text_t)(TessBaseAPI*);
typedef TessResultIterator* (*TessBaseAPIGetIterator_t)(TessBaseAPI*);
typedef void (*TessResultIteratorDelete_t)(TessResultIterator*);
typedef char* (*TessResultIteratorGetUTF8Text_t)(TessResultIterator*, int);
typedef float (*TessResultIteratorConfidence_t)(TessResultIterator*, int);
typedef BOOL (*TessResultIteratorNext_t)(TessResultIterator*, int);
typedef TessPageIterator* (*TessResultIteratorGetPageIterator_t)(TessResultIterator*);
typedef void (*TessPageIteratorDelete_t)(TessPageIterator*);
typedef BOOL (*TessPageIteratorBoundingBox_t)(TessPageIterator*, int, int*, int*, int*, int*);
typedef void (*TessDeleteText_t)(char*);
typedef void (*TessBaseAPIEnd_t)(TessBaseAPI*);

static BOOL g_ocrInitialized = FALSE;
static HMODULE g_tessDll = NULL;
static TessBaseAPI* g_tessApi = NULL;
static char* g_tessdataPath = NULL;

/* Tesseract C API function pointers */
static TessBaseAPICreate_t fp_TessBaseAPICreate;
static TessBaseAPIDelete_t fp_TessBaseAPIDelete;
static TessBaseAPIInit3_t fp_TessBaseAPIInit3;
static TessBaseAPISetPageSegMode_t fp_TessBaseAPISetPageSegMode;
static TessBaseAPISetImage_t fp_TessBaseAPISetImage;
static TessBaseAPIRecognize_t fp_TessBaseAPIRecognize;
static TessBaseAPIGetUTF8Text_t fp_TessBaseAPIGetUTF8Text;
static TessBaseAPIGetIterator_t fp_TessBaseAPIGetIterator;
static TessResultIteratorDelete_t fp_TessResultIteratorDelete;
static TessResultIteratorGetUTF8Text_t fp_TessResultIteratorGetUTF8Text;
static TessResultIteratorConfidence_t fp_TessResultIteratorConfidence;
static TessResultIteratorNext_t fp_TessResultIteratorNext;
static TessResultIteratorGetPageIterator_t fp_TessResultIteratorGetPageIterator;
static TessPageIteratorDelete_t fp_TessPageIteratorDelete;
static TessPageIteratorBoundingBox_t fp_TessPageIteratorBoundingBox;
static TessDeleteText_t fp_TessDeleteText;
static TessBaseAPIEnd_t fp_TessBaseAPIEnd;

/**
 * Load Tesseract function from DLL
 */
static void* LoadTessFunc(HMODULE dll, const char* name) {
    void* func = (void*)GetProcAddress(dll, name);
    if (func == NULL) {
        LOG_ERROR("[%s] 加载函数失败: %s", MODULE_NAME, name);
    }
    return func;
}

#define LOAD_TESS_FUNC(var, name) do { \
    var = (typeof(var))LoadTessFunc(g_tessDll, name); \
    if (var == NULL) return FALSE; \
} while(0)

/**
 * Initialize Tesseract OCR backend via dynamic loading
 */
BOOL OCRInitTesseract(const char* tessdataPath) {
    if (g_ocrInitialized && g_tessDll != NULL) {
        LOG_DEBUG("[%s] Tesseract 已初始化", MODULE_NAME);
        return TRUE;
    }

    /* 构建 DLL 路径 */
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char* lastSlash = strrchr(dllPath, '\\');
    if (lastSlash) *lastSlash = '\0';
    strcpy(dllPath + (lastSlash ? (lastSlash - dllPath + 1) : 0), "libtesseract-5.dll");

    LOG_DEBUG("[%s] 加载 Tesseract DLL: %s", MODULE_NAME, dllPath);

    /* 加载 DLL */
    g_tessDll = LoadLibraryA(dllPath);
    if (g_tessDll == NULL) {
        LOG_ERROR("[%s] 加载 DLL 失败: %s", MODULE_NAME, dllPath);
        return FALSE;
    }

    /* 加载 C API 函数 */
    LOAD_TESS_FUNC(fp_TessBaseAPICreate, "TessBaseAPICreate");
    LOAD_TESS_FUNC(fp_TessBaseAPIDelete, "TessBaseAPIDelete");
    LOAD_TESS_FUNC(fp_TessBaseAPIInit3, "TessBaseAPIInit3");
    LOAD_TESS_FUNC(fp_TessBaseAPISetPageSegMode, "TessBaseAPISetPageSegMode");
    LOAD_TESS_FUNC(fp_TessBaseAPISetImage, "TessBaseAPISetImage");
    LOAD_TESS_FUNC(fp_TessBaseAPIRecognize, "TessBaseAPIRecognize");
    LOAD_TESS_FUNC(fp_TessBaseAPIGetUTF8Text, "TessBaseAPIGetUTF8Text");
    LOAD_TESS_FUNC(fp_TessBaseAPIGetIterator, "TessBaseAPIGetIterator");
    LOAD_TESS_FUNC(fp_TessResultIteratorDelete, "TessResultIteratorDelete");
    LOAD_TESS_FUNC(fp_TessResultIteratorGetUTF8Text, "TessResultIteratorGetUTF8Text");
    LOAD_TESS_FUNC(fp_TessResultIteratorConfidence, "TessResultIteratorConfidence");
    LOAD_TESS_FUNC(fp_TessResultIteratorNext, "TessResultIteratorNext");
    LOAD_TESS_FUNC(fp_TessResultIteratorGetPageIterator, "TessResultIteratorGetPageIterator");
    LOAD_TESS_FUNC(fp_TessPageIteratorDelete, "TessPageIteratorDelete");
    LOAD_TESS_FUNC(fp_TessPageIteratorBoundingBox, "TessPageIteratorBoundingBox");
    LOAD_TESS_FUNC(fp_TessDeleteText, "TessDeleteText");
    LOAD_TESS_FUNC(fp_TessBaseAPIEnd, "TessBaseAPIEnd");

    /* 获取 tessdata 路径 */
    if (tessdataPath == NULL) {
        const char* envPath = getenv("TESSDATA_PREFIX");
        if (envPath != NULL) {
            tessdataPath = envPath;
        }
    }

    if (tessdataPath == NULL) {
        GetModuleFileNameA(NULL, dllPath, MAX_PATH);
        lastSlash = strrchr(dllPath, '\\');
        if (lastSlash) *lastSlash = '\0';
        strcpy(dllPath + (lastSlash ? (lastSlash - dllPath + 1) : 0), "tessdata");
        tessdataPath = dllPath;
    }

    LOG_DEBUG("[%s] 初始化 Tesseract, tessdata: %s", MODULE_NAME, tessdataPath);

    /* 创建 API 实例 */
    g_tessApi = fp_TessBaseAPICreate();
    if (g_tessApi == NULL) {
        LOG_ERROR("[%s] 创建 Tesseract API 失败", MODULE_NAME);
        FreeLibrary(g_tessDll);
        g_tessDll = NULL;
        return FALSE;
    }

    /* 初始化 */
    if (fp_TessBaseAPIInit3(g_tessApi, tessdataPath, "eng") != 0) {
        LOG_ERROR("[%s] Tesseract 初始化失败", MODULE_NAME);
        fp_TessBaseAPIDelete(g_tessApi);
        g_tessApi = NULL;
        FreeLibrary(g_tessDll);
        g_tessDll = NULL;
        return FALSE;
    }

    fp_TessBaseAPISetPageSegMode(g_tessApi, PSM_AUTO);

    if (tessdataPath != NULL) {
        g_tessdataPath = strdup(tessdataPath);
    }
    g_ocrInitialized = TRUE;

    LOG_INFO("[%s] Tesseract OCR 初始化成功 (动态加载)", MODULE_NAME);
    return TRUE;
}

/**
 * Cleanup Tesseract OCR backend
 */
void OCRCleanupTesseract(void) {
    if (!g_ocrInitialized) {
        return;
    }

    LOG_DEBUG("[%s] 清理 Tesseract...", MODULE_NAME);

    if (g_tessApi != NULL) {
        fp_TessBaseAPIEnd(g_tessApi);
        fp_TessBaseAPIDelete(g_tessApi);
        g_tessApi = NULL;
    }

    if (g_tessDll != NULL) {
        FreeLibrary(g_tessDll);
        g_tessDll = NULL;
    }

    if (g_tessdataPath != NULL) {
        free(g_tessdataPath);
        g_tessdataPath = NULL;
    }

    g_ocrInitialized = FALSE;
}

/**
 * Recognize text using Tesseract C API via dynamic loading
 */
OCRResults* OCRRecognizeTesseract(const ScreenshotImage* image) {
    if (!g_ocrInitialized || image == NULL || g_tessDll == NULL || g_tessApi == NULL) {
        LOG_ERROR("[%s] Tesseract 识别失败: 未初始化", MODULE_NAME);
        return NULL;
    }

    if (image->pixels == NULL || image->width <= 0 || image->height <= 0) {
        LOG_ERROR("[%s] Tesseract 识别失败: 无效图像数据", MODULE_NAME);
        return NULL;
    }

    LOG_DEBUG("[%s] Tesseract 识别: %dx%d", MODULE_NAME, image->width, image->height);

    /* 使用 C API 设置图像 - 32位 RGBA 格式 */
    fp_TessBaseAPISetImage(g_tessApi,
                            (const unsigned char*)image->pixels,
                            image->width,
                            image->height,
                            4,  /* bytes_per_pixel - 32bit */
                            image->width * 4);  /* bytes_per_line */

    /* 执行识别 */
    if (fp_TessBaseAPIRecognize(g_tessApi, NULL) != 0) {
        LOG_ERROR("[%s] Tesseract 识别执行失败", MODULE_NAME);
        return NULL;
    }

    /* 创建结果结构 */
    OCRResults* results = (OCRResults*)malloc(sizeof(OCRResults));
    if (results == NULL) {
        return NULL;
    }
    memset(results, 0, sizeof(OCRResults));
    results->imageWidth = image->width;
    results->imageHeight = image->height;

    /* 获取完整文本 */
    char* fullText = fp_TessBaseAPIGetUTF8Text(g_tessApi);
    if (fullText != NULL) {
        results->fullText = strdup(fullText);
        fp_TessDeleteText(fullText);
    }

    /* 使用 Result Iterator 遍历单词 */
    int maxWords = 512;
    int wordCount = 0;
    results->words = (OCRWord*)malloc(maxWords * sizeof(OCRWord));
    if (results->words == NULL) {
        OCRFreeResults(results);
        return NULL;
    }

    TessResultIterator* ri = fp_TessBaseAPIGetIterator(g_tessApi);
    if (ri != NULL) {
        TessPageIterator* pi = fp_TessResultIteratorGetPageIterator(ri);
        do {
            char* word = fp_TessResultIteratorGetUTF8Text(ri, RIL_WORD);
            if (word != NULL && strlen(word) > 0) {
                int x1, y1, x2, y2;
                if (fp_TessPageIteratorBoundingBox(pi, RIL_WORD, &x1, &y1, &x2, &y2)) {
                    if (wordCount < maxWords) {
                        results->words[wordCount].text = strdup(word);
                        results->words[wordCount].boundingBox.left = x1;
                        results->words[wordCount].boundingBox.top = y1;
                        results->words[wordCount].boundingBox.right = x2;
                        results->words[wordCount].boundingBox.bottom = y2;
                        results->words[wordCount].confidence = fp_TessResultIteratorConfidence(ri, RIL_WORD);
                        results->words[wordCount].isLineBreak = FALSE;
                        wordCount++;
                    }
                }
            }
            fp_TessDeleteText(word);
        } while (fp_TessResultIteratorNext(ri, RIL_WORD));

        fp_TessPageIteratorDelete(pi);
        fp_TessResultIteratorDelete(ri);
    }

    results->wordCount = wordCount;

    if (results->wordCount > 0) {
        LOG_INFO("[%s] Tesseract 识别完成: %d 个单词", MODULE_NAME, results->wordCount);
    } else {
        LOG_WARN("[%s] Tesseract 未识别到文本", MODULE_NAME);
    }

    return results;
}

#else /* !USE_TESSERACT */

#define STUB_MODULE_NAME "OCR"

BOOL OCRInitTesseract(const char* tessdataPath) {
    LOG_DEBUG("[%s] Tesseract 未编译启用", STUB_MODULE_NAME);
    return FALSE;
}

void OCRCleanupTesseract(void) {
    /* nothing to do */
}

OCRResults* OCRRecognizeTesseract(const ScreenshotImage* image) {
    LOG_ERROR("[%s] Tesseract 未启用", STUB_MODULE_NAME);
    return NULL;
}

#endif /* USE_TESSERACT */
