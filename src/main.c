#include "logger.h"
#include "config.h"
#include "keymap.h"
#include "hook.h"
#include "tray.h"
#include "keyboard_layout.h"
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

// Windows 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

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
