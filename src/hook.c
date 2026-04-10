#include "hook.h"
#include "keymap.h"
#include "logger.h"
#include "voice.h"
#include "audio.h"
#include "voice_prompt.h"
#include <stdio.h>

// 全局状态
typedef struct {
    bool enabled;           // 是否启用
    bool capslockHeld;      // CapsLock 是否按下
    HHOOK hHook;            // 钩子句柄
    bool voiceInputActive;  // 语音输入是否正在进行
} HookState;

static HookState g_hook = {0};

// 获取当前修饰键物理状态（使用 GetAsyncKeyState 获取实时状态）
static void GetModifierState(bool* shift, bool* ctrl, bool* alt) {
    *shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    *ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    *alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
}

// 检查是否是扩展键（导航键、数字小键盘等）
static bool IsExtendedKey(UINT vk) {
    switch (vk) {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:  // PageUp
        case VK_NEXT:   // PageDown
        case VK_INSERT:
        case VK_DELETE:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
        case VK_SNAPSHOT:
        case VK_CANCEL:
            return true;
        default:
            return false;
    }
}

// 发送按键输入（保留修饰键状态）
// 由于修饰键（Shift/Ctrl/Alt）物理按下时系统会自动识别，
// 我们只需要发送目标键即可，系统会自动组合成 Shift+目标键 等
static void SendKeyInput(UINT vk, bool keyDown) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;

    // 扩展键需要设置 KEYEVENTF_EXTENDEDKEY 标志
    if (IsExtendedKey(vk)) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    if (sent != 1) {
        LOG_ERROR("SendInput failed for VK=%d, keyDown=%d", vk, keyDown);
    }
}

// 设置CapsLock LED状态
static void SetCapsLockLED(bool turnOn) {
    // 模拟按键来控制LED
    INPUT inputs[2] = {0};

    // 按下CapsLock
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CAPITAL;
    inputs[0].ki.dwFlags = 0;

    // 释放CapsLock
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_CAPITAL;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    // 获取当前CapsLock状态
    bool currentState = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;

    // 如果当前状态与目标状态不同，则切换
    if (currentState != turnOn) {
        SendInput(2, inputs, sizeof(INPUT));
        LOG_DEBUG("CapsLock LED toggled to: %s", turnOn ? "ON" : "OFF");
    }
}

// 低级别键盘钩子回调函数
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 检查是否需要处理
    if (nCode < 0 || !g_hook.enabled) {
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 获取键盘信息
    KBDLLHOOKSTRUCT* pKb = (KBDLLHOOKSTRUCT*)lParam;
    DWORD vkCode = pKb->vkCode;
    WORD scanCode = pKb->scanCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    // 检测是否是模拟按键（由 SendInput 生成）
    bool isInjected = (pKb->flags & LLKHF_INJECTED) != 0;

    // 检查 CapsLock (VK_CAPITAL = 20)
    if (vkCode == VK_CAPITAL) {
        // 放行模拟按键（用于自动关闭 CapsLock）
        if (isInjected) {
            return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
        }

        if (isKeyDown && !g_hook.capslockHeld) {
            g_hook.capslockHeld = true;
            LOG_DEBUG("CapsLock pressed (scanCode=0x%02X)", scanCode);
            return 1;  // 拦截
        }
        else if (isKeyUp && g_hook.capslockHeld) {
            g_hook.capslockHeld = false;
            LOG_DEBUG("CapsLock released");
            return 1;  // 拦截
        }
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 如果 CapsLock 没按下且语音输入未激活，正常传递
    if (!g_hook.capslockHeld && !g_hook.voiceInputActive) {
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 检测 CapsLock+A 语音输入
    // A 键扫描码是 0x1E
    if (scanCode == 0x1E) {
        static DWORD voiceStartTime = 0;

        if (isKeyDown && !g_hook.voiceInputActive) {
            g_hook.voiceInputActive = true;
            voiceStartTime = GetTickCount();
            LOG_INFO("[语音输入] CapsLock+A 按下，开始语音输入");

            // 检查模型是否加载
            if (!VoiceIsModelLoaded()) {
                LOG_WARN("[语音输入] 模型未加载，无法进行语音识别");
                MessageBoxW(NULL,
                    L"语音识别模型未加载。\n\n"
                    L"请将模型文件放入 models 目录后重启程序。\n"
                    L"下载地址: https://github.com/k2-fsa/sherpa-onnx/releases",
                    L"PowerCapslock - 语音输入",
                    MB_OK | MB_ICONINFORMATION);
                g_hook.voiceInputActive = false;
                return 1;
            }

            // 开始录音
            LOG_INFO("[语音输入] 开始录音...");
            if (AudioStartRecording()) {
                // 显示语音输入提示窗口
                VoicePromptShow();
                LOG_INFO("[语音输入] 提示窗口已显示，正在录音...");
            } else {
                LOG_ERROR("[语音输入] 录音启动失败");
                g_hook.voiceInputActive = false;
            }
        }
        else if (isKeyUp && g_hook.voiceInputActive) {
            g_hook.voiceInputActive = false;
            DWORD voiceEndTime = GetTickCount();
            DWORD duration = voiceEndTime - voiceStartTime;
            LOG_INFO("[语音输入] CapsLock+A 释放，录音时长: %lu 毫秒", duration);

            // 隐藏语音输入提示窗口
            VoicePromptHide();
            LOG_INFO("[语音输入] 提示窗口已隐藏");

            // 停止录音并识别
            float* samples = NULL;
            int numSamples = 0;
            LOG_INFO("[语音输入] 停止录音，开始识别...");
            if (AudioStopRecording(&samples, &numSamples)) {
                LOG_INFO("[语音输入] 录音样本数: %d, 时长: %.2f 秒", numSamples, (float)numSamples / 16000.0f);
                char* text = VoiceRecognize(samples, numSamples);
                if (text != NULL) {
                    LOG_INFO("[语音输入] 识别结果: %s", text);

                    // 将 UTF-8 转换为宽字符（UTF-16）
                    int textLen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
                    if (textLen > 0) {
                        wchar_t* wtext = (wchar_t*)malloc(textLen * sizeof(wchar_t));
                        if (wtext != NULL) {
                            MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, textLen);
                            LOG_INFO("[语音输入] 宽字符长度: %d", textLen - 1);

                            // 模拟键盘输入识别结果（使用宽字符）
                            INPUT* inputs = NULL;
                            int inputCount = 0;
                            int charCount = textLen - 1;  // 不包括结尾的 null

                            // 为每个字符创建输入事件
                            inputs = (INPUT*)malloc(charCount * 2 * sizeof(INPUT));
                            if (inputs != NULL) {
                                for (int i = 0; i < charCount; i++) {
                                    // 按下
                                    inputs[inputCount].type = INPUT_KEYBOARD;
                                    inputs[inputCount].ki.wVk = 0;
                                    inputs[inputCount].ki.wScan = (WORD)wtext[i];
                                    inputs[inputCount].ki.dwFlags = KEYEVENTF_UNICODE;
                                    inputs[inputCount].ki.time = 0;
                                    inputs[inputCount].ki.dwExtraInfo = 0;
                                    inputCount++;

                                    // 释放
                                    inputs[inputCount].type = INPUT_KEYBOARD;
                                    inputs[inputCount].ki.wVk = 0;
                                    inputs[inputCount].ki.wScan = (WORD)wtext[i];
                                    inputs[inputCount].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                                    inputs[inputCount].ki.time = 0;
                                    inputs[inputCount].ki.dwExtraInfo = 0;
                                    inputCount++;
                                }

                                UINT sent = SendInput(inputCount, inputs, sizeof(INPUT));
                                LOG_INFO("[语音输入] SendInput 发送 %u 个输入事件", sent);
                                free(inputs);
                            }
                            free(wtext);
                        }
                    }
                    free(text);
                } else {
                    LOG_WARN("[语音输入] 识别结果为空");
                }
                if (samples != NULL) {
                    free(samples);
                }
            } else {
                LOG_ERROR("[语音输入] 停止录音失败");
            }
            LOG_INFO("[语音输入] 语音输入结束");
        } else if (g_hook.voiceInputActive && g_hook.capslockHeld) {
            // 按键持续按下时，持续采集音频数据
            AudioCaptureData();
        }
        return 1;  // 拦截
    }

    // 查找映射
    const KeyMapping* mapping = KeymapFindByScanCode(scanCode);
    if (mapping == NULL) {
        // 没有映射，正常传递
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    // 找到映射，发送目标键（保留修饰键状态）
    if (isKeyDown) {
        bool shift = false, ctrl = false, alt = false;
        GetModifierState(&shift, &ctrl, &alt);
        LOG_DEBUG("Mapping triggered: %s (scanCode=0x%02X -> VK=%d) [Shift=%d Ctrl=%d Alt=%d]",
                  mapping->name, scanCode, mapping->targetVk, shift, ctrl, alt);
        SendKeyInput(mapping->targetVk, true);
        return 1;  // 拦截原按键
    }
    else if (isKeyUp) {
        SendKeyInput(mapping->targetVk, false);
        return 1;  // 拦截原按键
    }

    return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
}

HHOOK HookInstall(void) {
    if (g_hook.hHook != NULL) {
        LOG_WARN("Hook already installed");
        return g_hook.hHook;
    }

    g_hook.hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                     GetModuleHandle(NULL), 0);
    if (g_hook.hHook == NULL) {
        LOG_ERROR("Failed to install keyboard hook, error=%d", GetLastError());
        return NULL;
    }

    g_hook.enabled = true;

    // 安装钩子后，确保CapsLock LED熄灭（小写状态）
    SetCapsLockLED(false);

    LOG_INFO("Keyboard hook installed successfully");
    return g_hook.hHook;
}

void HookUninstall(HHOOK hHook) {
    if (hHook == NULL) {
        return;
    }

    if (UnhookWindowsHookEx(hHook)) {
        LOG_INFO("Keyboard hook uninstalled successfully");
    } else {
        LOG_ERROR("Failed to uninstall keyboard hook, error=%d", GetLastError());
    }

    if (hHook == g_hook.hHook) {
        g_hook.hHook = NULL;
        g_hook.enabled = false;
        g_hook.capslockHeld = false;
    }
}

void HookSetEnabled(bool enabled) {
    g_hook.enabled = enabled;
    if (!enabled) {
        g_hook.capslockHeld = false;
        g_hook.voiceInputActive = false;
    } else {
        // 启用时，确保CapsLock LED熄灭（小写状态）
        SetCapsLockLED(false);
    }
    LOG_DEBUG("Hook enabled: %s", enabled ? "true" : "false");
}

bool HookIsEnabled(void) {
    return g_hook.enabled;
}

bool HookIsCapsLockHeld(void) {
    return g_hook.capslockHeld;
}
