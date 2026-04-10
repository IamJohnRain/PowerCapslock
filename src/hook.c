#include "hook.h"
#include "keymap.h"
#include "logger.h"
#include "voice.h"
#include "audio.h"
#include "voice_prompt.h"
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
            L"请将模型文件放入 models 目录后重启程序。\n"
            L"下载地址: https://github.com/k2-fsa/sherpa-onnx/releases",
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

// 低级别键盘钩子回调函数
static HookAction ProcessKeyEvent(const KBDLLHOOKSTRUCT* pKb, WPARAM wParam) {
    DWORD vkCode = pKb->vkCode;
    WORD scanCode = pKb->scanCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    bool isInjected = (pKb->flags & LLKHF_INJECTED) != 0;
    bool isAKey = (vkCode == 'A' || scanCode == 0x1E);

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
        const KeyMapping* mapping = KeymapFindByScanCode(scanCode);
        if (mapping == NULL) {
            return HOOK_ACTION_PASS_THROUGH;
        }

        if (isKeyDown) {
            bool shift = false;
            bool ctrl = false;
            bool alt = false;
            GetModifierState(&shift, &ctrl, &alt);
            LOG_DEBUG("Mapping triggered: %s (scanCode=0x%02X -> VK=%d) [Shift=%d Ctrl=%d Alt=%d]",
                      mapping->name, scanCode, mapping->targetVk, shift, ctrl, alt);
            SendKeyInput(mapping->targetVk, true);
            return HOOK_ACTION_INTERCEPT;
        }

        if (isKeyUp) {
            SendKeyInput(mapping->targetVk, false);
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
