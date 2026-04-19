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
#include <ctype.h>
#include <stdio.h>
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
typedef int (*TessBaseAPIInit2_t)(TessBaseAPI*, const char*, const char*, int);
typedef int (*TessBaseAPIInit3_t)(TessBaseAPI*, const char*, const char*);
typedef void (*TessBaseAPISetPageSegMode_t)(TessBaseAPI*, int);
typedef int (*TessBaseAPISetVariable_t)(TessBaseAPI*, const char*, const char*);
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
static TessBaseAPIInit2_t fp_TessBaseAPIInit2;
static TessBaseAPIInit3_t fp_TessBaseAPIInit3;
static TessBaseAPISetPageSegMode_t fp_TessBaseAPISetPageSegMode;
static TessBaseAPISetVariable_t fp_TessBaseAPISetVariable;
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
 * 检查文件是否存在
 */
static BOOL FileExists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static BOOL DirectoryExists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static BOOL BuildExeRelativePath(const char* child, char* out, size_t outSize) {
    DWORD len;
    char* lastSlash;
    size_t baseLen;
    int written;

    if (child == NULL || out == NULL || outSize == 0) {
        return FALSE;
    }

    len = GetModuleFileNameA(NULL, out, (DWORD)outSize);
    if (len == 0 || len >= outSize) {
        return FALSE;
    }

    lastSlash = strrchr(out, '\\');
    if (lastSlash == NULL) {
        return FALSE;
    }
    *(lastSlash + 1) = '\0';
    baseLen = strlen(out);

    written = snprintf(out + baseLen, outSize - baseLen, "%s", child);
    return written >= 0 && (size_t)written < outSize - baseLen;
}

static BOOL Utf8NextCodepoint(const char* text, size_t len, size_t* index, unsigned int* codepoint) {
    const unsigned char* p;
    size_t i;

    if (text == NULL || index == NULL || codepoint == NULL || *index >= len) {
        return FALSE;
    }

    p = (const unsigned char*)text;
    i = *index;

    if (p[i] < 0x80) {
        *codepoint = p[i];
        *index = i + 1;
        return TRUE;
    }

    if ((p[i] & 0xE0) == 0xC0 && i + 1 < len &&
        (p[i + 1] & 0xC0) == 0x80) {
        *codepoint = ((unsigned int)(p[i] & 0x1F) << 6) |
                     (unsigned int)(p[i + 1] & 0x3F);
        *index = i + 2;
        return TRUE;
    }

    if ((p[i] & 0xF0) == 0xE0 && i + 2 < len &&
        (p[i + 1] & 0xC0) == 0x80 &&
        (p[i + 2] & 0xC0) == 0x80) {
        *codepoint = ((unsigned int)(p[i] & 0x0F) << 12) |
                     ((unsigned int)(p[i + 1] & 0x3F) << 6) |
                     (unsigned int)(p[i + 2] & 0x3F);
        *index = i + 3;
        return TRUE;
    }

    if ((p[i] & 0xF8) == 0xF0 && i + 3 < len &&
        (p[i + 1] & 0xC0) == 0x80 &&
        (p[i + 2] & 0xC0) == 0x80 &&
        (p[i + 3] & 0xC0) == 0x80) {
        *codepoint = ((unsigned int)(p[i] & 0x07) << 18) |
                     ((unsigned int)(p[i + 1] & 0x3F) << 12) |
                     ((unsigned int)(p[i + 2] & 0x3F) << 6) |
                     (unsigned int)(p[i + 3] & 0x3F);
        *index = i + 4;
        return TRUE;
    }

    *codepoint = p[i];
    *index = i + 1;
    return FALSE;
}

static BOOL CodepointIsCjk(unsigned int codepoint) {
    return (codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
           (codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
           (codepoint >= 0xF900 && codepoint <= 0xFAFF) ||
           (codepoint >= 0x20000 && codepoint <= 0x2A6DF) ||
           (codepoint >= 0x2A700 && codepoint <= 0x2B73F) ||
           (codepoint >= 0x2B740 && codepoint <= 0x2B81F) ||
           (codepoint >= 0x2B820 && codepoint <= 0x2CEAF);
}

static BOOL Utf8TextContainsCjk(const char* text) {
    size_t len;
    size_t index = 0;

    if (text == NULL) {
        return FALSE;
    }

    len = strlen(text);
    while (index < len) {
        unsigned int codepoint = 0;
        Utf8NextCodepoint(text, len, &index, &codepoint);
        if (CodepointIsCjk(codepoint)) {
            return TRUE;
        }
    }
    return FALSE;
}

static char* DuplicateTrimmedText(const char* text) {
    const char* start;
    const char* end;
    size_t len;
    char* copy;

    if (text == NULL) {
        return NULL;
    }

    start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        end--;
    }

    len = (size_t)(end - start);
    if (len == 0) {
        return NULL;
    }

    copy = (char*)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

static BOOL EnsureOcrWordCapacity(OCRResults* results, int* capacity, int needed) {
    OCRWord* newWords;
    int newCapacity;

    if (results == NULL || capacity == NULL) {
        return FALSE;
    }

    if (needed <= *capacity) {
        return TRUE;
    }

    newCapacity = *capacity > 0 ? *capacity : 64;
    while (newCapacity < needed) {
        newCapacity *= 2;
    }

    newWords = (OCRWord*)realloc(results->words, (size_t)newCapacity * sizeof(OCRWord));
    if (newWords == NULL) {
        return FALSE;
    }

    results->words = newWords;
    *capacity = newCapacity;
    return TRUE;
}

static BOOL AppendOcrWord(OCRResults* results, int* wordCount, int* capacity,
                          const char* text, RECT boundingBox, float confidence, BOOL isLineBreak) {
    char* copy;

    if (results == NULL || wordCount == NULL || capacity == NULL) {
        return FALSE;
    }

    copy = DuplicateTrimmedText(text);
    if (copy == NULL) {
        return TRUE;
    }

    if (!EnsureOcrWordCapacity(results, capacity, *wordCount + 1)) {
        free(copy);
        return FALSE;
    }

    results->words[*wordCount].text = copy;
    results->words[*wordCount].boundingBox = boundingBox;
    results->words[*wordCount].confidence = confidence;
    results->words[*wordCount].isLineBreak = isLineBreak;
    (*wordCount)++;
    return TRUE;
}

static void ClearOcrWords(OCRResults* results, int wordCount) {
    int i;

    if (results == NULL || results->words == NULL) {
        return;
    }

    for (i = 0; i < wordCount; i++) {
        free(results->words[i].text);
        results->words[i].text = NULL;
    }
}

static BOOL OcrWordsContainCjk(const OCRResults* results, int wordCount) {
    int i;

    if (results == NULL || results->words == NULL) {
        return FALSE;
    }

    for (i = 0; i < wordCount; i++) {
        if (Utf8TextContainsCjk(results->words[i].text)) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL CollectIteratorItems(OCRResults* results, int* wordCount, int* capacity, int level) {
    TessResultIterator* ri;
    TessPageIterator* pi;
    int startCount;

    if (results == NULL || wordCount == NULL || capacity == NULL) {
        return FALSE;
    }

    ri = fp_TessBaseAPIGetIterator(g_tessApi);
    if (ri == NULL) {
        return FALSE;
    }

    pi = fp_TessResultIteratorGetPageIterator(ri);
    startCount = *wordCount;
    do {
        char* text = fp_TessResultIteratorGetUTF8Text(ri, level);
        if (text != NULL) {
            int x1, y1, x2, y2;
            if (fp_TessPageIteratorBoundingBox(pi, level, &x1, &y1, &x2, &y2)) {
                RECT bbox;
                bbox.left = x1;
                bbox.top = y1;
                bbox.right = x2;
                bbox.bottom = y2;
                if (!AppendOcrWord(results, wordCount, capacity, text, bbox,
                                   fp_TessResultIteratorConfidence(ri, level), FALSE)) {
                    fp_TessDeleteText(text);
                    return FALSE;
                }
            }
            fp_TessDeleteText(text);
        }
    } while (fp_TessResultIteratorNext(ri, level));

    return *wordCount > startCount;
}

static void MarkLineBreaksBetweenItems(OCRResults* results, int wordCount) {
    int i;

    if (results == NULL || results->words == NULL) {
        return;
    }

    for (i = 0; i < wordCount; i++) {
        results->words[i].isLineBreak = (i < wordCount - 1);
    }
}

static int ChooseOcrInputScale(int width, int height) {
    int maxDim = width > height ? width : height;

    if (maxDim <= 1200) {
        return 3;
    }
    if (maxDim <= 2200) {
        return 2;
    }
    return 1;
}

static BYTE* CreateScaledRgbBuffer(const ScreenshotImage* image, int scale,
                                   int* outWidth, int* outHeight, int* outStride) {
    int scaledWidth;
    int scaledHeight;
    int scaledStride;
    BYTE* rgbPixels;

    if (image == NULL || image->pixels == NULL || scale <= 0 ||
        outWidth == NULL || outHeight == NULL || outStride == NULL) {
        return NULL;
    }

    scaledWidth = image->width * scale;
    scaledHeight = image->height * scale;
    scaledStride = scaledWidth * 3;
    rgbPixels = (BYTE*)malloc((size_t)scaledStride * scaledHeight);
    if (rgbPixels == NULL) {
        return NULL;
    }

    for (int y = 0; y < scaledHeight; y++) {
        int srcY = y / scale;
        const BYTE* srcRow = image->pixels + srcY * image->stride;
        BYTE* dstRow = rgbPixels + y * scaledStride;
        for (int x = 0; x < scaledWidth; x++) {
            int srcX = x / scale;
            const BYTE* src = srcRow + srcX * 4;
            BYTE* dst = dstRow + x * 3;
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
        }
    }

    *outWidth = scaledWidth;
    *outHeight = scaledHeight;
    *outStride = scaledStride;
    return rgbPixels;
}

static int ScaleCoordDown(int value, int scale, BOOL upperBound) {
    if (scale <= 1) {
        return value;
    }
    if (upperBound) {
        return (value + scale - 1) / scale;
    }
    return value / scale;
}

static void ScaleOcrBoundingBoxesToOriginal(OCRResults* results, int wordCount,
                                            int scale, int imageWidth, int imageHeight) {
    int i;

    if (results == NULL || results->words == NULL || scale <= 1) {
        return;
    }

    for (i = 0; i < wordCount; i++) {
        RECT* box = &results->words[i].boundingBox;
        box->left = ScaleCoordDown(box->left, scale, FALSE);
        box->top = ScaleCoordDown(box->top, scale, FALSE);
        box->right = ScaleCoordDown(box->right, scale, TRUE);
        box->bottom = ScaleCoordDown(box->bottom, scale, TRUE);

        if (box->left < 0) box->left = 0;
        if (box->top < 0) box->top = 0;
        if (box->right > imageWidth) box->right = imageWidth;
        if (box->bottom > imageHeight) box->bottom = imageHeight;
        if (box->right <= box->left) box->right = box->left + 1;
        if (box->bottom <= box->top) box->bottom = box->top + 1;
    }
}

/**
 * Initialize Tesseract OCR backend via dynamic loading
 */
BOOL OCRInitTesseract(const char* tessdataPath) {
    LOG_DEBUG("[%s] OCRInitTesseract 开始", MODULE_NAME);

    if (g_ocrInitialized && g_tessDll != NULL) {
        LOG_DEBUG("[%s] Tesseract 已初始化", MODULE_NAME);
        return TRUE;
    }

    /* 使用系统 Tesseract 安装目录的 DLL */
    char dllPath[MAX_PATH];
    const char* tessDir = "C:\\Program Files\\Tesseract-OCR";
    strcpy(dllPath, tessDir);
    strcat(dllPath, "\\libtesseract-5.dll");

    LOG_DEBUG("[%s] 加载 Tesseract DLL: %s", MODULE_NAME, dllPath);

    /* 添加 DLL 搜索路径（Windows Vista+） */
#ifdef __MINGW32__
    /* MinGW doesn't have AddDllDirectory, use SetDllDirectory instead */
    SetDllDirectoryA(tessDir);
#else
    AddDllDirectory(tessDir);
#endif

    /* 加载 DLL */
    g_tessDll = LoadLibraryA(dllPath);
    if (g_tessDll == NULL) {
        LOG_ERROR("[%s] 加载 DLL 失败: %s", MODULE_NAME, dllPath);
        return FALSE;
    }

    /* 加载 C API 函数 */
    LOAD_TESS_FUNC(fp_TessBaseAPICreate, "TessBaseAPICreate");
    LOAD_TESS_FUNC(fp_TessBaseAPIDelete, "TessBaseAPIDelete");
    LOAD_TESS_FUNC(fp_TessBaseAPIInit2, "TessBaseAPIInit2");
    LOAD_TESS_FUNC(fp_TessBaseAPIInit3, "TessBaseAPIInit3");
    LOAD_TESS_FUNC(fp_TessBaseAPISetPageSegMode, "TessBaseAPISetPageSegMode");
    LOAD_TESS_FUNC(fp_TessBaseAPISetVariable, "TessBaseAPISetVariable");
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

    char tessdataBuffer[MAX_PATH];

    /* 获取 tessdata 路径 */
    if (tessdataPath == NULL) {
        const char* envPath = getenv("TESSDATA_PREFIX");
        if (envPath != NULL) {
            tessdataPath = envPath;
        }
    }

    if (tessdataPath == NULL) {
        if (BuildExeRelativePath("tessdata", tessdataBuffer, sizeof(tessdataBuffer)) &&
            DirectoryExists(tessdataBuffer)) {
            tessdataPath = tessdataBuffer;
        } else {
            /* 使用系统 Tesseract tessdata 目录 */
            strcpy(tessdataBuffer, "C:\\Program Files\\Tesseract-OCR\\tessdata");
            tessdataPath = tessdataBuffer;
        }
    }

    /* 检查可用的语言包 */
    char langList[MAX_PATH];
    strcpy(langList, "eng");

    char chiPath[MAX_PATH];
    strcpy(chiPath, tessdataPath);
    strcat(chiPath, "\\script\\HanS.traineddata");
    if (FileExists(chiPath)) {
        strcpy(langList, "script/HanS+eng");
        LOG_INFO("[%s] 检测到简体中文脚本语言包: script/HanS.traineddata", MODULE_NAME);
    } else {
        strcpy(chiPath, tessdataPath);
        strcat(chiPath, "\\chi_sim.traineddata");
        if (FileExists(chiPath)) {
            strcpy(langList, "chi_sim+eng");
            LOG_INFO("[%s] 检测到简体中文语言包: chi_sim.traineddata", MODULE_NAME);
        } else {
            strcpy(chiPath, tessdataPath);
            strcat(chiPath, "\\script\\HanT.traineddata");
            if (FileExists(chiPath)) {
                strcpy(langList, "script/HanT+eng");
                LOG_INFO("[%s] 检测到繁体中文脚本语言包: script/HanT.traineddata", MODULE_NAME);
            } else {
                strcpy(chiPath, tessdataPath);
                strcat(chiPath, "\\chi_tra.traineddata");
                if (FileExists(chiPath)) {
                    strcpy(langList, "chi_tra+eng");
                    LOG_INFO("[%s] 检测到繁体中文语言包: chi_tra.traineddata", MODULE_NAME);
                }
            }
        }
    }

    LOG_DEBUG("[%s] 初始化 Tesseract, tessdata: %s, languages: %s", MODULE_NAME, tessdataPath, langList);

    /* 创建 API 实例 */
    g_tessApi = fp_TessBaseAPICreate();
    if (g_tessApi == NULL) {
        LOG_ERROR("[%s] 创建 Tesseract API 失败", MODULE_NAME);
        FreeLibrary(g_tessDll);
        g_tessDll = NULL;
        return FALSE;
    }

    /* 使用 OEM_DEFAULT 模式初始化（LSTM 模型） */
    int initResult = fp_TessBaseAPIInit3(g_tessApi, tessdataPath, langList);
    if (initResult != 0) {
        LOG_ERROR("[%s] Tesseract 初始化失败(err=%d)，尝试仅英文", MODULE_NAME, initResult);
        /* 初始化失败，尝试仅使用英文 */
        initResult = fp_TessBaseAPIInit3(g_tessApi, tessdataPath, "eng");
        if (initResult != 0) {
            LOG_ERROR("[%s] Tesseract 英文初始化也失败(err=%d)", MODULE_NAME, initResult);
            fp_TessBaseAPIDelete(g_tessApi);
            g_tessApi = NULL;
            FreeLibrary(g_tessDll);
            g_tessDll = NULL;
            return FALSE;
        }
    }

    fp_TessBaseAPISetPageSegMode(g_tessApi, PSM_SPARSE_TEXT);

    /* 设置变量以避免识别挂起 */
    fp_TessBaseAPISetVariable(g_tessApi, "interactive_display_mode", "false");
    fp_TessBaseAPISetVariable(g_tessApi, "no_progress", "true");
    fp_TessBaseAPISetVariable(g_tessApi, "preserve_interword_spaces", "1");
    fp_TessBaseAPISetVariable(g_tessApi, "user_defined_dpi", "300");

    if (tessdataPath != NULL) {
        g_tessdataPath = strdup(tessdataPath);
    }
    g_ocrInitialized = TRUE;

    LOG_INFO("[%s] Tesseract OCR 初始化成功 (动态加载, 语言: %s)", MODULE_NAME, langList);
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

    int ocrScale = ChooseOcrInputScale(image->width, image->height);
    int ocrWidth = 0;
    int ocrHeight = 0;
    int rgbStride = 0;
    BYTE* rgbPixels = CreateScaledRgbBuffer(image, ocrScale, &ocrWidth, &ocrHeight, &rgbStride);
    if (rgbPixels == NULL) {
        LOG_ERROR("[%s] Tesseract 识别失败: RGB 缓冲区分配失败", MODULE_NAME);
        return NULL;
    }

    LOG_DEBUG("[%s] OCR input scaled to %dx%d (scale=%d)",
              MODULE_NAME, ocrWidth, ocrHeight, ocrScale);

    fp_TessBaseAPISetImage(g_tessApi,
                            (const unsigned char*)rgbPixels,
                            ocrWidth,
                            ocrHeight,
                            3,  /* bytes_per_pixel - RGB */
                            rgbStride);

    /* 执行识别 */
    int recognizeResult = fp_TessBaseAPIRecognize(g_tessApi, NULL);
    if (recognizeResult != 0) {
        LOG_ERROR("[%s] Tesseract 识别执行失败, result=%d", MODULE_NAME, recognizeResult);
        free(rgbPixels);
        return NULL;
    }

    free(rgbPixels);

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
    int wordCapacity = 512;
    int wordCount = 0;
    results->words = (OCRWord*)calloc((size_t)wordCapacity, sizeof(OCRWord));
    if (results->words == NULL) {
        OCRFreeResults(results);
        return NULL;
    }

    CollectIteratorItems(results, &wordCount, &wordCapacity, RIL_WORD);

    if ((results->fullText != NULL && Utf8TextContainsCjk(results->fullText) &&
         !OcrWordsContainCjk(results, wordCount)) ||
        (wordCount == 0 && results->fullText != NULL && results->fullText[0] != '\0')) {
        LOG_DEBUG("[%s] Rebuilding OCR selectable items from text lines for CJK/fallback copy", MODULE_NAME);
        ClearOcrWords(results, wordCount);
        wordCount = 0;

        if (CollectIteratorItems(results, &wordCount, &wordCapacity, RIL_TEXTLINE)) {
            MarkLineBreaksBetweenItems(results, wordCount);
        } else {
            CollectIteratorItems(results, &wordCount, &wordCapacity, RIL_SYMBOL);
        }
    }

    ScaleOcrBoundingBoxesToOriginal(results, wordCount, ocrScale, image->width, image->height);
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
