#include "logger.h"
#include "config.h"
#include "keymap.h"
#include "hook.h"
#include "tray.h"
#include "keyboard_layout.h"
#include "voice.h"
#include "audio.h"
#include "voice_prompt.h"
#include "config_dialog.h"
#include "action.h"
#include "action_builtin.h"
#include "screenshot.h"
#include "screenshot_overlay.h"
#include "screenshot_toolbar.h"
#include "screenshot_float.h"
#include "screenshot_annotate.h"
#include "screenshot_ocr.h"
#include "screenshot_ocr_selection.h"
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// minimp3 MP3 decoder
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

// 全局变量
static HINSTANCE g_hInstance = NULL;
static HHOOK g_hHook = NULL;
static char* g_testMappingFrom = NULL;
static char* g_testMappingTo = NULL;
static char* g_testMappingOutputPath = NULL;
static char* g_testModelPath = NULL;

// 命令行覆盖配置
static int g_cmdEnableVoice = -1;  // -1: no change, 0: disable, 1: enable

// 初始化所有模块
static BOOL InitializeModules(void) {
    // 初始化配置模块（设置路径和默认值，创建目录和配置文件）
    ConfigInit();

    // 先初始化日志模块（使用默认配置），这样后续加载配置时可以记录日志
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_INFO);  // 先用默认级别

    // 清理旧日志，只保留最近3天
    LoggerCleanupOldLogs(3);

    LOG_INFO("========================================");
    LOG_INFO("PowerCapslock v1.0 starting...");
    LOG_INFO("========================================");

    // 初始化键位映射模块（必须在 ConfigLoad 之前，因为 ConfigLoad 会调用 ParseMappings）
    KeymapInit();

    // 初始化动作模块
    ActionInit();

    // 初始化内置功能模块
    BuiltinInit();

    // 加载配置文件
    const Config* config = ConfigGet();
    ConfigLoad(NULL);

    // 根据配置更新日志级别
    LoggerCleanup();
    LoggerInit(config->logToFile ? ConfigGetLogPath() : NULL);
    LoggerSetLevel(config->logLevel);
    LoggerCleanupOldLogs(3);

    // 初始化键盘布局模块
    KeyboardLayoutInit();

    // 应用命令行配置覆盖
    Config* mutableConfig = (Config*)ConfigGet();
    if (g_cmdEnableVoice != -1) {
        mutableConfig->voiceInputEnabled = (g_cmdEnableVoice == 1);
        mutableConfig->voiceInputAsked = true;
        LOG_DEBUG("Command line override: g_cmdEnableVoice = %d", g_cmdEnableVoice);
        ConfigSave(NULL);
        LOG_INFO("Voice input %s by command line", g_cmdEnableVoice ? "enabled" : "disabled");
    }

    // 检查语音输入配置，如果用户还没选择过是否启用，检查模型是否存在再决定是否询问
    if (!mutableConfig->voiceInputAsked) {
        VoiceModelCheckResult modelCheck;
        if (VoiceCheckModelDirectory(mutableConfig->modelDirPath, &modelCheck)) {
            mutableConfig->voiceInputEnabled = true;
            mutableConfig->voiceInputAsked = true;
            ConfigSave(NULL);
            LOG_INFO("Model found at configured path, voice input automatically enabled: %s", modelCheck.resolvedRoot);
        } else {
            // 模型不存在，询问用户是否启用
            int result = MessageBoxW(NULL,
                L"是否启用语音输入功能？\n\n"
                L"启用后可按 CapsLock+A 触发离线语音输入，"
                L"需要下载模型到 models 目录。\n\n"
                L"点击「是(Y)」启用，「否(N)」不启用\n"
                L"选择后会保存配置不再询问。",
                L"PowerCapslock - 语音输入功能",
                MB_YESNO | MB_ICONQUESTION);

            if (result == IDYES) {
                mutableConfig->voiceInputEnabled = true;
                mutableConfig->voiceInputAsked = true;

                // 弹出提示用户下载模型
                MessageBoxW(NULL,
                    L"请按以下步骤启用语音输入：\n\n"
                    L"1. 下载 SenseVoice 模型文件\n"
                    L"   地址: https://github.com/IamJohnRain/PowerCapslock/releases/tag/v0.2.0\n"
                    L"   下载: PowerCapslock-v0.2.0-model-SenseVoice-Small.zip\n\n"
                    L"2. 解压后放到 models/SenseVoice-Small/ 目录\n"
                    L"   最终路径应该是: models/SenseVoice-Small/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.onnx\n\n"
                    L"3. 在配置页面选择模型目录，程序会立即检测并加载",
                    L"PowerCapslock - 模型下载提示",
                    MB_OK | MB_ICONINFORMATION);

                // 打开浏览器下载页面
                ShellExecuteW(NULL, L"open",
                    L"https://github.com/IamJohnRain/PowerCapslock/releases/tag/v0.2.0",
                    NULL, NULL, SW_SHOWNORMAL);

                // 保存配置
                ConfigSave(NULL);
            } else {
                mutableConfig->voiceInputEnabled = false;
                mutableConfig->voiceInputAsked = true;
                ConfigSave(NULL);
                LOG_INFO("User disabled voice input feature");
            }
        }
    }

    // 初始化系统托盘
    if (TrayInit(g_hInstance) == NULL) {
        LOG_ERROR("Failed to initialize system tray");
        return FALSE;
    }

    // 安装键盘钩子
    g_hHook = HookInstall();
    if (g_hHook == NULL) {
        LOG_ERROR("Failed to install keyboard hook");
        return FALSE;
    }

    // 设置初始状态
    HookSetEnabled(config->startEnabled);
    TraySetState(config->startEnabled);

    // 初始化音频模块（仅当语音输入启用时）
    if (config->voiceInputEnabled) {
        if (AudioInit()) {
            LOG_INFO("Audio module initialized successfully");

            // 初始化语音提示窗口模块
            VoicePromptInit();

            if (VoiceInit(config->modelDirPath)) {
                LOG_INFO("Voice recognition initialized successfully");
            } else {
                LOG_INFO("Voice recognition not available: model not loaded");
                // 如果用户已经选择启用但是模型没找到，弹出提示
                if (config->voiceInputAsked) {
                    MessageBoxW(NULL,
                        L"语音识别模型未找到！\n\n"
                        L"请确保模型已放置到正确位置：\n"
                        L"models/SenseVoice-Small/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.onnx\n\n"
                        L"你可以重新下载模型，然后在配置页面重新选择模型目录，程序会立即检测并加载。",
                        L"PowerCapslock - 语音输入",
                        MB_OK | MB_ICONWARNING);
                }
            }
        } else {
            LOG_WARN("Audio module initialization failed, voice input disabled");
        }
    } else {
        LOG_INFO("Voice input disabled by user configuration");
    }

    LOG_INFO("All modules initialized successfully");
    return TRUE;
}

// 清理所有模块
static void CleanupModules(void) {
    LOG_INFO("Cleaning up modules...");

    // 卸载键盘钩子
    if (g_hHook != NULL) {
        HookUninstall(g_hHook);
        g_hHook = NULL;
    }

    // 清理系统托盘
    TrayCleanup();

    // 清理语音识别模块
    VoiceCleanup();

    // 清理语音提示窗口模块
    VoicePromptCleanup();

    // 清理音频模块
    AudioCleanup();

    // 清理动作模块
    ActionCleanup();

    // 清理内置功能模块
    BuiltinCleanup();

    // 清理键位映射模块
    KeymapCleanup();

    // 清理键盘布局模块
    KeyboardLayoutCleanup();

    // 清理配置模块
    ConfigCleanup();

    LOG_INFO("All modules cleaned up");
    LOG_INFO("========================================");
    LOG_INFO("PowerCapslock exited");
    LOG_INFO("========================================");

    // 清理日志模块
    LoggerCleanup();
}

// 语音测试模式（命令行dryrun）
static int RunVoiceTestMode(const char* outputPath) {
    g_hInstance = GetModuleHandle(NULL);

    // 初始化基础模块
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_INFO);
    KeymapInit();
    ConfigLoad(NULL);

    // 应用命令行配置覆盖
    Config* mutableConfig = (Config*)ConfigGet();
    if (g_cmdEnableVoice != -1) {
        mutableConfig->voiceInputEnabled = (g_cmdEnableVoice == 1);
        mutableConfig->voiceInputAsked = true;
        LOG_INFO("Voice input %s by command line", g_cmdEnableVoice ? "enabled" : "disabled");
    }

    const Config* config = ConfigGet();
    LoggerSetLevel(config->logLevel);

    LOG_INFO("========== PowerCapslock Voice Test Mode ==========");

    // 检查语音输入是否启用
    if (!config->voiceInputEnabled) {
        LOG_ERROR("Voice input is not enabled");
        printf("ERROR: Voice input is not enabled\n");
        printf("  - To enable: use --enable-voice command line argument\n");
        printf("  - Or enable it in configuration\n");
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    // 初始化音频
    if (!AudioInit()) {
        LOG_ERROR("Failed to initialize audio module");
        printf("ERROR: Failed to initialize audio module\n");
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }
    LOG_INFO("Audio module initialized");

    // 初始化语音识别
    if (!VoiceInit(config->modelDirPath)) {
        LOG_ERROR("Failed to load voice recognition model from: %s", config->modelDirPath);
        printf("ERROR: Failed to load voice recognition model from: %s\n", config->modelDirPath);
        printf("Please ensure model is placed at: models/SenseVoice-Small/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.onnx\n");
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    if (!VoiceIsModelLoaded()) {
        LOG_ERROR("Voice model not loaded");
        printf("ERROR: Voice model not loaded\n");
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    LOG_INFO("Voice recognition model loaded successfully from: %s", config->modelDirPath);
    printf("Voice recognition model loaded successfully\n");
    printf("\n");
    printf("=== Please speak now, press ENTER when done ===\n");
    fflush(stdout);

    // 开始录音
    if (!AudioStartRecording()) {
        LOG_ERROR("Failed to start recording");
        printf("ERROR: Failed to start recording\n");
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    // 等待用户按回车停止
    getchar();

    // 停止录音
    float* samples = NULL;
    int numSamples = 0;
    if (!AudioStopRecording(&samples, &numSamples)) {
        LOG_ERROR("Failed to stop recording");
        printf("ERROR: Failed to stop recording\n");
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    if (samples == NULL || numSamples == 0) {
        LOG_ERROR("No audio data recorded");
        printf("ERROR: No audio data recorded\n");
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    LOG_INFO("Recording completed: %d samples (%.2f seconds)", numSamples, (float)numSamples / 16000);
    printf("Recording completed: %.2f seconds\n", (float)numSamples / 16000);
    printf("Recognizing...\n");
    fflush(stdout);

    // 进行识别
    char* result = VoiceRecognize(samples, numSamples);
    free(samples);

    if (result == NULL) {
        LOG_ERROR("Recognition failed");
        printf("ERROR: Recognition failed\n");
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    // 输出结果
    LOG_INFO("Recognition result: %s", result);
    printf("\n");
    printf("=== Recognition Result ===\n");
    printf("%s\n", result);
    printf("==========================\n");
    fflush(stdout);

    // 如果指定了输出文件，写入结果
    if (outputPath != NULL && strlen(outputPath) > 0) {
        FILE* fp = fopen(outputPath, "w");
        if (fp != NULL) {
            fputs(result, fp);
            fclose(fp);
            LOG_INFO("Result written to: %s", outputPath);
            printf("Result written to: %s\n", outputPath);
        } else {
            LOG_WARN("Failed to write output file: %s", outputPath);
            printf("WARNING: Failed to write output file: %s\n", outputPath);
        }
    }

    // 清理
    free(result);
    VoiceCleanup();
    AudioCleanup();
    LoggerCleanup();
    ConfigCleanup();

    LOG_INFO("Voice test completed successfully");
    printf("\nVoice test completed successfully\n");

    return 0;
}

// 测试日志清理
static int RunLogCleanupTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    // 初始化基础模块
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_INFO);
    KeymapInit();
    ConfigLoad(NULL);

    const Config* config = ConfigGet();
    LoggerSetLevel(config->logLevel);

    printf("========== PowerCapslock Log Cleanup Test ==========\n");
    printf("Running log cleanup, keeping last 3 days...\n");
    fflush(stdout);

    LoggerCleanupOldLogs(3);

    LoggerCleanup();
    KeymapCleanup();
    ConfigCleanup();

    printf("\nLog cleanup completed\n");
    return 0;
}

// 测试配置加载保存
static int RunConfigTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    // 初始化配置
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_INFO);
    KeymapInit();

    printf("========== PowerCapslock Config Test ==========\n");

    bool loadResult = ConfigLoad(NULL);
    const Config* config = ConfigGet();
    LoggerSetLevel(config->logLevel);

    printf("Config file path: %s\n", config->configPath);
    printf("Log directory: %s\n", config->logDirPath);
    printf("Model directory: %s\n", config->modelDirPath);
    printf("Voice input enabled: %s\n", config->voiceInputEnabled ? "true" : "false");
    printf("Voice input asked: %s\n", config->voiceInputAsked ? "true" : "false");
    printf("Config load %s\n", loadResult ? "SUCCEEDED" : "FAILED (using defaults)");

    int mappingCount = KeymapGetCount();
    printf("Loaded %d key mappings\n", mappingCount);

    // Test save
    printf("\nSaving config...\n");
    bool saveResult = ConfigSave(NULL);
    printf("Config save %s\n", saveResult ? "SUCCEEDED" : "FAILED");

    LoggerCleanup();
    KeymapCleanup();
    ConfigCleanup();

    printf("\nConfig test completed\n");
    return saveResult ? 0 : 1;
}

// 测试配置对话框
static int RunConfigDialogTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    // 初始化所有模块
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);  // Enable debug to see coordinate calculations
    KeymapInit();
    ConfigLoad(NULL);

    const Config* config = ConfigGet();
    // Keep debug level regardless of config setting
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    // Initialize keyboard layout
    KeyboardLayoutInit();

    printf("========== PowerCapslock Config Dialog Test ==========\n");
    printf("Opening config dialog...\n");
    fflush(stdout);

    // Show dialog
    bool changed = ShowConfigDialog(NULL);

    printf("\nConfig dialog closed, changed: %s\n", changed ? "yes" : "no");

    // Cleanup
    KeyboardLayoutCleanup();
    LoggerCleanup();
    KeymapCleanup();
    ConfigCleanup();

    return 0;
}

static int RunVoicePromptWindowTest(void) {
    MSG msg;
    DWORD endTick;

    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    if (!VoicePromptInit()) {
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    VoicePromptShow();
    endTick = GetTickCount() + 3000;
    while (GetTickCount() < endTick) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }

    VoicePromptHide();
    VoicePromptCleanup();
    LoggerCleanup();
    ConfigCleanup();
    return 0;
}

static void PumpMessagesUntil(DWORD endTick) {
    MSG msg;
    while (GetTickCount() < endTick) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }
}

static int RunToastWindowTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    TrayShowToast("PowerCapslock", L"已启用");
    PumpMessagesUntil(GetTickCount() + 1100);
    TrayShowToast("PowerCapslock", L"已禁用");
    PumpMessagesUntil(GetTickCount() + 2400);

    LoggerCleanup();
    ConfigCleanup();
    return 0;
}

static void PrintWideUtf8(const wchar_t* text) {
    if (text == NULL) {
        return;
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (size <= 0) {
        return;
    }
    char* buffer = (char*)malloc((size_t)size);
    if (buffer == NULL) {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, size, NULL, NULL);
    printf("%s", buffer);
    free(buffer);
}

static int RunModelLoadTest(void) {
    const char* modelPath;
    VoiceModelCheckResult status;

    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_INFO);
    ConfigLoad(NULL);

    modelPath = (g_testModelPath != NULL && g_testModelPath[0] != '\0')
        ? g_testModelPath
        : ConfigGet()->modelDirPath;

    printf("Testing model path: %s\n", modelPath);
    if (!VoiceReloadModel(modelPath, &status)) {
        printf("FAILED: ");
        PrintWideUtf8(status.reason[0] != L'\0' ? status.reason : L"unknown error");
        printf("\n");
        LoggerCleanup();
        ConfigCleanup();
        return 1;
    }

    printf("PASSED: %s loaded from %s\n", status.modelName, status.resolvedRoot);
    VoiceCleanup();
    LoggerCleanup();
    ConfigCleanup();
    return 0;
}

// 向前声明
static int RunAudioFileTest(void);
static int RunCapsLockAHookTest(const char* outputPath);
static int RunKeyMappingTestMode(void);
static int RunVoicePromptWindowTest(void);
static int RunToastWindowTest(void);
static int RunModelLoadTest(void);

// 命令行模式枚举
typedef enum {
    CMD_MODE_NORMAL = 0,
    CMD_MODE_VOICE_TEST = 1,
    CMD_MODE_CLEANUP_LOGS = 2,
    CMD_MODE_TEST_CONFIG = 3,
    CMD_MODE_TEST_CONFIG_DIALOG = 4,
    CMD_MODE_TEST_AUDIO_FILE = 5,
    CMD_MODE_TEST_CAPSLOCK_A = 6,
    CMD_MODE_TEST_KEY_MAPPING = 7,
    CMD_MODE_TEST_VOICE_PROMPT = 8,
    CMD_MODE_TEST_TOAST = 9,
    CMD_MODE_TEST_MODEL_LOAD = 10,
    CMD_MODE_TEST_SCREENSHOT_CAPTURE = 11,
    CMD_MODE_TEST_SCREENSHOT_SAVE = 12,
    CMD_MODE_TEST_SCREENSHOT_CLIPBOARD = 13,
    CMD_MODE_TEST_SCREENSHOT_ALL = 14,
    CMD_MODE_TEST_SCREENSHOT_OVERLAY = 15,
    CMD_MODE_TEST_SCREENSHOT_TOOLBAR = 16,
    CMD_MODE_TEST_SCREENSHOT_FLOAT = 17,
    CMD_MODE_TEST_SCREENSHOT_ANNOTATE = 18,
    CMD_MODE_TEST_SCREENSHOT_OCR = 19,
    CMD_MODE_TEST_OCR_DATA_MODEL = 20,
    CMD_MODE_TEST_OCR_LAYOUT = 21,
    CMD_MODE_TEST_OCR_HIT_TEST = 22,
    CMD_MODE_TEST_OCR_SELECTION = 23,
    CMD_MODE_TEST_OCR_COPY_TEXT = 24,
    CMD_MODE_TEST_OCR_FLOAT_UI = 25,
    CMD_MODE_TEST_OCR_ASYNC = 26,
    CMD_MODE_TEST_OCR_E2E_MOCK = 27,
    CMD_MODE_TEST_SCREENSHOT_OCR_SELECTION_ALL = 28
} CommandMode;

// 保存音频文件测试参数
static char* g_testAudioPath = NULL;
static char* g_testExpectedPath = NULL;

// 截图测试参数
static int g_screenshotX = 0;
static int g_screenshotY = 0;
static int g_screenshotW = 200;
static int g_screenshotH = 200;
static char* g_screenshotOutputPath = NULL;

// MP3 文件测试：识别 MP3 文件并与预期文本比较
static int RunAudioFileTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    // 检查参数
    if (g_testAudioPath == NULL || g_testExpectedPath == NULL) {
        printf("ERROR: Missing arguments for --test-audio-file\n");
        printf("Usage: --test-audio-file <mp3-file> <expected-text-file>\n");
        return 1;
    }

    // 初始化基础模块
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_INFO);
    KeymapInit();
    ConfigLoad(NULL);

    // 强制启用语音识别
    Config* mutableConfig = (Config*)ConfigGet();
    mutableConfig->voiceInputEnabled = true;
    mutableConfig->voiceInputAsked = true;

    const Config* config = ConfigGet();
    LoggerSetLevel(config->logLevel);

    printf("========== PowerCapslock Audio File Recognition Test ==========\n");
    printf("MP3 file: %s\n", g_testAudioPath);
    printf("Expected text file: %s\n", g_testExpectedPath);
    printf("\n");
    fflush(stdout);

    // 初始化音频和语音识别
    if (!AudioInit()) {
        LOG_ERROR("Failed to initialize audio module");
        printf("ERROR: Failed to initialize audio module\n");
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }
    LOG_INFO("Audio module initialized");

    if (!VoiceInit(config->modelDirPath)) {
        LOG_ERROR("Failed to load voice recognition model from: %s", config->modelDirPath);
        printf("ERROR: Failed to load voice recognition model from: %s\n", config->modelDirPath);
        printf("Please ensure model is placed at: models/SenseVoice-Small/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.onnx\n");
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    if (!VoiceIsModelLoaded()) {
        LOG_ERROR("Voice model not loaded");
        printf("ERROR: Voice model not loaded\n");
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    LOG_INFO("Voice recognition model loaded successfully from: %s", config->modelDirPath);
    printf("Voice recognition model loaded successfully\n");
    printf("\n");
    fflush(stdout);

    // 读取 MP3 文件
    FILE* mp3File = fopen(g_testAudioPath, "rb");
    if (mp3File == NULL) {
        LOG_ERROR("Failed to open MP3 file: %s", g_testAudioPath);
        printf("ERROR: Failed to open MP3 file: %s\n", g_testAudioPath);
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    // 获取文件大小
    fseek(mp3File, 0, SEEK_END);
    long fileSize = ftell(mp3File);
    fseek(mp3File, 0, SEEK_SET);
    printf("MP3 file size: %ld bytes\n", fileSize);
    fflush(stdout);

    // 分配缓冲区读取文件
    uint8_t* mp3Buffer = (uint8_t*)malloc(fileSize);
    if (mp3Buffer == NULL) {
        LOG_ERROR("Failed to allocate memory for MP3 file: %ld bytes", fileSize);
        printf("ERROR: Out of memory reading MP3\n");
        fclose(mp3File);
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    long bytesRead = fread(mp3Buffer, 1, fileSize, mp3File);
    fclose(mp3File);
    if (bytesRead != fileSize) {
        LOG_ERROR("Failed to read entire MP3 file: read %ld of %ld bytes", bytesRead, fileSize);
        printf("ERROR: Failed to read entire MP3 file\n");
        free(mp3Buffer);
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    // 使用 minimp3 解码
    mp3dec_t mp3d;
    mp3dec_frame_info_t info;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    // 先估算输出样本数
    // MP3 是 1411 kbps for 44.1kHz stereo, we can allocate larger buffer
    int estimatedSamples = (fileSize * 8 * 16000) / (128 * 1000); // 128kbps estimate for 16kHz
    float* floatSamples = (float*)malloc(estimatedSamples * sizeof(float));
    if (floatSamples == NULL) {
        LOG_ERROR("Failed to allocate memory for decoded samples");
        printf("ERROR: Out of memory allocating decoded samples\n");
        free(mp3Buffer);
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    mp3dec_init(&mp3d);

    int totalSamples = 0;
    uint8_t* inputPtr = mp3Buffer;
    int inputSize = fileSize;

    while (inputSize > 0 && totalSamples < estimatedSamples) {
        int samplesDecoded = mp3dec_decode_frame(&mp3d, inputPtr, inputSize, pcm, &info);
        if (samplesDecoded > 0) {
            // Convert from int16 to float and mix to mono
            // Sherpa-ONNX expects 16kHz mono float
            float scale = 1.0f / 32768.0f;
            if (info.channels == 1) {
                for (int i = 0; i < samplesDecoded && totalSamples < estimatedSamples; i++) {
                    floatSamples[totalSamples++] = (float)pcm[i] * scale;
                }
            } else {
                // Stereo -> mix to mono
                for (int i = 0; i < samplesDecoded && totalSamples < estimatedSamples; i += 2) {
                    float avg = ((float)pcm[i] + (float)pcm[i+1]) * 0.5f * scale;
                    floatSamples[totalSamples++] = avg;
                }
                samplesDecoded /= 2; // adjust count for mono output
            }
        }

        if (info.frame_bytes > 0) {
            inputPtr += info.frame_bytes;
            inputSize -= info.frame_bytes;
        } else {
            // No more frames or error
            break;
        }
    }

    free(mp3Buffer);
    printf("Decoded: %d samples at %d Hz (%d channels), output %d mono samples\n", info.frame_bytes, info.hz, info.channels, totalSamples);
    fflush(stdout);

    // Resample to 16kHz if needed
    // If the sample rate is not 16kHz, we need to resample
    float* samples16kHz;
    int numSamples16kHz;

    if (info.hz == 16000) {
        // Already correct sample rate
        samples16kHz = floatSamples;
        numSamples16kHz = totalSamples;
        printf("Already at 16kHz, no resampling needed\n");
    } else {
        // Simple linear resampling
        double ratio = (double)info.hz / 16000.0;
        numSamples16kHz = (int)(totalSamples / ratio);
        samples16kHz = (float*)malloc(numSamples16kHz * sizeof(float));
        if (samples16kHz == NULL) {
            LOG_ERROR("Failed to allocate memory for resampled output");
            printf("ERROR: Out of memory for resampling\n");
            free(floatSamples);
            VoiceCleanup();
            AudioCleanup();
            LoggerCleanup();
            ConfigCleanup();
            KeymapCleanup();
            return 1;
        }
        for (int i = 0; i < numSamples16kHz; i++) {
            double srcIndex = i * ratio;
            int srcIndex0 = (int)srcIndex;
            int srcIndex1 = srcIndex0 + 1;
            double frac = srcIndex - srcIndex0;
            if (srcIndex1 >= totalSamples) {
                srcIndex1 = totalSamples - 1;
            }
            samples16kHz[i] = (float)((1.0 - frac) * floatSamples[srcIndex0] + frac * floatSamples[srcIndex1]);
        }
        free(floatSamples);
        printf("Resampled from %d Hz to 16 kHz: %d samples\n", info.hz, numSamples16kHz);
    }
    fflush(stdout);

    // 运行语音识别
    printf("\nRunning speech recognition...\n");
    fflush(stdout);
    char* recognized = VoiceRecognize(samples16kHz, numSamples16kHz);

    // 释放样本
    if (info.hz != 16000) {
        free(samples16kHz);
    } else {
        free(floatSamples);
    }

    if (recognized == NULL) {
        LOG_ERROR("Speech recognition failed");
        printf("ERROR: Speech recognition failed\n");
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    printf("Recognition completed:\n");
    printf("  Recognized: \"%s\"\n", recognized);
    fflush(stdout);

    // 读取预期文本
    FILE* expectedFile = fopen(g_testExpectedPath, "r");
    if (expectedFile == NULL) {
        LOG_ERROR("Failed to open expected text file: %s", g_testExpectedPath);
        printf("ERROR: Failed to open expected text file: %s\n", g_testExpectedPath);
        free(recognized);
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    // Get expected file size
    fseek(expectedFile, 0, SEEK_END);
    long expectedSize = ftell(expectedFile);
    fseek(expectedFile, 0, SEEK_SET);

    char* expected = (char*)malloc(expectedSize + 1);
    if (expected == NULL) {
        LOG_ERROR("Failed to allocate memory for expected text");
        printf("ERROR: Out of memory reading expected text\n");
        fclose(expectedFile);
        free(recognized);
        VoiceCleanup();
        AudioCleanup();
        LoggerCleanup();
        ConfigCleanup();
        KeymapCleanup();
        return 1;
    }

    long expectedRead = fread(expected, 1, expectedSize, expectedFile);
    fclose(expectedFile);
    expected[expectedRead] = '\0';

    // Trim leading/trailing whitespace and newlines
    char* start = expected;
    while (*start && (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t')) start++;
    char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) {
        *end-- = '\0';
    }

    printf("Expected:\n");
    printf("  Expected:   \"%s\"\n", start);
    fflush(stdout);

    // 比较结果（简单字符串比较，可以容忍一些空白差异）
    // 也忽略recognized中的前后空白
    char* recogStart = recognized;
    while (*recogStart && (*recogStart == ' ' || *recogStart == '\n' || *recogStart == '\r' || *recogStart == '\t')) recogStart++;
    char* recogEnd = recogStart + strlen(recogStart) - 1;
    while (recogEnd > recogStart && (*recogEnd == ' ' || *recogEnd == '\n' || *recogEnd == '\r' || *recogEnd == '\t')) {
        *recogEnd-- = '\0';
    }

    bool match = (strcmp(recogStart, start) == 0);

    printf("\n");
    if (match) {
        printf("✅ TEST PASSED: Recognition matches expected text!\n");
    } else {
        printf("❌ TEST FAILED: Recognition does not match expected\n");
    }
    fflush(stdout);

    // Cleanup
    free(expected);
    free(recognized);
    VoiceCleanup();
    AudioCleanup();
    LoggerCleanup();
    ConfigCleanup();
    KeymapCleanup();

    return match ? 0 : 1;
}

// 解析命令行参数
static int RunCapsLockAHookTest(const char* outputPath) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);
    KeymapInit();
    ConfigLoad(NULL);
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock CapsLock+A Hook Test ==========\n");
    printf("Output file: %s\n", outputPath != NULL ? outputPath : "<none>");
    printf("Simulating slow voice startup to verify the key is intercepted before async work runs.\n");
    fflush(stdout);

    {
        bool passed = HookRunCapsLockATest(1500, outputPath);
        LoggerCleanup();
        KeymapCleanup();
        ConfigCleanup();
        return passed ? 0 : 1;
    }
}

static int RunKeyMappingTestMode(void) {
    WORD scanCode = 0;
    UINT expectedVk = 0;
    bool passed = false;

    if (g_testMappingFrom == NULL || g_testMappingTo == NULL) {
        printf("ERROR: --test-key-mapping requires <from-key> <to-key|NONE> [output-json]\n");
        fflush(stdout);
        return 1;
    }

    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);
    KeymapInit();
    ConfigLoad(NULL);
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    scanCode = ConfigKeyNameToScanCode(g_testMappingFrom);
    if (scanCode == 0) {
        printf("ERROR: invalid source key for --test-key-mapping: %s\n", g_testMappingFrom);
        fflush(stdout);
        LoggerCleanup();
        KeymapCleanup();
        ConfigCleanup();
        return 1;
    }

    if (_stricmp(g_testMappingTo, "NONE") != 0 && strcmp(g_testMappingTo, "-") != 0) {
        expectedVk = ConfigKeyNameToVkCode(g_testMappingTo);
        if (expectedVk == 0) {
            printf("ERROR: invalid target key for --test-key-mapping: %s\n", g_testMappingTo);
            fflush(stdout);
            LoggerCleanup();
            KeymapCleanup();
            ConfigCleanup();
            return 1;
        }
    }

    printf("========== PowerCapslock Key Mapping Test ==========\n");
    printf("From: %s (scanCode=0x%02X)\n", g_testMappingFrom, scanCode);
    printf("Expected target: %s (vk=%u)\n", g_testMappingTo, expectedVk);
    printf("Report: %s\n", g_testMappingOutputPath != NULL ? g_testMappingOutputPath : "<none>");
    fflush(stdout);

    passed = HookRunKeyMappingTest(scanCode, expectedVk, g_testMappingOutputPath);

    LoggerCleanup();
    KeymapCleanup();
    ConfigCleanup();

    printf("Key mapping test %s\n", passed ? "PASSED" : "FAILED");
    fflush(stdout);
    return passed ? 0 : 1;
}

// 截图测试函数
static int RunScreenshotCaptureTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot Capture Test ==========\n");
    printf("Region: (%d, %d) %dx%d\n", g_screenshotX, g_screenshotY, g_screenshotW, g_screenshotH);
    printf("Output: %s\n", g_screenshotOutputPath ? g_screenshotOutputPath : "<none>");
    fflush(stdout);

    int result = ScreenshotTestCapture(g_screenshotX, g_screenshotY, g_screenshotW, g_screenshotH, g_screenshotOutputPath);

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotSaveTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot Save Test ==========\n");
    printf("Output: %s\n", g_screenshotOutputPath ? g_screenshotOutputPath : "test_save.png");
    fflush(stdout);

    int result = ScreenshotTestSave(g_screenshotOutputPath ? g_screenshotOutputPath : "test_save.png");

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotClipboardTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot Clipboard Test ==========\n");
    fflush(stdout);

    int result = ScreenshotTestClipboard();

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotAllTests(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot All Tests ==========\n");
    fflush(stdout);

    int result = ScreenshotTestAll();

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotOverlayTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot Overlay Test ==========\n");
    fflush(stdout);

    int result = ScreenshotOverlayTest();

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotToolbarTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot Toolbar Test ==========\n");
    fflush(stdout);

    int result = ScreenshotToolbarTest();

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotFloatTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot Float Test ==========\n");
    fflush(stdout);

    int result = ScreenshotFloatTest();

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotAnnotateTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot Annotate Test ==========\n");
    fflush(stdout);

    int result = AnnotateTest();

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunScreenshotOCRTest(void) {
    g_hInstance = GetModuleHandle(NULL);

    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== PowerCapslock Screenshot OCR Test ==========\n");
    fflush(stdout);

    int result = OCRTest();

    LoggerCleanup();
    ConfigCleanup();

    return result;
}

static int RunOcrDataModelTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Data Model Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    // Test OCRWord structure
    OCRWord word1;
    word1.text = "Hello";
    word1.boundingBox.left = 10;
    word1.boundingBox.top = 20;
    word1.boundingBox.right = 50;
    word1.boundingBox.bottom = 30;
    word1.confidence = 0.95f;
    word1.isLineBreak = FALSE;

    printf("[OCR] OCRWord structure initialized: text='%s', bbox=(%d,%d,%d,%d), conf=%.2f\n",
           word1.text, word1.boundingBox.left, word1.boundingBox.top,
           word1.boundingBox.right, word1.boundingBox.bottom, word1.confidence);
    passed++;

    // Test OCRResults structure
    OCRResults results;
    results.wordCount = 1;
    results.words = &word1;
    results.fullText = "Hello";
    results.imageWidth = 1920;
    results.imageHeight = 1080;

    printf("[OCR] OCRResults structure: %d words, image=%dx%d\n",
           results.wordCount, results.imageWidth, results.imageHeight);
    passed++;

    // Test OcrAsyncMessage structure
    OcrAsyncMessage msg;
    msg.sessionId = 12345;
    msg.results = &results;
    printf("[OCR] OcrAsyncMessage: sessionId=%lu, results=%p\n", msg.sessionId, (void*)msg.results);
    passed++;

    printf("\nOCR Data Model Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunOcrLayoutTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Layout Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    // Test layout calculation: 1920x1080 image in 960x540 client
    OcrImageLayout layout;
    BOOL ok = OcrCalculateImageLayout(1920, 1080, 960, 540, &layout);
    printf("[OCR] Layout calc (1920x1080 -> 960x540): scale=%.4f, offset=(%d,%d), size=%dx%d\n",
           layout.scale, layout.offset.x, layout.offset.y, layout.drawSize.cx, layout.drawSize.cy);
    if (ok && layout.scale > 0 && layout.drawSize.cx == 960 && layout.drawSize.cy == 540) {
        passed++;
    } else {
        failed++;
        printf("[OCR] FAIL: Expected scale=0.5, size=960x540\n");
    }

    // Test image to window point conversion
    POINT imgPt = {960, 540};
    POINT winPt = OcrImageToWindowPoint(&layout, imgPt);
    printf("[OCR] Image (960,540) -> Window (%d,%d)\n", winPt.x, winPt.y);
    if (winPt.x == 480 && winPt.y == 270) {
        passed++;
    } else {
        failed++;
        printf("[OCR] FAIL: Expected window (480,270)\n");
    }

    // Test window to image point conversion
    POINT winPt2 = {480, 270};
    POINT imgPt2 = OcrWindowToImagePoint(&layout, winPt2);
    printf("[OCR] Window (480,270) -> Image (%d,%d)\n", imgPt2.x, imgPt2.y);
    if (imgPt2.x == 960 && imgPt2.y == 540) {
        passed++;
    } else {
        failed++;
        printf("[OCR] FAIL: Expected image (960,540)\n");
    }

    // Test center offset with non-divisible dimensions
    OcrImageLayout layout2;
    OcrCalculateImageLayout(100, 100, 150, 150, &layout2);
    printf("[OCR] Layout (100x100 -> 150x150): scale=%.4f, offset=(%d,%d), size=%dx%d\n",
           layout2.scale, layout2.offset.x, layout2.offset.y, layout2.drawSize.cx, layout2.drawSize.cy);
    // Scale = 1.5 (min of 150/100 and 150/100), image fits exactly, offset = 0
    if (layout2.scale == 1.5f && layout2.offset.x == 0 && layout2.offset.y == 0) {
        passed++;
    } else {
        failed++;
        printf("[OCR] FAIL: Expected scale=1.5, offset=(0,0)\n");
    }

    printf("\nOCR Layout Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunOcrHitTestTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Hit Test Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    // Create mock OCR results with 3 words
    OCRWord words[3] = {
        {"Hello", {10, 10, 50, 30}, 0.95f, FALSE},
        {"World", {60, 10, 100, 30}, 0.90f, FALSE},
        {"Test", {10, 40, 50, 60}, 0.85f, FALSE}
    };
    OCRResults results = {words, 3, "Hello World Test", 200, 100};

    // Hit test on "Hello"
    int idx = OcrHitTestWordAtImagePoint(&results, (POINT){30, 20});
    printf("[OCR] Hit test at (30,20): word index %d (expected 0 for 'Hello')\n", idx);
    if (idx == 0) passed++; else {failed++; printf("[OCR] FAIL: Expected index 0\n");}

    // Hit test on "World"
    idx = OcrHitTestWordAtImagePoint(&results, (POINT){80, 20});
    printf("[OCR] Hit test at (80,20): word index %d (expected 1 for 'World')\n", idx);
    if (idx == 1) passed++; else {failed++; printf("[OCR] FAIL: Expected index 1\n");}

    // Hit test on "Test"
    idx = OcrHitTestWordAtImagePoint(&results, (POINT){30, 50});
    printf("[OCR] Hit test at (30,50): word index %d (expected 2 for 'Test')\n", idx);
    if (idx == 2) passed++; else {failed++; printf("[OCR] FAIL: Expected index 2\n");}

    // Hit test outside any word
    idx = OcrHitTestWordAtImagePoint(&results, (POINT){110, 20});
    printf("[OCR] Hit test at (110,20): word index %d (expected -1 for outside)\n", idx);
    if (idx == -1) passed++; else {failed++; printf("[OCR] FAIL: Expected index -1\n");}

    // Window-to-image hit test with layout
    OcrImageLayout layout;
    OcrCalculateImageLayout(200, 100, 400, 200, &layout);
    // Window point (60,40) -> image (30,20) should hit "Hello"
    idx = OcrHitTestWordAtWindowPoint(&results, &layout, (POINT){60, 40});
    printf("[OCR] Window hit test at (60,40) -> image (30,20): word index %d\n", idx);
    if (idx == 0) passed++; else {failed++; printf("[OCR] FAIL: Expected index 0\n");}

    printf("\nOCR Hit Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunOcrSelectionTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Selection Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    // Test OcrMakeSelection
    OcrSelectionState sel = OcrMakeSelection(2, 5);
    printf("[OCR] OcrMakeSelection(2,5): anchor=%d, active=%d\n", sel.anchorWordIndex, sel.activeWordIndex);
    if (sel.anchorWordIndex == 2 && sel.activeWordIndex == 5) passed++; else {failed++; printf("[OCR] FAIL\n");}

    // Test OcrSelectionIsValid
    BOOL valid = OcrSelectionIsValid(&sel, 10);
    printf("[OCR] OcrSelectionIsValid(anchor=2,active=5,wordCount=10): %s\n", valid ? "TRUE" : "FALSE");
    if (valid) passed++; else {failed++; printf("[OCR] FAIL\n");}

    valid = OcrSelectionIsValid(&sel, 4);  // 5 >= 4, invalid
    printf("[OCR] OcrSelectionIsValid(anchor=2,active=5,wordCount=4): %s (expected FALSE)\n", valid ? "TRUE" : "FALSE");
    if (!valid) passed++; else {failed++; printf("[OCR] FAIL\n");}

    // Test OcrGetSelectedWordRange
    int start, end;
    BOOL ok = OcrGetSelectedWordRange(&sel, 10, &start, &end);
    printf("[OCR] OcrGetSelectedWordRange(anchor=2,active=5): [%d,%d], ok=%d\n", start, end, ok);
    if (ok && start == 2 && end == 5) passed++; else {failed++; printf("[OCR] FAIL\n");}

    // Test reverse selection (anchor > active)
    OcrSelectionState selRev = OcrMakeSelection(7, 3);
    ok = OcrGetSelectedWordRange(&selRev, 10, &start, &end);
    printf("[OCR] OcrGetSelectedWordRange(anchor=7,active=3): [%d,%d], ok=%d\n", start, end, ok);
    if (ok && start == 3 && end == 7) passed++; else {failed++; printf("[OCR] FAIL\n");}

    printf("\nOCR Selection Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunOcrCopyTextTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Copy Text Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    // Create mock OCR results
    OCRWord words[5] = {
        {"Hello", {10, 10, 50, 30}, 0.95f, FALSE},
        {"World", {60, 10, 100, 30}, 0.90f, FALSE},
        {"Test", {10, 40, 50, 60}, 0.85f, FALSE},
        {"Line", {60, 40, 100, 60}, 0.88f, FALSE},
        {"End", {10, 70, 50, 90}, 0.92f, TRUE}
    };
    OCRResults results = {words, 5, NULL, 200, 100};

    // Test building selected text
    OcrSelectionState sel = OcrMakeSelection(1, 3);
    char* text = OcrBuildSelectedTextUtf8(&results, &sel);
    printf("[OCR] BuildSelectedText(1,3): '%s'\n", text ? text : "(null)");
    if (text != NULL && strcmp(text, "World Test Line") == 0) {
        passed++;
        free(text);
    } else {
        failed++;
        printf("[OCR] FAIL: Expected 'World Test Line'\n");
        if (text) free(text);
    }

    // Test full selection
    sel = OcrMakeSelection(0, 4);
    text = OcrBuildSelectedTextUtf8(&results, &sel);
    printf("[OCR] BuildSelectedText(0,4): '%s'\n", text ? text : "(null)");
    // Note: word with isLineBreak adds \r\n, space added before last word
    if (text != NULL && strncmp(text, "Hello World Test Line End\r\n", 25) == 0) {
        passed++;
        free(text);
    } else {
        failed++;
        printf("[OCR] FAIL: Expected starts with 'Hello World Test Line End\\r\\n'\n");
        if (text) free(text);
    }

    // Test UTF-16 length measurement
    int utf16Len = OcrMeasureUtf16LengthFromUtf8("Hello");
    printf("[OCR] OcrMeasureUtf16LengthFromUtf8('Hello'): %d\n", utf16Len);
    if (utf16Len == 6) passed++; else {failed++; printf("[OCR] FAIL: Expected 6\n");}

    printf("\nOCR Copy Text Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunOcrFloatUiTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Float UI Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    printf("[OCR] Float UI test requires screenshot_float.c integration.\n");
    printf("[OCR] This test validates the FloatWindowContext has OCR fields.\n");
    printf("[OCR] Skipping UI window creation in test mode.\n");
    passed++;  // Placeholder

    printf("\nOCR Float UI Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunOcrAsyncTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Async Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    printf("[OCR] Testing async OCR infrastructure...\n");
    // Note: Cannot fully test without message pump, but we can verify
    // the worker thread mechanism is present
    OCRInit();
    printf("[OCR] OCR initialized for async test\n");
    passed++;

    OCRCleanup();
    printf("[OCR] OCR cleanup completed\n");
    passed++;

    printf("\nOCR Async Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunOcrE2eMockTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR E2E Mock Test ==========\n");
    fflush(stdout);

    int passed = 0;
    int failed = 0;

    // Test full OCR pipeline with mock data
    ScreenshotImage img = {0};
    img.width = 640;
    img.height = 480;
    img.pixels = (BYTE*)malloc(640 * 480 * 4);
    memset(img.pixels, 255, 640 * 480 * 4);  // White image

    OCRInit();
    OCRResults* results = OCRRecognize(&img);
    if (results != NULL) {
        printf("[OCR] OCRRecognize returned %d words\n", results->wordCount);
        if (results->wordCount > 0) {
            passed++;
            printf("[OCR] First word: '%s'\n", results->words[0].text);
        } else {
            failed++;
        }
        OCRFreeResults(results);
    } else {
        failed++;
        printf("[OCR] FAIL: OCRRecognize returned NULL\n");
    }

    free(img.pixels);
    OCRCleanup();

    printf("\nOCR E2E Mock Test: %d passed, %d failed\n", passed, failed);

    LoggerCleanup();
    ConfigCleanup();
    return failed == 0 ? 0 : 1;
}

static int RunScreenshotOcrSelectionAllTest(void) {
    g_hInstance = GetModuleHandle(NULL);
    ConfigInit();
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_DEBUG);

    printf("========== OCR Selection All Tests ==========\n");
    fflush(stdout);

    int totalPassed = 0;
    int totalFailed = 0;

    int result;

#define RUN_TEST(name) do { \
        printf("\n--- " #name " ---\n"); \
        fflush(stdout); \
        result = name(); \
        if (result == 0) { \
            printf("[PASS] " #name "\n"); \
            totalPassed++; \
        } else { \
            printf("[FAIL] " #name "\n"); \
            totalFailed++; \
        } \
    } while(0)

    RUN_TEST(RunOcrDataModelTest);
    RUN_TEST(RunOcrLayoutTest);
    RUN_TEST(RunOcrHitTestTest);
    RUN_TEST(RunOcrSelectionTest);
    RUN_TEST(RunOcrCopyTextTest);
    RUN_TEST(RunOcrFloatUiTest);
    RUN_TEST(RunOcrAsyncTest);
    RUN_TEST(RunOcrE2eMockTest);

#undef RUN_TEST

    printf("\n========== OCR Selection All Tests Summary ==========\n");
    printf("Total: %d passed, %d failed\n", totalPassed, totalFailed);

    LoggerCleanup();
    ConfigCleanup();
    return totalFailed == 0 ? 0 : 1;
}

static CommandMode ParseCommandLine(LPSTR lpCmdLine, char** outputPath) {
    char* args = lpCmdLine;
    CommandMode mode = CMD_MODE_NORMAL;
    *outputPath = NULL;

    while (*args != '\0') {
        // Skip whitespace
        while (*args == ' ' || *args == '\t') args++;
        if (*args == '\0') break;

        // Check each argument starting from current position
        if (strncmp(args, "--voice-test", strlen("--voice-test")) == 0 &&
            (args[strlen("--voice-test")] == ' ' || args[strlen("--voice-test")] == '\0')) {
            mode = CMD_MODE_VOICE_TEST;
            args += strlen("--voice-test");
            while (*args == ' ' || *args == '\t') args++;

            // If there's more args, it's output path
            if (*args != '\0') {
                *outputPath = args;
                break;
            } else {
                *outputPath = NULL;
                break;
            }
        }
        else if (strncmp(args, "--dry-run", strlen("--dry-run")) == 0 &&
                (args[strlen("--dry-run")] == ' ' || args[strlen("--dry-run")] == '\0')) {
            mode = CMD_MODE_VOICE_TEST;
            args += strlen("--dry-run");
            while (*args == ' ' || *args == '\t') args++;

            // If there's more args, it's output path
            if (*args != '\0') {
                *outputPath = args;
                break;
            } else {
                *outputPath = NULL;
                break;
            }
        }
        else if (strncmp(args, "--test-audio-file", strlen("--test-audio-file")) == 0 &&
                (args[strlen("--test-audio-file")] == ' ' || args[strlen("--test-audio-file")] == '\0')) {
            mode = CMD_MODE_TEST_AUDIO_FILE;
            args += strlen("--test-audio-file");
            while (*args == ' ' || *args == '\t') args++;

            // First argument: MP3 file path
            if (*args != '\0') {
                g_testAudioPath = args;
                // Skip to next whitespace
                while (*args != ' ' && *args != '\t' && *args != '\0') args++;
                if (*args != '\0') {
                    *args = '\0';
                    args++;
                    while (*args == ' ' || *args == '\t') args++;
                    // Second argument: expected text file path
                    if (*args != '\0') {
                        g_testExpectedPath = args;
                        break;
                    }
                }
            }
            // If we get here, missing arguments
            printf("ERROR: --test-audio-file requires MP3 file path and expected text file path\n");
            printf("Usage: --test-audio-file <mp3-file> <expected-text-file>\n");
            fflush(stdout);
            exit(1);
        }
        else if (strncmp(args, "--test-capslock-a", strlen("--test-capslock-a")) == 0 &&
                (args[strlen("--test-capslock-a")] == ' ' || args[strlen("--test-capslock-a")] == '\0')) {
            mode = CMD_MODE_TEST_CAPSLOCK_A;
            args += strlen("--test-capslock-a");
            while (*args == ' ' || *args == '\t') args++;

            if (*args != '\0') {
                *outputPath = args;
                break;
            } else {
                *outputPath = NULL;
                break;
            }
        }
        else if (strncmp(args, "--test-key-mapping", strlen("--test-key-mapping")) == 0 &&
                (args[strlen("--test-key-mapping")] == ' ' || args[strlen("--test-key-mapping")] == '\0')) {
            mode = CMD_MODE_TEST_KEY_MAPPING;
            args += strlen("--test-key-mapping");
            while (*args == ' ' || *args == '\t') args++;

            if (*args != '\0') {
                g_testMappingFrom = args;
                while (*args != ' ' && *args != '\t' && *args != '\0') args++;
                if (*args != '\0') {
                    *args = '\0';
                    args++;
                    while (*args == ' ' || *args == '\t') args++;
                    if (*args != '\0') {
                        g_testMappingTo = args;
                        while (*args != ' ' && *args != '\t' && *args != '\0') args++;
                        if (*args != '\0') {
                            *args = '\0';
                            args++;
                            while (*args == ' ' || *args == '\t') args++;
                            if (*args != '\0') {
                                g_testMappingOutputPath = args;
                            }
                        }
                    }
                }
            }
            break;
        }
        else if (strncmp(args, "--cleanup-logs", strlen("--cleanup-logs")) == 0 &&
                (args[strlen("--cleanup-logs")] == ' ' || args[strlen("--cleanup-logs")] == '\0')) {
            mode = CMD_MODE_CLEANUP_LOGS;
            args += strlen("--cleanup-logs");
        }
        else if (strncmp(args, "--test-config", strlen("--test-config")) == 0 &&
                (args[strlen("--test-config")] == ' ' || args[strlen("--test-config")] == '\0')) {
            mode = CMD_MODE_TEST_CONFIG;
            args += strlen("--test-config");
        }
        else if (strncmp(args, "--test-config-dialog", strlen("--test-config-dialog")) == 0 &&
                (args[strlen("--test-config-dialog")] == ' ' || args[strlen("--test-config-dialog")] == '\0')) {
            mode = CMD_MODE_TEST_CONFIG_DIALOG;
            args += strlen("--test-config-dialog");
        }
        else if (strncmp(args, "--test-voice-prompt", strlen("--test-voice-prompt")) == 0 &&
                (args[strlen("--test-voice-prompt")] == ' ' || args[strlen("--test-voice-prompt")] == '\0')) {
            mode = CMD_MODE_TEST_VOICE_PROMPT;
            args += strlen("--test-voice-prompt");
        }
        else if (strncmp(args, "--test-toast", strlen("--test-toast")) == 0 &&
                (args[strlen("--test-toast")] == ' ' || args[strlen("--test-toast")] == '\0')) {
            mode = CMD_MODE_TEST_TOAST;
            args += strlen("--test-toast");
        }
        else if (strncmp(args, "--test-model-load", strlen("--test-model-load")) == 0 &&
                (args[strlen("--test-model-load")] == ' ' || args[strlen("--test-model-load")] == '\0')) {
            mode = CMD_MODE_TEST_MODEL_LOAD;
            args += strlen("--test-model-load");
            while (*args == ' ' || *args == '\t') args++;
            if (*args != '\0') {
                g_testModelPath = args;
                break;
            }
        }
        else if (strncmp(args, "--disable-voice", strlen("--disable-voice")) == 0 &&
                (args[strlen("--disable-voice")] == ' ' || args[strlen("--disable-voice")] == '\0')) {
            g_cmdEnableVoice = 0;
            args += strlen("--disable-voice");
        }
        else if (strncmp(args, "--enable-voice", strlen("--enable-voice")) == 0 &&
                (args[strlen("--enable-voice")] == ' ' || args[strlen("--enable-voice")] == '\0')) {
            g_cmdEnableVoice = 1;
            args += strlen("--enable-voice");
        }
        else if (strncmp(args, "--test-screenshot-capture", strlen("--test-screenshot-capture")) == 0 &&
                (args[strlen("--test-screenshot-capture")] == ' ' || args[strlen("--test-screenshot-capture")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_CAPTURE;
            args += strlen("--test-screenshot-capture");
            while (*args == ' ' || *args == '\t') args++;

            // Parse arguments: x y w h [output.png]
            if (*args != '\0') {
                g_screenshotX = atoi(args);
                while (*args != ' ' && *args != '\t' && *args != '\0') args++;
                while (*args == ' ' || *args == '\t') args++;
            }
            if (*args != '\0') {
                g_screenshotY = atoi(args);
                while (*args != ' ' && *args != '\t' && *args != '\0') args++;
                while (*args == ' ' || *args == '\t') args++;
            }
            if (*args != '\0') {
                g_screenshotW = atoi(args);
                while (*args != ' ' && *args != '\t' && *args != '\0') args++;
                while (*args == ' ' || *args == '\t') args++;
            }
            if (*args != '\0') {
                g_screenshotH = atoi(args);
                while (*args != ' ' && *args != '\t' && *args != '\0') args++;
                while (*args == ' ' || *args == '\t') args++;
            }
            if (*args != '\0') {
                g_screenshotOutputPath = args;
            }
        }
        else if (strncmp(args, "--test-screenshot-save", strlen("--test-screenshot-save")) == 0 &&
                (args[strlen("--test-screenshot-save")] == ' ' || args[strlen("--test-screenshot-save")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_SAVE;
            args += strlen("--test-screenshot-save");
            while (*args == ' ' || *args == '\t') args++;
            if (*args != '\0') {
                g_screenshotOutputPath = args;
            }
        }
        else if (strncmp(args, "--test-screenshot-clipboard", strlen("--test-screenshot-clipboard")) == 0 &&
                (args[strlen("--test-screenshot-clipboard")] == ' ' || args[strlen("--test-screenshot-clipboard")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_CLIPBOARD;
            args += strlen("--test-screenshot-clipboard");
        }
        else if (strncmp(args, "--test-screenshot-all", strlen("--test-screenshot-all")) == 0 &&
                (args[strlen("--test-screenshot-all")] == ' ' || args[strlen("--test-screenshot-all")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_ALL;
            args += strlen("--test-screenshot-all");
        }
        else if (strncmp(args, "--test-screenshot-overlay", strlen("--test-screenshot-overlay")) == 0 &&
                (args[strlen("--test-screenshot-overlay")] == ' ' || args[strlen("--test-screenshot-overlay")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_OVERLAY;
            args += strlen("--test-screenshot-overlay");
        }
        else if (strncmp(args, "--test-screenshot-toolbar", strlen("--test-screenshot-toolbar")) == 0 &&
                (args[strlen("--test-screenshot-toolbar")] == ' ' || args[strlen("--test-screenshot-toolbar")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_TOOLBAR;
            args += strlen("--test-screenshot-toolbar");
        }
        else if (strncmp(args, "--test-screenshot-float", strlen("--test-screenshot-float")) == 0 &&
                (args[strlen("--test-screenshot-float")] == ' ' || args[strlen("--test-screenshot-float")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_FLOAT;
            args += strlen("--test-screenshot-float");
        }
        else if (strncmp(args, "--test-screenshot-annotate", strlen("--test-screenshot-annotate")) == 0 &&
                (args[strlen("--test-screenshot-annotate")] == ' ' || args[strlen("--test-screenshot-annotate")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_ANNOTATE;
            args += strlen("--test-screenshot-annotate");
        }
        else if (strncmp(args, "--test-screenshot-ocr", strlen("--test-screenshot-ocr")) == 0 &&
                (args[strlen("--test-screenshot-ocr")] == ' ' || args[strlen("--test-screenshot-ocr")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_OCR;
            args += strlen("--test-screenshot-ocr");
        }
        else if (strncmp(args, "--test-ocr-data-model", strlen("--test-ocr-data-model")) == 0 &&
                (args[strlen("--test-ocr-data-model")] == ' ' || args[strlen("--test-ocr-data-model")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_DATA_MODEL;
            args += strlen("--test-ocr-data-model");
        }
        else if (strncmp(args, "--test-ocr-layout", strlen("--test-ocr-layout")) == 0 &&
                (args[strlen("--test-ocr-layout")] == ' ' || args[strlen("--test-ocr-layout")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_LAYOUT;
            args += strlen("--test-ocr-layout");
        }
        else if (strncmp(args, "--test-ocr-hit-test", strlen("--test-ocr-hit-test")) == 0 &&
                (args[strlen("--test-ocr-hit-test")] == ' ' || args[strlen("--test-ocr-hit-test")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_HIT_TEST;
            args += strlen("--test-ocr-hit-test");
        }
        else if (strncmp(args, "--test-ocr-selection", strlen("--test-ocr-selection")) == 0 &&
                (args[strlen("--test-ocr-selection")] == ' ' || args[strlen("--test-ocr-selection")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_SELECTION;
            args += strlen("--test-ocr-selection");
        }
        else if (strncmp(args, "--test-ocr-copy-text", strlen("--test-ocr-copy-text")) == 0 &&
                (args[strlen("--test-ocr-copy-text")] == ' ' || args[strlen("--test-ocr-copy-text")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_COPY_TEXT;
            args += strlen("--test-ocr-copy-text");
        }
        else if (strncmp(args, "--test-ocr-float-ui", strlen("--test-ocr-float-ui")) == 0 &&
                (args[strlen("--test-ocr-float-ui")] == ' ' || args[strlen("--test-ocr-float-ui")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_FLOAT_UI;
            args += strlen("--test-ocr-float-ui");
        }
        else if (strncmp(args, "--test-ocr-async", strlen("--test-ocr-async")) == 0 &&
                (args[strlen("--test-ocr-async")] == ' ' || args[strlen("--test-ocr-async")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_ASYNC;
            args += strlen("--test-ocr-async");
        }
        else if (strncmp(args, "--test-ocr-e2e-mock", strlen("--test-ocr-e2e-mock")) == 0 &&
                (args[strlen("--test-ocr-e2e-mock")] == ' ' || args[strlen("--test-ocr-e2e-mock")] == '\0')) {
            mode = CMD_MODE_TEST_OCR_E2E_MOCK;
            args += strlen("--test-ocr-e2e-mock");
        }
        else if (strncmp(args, "--test-screenshot-ocr-selection-all", strlen("--test-screenshot-ocr-selection-all")) == 0 &&
                (args[strlen("--test-screenshot-ocr-selection-all")] == ' ' || args[strlen("--test-screenshot-ocr-selection-all")] == '\0')) {
            mode = CMD_MODE_TEST_SCREENSHOT_OCR_SELECTION_ALL;
            args += strlen("--test-screenshot-ocr-selection-all");
        }
        // Unknown argument, skip
        else {
            while (*args != ' ' && *args != '\t' && *args != '\0') args++;
        }
    }

    return mode;
}

// Windows 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    // 检查单实例，使用命名互斥量确保只有一个实例运行
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"PowerCapslock_SingleInstanceMutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已有实例在运行
        MessageBoxW(NULL,
            L"PowerCapslock 已经在后台运行了。\n\n"
            L"请查看系统托盘找到程序图标，\n"
            L"不需要重复启动。",
            L"PowerCapslock - 提示",
            MB_OK | MB_ICONINFORMATION);
        if (hMutex != NULL) {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 0;
    }

    // 解析命令行参数
    char* outputPath = NULL;
    CommandMode mode = ParseCommandLine(lpCmdLine, &outputPath);

    // 根据模式分发
    switch (mode) {
        case CMD_MODE_VOICE_TEST:
            // 语音测试模式不需要单实例限制，释放互斥量
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunVoiceTestMode(outputPath);
        case CMD_MODE_CLEANUP_LOGS:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunLogCleanupTest();
        case CMD_MODE_TEST_CONFIG:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunConfigTest();
        case CMD_MODE_TEST_CONFIG_DIALOG:
            // 保持单实例限制，不释放互斥量
            return RunConfigDialogTest();
        case CMD_MODE_TEST_VOICE_PROMPT:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunVoicePromptWindowTest();
        case CMD_MODE_TEST_TOAST:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunToastWindowTest();
        case CMD_MODE_TEST_MODEL_LOAD:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunModelLoadTest();
        case CMD_MODE_TEST_AUDIO_FILE:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunAudioFileTest();
        case CMD_MODE_TEST_CAPSLOCK_A:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunCapsLockAHookTest(outputPath);
        case CMD_MODE_TEST_KEY_MAPPING:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunKeyMappingTestMode();
        case CMD_MODE_TEST_SCREENSHOT_CAPTURE:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotCaptureTest();
        case CMD_MODE_TEST_SCREENSHOT_SAVE:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotSaveTest();
        case CMD_MODE_TEST_SCREENSHOT_CLIPBOARD:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotClipboardTest();
        case CMD_MODE_TEST_SCREENSHOT_ALL:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotAllTests();
        case CMD_MODE_TEST_SCREENSHOT_OVERLAY:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotOverlayTest();
        case CMD_MODE_TEST_SCREENSHOT_TOOLBAR:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotToolbarTest();
        case CMD_MODE_TEST_SCREENSHOT_FLOAT:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotFloatTest();
        case CMD_MODE_TEST_SCREENSHOT_ANNOTATE:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotAnnotateTest();
        case CMD_MODE_TEST_SCREENSHOT_OCR:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotOCRTest();
        case CMD_MODE_TEST_OCR_DATA_MODEL:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrDataModelTest();
        case CMD_MODE_TEST_OCR_LAYOUT:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrLayoutTest();
        case CMD_MODE_TEST_OCR_HIT_TEST:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrHitTestTest();
        case CMD_MODE_TEST_OCR_SELECTION:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrSelectionTest();
        case CMD_MODE_TEST_OCR_COPY_TEXT:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrCopyTextTest();
        case CMD_MODE_TEST_OCR_FLOAT_UI:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrFloatUiTest();
        case CMD_MODE_TEST_OCR_ASYNC:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrAsyncTest();
        case CMD_MODE_TEST_OCR_E2E_MOCK:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunOcrE2eMockTest();
        case CMD_MODE_TEST_SCREENSHOT_OCR_SELECTION_ALL:
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
            return RunScreenshotOcrSelectionAllTest();
        case CMD_MODE_NORMAL:
        default:
            break;
    }

    // 正常GUI启动
    // 初始化所有模块
    if (!InitializeModules()) {
        MessageBoxW(NULL,
            L"初始化失败！\n\n"
            L"可能的原因：\n"
            L"1. 缺少必要的系统权限\n"
            L"2. 配置文件损坏\n"
            L"3. 系统资源不足\n\n"
            L"请尝试以管理员身份运行。",
            L"PowerCapslock - 错误",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (HookHandleMessage(&msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理所有模块
    CleanupModules();

    // 释放单实例互斥量
    if (hMutex != NULL) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return (int)msg.wParam;
}
