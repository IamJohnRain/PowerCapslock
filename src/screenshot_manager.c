#include "screenshot_manager.h"
#include "screenshot_overlay.h"
#include "screenshot_toolbar.h"
#include "screenshot_float.h"
#include "screenshot_ocr.h"
#include "logger.h"

#define MODULE_NAME "截图管理器"

static bool g_initialized = false;
static bool g_active = false;

bool ScreenshotManagerInit(void) {
    if (g_initialized) {
        return true;
    }

    LOG_DEBUG("[%s] 开始初始化...", MODULE_NAME);

    if (!ScreenshotInit()) {
        LOG_ERROR("[%s] 截图模块初始化失败", MODULE_NAME);
        return false;
    }

    if (!ScreenshotOverlayInit()) {
        LOG_ERROR("[%s] 选区窗口模块初始化失败", MODULE_NAME);
        return false;
    }

    if (!ScreenshotToolbarInit()) {
        LOG_ERROR("[%s] 工具栏模块初始化失败", MODULE_NAME);
        return false;
    }

    if (!ScreenshotFloatInit()) {
        LOG_ERROR("[%s] 浮动窗口模块初始化失败", MODULE_NAME);
        return false;
    }

    if (!OCRInit()) {
        LOG_ERROR("[%s] OCR模块初始化失败", MODULE_NAME);
        return false;
    }

    g_initialized = true;
    LOG_INFO("[%s] 模块初始化成功", MODULE_NAME);
    return true;
}

void ScreenshotManagerCleanup(void) {
    if (!g_initialized) {
        return;
    }

    LOG_DEBUG("[%s] 开始清理...", MODULE_NAME);

    if (g_active) {
        g_active = false;
    }

    OCRCleanup();
    ScreenshotFloatCleanup();
    ScreenshotToolbarCleanup();
    ScreenshotOverlayCleanup();
    ScreenshotCleanup();
    g_initialized = false;
    LOG_INFO("[%s] 模块已清理", MODULE_NAME);
}

bool ScreenshotManagerStart(void) {
    if (!g_initialized) {
        if (!ScreenshotManagerInit()) {
            return false;
        }
    }

    LOG_INFO("[%s] 开始截图流程", MODULE_NAME);
    g_active = true;

    if (!ScreenshotOverlayShow()) {
        LOG_ERROR("[%s] 选区窗口显示失败", MODULE_NAME);
        g_active = false;
        return false;
    }

    return true;
}

bool ScreenshotManagerIsActive(void) {
    return g_active;
}
