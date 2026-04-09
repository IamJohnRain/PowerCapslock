#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

/**
 * 初始化音频录制模块
 * @return true 成功，false 失败
 */
bool AudioInit(void);

/**
 * 清理音频资源
 */
void AudioCleanup(void);

/**
 * 开始录音
 * @return true 成功，false 失败
 */
bool AudioStartRecording(void);

/**
 * 停止录音并获取音频数据
 * @param samples 输出音频样本指针（16kHz, 单声道, float32，需调用者 free() 释放）
 * @param numSamples 输出样本数量
 * @return true 成功，false 失败
 */
bool AudioStopRecording(float** samples, int* numSamples);

/**
 * 检查是否正在录音
 * @return true 正在录音，false 未录音
 */
bool AudioIsRecording(void);

#endif // AUDIO_H
