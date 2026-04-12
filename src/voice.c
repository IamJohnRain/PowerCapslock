#include "voice.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <windows.h>

// sherpa-onnx C API 头文件
#ifdef USE_SHERPA_ONNX
#include "c-api.h"
#endif

// 全局状态
static bool g_modelLoaded = false;
static char g_modelPath[MAX_PATH] = {0};

#ifdef USE_SHERPA_ONNX
static SherpaOnnxOfflineRecognizer* g_recognizer = NULL;
#endif

static const char* SENSEVOICE_MODEL_NAME = "SenseVoice-Small";
static const char* SENSEVOICE_MODEL_FOLDER = "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17";

static void ClearCheckResult(VoiceModelCheckResult* result) {
    if (result != NULL) {
        memset(result, 0, sizeof(VoiceModelCheckResult));
    }
}

static void CopyResult(VoiceModelCheckResult* dest, const VoiceModelCheckResult* source) {
    if (dest != NULL && source != NULL) {
        memcpy(dest, source, sizeof(VoiceModelCheckResult));
    }
}

static void SetReason(VoiceModelCheckResult* result, const wchar_t* reason) {
    if (result == NULL || reason == NULL) {
        return;
    }
    wcsncpy(result->reason, reason, (sizeof(result->reason) / sizeof(result->reason[0])) - 1);
    result->reason[(sizeof(result->reason) / sizeof(result->reason[0])) - 1] = L'\0';
}

static void Utf8ToWideBuffer(const char* text, wchar_t* output, size_t outputChars) {
    if (output == NULL || outputChars == 0) {
        return;
    }
    output[0] = L'\0';
    if (text == NULL || text[0] == '\0') {
        return;
    }

    int written = MultiByteToWideChar(CP_UTF8, 0, text, -1, output, (int)outputChars);
    if (written <= 0) {
        MultiByteToWideChar(CP_ACP, 0, text, -1, output, (int)outputChars);
    }
    output[outputChars - 1] = L'\0';
}

static bool CopyString(char* dest, size_t destSize, const char* source) {
    if (dest == NULL || destSize == 0 || source == NULL) {
        return false;
    }
    int written = snprintf(dest, destSize, "%s", source);
    return written >= 0 && (size_t)written < destSize;
}

static bool JoinPath(char* dest, size_t destSize, const char* left, const char* right) {
    if (dest == NULL || destSize == 0 || left == NULL || right == NULL) {
        return false;
    }
    if (left[0] == '\0') {
        return CopyString(dest, destSize, right);
    }
    size_t len = strlen(left);
    const char* separator = (len > 0 && (left[len - 1] == '\\' || left[len - 1] == '/')) ? "" : "\\";
    int written = snprintf(dest, destSize, "%s%s%s", left, separator, right);
    return written >= 0 && (size_t)written < destSize;
}

static bool IsAbsolutePath(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if ((isalpha((unsigned char)path[0]) && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
        (path[0] == '\\' && path[1] == '\\') ||
        path[0] == '/') {
        return true;
    }
    return false;
}

static void NormalizeSlashesAndTrim(char* path) {
    if (path == NULL) {
        return;
    }
    for (char* p = path; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }

    size_t len = strlen(path);
    while (len > 0 && (path[len - 1] == ' ' || path[len - 1] == '\t' || path[len - 1] == '"' || path[len - 1] == '\'')) {
        path[--len] = '\0';
    }
    while (len > 3 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        path[--len] = '\0';
    }
}

static bool ResolveModelRoot(const char* modelDir, char* resolvedRoot, size_t resolvedRootSize, VoiceModelCheckResult* result) {
    char trimmed[MAX_PATH];
    const char* start = modelDir;

    if (resolvedRoot == NULL || resolvedRootSize == 0) {
        return false;
    }
    resolvedRoot[0] = '\0';

    if (modelDir == NULL) {
        SetReason(result, L"模型目录为空，请选择 SenseVoice 模型所在目录。");
        return false;
    }

    while (*start == ' ' || *start == '\t' || *start == '"' || *start == '\'') {
        start++;
    }
    if (!CopyString(trimmed, sizeof(trimmed), start)) {
        SetReason(result, L"模型目录路径过长，请选择更短的目录路径。");
        return false;
    }
    NormalizeSlashesAndTrim(trimmed);
    if (trimmed[0] == '\0') {
        SetReason(result, L"模型目录为空，请选择 SenseVoice 模型所在目录。");
        return false;
    }

    char candidate[MAX_PATH];
    if (IsAbsolutePath(trimmed)) {
        if (!CopyString(candidate, sizeof(candidate), trimmed)) {
            SetReason(result, L"模型目录路径过长，请选择更短的目录路径。");
            return false;
        }
    } else {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        char* slash = strrchr(exePath, '\\');
        if (slash != NULL) {
            *slash = '\0';
        }
        if (!JoinPath(candidate, sizeof(candidate), exePath, trimmed)) {
            SetReason(result, L"模型目录路径过长，请选择更短的目录路径。");
            return false;
        }
    }

    DWORD length = GetFullPathNameA(candidate, (DWORD)resolvedRootSize, resolvedRoot, NULL);
    if (length == 0 || length >= resolvedRootSize) {
        SetReason(result, L"模型目录路径无法解析或过长，请重新选择目录。");
        return false;
    }
    NormalizeSlashesAndTrim(resolvedRoot);
    return true;
}

static bool IsExistingDirectory(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool BuildCandidatePaths(const char* baseDir, char* modelPath, size_t modelPathSize,
                                char* tokensPath, size_t tokensPathSize) {
    return JoinPath(modelPath, modelPathSize, baseDir, "model.onnx") &&
           JoinPath(tokensPath, tokensPathSize, baseDir, "tokens.txt");
}

static bool TryModelCandidate(const char* resolvedRoot, const char* baseDir, VoiceModelCheckResult* result) {
    char modelPath[MAX_PATH];
    char tokensPath[MAX_PATH];
    if (!BuildCandidatePaths(baseDir, modelPath, sizeof(modelPath), tokensPath, sizeof(tokensPath))) {
        SetReason(result, L"模型文件路径过长，请将模型目录移动到更短路径后重试。");
        return false;
    }

    DWORD modelAttr = GetFileAttributesA(modelPath);
    DWORD tokensAttr = GetFileAttributesA(tokensPath);
    if (modelAttr != INVALID_FILE_ATTRIBUTES && !(modelAttr & FILE_ATTRIBUTE_DIRECTORY) &&
        tokensAttr != INVALID_FILE_ATTRIBUTES && !(tokensAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        CopyString(result->resolvedRoot, sizeof(result->resolvedRoot), resolvedRoot);
        CopyString(result->modelPath, sizeof(result->modelPath), modelPath);
        CopyString(result->tokensPath, sizeof(result->tokensPath), tokensPath);
        CopyString(result->modelName, sizeof(result->modelName), SENSEVOICE_MODEL_NAME);
        SetReason(result, L"SenseVoice-Small 模型检查通过。");
        return true;
    }

    if (result->modelPath[0] == '\0') {
        CopyString(result->modelPath, sizeof(result->modelPath), modelPath);
        CopyString(result->tokensPath, sizeof(result->tokensPath), tokensPath);
    }
    return false;
}

bool VoiceCheckModelDirectory(const char* modelDir, VoiceModelCheckResult* result) {
    VoiceModelCheckResult localResult;
    VoiceModelCheckResult* out = result != NULL ? result : &localResult;
    ClearCheckResult(out);

    char resolvedRoot[MAX_PATH];
    if (!ResolveModelRoot(modelDir, resolvedRoot, sizeof(resolvedRoot), out)) {
        return false;
    }
    CopyString(out->resolvedRoot, sizeof(out->resolvedRoot), resolvedRoot);
    CopyString(out->modelName, sizeof(out->modelName), SENSEVOICE_MODEL_NAME);

    if (!IsExistingDirectory(resolvedRoot)) {
        wchar_t wideRoot[MAX_PATH];
        Utf8ToWideBuffer(resolvedRoot, wideRoot, sizeof(wideRoot) / sizeof(wideRoot[0]));
        swprintf(out->reason, sizeof(out->reason) / sizeof(out->reason[0]),
                 L"模型目录不存在：\n%s\n\n请确认目录已解压并且路径可访问。", wideRoot);
        out->reason[(sizeof(out->reason) / sizeof(out->reason[0])) - 1] = L'\0';
        return false;
    }

    char senseVoiceDir[MAX_PATH];
    char nestedModelDir[MAX_PATH];
    if (JoinPath(senseVoiceDir, sizeof(senseVoiceDir), resolvedRoot, SENSEVOICE_MODEL_NAME) &&
        JoinPath(nestedModelDir, sizeof(nestedModelDir), senseVoiceDir, SENSEVOICE_MODEL_FOLDER) &&
        TryModelCandidate(resolvedRoot, nestedModelDir, out)) {
        return true;
    }

    char directNestedDir[MAX_PATH];
    if (JoinPath(directNestedDir, sizeof(directNestedDir), resolvedRoot, SENSEVOICE_MODEL_FOLDER) &&
        TryModelCandidate(resolvedRoot, directNestedDir, out)) {
        return true;
    }

    if (TryModelCandidate(resolvedRoot, resolvedRoot, out)) {
        return true;
    }

    wchar_t wideRoot[MAX_PATH];
    wchar_t wideExpected[MAX_PATH];
    Utf8ToWideBuffer(resolvedRoot, wideRoot, sizeof(wideRoot) / sizeof(wideRoot[0]));
    Utf8ToWideBuffer(out->modelPath, wideExpected, sizeof(wideExpected) / sizeof(wideExpected[0]));
    swprintf(out->reason, sizeof(out->reason) / sizeof(out->reason[0]),
             L"未找到可用的 SenseVoice-Small 模型。\n\n"
             L"当前选择：\n%s\n\n"
             L"请确认目录中存在 model.onnx 和 tokens.txt。\n"
             L"例如：\n%s",
             wideRoot, wideExpected);
    out->reason[(sizeof(out->reason) / sizeof(out->reason[0])) - 1] = L'\0';
    return false;
}

#ifdef USE_SHERPA_ONNX
static SherpaOnnxOfflineRecognizer* CreateRecognizer(const VoiceModelCheckResult* check, VoiceModelCheckResult* result) {
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    config.model_config.tokens = check->tokensPath;
    config.model_config.sense_voice.model = check->modelPath;
    config.model_config.sense_voice.language = "zh";
    config.model_config.sense_voice.use_itn = 1;
    config.model_config.num_threads = 4;
    config.model_config.provider = "cpu";
    config.model_config.debug = 0;

    SherpaOnnxOfflineRecognizer* recognizer =
        (SherpaOnnxOfflineRecognizer*)SherpaOnnxCreateOfflineRecognizer(&config);
    if (recognizer == NULL) {
        SetReason(result,
                  L"模型文件存在，但创建语音识别器失败。\n\n"
                  L"请确认模型文件完整，且 onnxruntime / sherpa-onnx DLL 依赖已随程序一起放置。");
    }
    return recognizer;
}
#endif

bool VoiceReloadModel(const char* modelDir, VoiceModelCheckResult* result) {
    VoiceModelCheckResult check;
    ClearCheckResult(&check);
    if (!VoiceCheckModelDirectory(modelDir, &check)) {
        CopyResult(result, &check);
        return false;
    }

    if (g_modelLoaded && strcmp(g_modelPath, check.resolvedRoot) == 0) {
        SetReason(&check, L"SenseVoice-Small 模型已经加载，无需重启。");
        CopyResult(result, &check);
        LOG_INFO("Voice recognition model already loaded from: %s", check.resolvedRoot);
        return true;
    }

#ifdef USE_SHERPA_ONNX
    SherpaOnnxOfflineRecognizer* newRecognizer = CreateRecognizer(&check, &check);
    if (newRecognizer == NULL) {
        CopyResult(result, &check);
        return false;
    }

    if (g_recognizer != NULL) {
        SherpaOnnxDestroyOfflineRecognizer(g_recognizer);
    }
    g_recognizer = newRecognizer;
    CopyString(g_modelPath, sizeof(g_modelPath), check.resolvedRoot);
    g_modelLoaded = true;
    SetReason(&check, L"SenseVoice-Small 模型加载成功，无需重启即可使用。");
    CopyResult(result, &check);
    LOG_INFO("Voice recognition model loaded successfully from: %s", check.resolvedRoot);
    return true;
#else
    SetReason(&check, L"当前程序未链接 sherpa-onnx，无法加载语音模型。请使用正式发布包。");
    CopyResult(result, &check);
    LOG_WARN("sherpa-onnx library not linked, voice recognition unavailable");
    return false;
#endif
}

bool VoiceInit(const char* modelDir) {
    return VoiceReloadModel(modelDir, NULL);
}

void VoiceCleanup(void) {
#ifdef USE_SHERPA_ONNX
    if (g_recognizer != NULL) {
        SherpaOnnxDestroyOfflineRecognizer(g_recognizer);
        g_recognizer = NULL;
    }
#endif
    g_modelLoaded = false;
    g_modelPath[0] = '\0';
    LOG_DEBUG("语音识别资源已清理");
}

bool VoiceIsModelLoaded(void) {
    return g_modelLoaded;
}

char* VoiceRecognize(const float* samples, int numSamples) {
    if (!g_modelLoaded) {
        LOG_WARN("模型未加载，无法识别");
        return NULL;
    }

    if (samples == NULL || numSamples <= 0) {
        LOG_WARN("音频数据无效");
        return NULL;
    }

#ifdef USE_SHERPA_ONNX
    if (g_recognizer == NULL) {
        LOG_ERROR("识别器未初始化");
        return NULL;
    }

    // 创建流
    SherpaOnnxOfflineStream* stream = (SherpaOnnxOfflineStream*)SherpaOnnxCreateOfflineStream(g_recognizer);
    if (stream == NULL) {
        LOG_ERROR("创建识别流失败");
        return NULL;
    }

    // 接受波形数据
    SherpaOnnxAcceptWaveformOffline(stream, 16000, samples, numSamples);

    // 解码
    SherpaOnnxDecodeOfflineStream(g_recognizer, stream);

    // 获取结果
    const SherpaOnnxOfflineRecognizerResult* result = SherpaOnnxGetOfflineStreamResult(stream);
    if (result == NULL) {
        SherpaOnnxDestroyOfflineStream(stream);
        LOG_ERROR("获取识别结果失败");
        return NULL;
    }

    // 复制结果
    char* output = NULL;
    if (result->text != NULL && strlen(result->text) > 0) {
        output = _strdup(result->text);
        LOG_DEBUG("识别结果: %s", output);
    }

    // 清理
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    SherpaOnnxDestroyOfflineStream(stream);

    return output;
#else
    LOG_ERROR("sherpa-onnx 库未链接");
    return NULL;
#endif
}
