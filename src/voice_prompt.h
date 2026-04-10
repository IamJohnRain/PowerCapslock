#ifndef VOICE_PROMPT_H
#define VOICE_PROMPT_H

#include <stdbool.h>

/**
 * 初始化语音提示窗口模块
 * @return true 成功，false 失败
 */
bool VoicePromptInit(void);

/**
 * 清理语音提示窗口资源
 */
void VoicePromptCleanup(void);

/**
 * 显示语音输入提示窗口
 */
void VoicePromptShow(void);

/**
 * 隐藏语音输入提示窗口
 */
void VoicePromptHide(void);

/**
 * 检查提示窗口是否可见
 * @return true 可见，false 不可见
 */
bool VoicePromptIsVisible(void);

#endif // VOICE_PROMPT_H
