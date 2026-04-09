#include "voice.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

bool VoiceInit(const char* modelDir) {
    if (g_modelLoaded) {
        return true;  // 已初始化
    }

    if (modelDir == NULL) {
        LOG_ERROR("模型目录路径为空");
        return false;
    }

    // 构建模型文件路径
    char modelPath[MAX_PATH];

    // SenseVoice 模型路径
    snprintf(modelPath, sizeof(modelPath),
             "%s\\SenseVoice-Small\\sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17\\model.onnx",
             modelDir);

    // 检查模型文件是否存在
    DWORD modelAttr = GetFileAttributesA(modelPath);

    if (modelAttr == INVALID_FILE_ATTRIBUTES) {
        LOG_WARN("语音模型文件不存在: %s", modelDir);
        LOG_INFO("请下载模型并放入 models 目录");
        LOG_INFO("下载地址: https://github.com/HaujetZhao/CapsWriter-Offline/releases/tag/models");
        return false;
    }

    strncpy(g_modelPath, modelDir, sizeof(g_modelPath) - 1);

#ifdef USE_SHERPA_ONNX
    // 创建识别器配置
    SherpaOnnxOfflineRecognizerConfig config;
    memset(&config, 0, sizeof(config));

    // SenseVoice 模型配置
    config.model_config.sense_voice.model = modelPath;
    config.model_config.sense_voice.language = "zh";
    config.model_config.sense_voice.use_itn = 1;
    config.model_config.num_threads = 4;
    config.model_config.provider = "cpu";
    config.model_config.debug = 0;

    // 创建识别器
    g_recognizer = (SherpaOnnxOfflineRecognizer*)SherpaOnnxCreateOfflineRecognizer(&config);
    if (g_recognizer == NULL) {
        LOG_ERROR("创建语音识别器失败");
        return false;
    }

    LOG_INFO("语音识别模型加载成功");
    g_modelLoaded = true;
    return true;
#else
    // 未链接 sherpa-onnx 库时的占位实现
    LOG_WARN("sherpa-onnx 库未链接，语音识别功能不可用");
    LOG_INFO("请下载 sherpa-onnx 预编译库并放入 lib/ 目录");
    LOG_INFO("下载地址: https://github.com/k2-fsa/sherpa-onnx/releases");
    return false;
#endif
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
