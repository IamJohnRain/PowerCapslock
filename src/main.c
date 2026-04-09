#include "logger.h"
#include "config.h"
#include "keymap.h"
#include "hook.h"
#include "tray.h"
#include "keyboard_layout.h"
#include "voice.h"
#include "audio.h"
#include <windows.h>
#include <stdio.h>

// 全局变量
static HINSTANCE g_hInstance = NULL;
static HHOOK g_hHook = NULL;

// 初始化所有模块
static BOOL InitializeModules(void) {
    // 初始化配置模块（设置路径和默认值，创建目录和配置文件）
    ConfigInit();

    // 先初始化日志模块（使用默认配置），这样后续加载配置时可以记录日志
    LoggerInit(ConfigGetLogPath());
    LoggerSetLevel(LOG_LEVEL_INFO);  // 先用默认级别

    LOG_INFO("========================================");
    LOG_INFO("PowerCapslock v1.0 starting...");
    LOG_INFO("========================================");

    // 初始化键位映射模块（必须在 ConfigLoad 之前，因为 ConfigLoad 会调用 ParseMappings）
    KeymapInit();

    // 加载配置文件
    const Config* config = ConfigGet();
    ConfigLoad(NULL);

    // 根据配置更新日志级别
    LoggerSetLevel(config->logLevel);

    // 初始化键盘布局模块
    KeyboardLayoutInit();

    // 检查语音输入配置，如果用户还没选择过是否启用，询问用户
    Config* mutableConfig = (Config*)ConfigGet();
    if (!mutableConfig->voiceInputAsked) {
        int result = MessageBox(NULL,
            TEXT("是否启用语音输入功能？\n\n")
            TEXT("启用后可按 CapsLock+A 触发离线语音输入，")
            TEXT("需要下载模型到 models 目录。\n\n")
            TEXT("点击「是(Y)」启用，「否(N)」不启用\n")
            TEXT("选择后会保存配置不再询问。"),
            TEXT("PowerCapslock - 语音输入功能"),
            MB_YESNO | MB_ICONQUESTION);

        if (result == IDYES) {
            mutableConfig->voiceInputEnabled = true;
            mutableConfig->voiceInputAsked = true;

            // 弹出提示用户下载模型
            MessageBox(NULL,
                TEXT("请按以下步骤启用语音输入：\n\n")
                TEXT("1. 下载 SenseVoice 模型文件\n")
                TEXT("   地址: https://github.com/HaujetZhao/CapsWriter-Offline/releases/tag/models\n")
                TEXT("   下载: sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.zip\n\n")
                TEXT("2. 解压后放到 models/SenseVoice-Small/ 目录\n")
                TEXT("   最终路径应该是: models/SenseVoice-Small/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.onnx\n\n")
                TEXT("3. 重启 PowerCapslock 即可使用"),
                TEXT("PowerCapslock - 模型下载提示"),
                MB_OK | MB_ICONINFORMATION);

            // 打开浏览器下载页面
            ShellExecute(NULL, TEXT("open"),
                TEXT("https://github.com/HaujetZhao/CapsWriter-Offline/releases/tag/models"),
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

            // 初始化语音识别模块
            char modelDir[MAX_PATH];
            GetModuleFileNameA(NULL, modelDir, MAX_PATH);
            char* lastSlash = strrchr(modelDir, '\\');
            if (lastSlash != NULL) {
                *(lastSlash + 1) = '\0';
                strcat(modelDir, "models");
            }

            if (VoiceInit(modelDir)) {
                LOG_INFO("Voice recognition initialized successfully");
            } else {
                LOG_INFO("Voice recognition not available: model not loaded");
                // 如果用户已经选择启用但是模型没找到，弹出提示
                if (config->voiceInputAsked) {
                    MessageBox(NULL,
                        TEXT("语音识别模型未找到！\n\n")
                        TEXT("请确保模型已放置到正确位置：\n")
                        TEXT("models/SenseVoice-Small/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/model.onnx\n\n")
                        TEXT("你可以重新下载模型放入该位置后重启程序。"),
                        TEXT("PowerCapslock - 语音输入"),
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

    // 清理音频模块
    AudioCleanup();

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

    const Config* config = ConfigGet();
    LoggerSetLevel(config->logLevel);

    LOG_INFO("========== PowerCapslock Voice Test Mode ==========");

    // 检查语音输入是否启用
    if (!config->voiceInputEnabled) {
        LOG_ERROR("Voice input is not enabled in configuration");
        printf("ERROR: Voice input is not enabled in configuration\n");
        printf("Please enable it in config or run PowerCapslock normally to enable it\n");
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

    // 构建模型路径
    char modelDir[MAX_PATH];
    GetModuleFileNameA(NULL, modelDir, MAX_PATH);
    char* lastSlash = strrchr(modelDir, '\\');
    if (lastSlash != NULL) {
        *(lastSlash + 1) = '\0';
        strcat(modelDir, "models");
    }

    // 初始化语音识别
    if (!VoiceInit(modelDir)) {
        LOG_ERROR("Failed to load voice recognition model from: %s", modelDir);
        printf("ERROR: Failed to load voice recognition model from: %s\n", modelDir);
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

    LOG_INFO("Voice recognition model loaded successfully from: %s", modelDir);
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

// 解析命令行参数，检查是否是语音测试模式
static bool ParseCommandLine(LPSTR lpCmdLine, char** outputPath) {
    char* args = lpCmdLine;
    while (*args == ' ' || *args == '\t') args++;

    // 检查 --voice-test 或 --dry-run
    if (strstr(args, "--voice-test") == args ||
        strstr(args, "--dry-run") == args) {
        // 跳过选项
        args += strlen("--voice-test");
        while (*args == ' ' || *args == '\t') args++;

        // 如果还有参数，就是输出文件路径
        if (*args != '\0') {
            *outputPath = args;
        } else {
            *outputPath = NULL;
        }
        return true;
    }

    return false;
}

// Windows 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    // 检查是否是语音测试模式
    char* outputPath = NULL;
    if (ParseCommandLine(lpCmdLine, &outputPath)) {
        return RunVoiceTestMode(outputPath);
    }

    // 正常GUI启动
    // 初始化所有模块
    if (!InitializeModules()) {
        MessageBoxA(NULL,
            "初始化失败！\n\n"
            "可能的原因：\n"
            "1. 缺少必要的系统权限\n"
            "2. 配置文件损坏\n"
            "3. 系统资源不足\n\n"
            "请尝试以管理员身份运行。",
            "PowerCapslock - 错误",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理所有模块
    CleanupModules();

    return (int)msg.wParam;
}
