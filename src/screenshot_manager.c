#include "screenshot_manager.h"
#include "screenshot.h"
#include "screenshot_overlay.h"
#include "screenshot_toolbar.h"
#include "screenshot_float.h"
#include "screenshot_ocr.h"
#include "logger.h"
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

#define MODULE_NAME "截图管理器"

static bool g_initialized = false;
static bool g_active = false;

static void OnToolbarButton(ToolbarButtonType button, void* userData);
static void FinishCaptureSession(void);
static ScreenshotRect SelectionToScreenRect(const ScreenshotRect* selection);
static ScreenshotImage* GetSelectionImageForAction(void);
static bool BuildDefaultScreenshotPath(char* path, size_t pathSize);
static bool PromptSaveScreenshotPath(char* path, size_t pathSize);
static void SelectAnnotationTool(OverlayAnnotateTool tool);

static void OnToolbarButton(ToolbarButtonType button, void* userData) {
    ScreenshotImage* image;
    ScreenshotRect screenSelection;

    (void)userData;

    switch (button) {
        case TOOLBAR_BTN_COPY:
            image = GetSelectionImageForAction();
            if (image != NULL) {
                if (ScreenshotCopyToClipboard(image)) {
                    LOG_INFO("[%s] Selection copied to clipboard", MODULE_NAME);
                }
                ScreenshotImageFree(image);
                FinishCaptureSession();
            }
            break;

        case TOOLBAR_BTN_PIN:
            image = GetSelectionImageForAction();
            if (image != NULL) {
                const ScreenshotRect* selection = ScreenshotOverlayGetSelection();
                screenSelection = SelectionToScreenRect(selection);
                ScreenshotToolbarHide();
                ScreenshotOverlayHide();
                g_active = false;
                if (!ScreenshotFloatShow(image, screenSelection.x, screenSelection.y)) {
                    LOG_ERROR("[%s] Failed to pin screenshot float window", MODULE_NAME);
                }
                ScreenshotImageFree(image);
            }
            break;

        case TOOLBAR_BTN_SAVE:
            image = GetSelectionImageForAction();
            if (image != NULL) {
                char path[MAX_PATH];
                if (PromptSaveScreenshotPath(path, sizeof(path)) &&
                    ScreenshotSaveToFile(image, path, SCREENSHOT_FORMAT_BMP, 100)) {
                    LOG_INFO("[%s] Selection saved to %s", MODULE_NAME, path);
                    FinishCaptureSession();
                }
                ScreenshotImageFree(image);
            }
            break;

        case TOOLBAR_BTN_OCR:
            image = GetSelectionImageForAction();
            if (image != NULL) {
                const ScreenshotRect* selection = ScreenshotOverlayGetSelection();
                screenSelection = SelectionToScreenRect(selection);
                ScreenshotToolbarHide();
                ScreenshotOverlayHide();
                g_active = false;
                if (!ScreenshotFloatShow(image, screenSelection.x, screenSelection.y)) {
                    LOG_ERROR("[%s] Failed to show OCR float window", MODULE_NAME);
                }
                ScreenshotImageFree(image);
            }
            break;

        case TOOLBAR_BTN_CLOSE:
            FinishCaptureSession();
            break;

        case TOOLBAR_BTN_RECT:
            SelectAnnotationTool(OVERLAY_ANNOTATE_RECT);
            break;
        case TOOLBAR_BTN_ARROW:
            SelectAnnotationTool(OVERLAY_ANNOTATE_ARROW);
            break;
        case TOOLBAR_BTN_PENCIL:
            SelectAnnotationTool(OVERLAY_ANNOTATE_PENCIL);
            break;
        case TOOLBAR_BTN_CIRCLE:
            SelectAnnotationTool(OVERLAY_ANNOTATE_CIRCLE);
            break;
        case TOOLBAR_BTN_TEXT:
            SelectAnnotationTool(OVERLAY_ANNOTATE_TEXT);
            break;

        default:
            LOG_DEBUG("[%s] Ignored toolbar button: %d", MODULE_NAME, button);
            break;
    }
}

static void FinishCaptureSession(void) {
    ScreenshotToolbarHide();
    ScreenshotOverlayHide();
    g_active = false;
}

static ScreenshotRect SelectionToScreenRect(const ScreenshotRect* selection) {
    ScreenshotRect rect = {0};
    int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);

    if (selection != NULL) {
        rect = *selection;
        rect.x += virtualX;
        rect.y += virtualY;
    }

    return rect;
}

static ScreenshotImage* GetSelectionImageForAction(void) {
    ScreenshotImage* image = ScreenshotOverlayGetAnnotatedSelectionImage();
    if (image == NULL) {
        LOG_ERROR("[%s] Failed to get selected screenshot image", MODULE_NAME);
    }
    return image;
}

static bool PromptSaveScreenshotPath(char* path, size_t pathSize) {
    OPENFILENAMEA ofn;

    if (path == NULL || pathSize == 0) {
        return false;
    }

    if (!BuildDefaultScreenshotPath(path, pathSize)) {
        strncpy(path, "screenshot.bmp", pathSize - 1);
        path[pathSize - 1] = '\0';
    }

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = ScreenshotToolbarGetWindow();
    ofn.lpstrFilter = "BMP Image (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)pathSize;
    ofn.lpstrDefExt = "bmp";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = "Save screenshot";

    if (!GetSaveFileNameA(&ofn)) {
        DWORD error = CommDlgExtendedError();
        if (error != 0) {
            LOG_ERROR("[%s] Save dialog failed: %lu", MODULE_NAME, error);
        }
        return false;
    }

    return true;
}

static void SelectAnnotationTool(OverlayAnnotateTool tool) {
    const ScreenshotRect* selection;
    ScreenshotRect screenSelection;
    HWND toolbarHwnd;

    ScreenshotToolbarSetOwner(ScreenshotOverlayGetWindow());
    ScreenshotOverlaySetAnnotationTool(tool);

    selection = ScreenshotOverlayGetSelection();
    if (selection != NULL) {
        screenSelection = SelectionToScreenRect(selection);
        ScreenshotToolbarUpdatePosition(&screenSelection);
    }

    toolbarHwnd = ScreenshotToolbarGetWindow();
    if (toolbarHwnd != NULL) {
        SetWindowPos(toolbarHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    LOG_INFO("[%s] Annotation tool selected: %d", MODULE_NAME, tool);
}

static bool BuildDefaultScreenshotPath(char* path, size_t pathSize) {
    char baseDir[MAX_PATH];
    char targetDir[MAX_PATH];
    SYSTEMTIME st;
    HRESULT hr;

    if (path == NULL || pathSize == 0) {
        return false;
    }

    hr = SHGetFolderPathA(NULL, CSIDL_MYPICTURES, NULL, SHGFP_TYPE_CURRENT, baseDir);
    if (FAILED(hr)) {
        DWORD len = GetTempPathA((DWORD)sizeof(baseDir), baseDir);
        if (len == 0 || len >= sizeof(baseDir)) {
            LOG_ERROR("[%s] Failed to resolve screenshot save directory", MODULE_NAME);
            return false;
        }
    }

    if (snprintf(targetDir, sizeof(targetDir), "%s\\PowerCapslock", baseDir) >= (int)sizeof(targetDir)) {
        LOG_ERROR("[%s] Screenshot save directory path is too long", MODULE_NAME);
        return false;
    }

    if (!CreateDirectoryA(targetDir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        LOG_ERROR("[%s] Failed to create screenshot save directory: %s", MODULE_NAME, targetDir);
        return false;
    }

    GetLocalTime(&st);
    if (snprintf(path, pathSize,
                 "%s\\screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
                 targetDir,
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond) >= (int)pathSize) {
        LOG_ERROR("[%s] Screenshot save path is too long", MODULE_NAME);
        return false;
    }

    return true;
}

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
        ScreenshotToolbarHide();
        ScreenshotOverlayHide();
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
    ScreenshotToolbarHide();

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

void ScreenshotManagerOnOverlayCancelled(void) {
    ScreenshotToolbarHide();
    g_active = false;
}

void ScreenshotManagerOnSelectionComplete(void) {
    const ScreenshotRect* selection = ScreenshotOverlayGetSelection();
    ScreenshotRect screenSelection;

    if (selection == NULL || selection->width <= 0 || selection->height <= 0) {
        LOG_WARN("[%s] Ignoring empty screenshot selection", MODULE_NAME);
        return;
    }

    screenSelection = SelectionToScreenRect(selection);
    ScreenshotToolbarSetOwner(ScreenshotOverlayGetWindow());
    if (!ScreenshotToolbarShow(&screenSelection, OnToolbarButton, NULL)) {
        LOG_ERROR("[%s] Failed to show screenshot toolbar", MODULE_NAME);
    }
}
