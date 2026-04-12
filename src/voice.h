#ifndef VOICE_H
#define VOICE_H

#include <stdbool.h>
#include <windows.h>

typedef struct {
    char resolvedRoot[MAX_PATH];
    char modelPath[MAX_PATH];
    char tokensPath[MAX_PATH];
    char modelName[64];
    wchar_t reason[512];
} VoiceModelCheckResult;

/**
 * 初始化语音识别模块
 * @param modelDir 模型目录路径
 * @return true 成功，false 失败（模型未找到）
 */
bool VoiceInit(const char* modelDir);

bool VoiceCheckModelDirectory(const char* modelDir, VoiceModelCheckResult* result);
bool VoiceReloadModel(const char* modelDir, VoiceModelCheckResult* result);

/**
 * 清理语音识别资源
 */
void VoiceCleanup(void);

/**
 * 检查模型是否已加载
 * @return true 已加载，false 未加载
 */
bool VoiceIsModelLoaded(void);

/**
 * 识别音频数据
 * @param samples 音频样本（16kHz, 单声道, float32）
 * @param numSamples 样本数量
 * @return 识别结果字符串（需调用者 free() 释放），失败返回 NULL
 */
char* VoiceRecognize(const float* samples, int numSamples);

#endif // VOICE_H
