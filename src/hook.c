#include "hook.h"
#include "keymap.h"
#include "logger.h"
#include "voice.h"
#include "audio.h"
#include "voice_prompt.h"
#include "action.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOOK_MSG_VOICE_START (WM_APP + 0x41)
#define HOOK_MSG_VOICE_STOP  (WM_APP + 0x42)
#define HOOK_CAPTURE_TIMER_INTERVAL_MS 30
#define HOOK_TEST_MAX_CALLBACK_MS 100

// 全局状态
typedef struct {
    bool enabled;           // 是否启用
    bool capslockHeld;      // CapsLock 是否按下
    HHOOK hHook;            // 钩子句柄
    bool voiceInputActive;  // 语音输入是否正在进行
    bool voiceStartPending;
    bool voiceStopPending;
    DWORD voiceStartTime;
    DWORD ownerThreadId;
    UINT_PTR captureTimerId;
    bool testMode;
    DWORD testRunId;
    DWORD testVoiceStartDelayMs;
    bool testAsyncStartHandled;
    bool testAsyncStopHandled;
    CaptureMode captureMode;
    char capturedKeyName[32];
    WORD capturedScanCode;
} HookState;

static HookState g_hook = {0};

typedef enum {
    HOOK_ACTION_PASS_THROUGH = 0,
    HOOK_ACTION_INTERCEPT = 1
} HookAction;

typedef struct {
    DWORD runId;
    DWORD slowStartDelayMs;
    DWORD maxAllowedHookDurationMs;
    DWORD aDownDurationMs;
    DWORD aUpDurationMs;
    HookAction capslockDownAction;
    HookAction aDownAction;
    HookAction aUpAction;
    HookAction capslockUpAction;
    bool asyncStartHandled;
    bool asyncStopHandled;
    bool passed;
} HookCapsLockATestReport;

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

static void StopCaptureTimer(void) {
    if (g_hook.captureTimerId != 0) {
        KillTimer(NULL, g_hook.captureTimerId);
        g_hook.captureTimerId = 0;
    }
}

static void ResetVoiceSessionState(void) {
    StopCaptureTimer();
    g_hook.voiceInputActive = false;
    g_hook.voiceStartPending = false;
    g_hook.voiceStopPending = false;
    g_hook.voiceStartTime = 0;
}

static bool EnsureThreadMessageQueue(void) {
    MSG msg;
    return PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) != 0 || GetLastError() == 0;
}

static bool QueueHookThreadMessage(UINT message) {
    if (g_hook.ownerThreadId == 0) {
        LOG_ERROR("Failed to queue hook message 0x%X: owner thread missing", message);
        return false;
    }

    EnsureThreadMessageQueue();

    if (!PostThreadMessage(g_hook.ownerThreadId, message, 0, 0)) {
        LOG_ERROR("PostThreadMessage failed for hook message 0x%X, error=%lu", message, GetLastError());
        return false;
    }

    return true;
}

static const char* HookActionToString(HookAction action) {
    return action == HOOK_ACTION_INTERCEPT ? "intercept" : "pass_through";
}

static void PerformVoiceRecognitionAndInput(void) {
    float* samples = NULL;
    int numSamples = 0;

    LOG_INFO("[语音输入] 停止录音，开始识别...");
    if (!AudioStopRecording(&samples, &numSamples)) {
        LOG_ERROR("[语音输入] 停止录音失败");
        return;
    }

    LOG_INFO("[语音输入] 录音样本数: %d, 时长: %.2f 秒", numSamples, (float)numSamples / 16000.0f);

    {
        char* text = VoiceRecognize(samples, numSamples);
        if (text == NULL) {
            LOG_WARN("[语音输入] 识别结果为空");
            free(samples);
            return;
        }

        LOG_INFO("[语音输入] 识别结果: %s", text);

        {
            int textLen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
            if (textLen > 0) {
                wchar_t* wtext = (wchar_t*)malloc(textLen * sizeof(wchar_t));
                if (wtext != NULL) {
                    MultiByteToWideChar(CP_UTF8, 0, text, -1, wtext, textLen);

                    {
                        INPUT* inputs = (INPUT*)malloc((textLen - 1) * 2 * sizeof(INPUT));
                        if (inputs != NULL) {
                            int inputCount = 0;
                            int i = 0;

                            for (i = 0; i < textLen - 1; i++) {
                                inputs[inputCount].type = INPUT_KEYBOARD;
                                inputs[inputCount].ki.wVk = 0;
                                inputs[inputCount].ki.wScan = (WORD)wtext[i];
                                inputs[inputCount].ki.dwFlags = KEYEVENTF_UNICODE;
                                inputs[inputCount].ki.time = 0;
                                inputs[inputCount].ki.dwExtraInfo = 0;
                                inputCount++;

                                inputs[inputCount].type = INPUT_KEYBOARD;
                                inputs[inputCount].ki.wVk = 0;
                                inputs[inputCount].ki.wScan = (WORD)wtext[i];
                                inputs[inputCount].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                                inputs[inputCount].ki.time = 0;
                                inputs[inputCount].ki.dwExtraInfo = 0;
                                inputCount++;
                            }

                            LOG_INFO("[语音输入] SendInput 发送 %u 个输入事件", SendInput(inputCount, inputs, sizeof(INPUT)));
                            free(inputs);
                        }
                    }

                    free(wtext);
                }
            }
        }

        free(text);
    }

    free(samples);
}

static void HandleVoiceStartAsync(void) {
    g_hook.voiceStartPending = false;

    if (!g_hook.voiceInputActive) {
        return;
    }

    if (g_hook.testMode) {
        LOG_INFO("[CapsLock+A测试] run_id=%lu async_start begin delay_ms=%lu",
                 g_hook.testRunId, g_hook.testVoiceStartDelayMs);
        if (g_hook.testVoiceStartDelayMs > 0) {
            Sleep(g_hook.testVoiceStartDelayMs);
        }
        g_hook.testAsyncStartHandled = true;
        LOG_INFO("[CapsLock+A测试] run_id=%lu async_start handled", g_hook.testRunId);
        return;
    }

    LOG_INFO("[语音输入] 开始异步启动语音输入");

    if (!VoiceIsModelLoaded()) {
        LOG_WARN("[语音输入] 模型未加载，无法进行语音识别");
        MessageBoxW(NULL,
            L"语音识别模型未加载。\n\n"
            L"请在配置页面选择 SenseVoice 模型目录。\n"
            L"保存后程序会立即检测并加载模型。",
            L"PowerCapslock - 语音输入",
            MB_OK | MB_ICONINFORMATION);
        ResetVoiceSessionState();
        return;
    }

    LOG_INFO("[语音输入] 开始录音...");
    if (!AudioStartRecording()) {
        LOG_ERROR("[语音输入] 录音启动失败");
        ResetVoiceSessionState();
        return;
    }

    VoicePromptShow();
    g_hook.captureTimerId = SetTimer(NULL, 0, HOOK_CAPTURE_TIMER_INTERVAL_MS, NULL);
    if (g_hook.captureTimerId == 0) {
        LOG_WARN("[语音输入] 录音采集定时器启动失败，错误=%lu", GetLastError());
    } else {
        LOG_DEBUG("[语音输入] 录音采集定时器已启动");
    }

    LOG_INFO("[语音输入] 提示窗口已显示，正在录音...");
}

static void HandleVoiceStopAsync(void) {
    DWORD duration = g_hook.voiceStartTime == 0 ? 0 : (GetTickCount() - g_hook.voiceStartTime);
    g_hook.voiceStopPending = false;

    if (g_hook.testMode) {
        g_hook.testAsyncStopHandled = true;
        LOG_INFO("[CapsLock+A测试] run_id=%lu async_stop handled duration_ms=%lu",
                 g_hook.testRunId, duration);
        ResetVoiceSessionState();
        return;
    }

    LOG_INFO("[语音输入] CapsLock+A 释放，录音时长: %lu 毫秒", duration);

    StopCaptureTimer();
    VoicePromptHide();
    LOG_INFO("[语音输入] 提示窗口已隐藏");

    if (AudioIsRecording()) {
        PerformVoiceRecognitionAndInput();
    } else {
        LOG_WARN("[语音输入] 收到停止请求时未在录音");
    }

    LOG_INFO("[语音输入] 语音输入结束");
    ResetVoiceSessionState();
}

// 扫描码到按键名称的转换（简化版，实际使用时可根据需要扩展）
static const char* ScanCodeToKeyName(WORD scanCode) {
    // 常见按键的扫描码映射
    switch (scanCode) {
        case 0x01: return "ESC";
        case 0x02: return "1";
        case 0x03: return "2";
        case 0x04: return "3";
        case 0x05: return "4";
        case 0x06: return "5";
        case 0x07: return "6";
        case 0x08: return "7";
        case 0x09: return "8";
        case 0x0A: return "9";
        case 0x0B: return "0";
        case 0x0C: return "MINUS";
        case 0x0D: return "EQUAL";
        case 0x0E: return "BACKSPACE";
        case 0x0F: return "TAB";
        case 0x10: return "Q";
        case 0x11: return "W";
        case 0x12: return "E";
        case 0x13: return "R";
        case 0x14: return "T";
        case 0x15: return "Y";
        case 0x16: return "U";
        case 0x17: return "I";
        case 0x18: return "O";
        case 0x19: return "P";
        case 0x1A: return "LBRACKET";
        case 0x1B: return "RBRACKET";
        case 0x1C: return "ENTER";
        case 0x1D: return "LCTRL";
        case 0x1E: return "A";
        case 0x1F: return "S";
        case 0x20: return "D";
        case 0x21: return "F";
        case 0x22: return "G";
        case 0x23: return "H";
        case 0x24: return "J";
        case 0x25: return "K";
        case 0x26: return "L";
        case 0x27: return "SEMICOLON";
        case 0x28: return "APOSTROPHE";
        case 0x29: return "GRAVE";
        case 0x2A: return "LSHIFT";
        case 0x2B: return "BACKSLASH";
        case 0x2C: return "Z";
        case 0x2D: return "X";
        case 0x2E: return "C";
        case 0x2F: return "V";
        case 0x30: return "B";
        case 0x31: return "N";
        case 0x32: return "M";
        case 0x33: return "COMMA";
        case 0x34: return "PERIOD";
        case 0x35: return "SLASH";
        case 0x36: return "RSHIFT";
        case 0x37: return "KP_MULTIPLY";
        case 0x38: return "LALT";
        case 0x39: return "SPACE";
        case 0x3A: return "CAPSLOCK";
        case 0x3B: return "F1";
        case 0x3C: return "F2";
        case 0x3D: return "F3";
        case 0x3E: return "F4";
        case 0x3F: return "F5";
        case 0x40: return "F6";
        case 0x41: return "F7";
        case 0x42: return "F8";
        case 0x43: return "F9";
        case 0x44: return "F10";
        case 0x57: return "F11";
        case 0x58: return "F12";
        case 0x47: return "HOME";
        case 0x48: return "UP";
        case 0x49: return "PAGEUP";
        case 0x4B: return "LEFT";
        case 0x4D: return "RIGHT";
        case 0x4F: return "END";
        case 0x50: return "DOWN";
        case 0x51: return "PAGEDOWN";
        case 0x52: return "INSERT";
        case 0x53: return "DELETE";
        case 0x46: return "SCROLLLOCK";
        case 0x45: return "NUMLOCK";
        case 0xE0: return "LWIN";
        case 0xE1: return "RWIN";
        default: return NULL;
    }
}

// 捕获模式函数实现
void HookSetCaptureMode(CaptureMode mode) {
    g_hook.captureMode = mode;
    g_hook.capturedKeyName[0] = '\0';
    g_hook.capturedScanCode = 0;
    LOG_DEBUG("Capture mode set to: %d", mode);
}

bool HookGetCapturedKey(char* keyName, int keyNameSize, WORD* scanCode) {
    if (g_hook.capturedKeyName[0] == '\0') {
        return false;
    }
    if (keyName != NULL && keyNameSize > 0) {
        strncpy(keyName, g_hook.capturedKeyName, keyNameSize - 1);
        keyName[keyNameSize - 1] = '\0';
    }
    if (scanCode != NULL) {
        *scanCode = g_hook.capturedScanCode;
    }
    return true;
}

void HookClearCapturedKey(void) {
    g_hook.capturedKeyName[0] = '\0';
    g_hook.capturedScanCode = 0;
}

bool HookIsCaptureMode(void) {
    return g_hook.captureMode != CAPTURE_MODE_NONE;
}

// 低级别键盘钩子回调函数
static HookAction ProcessKeyEvent(const KBDLLHOOKSTRUCT* pKb, WPARAM wParam) {
    DWORD vkCode = pKb->vkCode;
    WORD scanCode = pKb->scanCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    bool isInjected = (pKb->flags & LLKHF_INJECTED) != 0;
    bool isAKey = (vkCode == 'A' || scanCode == 0x1E);

    // 捕获模式优先处理
    if (g_hook.captureMode != CAPTURE_MODE_NONE && isKeyDown) {
        // 忽略 CapsLock 本身
        if (vkCode != VK_CAPITAL) {
            // 获取按键名称
            const char* keyName = ScanCodeToKeyName(scanCode);
            if (keyName != NULL) {
                strncpy(g_hook.capturedKeyName, keyName, sizeof(g_hook.capturedKeyName) - 1);
                g_hook.capturedKeyName[sizeof(g_hook.capturedKeyName) - 1] = '\0';
                g_hook.capturedScanCode = scanCode;
                LOG_DEBUG("Captured key: %s (scanCode=0x%02X)", keyName, scanCode);
            }
            return HOOK_ACTION_INTERCEPT; // 拦截按键
        }
    }

    if (vkCode == VK_CAPITAL) {
        if (isInjected) {
            return HOOK_ACTION_PASS_THROUGH;
        }

        if (isKeyDown) {
            if (!g_hook.capslockHeld) {
                LOG_DEBUG("CapsLock pressed (scanCode=0x%02X)", scanCode);
            }
            g_hook.capslockHeld = true;
            return HOOK_ACTION_INTERCEPT;
        }

        if (isKeyUp) {
            g_hook.capslockHeld = false;
            LOG_DEBUG("CapsLock released");
            return HOOK_ACTION_INTERCEPT;
        }

        return HOOK_ACTION_INTERCEPT;
    }

    if (isAKey && (g_hook.capslockHeld || g_hook.voiceInputActive || g_hook.voiceStartPending)) {
        if (isKeyDown && !g_hook.voiceInputActive && !g_hook.voiceStartPending) {
            g_hook.voiceInputActive = true;
            g_hook.voiceStartPending = true;
            g_hook.voiceStopPending = false;
            g_hook.voiceStartTime = GetTickCount();

            LOG_INFO("[语音输入] CapsLock+A 按下，已拦截按键并异步启动语音输入");

            if (!QueueHookThreadMessage(HOOK_MSG_VOICE_START)) {
                LOG_ERROR("[语音输入] 语音启动消息投递失败");
                ResetVoiceSessionState();
            }

            return HOOK_ACTION_INTERCEPT;
        }

        if (isKeyUp && g_hook.voiceInputActive && !g_hook.voiceStopPending) {
            g_hook.voiceStopPending = true;
            LOG_INFO("[语音输入] CapsLock+A 释放，已拦截按键并异步结束语音输入");

            if (!QueueHookThreadMessage(HOOK_MSG_VOICE_STOP)) {
                LOG_ERROR("[语音输入] 语音停止消息投递失败");
                ResetVoiceSessionState();
            }

            return HOOK_ACTION_INTERCEPT;
        }

        return HOOK_ACTION_INTERCEPT;
    }

    if (!g_hook.capslockHeld) {
        return HOOK_ACTION_PASS_THROUGH;
    }

    {
        const Action* action = ActionFindByScanCode(scanCode);
        if (action == NULL) {
            return HOOK_ACTION_PASS_THROUGH;
        }

        if (isKeyDown) {
            bool shift = false;
            bool ctrl = false;
            bool alt = false;
            GetModifierState(&shift, &ctrl, &alt);
            LOG_DEBUG("Action triggered: %s (scanCode=0x%02X -> type=%d) [Shift=%d Ctrl=%d Alt=%d]",
                      action->trigger, scanCode, action->type, shift, ctrl, alt);

            if (action->type == ACTION_TYPE_KEY_MAPPING) {
                // 按键映射：发送目标键
                UINT targetVk = ConfigKeyNameToVkCode(action->param);
                if (targetVk != 0) {
                    if (!g_hook.testMode) {
                        SendKeyInput(targetVk, true);
                    }
                }
            } else {
                // 其他动作类型（内置功能、命令）
                ActionExecute(action);
            }
            return HOOK_ACTION_INTERCEPT;
        }

        if (isKeyUp) {
            if (action->type == ACTION_TYPE_KEY_MAPPING) {
                UINT targetVk = ConfigKeyNameToVkCode(action->param);
                if (targetVk != 0) {
                    if (!g_hook.testMode) {
                        SendKeyInput(targetVk, false);
                    }
                }
            }
            // 其他动作类型在keyup时不需要额外处理
            return HOOK_ACTION_INTERCEPT;
        }
    }

    return HOOK_ACTION_PASS_THROUGH;
}

static LRESULT CALLBACK FastLowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0 || !g_hook.enabled) {
        return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
    }

    if (ProcessKeyEvent((const KBDLLHOOKSTRUCT*)lParam, wParam) == HOOK_ACTION_INTERCEPT) {
        return 1;
    }

    return CallNextHookEx(g_hook.hHook, nCode, wParam, lParam);
}

HHOOK HookInstall(void) {
    if (g_hook.hHook != NULL) {
        LOG_WARN("Hook already installed");
        return g_hook.hHook;
    }

    EnsureThreadMessageQueue();
    g_hook.ownerThreadId = GetCurrentThreadId();

    g_hook.hHook = SetWindowsHookEx(WH_KEYBOARD_LL, FastLowLevelKeyboardProc,
                                     GetModuleHandle(NULL), 0);
    if (g_hook.hHook == NULL) {
        LOG_ERROR("Failed to install keyboard hook, error=%d", GetLastError());
        return NULL;
    }

    g_hook.enabled = true;
    g_hook.voiceInputActive = false;
    g_hook.voiceStartPending = false;
    g_hook.voiceStopPending = false;
    g_hook.voiceStartTime = 0;
    g_hook.captureTimerId = 0;

    // 安装钩子后，确保CapsLock LED熄灭（小写状态）
    SetCapsLockLED(false);

    LOG_INFO("Keyboard hook installed successfully");
    return g_hook.hHook;
}

void HookUninstall(HHOOK hHook) {
    if (hHook == NULL) {
        return;
    }

    StopCaptureTimer();

    if (UnhookWindowsHookEx(hHook)) {
        LOG_INFO("Keyboard hook uninstalled successfully");
    } else {
        LOG_ERROR("Failed to uninstall keyboard hook, error=%d", GetLastError());
    }

    if (hHook == g_hook.hHook) {
        g_hook.hHook = NULL;
        g_hook.enabled = false;
        g_hook.capslockHeld = false;
        g_hook.voiceInputActive = false;
        g_hook.voiceStartPending = false;
        g_hook.voiceStopPending = false;
        g_hook.voiceStartTime = 0;
        g_hook.ownerThreadId = 0;
        g_hook.captureTimerId = 0;
    }
}

void HookSetEnabled(bool enabled) {
    g_hook.enabled = enabled;
    if (!enabled) {
        ResetVoiceSessionState();
        g_hook.capslockHeld = false;
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

bool HookHandleMessage(const MSG* msg) {
    if (msg == NULL || msg->hwnd != NULL) {
        return false;
    }

    if (msg->message == HOOK_MSG_VOICE_START) {
        HandleVoiceStartAsync();
        return true;
    }

    if (msg->message == HOOK_MSG_VOICE_STOP) {
        HandleVoiceStopAsync();
        return true;
    }

    if (msg->message == WM_TIMER && g_hook.captureTimerId != 0 && msg->wParam == g_hook.captureTimerId) {
        if (!g_hook.testMode && g_hook.voiceInputActive && AudioIsRecording()) {
            AudioCaptureData();
        }
        return true;
    }

    return false;
}

static void PumpHookMessagesUntilIdle(void) {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (!HookHandleMessage(&msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

static bool WriteCapsLockATestReport(const char* outputPath, const HookCapsLockATestReport* report) {
    FILE* fp;

    if (outputPath == NULL || *outputPath == '\0') {
        return true;
    }

    fp = fopen(outputPath, "w");
    if (fp == NULL) {
        LOG_ERROR("[CapsLock+A测试] Failed to write report: %s", outputPath);
        return false;
    }

    fprintf(fp,
        "{\n"
        "  \"run_id\": %lu,\n"
        "  \"slow_start_delay_ms\": %lu,\n"
        "  \"max_allowed_hook_duration_ms\": %lu,\n"
        "  \"capslock_down_action\": \"%s\",\n"
        "  \"a_down_action\": \"%s\",\n"
        "  \"a_up_action\": \"%s\",\n"
        "  \"capslock_up_action\": \"%s\",\n"
        "  \"a_down_duration_ms\": %lu,\n"
        "  \"a_up_duration_ms\": %lu,\n"
        "  \"async_start_handled\": %s,\n"
        "  \"async_stop_handled\": %s,\n"
        "  \"passed\": %s\n"
        "}\n",
        report->runId,
        report->slowStartDelayMs,
        report->maxAllowedHookDurationMs,
        HookActionToString(report->capslockDownAction),
        HookActionToString(report->aDownAction),
        HookActionToString(report->aUpAction),
        HookActionToString(report->capslockUpAction),
        report->aDownDurationMs,
        report->aUpDurationMs,
        report->asyncStartHandled ? "true" : "false",
        report->asyncStopHandled ? "true" : "false",
        report->passed ? "true" : "false");

    fclose(fp);
    return true;
}

bool HookRunCapsLockATest(DWORD slowStartDelayMs, const char* outputPath) {
    HookCapsLockATestReport report = {0};
    KBDLLHOOKSTRUCT event = {0};
    DWORD startTick = 0;

    memset(&g_hook, 0, sizeof(g_hook));
    EnsureThreadMessageQueue();

    g_hook.enabled = true;
    g_hook.ownerThreadId = GetCurrentThreadId();
    g_hook.testMode = true;
    g_hook.testRunId = GetTickCount();
    g_hook.testVoiceStartDelayMs = slowStartDelayMs;

    report.runId = g_hook.testRunId;
    report.slowStartDelayMs = slowStartDelayMs;
    report.maxAllowedHookDurationMs = HOOK_TEST_MAX_CALLBACK_MS;

    LOG_INFO("[CapsLock+A测试] BEGIN run_id=%lu slow_start_delay_ms=%lu",
             report.runId, report.slowStartDelayMs);

    event.vkCode = VK_CAPITAL;
    event.scanCode = 0x3A;
    report.capslockDownAction = ProcessKeyEvent(&event, WM_KEYDOWN);
    LOG_INFO("[CapsLock+A测试] run_id=%lu simulate CapsLock down -> %s",
             report.runId, HookActionToString(report.capslockDownAction));

    event.vkCode = 'A';
    event.scanCode = 0x1E;
    startTick = GetTickCount();
    report.aDownAction = ProcessKeyEvent(&event, WM_KEYDOWN);
    report.aDownDurationMs = GetTickCount() - startTick;
    LOG_INFO("[CapsLock+A测试] run_id=%lu simulate A down -> %s duration_ms=%lu",
             report.runId, HookActionToString(report.aDownAction), report.aDownDurationMs);

    PumpHookMessagesUntilIdle();

    startTick = GetTickCount();
    report.aUpAction = ProcessKeyEvent(&event, WM_KEYUP);
    report.aUpDurationMs = GetTickCount() - startTick;
    LOG_INFO("[CapsLock+A测试] run_id=%lu simulate A up -> %s duration_ms=%lu",
             report.runId, HookActionToString(report.aUpAction), report.aUpDurationMs);

    PumpHookMessagesUntilIdle();

    event.vkCode = VK_CAPITAL;
    event.scanCode = 0x3A;
    report.capslockUpAction = ProcessKeyEvent(&event, WM_KEYUP);
    LOG_INFO("[CapsLock+A测试] run_id=%lu simulate CapsLock up -> %s",
             report.runId, HookActionToString(report.capslockUpAction));

    PumpHookMessagesUntilIdle();

    report.asyncStartHandled = g_hook.testAsyncStartHandled;
    report.asyncStopHandled = g_hook.testAsyncStopHandled;
    report.passed =
        report.capslockDownAction == HOOK_ACTION_INTERCEPT &&
        report.aDownAction == HOOK_ACTION_INTERCEPT &&
        report.aUpAction == HOOK_ACTION_INTERCEPT &&
        report.capslockUpAction == HOOK_ACTION_INTERCEPT &&
        report.asyncStartHandled &&
        report.asyncStopHandled &&
        report.aDownDurationMs <= report.maxAllowedHookDurationMs &&
        report.aUpDurationMs <= report.maxAllowedHookDurationMs;

    LOG_INFO("[CapsLock+A测试] END run_id=%lu passed=%d a_down_duration_ms=%lu async_start=%d async_stop=%d",
             report.runId,
             report.passed ? 1 : 0,
             report.aDownDurationMs,
             report.asyncStartHandled ? 1 : 0,
             report.asyncStopHandled ? 1 : 0);

    WriteCapsLockATestReport(outputPath, &report);
    memset(&g_hook, 0, sizeof(g_hook));

    return report.passed;
}

typedef struct {
    DWORD runId;
    WORD scanCode;
    UINT expectedVk;
    bool expectMissing;
    bool mappingFound;
    UINT actualVk;
    HookAction capslockDownAction;
    HookAction keyDownAction;
    HookAction keyUpAction;
    HookAction capslockUpAction;
    bool passed;
} HookKeyMappingTestReport;

static void WriteKeyMappingTestReport(const char* outputPath, const HookKeyMappingTestReport* report) {
    FILE* file = NULL;

    if (outputPath == NULL || outputPath[0] == '\0' || report == NULL) {
        return;
    }

    file = fopen(outputPath, "w");
    if (file == NULL) {
        LOG_ERROR("[keymap-test] Failed to open report file: %s", outputPath);
        return;
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"run_id\": %lu,\n", report->runId);
    fprintf(file, "  \"scan_code\": %u,\n", (unsigned int)report->scanCode);
    fprintf(file, "  \"expected_vk\": %u,\n", report->expectedVk);
    fprintf(file, "  \"expect_missing\": %s,\n", report->expectMissing ? "true" : "false");
    fprintf(file, "  \"mapping_found\": %s,\n", report->mappingFound ? "true" : "false");
    fprintf(file, "  \"actual_vk\": %u,\n", report->actualVk);
    fprintf(file, "  \"capslock_down_action\": \"%s\",\n", HookActionToString(report->capslockDownAction));
    fprintf(file, "  \"key_down_action\": \"%s\",\n", HookActionToString(report->keyDownAction));
    fprintf(file, "  \"key_up_action\": \"%s\",\n", HookActionToString(report->keyUpAction));
    fprintf(file, "  \"capslock_up_action\": \"%s\",\n", HookActionToString(report->capslockUpAction));
    fprintf(file, "  \"passed\": %s\n", report->passed ? "true" : "false");
    fprintf(file, "}\n");
    fclose(file);
}

bool HookRunKeyMappingTest(WORD scanCode, UINT expectedVk, const char* outputPath) {
    HookKeyMappingTestReport report = {0};
    KBDLLHOOKSTRUCT event = {0};
    const KeyMapping* mapping = NULL;

    memset(&g_hook, 0, sizeof(g_hook));
    EnsureThreadMessageQueue();

    g_hook.enabled = true;
    g_hook.ownerThreadId = GetCurrentThreadId();
    g_hook.testMode = true;
    g_hook.testRunId = GetTickCount();

    report.runId = g_hook.testRunId;
    report.scanCode = scanCode;
    report.expectedVk = expectedVk;
    report.expectMissing = (expectedVk == 0);

    mapping = KeymapFindByScanCode(scanCode);
    report.mappingFound = (mapping != NULL);
    report.actualVk = mapping != NULL ? mapping->targetVk : 0;

    LOG_INFO("[keymap-test] BEGIN run_id=%lu scan_code=0x%02X expected_vk=%u",
             report.runId, scanCode, expectedVk);

    event.vkCode = VK_CAPITAL;
    event.scanCode = 0x3A;
    event.flags = 0;
    report.capslockDownAction = ProcessKeyEvent(&event, WM_KEYDOWN);

    event.vkCode = 0;
    event.scanCode = scanCode;
    event.flags = 0;
    report.keyDownAction = ProcessKeyEvent(&event, WM_KEYDOWN);
    report.keyUpAction = ProcessKeyEvent(&event, WM_KEYUP);

    event.vkCode = VK_CAPITAL;
    event.scanCode = 0x3A;
    report.capslockUpAction = ProcessKeyEvent(&event, WM_KEYUP);

    if (report.expectMissing) {
        report.passed =
            !report.mappingFound &&
            report.capslockDownAction == HOOK_ACTION_INTERCEPT &&
            report.keyDownAction == HOOK_ACTION_PASS_THROUGH &&
            report.keyUpAction == HOOK_ACTION_PASS_THROUGH &&
            report.capslockUpAction == HOOK_ACTION_INTERCEPT;
    } else {
        report.passed =
            report.mappingFound &&
            report.actualVk == report.expectedVk &&
            report.capslockDownAction == HOOK_ACTION_INTERCEPT &&
            report.keyDownAction == HOOK_ACTION_INTERCEPT &&
            report.keyUpAction == HOOK_ACTION_INTERCEPT &&
            report.capslockUpAction == HOOK_ACTION_INTERCEPT;
    }

    LOG_INFO("[keymap-test] END run_id=%lu passed=%d mapping_found=%d actual_vk=%u",
             report.runId,
             report.passed ? 1 : 0,
             report.mappingFound ? 1 : 0,
             report.actualVk);

    WriteKeyMappingTestReport(outputPath, &report);
    memset(&g_hook, 0, sizeof(g_hook));

    return report.passed;
}
