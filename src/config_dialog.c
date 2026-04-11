#include "config_dialog.h"
#include "config.h"
#include "keymap.h"
#include "logger.h"
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Control IDs matching resource.rc
#define IDD_CONFIG_DIALOG    200
#define IDC_TAB             201
#define IDC_LOG_PATH        202
#define IDC_BROWSE_LOG      203
#define IDC_MODEL_PATH      204
#define IDC_BROWSE_MODEL    205
#define IDC_MAPPING_LIST    206
#define IDC_BTN_ADD         207
#define IDC_BTN_EDIT        208
#define IDC_BTN_REMOVE      209
#define IDC_BTN_RESET       210
#define IDD_MAPPING_DIALOG  300
#define IDC_FROM_KEY        301
#define IDC_TO_KEY          302

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

// 需要链接 common controls v6 用于可视化样式
#ifdef _MSC_VER
#pragma comment(linker,"\"/manifestdependency:type='common_controls' version='6.0.0.0' processorArchitecture='*' name='Microsoft.Windows.Common-Controls'\"")
#endif

// 对话框全局状态（模态对话框，所以可以用静态变量）
static HWND g_hDlg;
static HWND g_hTab;
static HWND g_hLogEdit;
static HWND g_hModelEdit;
static HWND g_hMappingList;
static HWND g_hHoverButton;
static int g_currentTab;
static Config g_workingConfig;
static bool g_needRebuildMappings = false;
static HFONT g_hTitleFont = NULL;
static HFONT g_hSectionFont = NULL;
static HBRUSH g_hBgBrush = NULL;
static HBRUSH g_hPanelBrush = NULL;
static HBRUSH g_hPanelAltBrush = NULL;
static HBRUSH g_hEditBrush = NULL;

static const COLORREF COLOR_BG = RGB(226, 241, 255);
static const COLORREF COLOR_HEADER = RGB(215, 233, 252);
static const COLORREF COLOR_PANEL = RGB(244, 250, 255);
static const COLORREF COLOR_PANEL_ALT = RGB(232, 245, 255);
static const COLORREF COLOR_EDIT = RGB(255, 255, 255);
static const COLORREF COLOR_TEXT = RGB(25, 48, 76);
static const COLORREF COLOR_MUTED = RGB(84, 113, 146);
static const COLORREF COLOR_ACCENT = RGB(42, 145, 255);
static const COLORREF COLOR_ACCENT_GREEN = RGB(39, 188, 143);
static const COLORREF COLOR_ACCENT_CORAL = RGB(232, 92, 84);

// 控件句柄数组，用于切换标签时销毁
static HWND g_tabControls[2][20];
static int g_tabControlCount[2] = {0};

// Forward declarations
static INT_PTR CALLBACK ConfigDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static void OnInitDialog(HWND hDlg);
static void OnTabChange(HWND hDlg);
static void CreateTab1Controls(HWND hDlg);
static void CreateTab2Controls(HWND hDlg);
static void PopulateMappingsList(HWND hList);
static void OnBrowseDirectory(HWND hDlg, int editId);
static void OnAddMapping(HWND hDlg);
static void OnEditMapping(HWND hDlg);
static void OnRemoveMapping(HWND hDlg);
static void OnResetMappings(HWND hDlg);
static bool AddOrEditMapping(HWND hParent, bool isEdit, int selectedIndex);
static void OnOK(HWND hDlg);
static void OnCancel(HWND hDlg);
static void ClearTabControls(int tabIndex);
static void CapturePathEdits(void);
static void InitDialogFonts(HWND hDlg);
static void CleanupDialogFonts(void);
static void ApplySectionFont(HWND hWnd);
static void PaintConfigDialog(HWND hDlg);
static HBRUSH HandleCtlColor(HWND hCtrl, HDC hdc, UINT msg);
static void DrawOwnerButton(const DRAWITEMSTRUCT* dis);
static void DrawOwnerTab(const DRAWITEMSTRUCT* dis);
static LRESULT DrawMappingListItem(NMLVCUSTOMDRAW* draw);
static LRESULT CALLBACK GlassButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData);
static void AttachGlassButton(HWND hWnd);
static void Utf8ToWide(const char* source, wchar_t* dest, int destCount);
static void WideToUtf8(const wchar_t* source, char* dest, int destCount);
static void SetEditTextUtf8(HWND hWnd, const char* text);
static void GetEditTextUtf8(HWND hWnd, char* dest, int destCount);
static void ExtractMappingNames(const char* mappingName, char* fromName, size_t fromSize, char* toName, size_t toSize);
static const wchar_t* KeyDisplayNameW(const char* key, wchar_t* fallback, size_t fallbackCount);

static void ApplyDialogFont(HWND hWnd) {
    HFONT hFont = (HFONT)SendMessage(g_hDlg, WM_GETFONT, 0, 0);
    if (hWnd != NULL && hFont != NULL) {
        SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
}

static void TrackTabControl(int tabIndex, HWND hWnd) {
    if (hWnd == NULL || g_tabControlCount[tabIndex] >= 20) {
        return;
    }
    ApplyDialogFont(hWnd);
    g_tabControls[tabIndex][g_tabControlCount[tabIndex]++] = hWnd;
}

static void InitDialogFonts(HWND hDlg) {
    HFONT hFont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
    LOGFONTW lf;
    HDC hdc = GetDC(hDlg);
    int dpi = (hdc != NULL) ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc != NULL) {
        ReleaseDC(hDlg, hdc);
    }

    CleanupDialogFonts();
    ZeroMemory(&lf, sizeof(lf));
    if (hFont == NULL || GetObjectW(hFont, sizeof(lf), &lf) == 0) {
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
    }
    lstrcpynW(lf.lfFaceName, L"Microsoft YaHei UI", LF_FACESIZE);

    lf.lfHeight = -MulDiv(18, dpi, 72);
    lf.lfWeight = FW_BOLD;
    g_hTitleFont = CreateFontIndirectW(&lf);

    lf.lfHeight = -MulDiv(10, dpi, 72);
    lf.lfWeight = FW_SEMIBOLD;
    g_hSectionFont = CreateFontIndirectW(&lf);

    g_hBgBrush = CreateSolidBrush(COLOR_BG);
    g_hPanelBrush = CreateSolidBrush(COLOR_PANEL);
    g_hPanelAltBrush = CreateSolidBrush(COLOR_PANEL_ALT);
    g_hEditBrush = CreateSolidBrush(COLOR_EDIT);
}

static void CleanupDialogFonts(void) {
    if (g_hTitleFont != NULL) {
        DeleteObject(g_hTitleFont);
        g_hTitleFont = NULL;
    }
    if (g_hSectionFont != NULL) {
        DeleteObject(g_hSectionFont);
        g_hSectionFont = NULL;
    }
    if (g_hBgBrush != NULL) {
        DeleteObject(g_hBgBrush);
        g_hBgBrush = NULL;
    }
    if (g_hPanelBrush != NULL) {
        DeleteObject(g_hPanelBrush);
        g_hPanelBrush = NULL;
    }
    if (g_hPanelAltBrush != NULL) {
        DeleteObject(g_hPanelAltBrush);
        g_hPanelAltBrush = NULL;
    }
    if (g_hEditBrush != NULL) {
        DeleteObject(g_hEditBrush);
        g_hEditBrush = NULL;
    }
}

static void ApplySectionFont(HWND hWnd) {
    if (hWnd != NULL && g_hSectionFont != NULL) {
        SendMessage(hWnd, WM_SETFONT, (WPARAM)g_hSectionFont, TRUE);
    }
}

// 标签索引
#define TAB_BASIC  0
#define TAB_MAPPING 1
#define TAB_COUNT  2

static const wchar_t* tabTitles[TAB_COUNT] = {
    L"基础设置",
    L"快捷映射"
};

static COLORREF BlendColor(COLORREF a, COLORREF b, int percentB) {
    int percentA = 100 - percentB;
    return RGB(
        (GetRValue(a) * percentA + GetRValue(b) * percentB) / 100,
        (GetGValue(a) * percentA + GetGValue(b) * percentB) / 100,
        (GetBValue(a) * percentA + GetBValue(b) * percentB) / 100);
}

static COLORREF LerpColor(COLORREF a, COLORREF b, int step, int total) {
    if (total <= 0) {
        return b;
    }
    return RGB(
        GetRValue(a) + (GetRValue(b) - GetRValue(a)) * step / total,
        GetGValue(a) + (GetGValue(b) - GetGValue(a)) * step / total,
        GetBValue(a) + (GetBValue(b) - GetBValue(a)) * step / total);
}

static void FillRoundRect(HDC hdc, const RECT* rc, COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, 8, 8);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void DrawGradientRoundRect(HDC hdc, const RECT* rc, COLORREF topColor, COLORREF bottomColor, COLORREF border, int radius) {
    int height = rc->bottom - rc->top;
    HRGN clip = CreateRoundRectRgn(rc->left, rc->top, rc->right + 1, rc->bottom + 1, radius, radius);
    int saved = SaveDC(hdc);
    SelectClipRgn(hdc, clip);

    for (int y = 0; y < height; y++) {
        COLORREF c = LerpColor(topColor, bottomColor, y, height - 1);
        HPEN pen = CreatePen(PS_SOLID, 1, c);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, rc->left, rc->top + y, NULL);
        LineTo(hdc, rc->right, rc->top + y);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    RestoreDC(hdc, saved);
    DeleteObject(clip);

    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
}

static void PaintConfigDialog(HWND hDlg) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hDlg, &ps);
    RECT rc;
    GetClientRect(hDlg, &rc);

    FillRect(hdc, &rc, g_hBgBrush != NULL ? g_hBgBrush : GetSysColorBrush(COLOR_WINDOW));

    RECT header = {0, 0, rc.right, 62};
    HBRUSH headerBrush = CreateSolidBrush(COLOR_HEADER);
    FillRect(hdc, &header, headerBrush);
    DeleteObject(headerBrush);

    RECT glow = {16, 12, rc.right - 16, 94};
    DrawGradientRoundRect(hdc, &glow, RGB(248, 253, 255), RGB(210, 232, 252), RGB(248, 253, 255), 16);

    RECT accent = {28, 82, 200, 85};
    HBRUSH accentBrush = CreateSolidBrush(COLOR_ACCENT);
    FillRect(hdc, &accent, accentBrush);
    accent.left = 208;
    accent.right = 288;
    DeleteObject(accentBrush);
    accentBrush = CreateSolidBrush(COLOR_ACCENT_GREEN);
    FillRect(hdc, &accent, accentBrush);
    DeleteObject(accentBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, COLOR_TEXT);
    HGDIOBJ oldFont = NULL;
    if (g_hTitleFont != NULL) {
        oldFont = SelectObject(hdc, g_hTitleFont);
    }
    RECT title = {28, 16, rc.right - 28, 42};
    DrawTextW(hdc, L"设置中心", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (oldFont != NULL) {
        SelectObject(hdc, oldFont);
    }

    SetTextColor(hdc, COLOR_MUTED);
    RECT subtitle = {28, 44, rc.right - 28, 62};
    DrawTextW(hdc, L"管理日志、模型路径，以及 CapsLock 快捷映射。", -1, &subtitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT panelShadow = {24, 112, rc.right - 18, rc.bottom - 50};
    FillRoundRect(hdc, &panelShadow, RGB(196, 218, 238), RGB(196, 218, 238));

    RECT panel = {20, 108, rc.right - 22, rc.bottom - 58};
    DrawGradientRoundRect(hdc, &panel, RGB(253, 255, 255), COLOR_PANEL, RGB(205, 226, 244), 14);

    RECT sideAccent = {26, 132, 30, rc.bottom - 82};
    DrawGradientRoundRect(hdc, &sideAccent, COLOR_ACCENT, COLOR_ACCENT_GREEN, COLOR_ACCENT_GREEN, 5);

    EndPaint(hDlg, &ps);
}

static HBRUSH HandleCtlColor(HWND hCtrl, HDC hdc, UINT msg) {
    SetBkMode(hdc, TRANSPARENT);

    if (msg == WM_CTLCOLOREDIT) {
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_EDIT);
        return g_hEditBrush != NULL ? g_hEditBrush : GetSysColorBrush(COLOR_WINDOW);
    }

    if (msg == WM_CTLCOLORBTN) {
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_PANEL_ALT);
        return g_hPanelAltBrush != NULL ? g_hPanelAltBrush : GetSysColorBrush(COLOR_WINDOW);
    }

    SetTextColor(hdc, COLOR_TEXT);
    if (hCtrl != NULL && GetParent(hCtrl) == g_hDlg) {
        return g_hPanelBrush != NULL ? g_hPanelBrush : GetSysColorBrush(COLOR_WINDOW);
    }
    return g_hPanelBrush != NULL ? g_hPanelBrush : GetSysColorBrush(COLOR_WINDOW);
}

static void DrawOwnerButton(const DRAWITEMSTRUCT* dis) {
    RECT rc = dis->rcItem;
    RECT shadow = rc;
    RECT gloss;
    int id = (int)dis->CtlID;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool hot = (g_hHoverButton == dis->hwndItem) || ((dis->itemState & ODS_HOTLIGHT) != 0);
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    COLORREF accent = COLOR_ACCENT;
    COLORREF text = COLOR_TEXT;
    COLORREF topColor;
    COLORREF bottomColor;
    COLORREF border;

    if (id == IDOK || id == IDC_BTN_ADD) {
        accent = COLOR_ACCENT_GREEN;
    } else if (id == IDC_BTN_EDIT || id == IDC_BROWSE_LOG || id == IDC_BROWSE_MODEL) {
        accent = COLOR_ACCENT;
    } else if (id == IDC_BTN_REMOVE) {
        accent = COLOR_ACCENT_CORAL;
        text = RGB(92, 30, 28);
    } else if (id == IDCANCEL || id == IDC_BTN_RESET) {
        accent = RGB(94, 133, 174);
    }

    topColor = BlendColor(RGB(255, 255, 255), accent, hot ? 18 : 10);
    bottomColor = BlendColor(COLOR_BG, accent, hot ? 38 : 24);
    border = hot ? BlendColor(RGB(255, 255, 255), accent, 54) : BlendColor(RGB(255, 255, 255), accent, 30);

    if (pressed) {
        topColor = BlendColor(COLOR_BG, accent, 42);
        bottomColor = BlendColor(COLOR_BG, accent, 56);
        border = BlendColor(RGB(255, 255, 255), accent, 68);
    }
    if (disabled) {
        topColor = RGB(232, 240, 248);
        bottomColor = RGB(218, 229, 240);
        border = RGB(200, 214, 228);
        text = RGB(132, 150, 170);
    }

    InflateRect(&rc, -1, -1);
    shadow.left += 2;
    shadow.top += 3;
    shadow.right += 1;
    shadow.bottom += 2;
    if (!pressed && !disabled) {
        FillRoundRect(dis->hDC, &shadow, hot ? RGB(177, 210, 241) : RGB(196, 219, 240), RGB(196, 219, 240));
    }

    if (pressed) {
        OffsetRect(&rc, 1, 1);
    }
    DrawGradientRoundRect(dis->hDC, &rc, topColor, bottomColor, border, 10);

    gloss = rc;
    gloss.left += 4;
    gloss.right -= 4;
    gloss.top += 3;
    gloss.bottom = gloss.top + max(5, (rc.bottom - rc.top) / 3);
    DrawGradientRoundRect(dis->hDC, &gloss,
                          hot ? RGB(255, 255, 255) : RGB(250, 254, 255),
                          BlendColor(RGB(255, 255, 255), topColor, 35),
                          RGB(255, 255, 255), 8);

    if (hot && !disabled) {
        RECT glow = rc;
        InflateRect(&glow, 2, 2);
        HPEN glowPen = CreatePen(PS_SOLID, 1, BlendColor(RGB(255, 255, 255), accent, 45));
        HGDIOBJ oldPen = SelectObject(dis->hDC, glowPen);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(HOLLOW_BRUSH));
        RoundRect(dis->hDC, glow.left, glow.top, glow.right, glow.bottom, 12, 12);
        SelectObject(dis->hDC, oldBrush);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(glowPen);
    }

    wchar_t label[64];
    GetWindowTextW(dis->hwndItem, label, (int)ARRAYSIZE(label));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    DrawTextW(dis->hDC, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if ((dis->itemState & ODS_FOCUS) != 0) {
        InflateRect(&rc, -3, -3);
        DrawFocusRect(dis->hDC, &rc);
    }
}

static void DrawOwnerTab(const DRAWITEMSTRUCT* dis) {
    if (dis->itemID >= TAB_COUNT) {
        return;
    }

    RECT rc = dis->rcItem;
    InflateRect(&rc, -2, -2);
    bool selected = ((int)dis->itemID == TabCtrl_GetCurSel(dis->hwndItem));
    COLORREF accent = selected ? COLOR_ACCENT : RGB(116, 154, 194);
    COLORREF topColor = selected ? RGB(255, 255, 255) : RGB(246, 252, 255);
    COLORREF bottomColor = selected ? BlendColor(COLOR_BG, COLOR_ACCENT, 32) : BlendColor(COLOR_BG, RGB(116, 154, 194), 18);
    COLORREF border = selected ? COLOR_ACCENT : RGB(196, 218, 238);
    COLORREF text = selected ? RGB(16, 82, 145) : COLOR_MUTED;

    DrawGradientRoundRect(dis->hDC, &rc, topColor, bottomColor, border, 10);
    if (selected) {
        RECT line = {rc.left + 10, rc.bottom - 4, rc.right - 10, rc.bottom - 2};
        HBRUSH lineBrush = CreateSolidBrush(accent);
        FillRect(dis->hDC, &line, lineBrush);
        DeleteObject(lineBrush);
    }
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, text);
    DrawTextW(dis->hDC, tabTitles[dis->itemID], -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT DrawMappingListItem(NMLVCUSTOMDRAW* draw) {
    switch (draw->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            draw->clrText = COLOR_TEXT;
            draw->clrTextBk = (draw->nmcd.uItemState & CDIS_SELECTED) ? RGB(203, 230, 255) : RGB(250, 253, 255);
            return CDRF_NEWFONT;
        default:
            return CDRF_DODEFAULT;
    }
}

static LRESULT CALLBACK GlassButtonSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR refData) {
    (void)id;
    (void)refData;

    switch (msg) {
        case WM_MOUSEMOVE: {
            if (g_hHoverButton != hWnd) {
                HWND oldHover = g_hHoverButton;
                g_hHoverButton = hWnd;
                if (oldHover != NULL && IsWindow(oldHover)) {
                    InvalidateRect(oldHover, NULL, TRUE);
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }

            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
            break;
        }
        case WM_MOUSELEAVE:
            if (g_hHoverButton == hWnd) {
                g_hHoverButton = NULL;
                InvalidateRect(hWnd, NULL, TRUE);
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        case WM_NCDESTROY:
            if (g_hHoverButton == hWnd) {
                g_hHoverButton = NULL;
            }
            RemoveWindowSubclass(hWnd, GlassButtonSubclassProc, 1);
            break;
    }

    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

static void AttachGlassButton(HWND hWnd) {
    if (hWnd != NULL) {
        SetWindowSubclass(hWnd, GlassButtonSubclassProc, 1, 0);
    }
}

static void Utf8ToWide(const char* source, wchar_t* dest, int destCount) {
    if (dest == NULL || destCount <= 0) {
        return;
    }
    dest[0] = L'\0';
    if (source == NULL || source[0] == '\0') {
        return;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, dest, destCount) == 0) {
        MultiByteToWideChar(CP_ACP, 0, source, -1, dest, destCount);
    }
    dest[destCount - 1] = L'\0';
}

static void WideToUtf8(const wchar_t* source, char* dest, int destCount) {
    if (dest == NULL || destCount <= 0) {
        return;
    }
    dest[0] = '\0';
    if (source == NULL || source[0] == L'\0') {
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, source, -1, dest, destCount, NULL, NULL);
    dest[destCount - 1] = '\0';
}

static void SetEditTextUtf8(HWND hWnd, const char* text) {
    wchar_t wide[MAX_PATH];
    Utf8ToWide(text, wide, (int)ARRAYSIZE(wide));
    SetWindowTextW(hWnd, wide);
}

static void GetEditTextUtf8(HWND hWnd, char* dest, int destCount) {
    wchar_t wide[MAX_PATH];
    GetWindowTextW(hWnd, wide, (int)ARRAYSIZE(wide));
    WideToUtf8(wide, dest, destCount);
}

static void ExtractMappingNames(const char* mappingName, char* fromName, size_t fromSize, char* toName, size_t toSize) {
    const char* arrow;
    size_t len;

    if (fromSize > 0) {
        fromName[0] = '\0';
    }
    if (toSize > 0) {
        toName[0] = '\0';
    }
    if (mappingName == NULL) {
        return;
    }

    arrow = strstr(mappingName, "->");
    if (arrow != NULL) {
        len = (size_t)(arrow - mappingName);
        if (fromSize > 0) {
            if (len >= fromSize) {
                len = fromSize - 1;
            }
            memcpy(fromName, mappingName, len);
            fromName[len] = '\0';
        }
        if (toSize > 0) {
            snprintf(toName, toSize, "%s", arrow + 2);
        }
    } else {
        if (fromSize > 0) {
            snprintf(fromName, fromSize, "%s", mappingName);
        }
        if (toSize > 0) {
            snprintf(toName, toSize, "%s", mappingName);
        }
    }
}

typedef struct {
    const char* key;
    const wchar_t* display;
} KeyDisplayEntryW;

static const KeyDisplayEntryW keyDisplayTable[] = {
    {"LEFT", L"← 左方向"}, {"RIGHT", L"→ 右方向"}, {"UP", L"↑ 上方向"}, {"DOWN", L"↓ 下方向"},
    {"HOME", L"Home 行首"}, {"END", L"End 行尾"}, {"PAGEUP", L"PageUp 上翻页"}, {"PAGEDOWN", L"PageDown 下翻页"},
    {"DELETE", L"Delete 删除"}, {"INSERT", L"Insert 插入"}, {"ESCAPE", L"Esc 退出"}, {"ENTER", L"Enter 回车"},
    {"BACKSPACE", L"Backspace 退格"}, {"SPACE", L"Space 空格"}, {"TAB", L"Tab 制表"},
    {"MINUS", L"- 减号"}, {"EQUAL", L"= 等号"},
    {NULL, NULL}
};

static const wchar_t* KeyDisplayNameW(const char* key, wchar_t* fallback, size_t fallbackCount) {
    for (int i = 0; keyDisplayTable[i].key != NULL; i++) {
        if (_stricmp(keyDisplayTable[i].key, key) == 0) {
            return keyDisplayTable[i].display;
        }
    }
    Utf8ToWide(key, fallback, (int)fallbackCount);
    return fallback;
}

bool ShowConfigDialog(HWND hParent) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    if (!InitCommonControlsEx(&icex)) {
        LOG_ERROR("Failed to initialize common controls");
        return false;
    }

    // Make a working copy of config
    const Config* currentConfig = ConfigGet();
    memcpy(&g_workingConfig, currentConfig, sizeof(Config));
    g_needRebuildMappings = false;

    // Show modal dialog
    DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_CONFIG_DIALOG), hParent, ConfigDialogProc);

    return g_needRebuildMappings;
}

static INT_PTR CALLBACK ConfigDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    g_hDlg = hDlg;

    switch (msg) {
        case WM_INITDIALOG:
            OnInitDialog(hDlg);
            return (INT_PTR)TRUE;

        case WM_PAINT:
            PaintConfigDialog(hDlg);
            return (INT_PTR)TRUE;

        case WM_ERASEBKGND: {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hDlg, &rc);
            FillRect(hdc, &rc, g_hBgBrush != NULL ? g_hBgBrush : GetSysColorBrush(COLOR_WINDOW));
            RECT panel = {20, 108, rc.right - 22, rc.bottom - 58};
            FillRoundRect(hdc, &panel, COLOR_PANEL, RGB(205, 226, 244));
            return (INT_PTR)TRUE;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            return (INT_PTR)HandleCtlColor((HWND)lParam, (HDC)wParam, msg);

        case WM_DRAWITEM:
            if (wParam == IDC_TAB) {
                DrawOwnerTab((const DRAWITEMSTRUCT*)lParam);
                return (INT_PTR)TRUE;
            }
            DrawOwnerButton((const DRAWITEMSTRUCT*)lParam);
            return (INT_PTR)TRUE;

        case WM_NCDESTROY:
            CleanupDialogFonts();
            return (INT_PTR)FALSE;

        case WM_NOTIFY:
            if (((NMHDR*)lParam)->hwndFrom == g_hMappingList && ((NMHDR*)lParam)->code == NM_CUSTOMDRAW) {
                return (INT_PTR)DrawMappingListItem((NMLVCUSTOMDRAW*)lParam);
            }
            switch (((NMHDR*)lParam)->code) {
                case TCN_SELCHANGE:
                    OnTabChange(hDlg);
                    break;
            }
            return (INT_PTR)TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    OnOK(hDlg);
                    return (INT_PTR)TRUE;
                case IDCANCEL:
                    OnCancel(hDlg);
                    return (INT_PTR)TRUE;
                case IDC_BROWSE_LOG:
                    OnBrowseDirectory(hDlg, IDC_LOG_PATH);
                    return (INT_PTR)TRUE;
                case IDC_BROWSE_MODEL:
                    OnBrowseDirectory(hDlg, IDC_MODEL_PATH);
                    return (INT_PTR)TRUE;
                case IDC_BTN_ADD:
                    OnAddMapping(hDlg);
                    return (INT_PTR)TRUE;
                case IDC_BTN_EDIT:
                    OnEditMapping(hDlg);
                    return (INT_PTR)TRUE;
                case IDC_BTN_REMOVE:
                    OnRemoveMapping(hDlg);
                    return (INT_PTR)TRUE;
                case IDC_BTN_RESET:
                    OnResetMappings(hDlg);
                    return (INT_PTR)TRUE;
            }
            return (INT_PTR)FALSE;

        case WM_TIMER:
            // Create initial content after dialog is fully shown
            KillTimer(hDlg, 1);
            ClearTabControls(g_currentTab);
            if (g_currentTab == TAB_BASIC) {
                CreateTab1Controls(hDlg);
            } else {
                CreateTab2Controls(hDlg);
            }
            SetWindowPos(g_hTab, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            InvalidateRect(hDlg, NULL, TRUE);
            UpdateWindow(hDlg);
            return (INT_PTR)TRUE;

        default:
            return (INT_PTR)FALSE;
    }
}

static void OnInitDialog(HWND hDlg) {
    g_hTab = GetDlgItem(hDlg, IDC_TAB);
    g_currentTab = TAB_BASIC;
    g_tabControlCount[0] = 0;
    g_tabControlCount[1] = 0;
    InitDialogFonts(hDlg);
    AttachGlassButton(GetDlgItem(hDlg, IDOK));
    AttachGlassButton(GetDlgItem(hDlg, IDCANCEL));

    LONG_PTR style = GetWindowLongPtr(g_hTab, GWL_STYLE);
    SetWindowLongPtr(g_hTab, GWL_STYLE,
                     style | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH | TCS_BUTTONS | TCS_FLATBUTTONS);
    MoveWindow(g_hTab, 24, 68, 300, 34, TRUE);
    SetWindowPos(g_hTab, NULL, 24, 68, 300, 34, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    TabCtrl_SetItemSize(g_hTab, 144, 30);

    TCITEMW tie;
    ZeroMemory(&tie, sizeof(tie));
    tie.mask = TCIF_TEXT;
    for (int i = 0; i < TAB_COUNT; i++) {
        tie.pszText = (LPWSTR)tabTitles[i];
        SendMessageW(g_hTab, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&tie);
    }

    TabCtrl_SetCurSel(g_hTab, g_currentTab);

    SetTimer(hDlg, 1, 10, NULL);
}

static void OnTabChange(HWND hDlg) {
    int newTab = TabCtrl_GetCurSel(g_hTab);
    if (newTab != g_currentTab) {
        if (g_currentTab == TAB_BASIC) {
            CapturePathEdits();
        }
        ClearTabControls(g_currentTab);
        g_currentTab = newTab;
        if (newTab == TAB_BASIC) {
            CreateTab1Controls(hDlg);
        } else {
            CreateTab2Controls(hDlg);
        }
        SetWindowPos(g_hTab, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        InvalidateRect(hDlg, NULL, TRUE);
    }
}

static void CreateTab1Controls(HWND hDlg) {
    RECT rcTab;
    int left;
    int right;
    int top;
    int labelW = 82;
    int browseW = 88;
    int gap = 12;
    int editH = 28;
    int rowGap = 44;
    int editX;
    int editW;

    GetClientRect(hDlg, &rcTab);
    rcTab.left = 40;
    rcTab.top = 126;
    rcTab.right -= 40;
    rcTab.bottom -= 92;

    left = rcTab.left;
    right = rcTab.right;
    top = rcTab.top;
    editX = left + labelW;
    editW = right - editX - browseW - gap;

    HWND hStatic = CreateWindowExW(0, L"STATIC", L"路径与存储", WS_CHILD | SS_LEFT | WS_VISIBLE,
        left, top, 240, 20, hDlg, NULL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, hStatic);
    ApplySectionFont(hStatic);

    hStatic = CreateWindowExW(0, L"STATIC", L"日志和模型目录会写入当前用户配置。", WS_CHILD | SS_LEFT | WS_VISIBLE,
        left, top + 21, right - left, 18, hDlg, NULL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, hStatic);

    top += 48;

    hStatic = CreateWindowExW(0, L"STATIC", L"日志目录", WS_CHILD | SS_LEFT | WS_VISIBLE,
        left, top + 4, labelW - 12, 20, hDlg, NULL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, hStatic);

    g_hLogEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_CHILD | ES_AUTOHSCROLL | WS_VISIBLE,
        editX, top, editW, editH, hDlg, (HMENU)IDC_LOG_PATH, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, g_hLogEdit);
    SetEditTextUtf8(g_hLogEdit, g_workingConfig.logDirPath);

    HWND hBtn = CreateWindowExW(0, L"BUTTON", L"浏览...", WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW | WS_CHILD | WS_VISIBLE,
        right - browseW, top - 1, browseW, editH + 2, hDlg, (HMENU)IDC_BROWSE_LOG, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, hBtn);
    AttachGlassButton(hBtn);

    top += rowGap;

    hStatic = CreateWindowExW(0, L"STATIC", L"模型目录", WS_CHILD | SS_LEFT | WS_VISIBLE,
        left, top + 4, labelW - 12, 20, hDlg, NULL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, hStatic);

    g_hModelEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_CHILD | ES_AUTOHSCROLL | WS_VISIBLE,
        editX, top, editW, editH, hDlg, (HMENU)IDC_MODEL_PATH, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, g_hModelEdit);
    SetEditTextUtf8(g_hModelEdit, g_workingConfig.modelDirPath);

    hBtn = CreateWindowExW(0, L"BUTTON", L"浏览...", WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW | WS_CHILD | WS_VISIBLE,
        right - browseW, top - 1, browseW, editH + 2, hDlg, (HMENU)IDC_BROWSE_MODEL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, hBtn);
    AttachGlassButton(hBtn);

    top += rowGap + 4;

    hStatic = CreateWindowExW(0, L"STATIC", L"建议使用绝对路径。修改日志或模型目录后，重启程序即可写入新位置。",
        WS_CHILD | SS_LEFT | WS_VISIBLE, editX, top, right - editX, 34, hDlg, NULL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_BASIC, hStatic);

    for (int i = 0; i < g_tabControlCount[TAB_BASIC]; i++) {
        SetWindowPos(g_tabControls[TAB_BASIC][i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    LOG_DEBUG("CreateTab1Controls: created %d controls", g_tabControlCount[TAB_BASIC]);
    InvalidateRect(hDlg, &rcTab, TRUE);
    UpdateWindow(hDlg);
}

static void CreateTab2Controls(HWND hDlg) {
    RECT rcTab;
    int left;
    int right;
    int top;
    int bottom;
    int btnW = 104;
    int btnH = 28;
    int btnGap = 10;
    int btnX;
    int btnY;
    int listW;
    int listH;

    GetClientRect(hDlg, &rcTab);
    rcTab.left = 40;
    rcTab.top = 126;
    rcTab.right -= 40;
    rcTab.bottom -= 92;

    left = rcTab.left;
    right = rcTab.right;
    top = rcTab.top;
    bottom = rcTab.bottom;
    btnX = right - btnW;
    btnY = top + 48;
    listW = btnX - left - 14;
    listH = bottom - (top + 48);

    HWND hStatic = CreateWindowExW(0, L"STATIC", L"CapsLock 快捷映射", WS_CHILD | SS_LEFT | WS_VISIBLE,
        left, top, 220, 20, hDlg, NULL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_MAPPING, hStatic);
    ApplySectionFont(hStatic);

    hStatic = CreateWindowExW(0, L"STATIC", L"按住 CapsLock，再按触发键，即可发送目标按键。",
        WS_CHILD | SS_LEFT | WS_VISIBLE, left, top + 21, right - left, 18, hDlg, NULL, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_MAPPING, hStatic);

    DWORD listStyle = LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    g_hMappingList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL, listStyle,
        left, top + 48, listW, listH, hDlg, (HMENU)IDC_MAPPING_LIST, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_MAPPING, g_hMappingList);
    ListView_SetExtendedListViewStyle(g_hMappingList,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    ListView_SetBkColor(g_hMappingList, RGB(250, 253, 255));
    ListView_SetTextBkColor(g_hMappingList, RGB(250, 253, 255));
    ListView_SetTextColor(g_hMappingList, COLOR_TEXT);

    LVCOLUMNW lvc;
    ZeroMemory(&lvc, sizeof(lvc));
    lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;

    lvc.pszText = L"触发键";
    lvc.cx = 86;
    lvc.fmt = LVCFMT_LEFT;
    SendMessageW(g_hMappingList, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

    lvc.pszText = L"输出键";
    lvc.cx = 104;
    lvc.fmt = LVCFMT_LEFT;
    SendMessageW(g_hMappingList, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

    lvc.pszText = L"动作说明";
    lvc.cx = listW - 204;
    if (lvc.cx < 150) {
        lvc.cx = 150;
    }
    lvc.fmt = LVCFMT_LEFT;
    SendMessageW(g_hMappingList, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);

    PopulateMappingsList(g_hMappingList);

    HWND hBtn = CreateWindowExW(0, L"BUTTON", L"新增映射", WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW | WS_CHILD | WS_VISIBLE,
        btnX, btnY, btnW, btnH, hDlg, (HMENU)IDC_BTN_ADD, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_MAPPING, hBtn);
    AttachGlassButton(hBtn);
    btnY += btnH + btnGap;

    hBtn = CreateWindowExW(0, L"BUTTON", L"编辑映射", WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW | WS_CHILD | WS_VISIBLE,
        btnX, btnY, btnW, btnH, hDlg, (HMENU)IDC_BTN_EDIT, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_MAPPING, hBtn);
    AttachGlassButton(hBtn);
    btnY += btnH + btnGap;

    hBtn = CreateWindowExW(0, L"BUTTON", L"删除映射", WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW | WS_CHILD | WS_VISIBLE,
        btnX, btnY, btnW, btnH, hDlg, (HMENU)IDC_BTN_REMOVE, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_MAPPING, hBtn);
    AttachGlassButton(hBtn);
    btnY += btnH + btnGap;

    hBtn = CreateWindowExW(0, L"BUTTON", L"恢复默认", WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW | WS_CHILD | WS_VISIBLE,
        btnX, btnY, btnW, btnH, hDlg, (HMENU)IDC_BTN_RESET, GetModuleHandle(NULL), NULL);
    TrackTabControl(TAB_MAPPING, hBtn);
    AttachGlassButton(hBtn);

    for (int i = 0; i < g_tabControlCount[TAB_MAPPING]; i++) {
        SetWindowPos(g_tabControls[TAB_MAPPING][i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    LOG_DEBUG("CreateTab2Controls: created %d controls", g_tabControlCount[TAB_MAPPING]);
    InvalidateRect(hDlg, &rcTab, TRUE);
    UpdateWindow(hDlg);
}

static void PopulateMappingsList(HWND hList) {
    int count = ListView_GetItemCount(hList);
    for (int i = count - 1; i >= 0; i--) {
        ListView_DeleteItem(hList, i);
    }

    int mappingCount;
    const KeyMapping* mappings = KeymapGetAll(&mappingCount);

    for (int i = 0; i < mappingCount; i++) {
        LVITEMW lvi;
        char fromName[64];
        char toName[64];
        wchar_t fromText[64];
        wchar_t toText[64];
        wchar_t fromDisplayFallback[64];
        wchar_t toDisplayFallback[64];
        wchar_t desc[192];
        const wchar_t* fromDisplay;
        const wchar_t* toDisplay;

        ExtractMappingNames(mappings[i].name, fromName, sizeof(fromName), toName, sizeof(toName));
        Utf8ToWide(fromName, fromText, (int)ARRAYSIZE(fromText));
        Utf8ToWide(toName, toText, (int)ARRAYSIZE(toText));
        fromDisplay = KeyDisplayNameW(fromName, fromDisplayFallback, ARRAYSIZE(fromDisplayFallback));
        toDisplay = KeyDisplayNameW(toName, toDisplayFallback, ARRAYSIZE(toDisplayFallback));
        swprintf(desc, ARRAYSIZE(desc), L"CapsLock + %ls  发送  %ls", fromDisplay, toDisplay);

        ZeroMemory(&lvi, sizeof(lvi));
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = fromText;
        SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

        ZeroMemory(&lvi, sizeof(lvi));
        lvi.iSubItem = 1;
        lvi.pszText = toText;
        SendMessageW(hList, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&lvi);

        lvi.iSubItem = 2;
        lvi.pszText = desc;
        SendMessageW(hList, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&lvi);
    }
}

static void ClearTabControls(int tabIndex) {
    for (int i = 0; i < g_tabControlCount[tabIndex]; i++) {
        DestroyWindow(g_tabControls[tabIndex][i]);
    }
    g_tabControlCount[tabIndex] = 0;

    if (tabIndex == TAB_BASIC) {
        g_hLogEdit = NULL;
        g_hModelEdit = NULL;
    } else if (tabIndex == TAB_MAPPING) {
        g_hMappingList = NULL;
    }
}

static void CapturePathEdits(void) {
    if (g_hLogEdit != NULL && IsWindow(g_hLogEdit)) {
        GetEditTextUtf8(g_hLogEdit, g_workingConfig.logDirPath, sizeof(g_workingConfig.logDirPath));
    }
    if (g_hModelEdit != NULL && IsWindow(g_hModelEdit)) {
        GetEditTextUtf8(g_hModelEdit, g_workingConfig.modelDirPath, sizeof(g_workingConfig.modelDirPath));
    }
}

static void OnBrowseDirectory(HWND hDlg, int editId) {
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hDlg;
    bi.lpszTitle = (editId == IDC_LOG_PATH) ? L"选择日志目录" : L"选择模型目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH];
        SHGetPathFromIDListW(pidl, path);

        HWND hEdit = GetDlgItem(hDlg, editId);
        SetWindowTextW(hEdit, path);

        CoTaskMemFree(pidl);
    }
}

static void OnAddMapping(HWND hDlg) {
    if (AddOrEditMapping(hDlg, false, 0)) {
        PopulateMappingsList(g_hMappingList);
        g_needRebuildMappings = true;
    }
}

static void OnEditMapping(HWND hDlg) {
    int selected = ListView_GetSelectionMark(g_hMappingList);
    if (selected < 0) {
        MessageBoxW(hDlg, L"请先选择一条映射。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (AddOrEditMapping(hDlg, true, selected)) {
        PopulateMappingsList(g_hMappingList);
        g_needRebuildMappings = true;
    }
}

static void OnRemoveMapping(HWND hDlg) {
    int selected = ListView_GetSelectionMark(g_hMappingList);
    if (selected < 0) {
        MessageBoxW(hDlg, L"请先选择一条映射。", L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (MessageBoxW(hDlg, L"确定要删除这条映射吗？", L"确认删除", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        // We need to rebuild the entire mapping list from scratch because we don't have direct access
        // So for simplicity, we'll just get all current mappings and filter out the selected one
        int mappingCount;
        const KeyMapping* currentMappings = KeymapGetAll(&mappingCount);

        if (mappingCount <= 1) {
            MessageBoxW(hDlg, L"至少需要保留一条映射，不能全部删除。", L"警告", MB_OK | MB_ICONWARNING);
            return;
        }

        KeymapClear();

        for (int i = 0; i < mappingCount; i++) {
            if (i != selected) {
                char name[128];
                snprintf(name, sizeof(name), "%s", currentMappings[i].name);
                KeymapAddMapping(currentMappings[i].scanCode, currentMappings[i].targetVk, strdup(name));
            }
        }

        PopulateMappingsList(g_hMappingList);
        g_needRebuildMappings = true;
    }
}

static void OnResetMappings(HWND hDlg) {
    if (MessageBoxW(hDlg, L"确定要恢复默认映射吗？\n\n这会丢失当前所有自定义修改。", L"确认恢复", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        KeymapResetToDefaults();
        PopulateMappingsList(g_hMappingList);
        g_needRebuildMappings = true;
    }
}

// Forward declaration for mapping dialog proc
static INT_PTR CALLBACK MappingDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
// Global to pass parameters since modal dialog - it's modal so this is safe
static char g_mappingFromText[64];
static char g_mappingToText[64];

// Local lookup table for key name conversion (duplicate of config.c to avoid static linking issues)
typedef struct {
    const char* name;
    WORD scanCode;
    UINT vkCode;
} LocalKeyNameEntry;

static const LocalKeyNameEntry localKeyNameTable[] = {
    {"A", 0x1E, 0x41}, {"B", 0x30, 0x42}, {"C", 0x2E, 0x43}, {"D", 0x20, 0x44},
    {"E", 0x12, 0x45}, {"F", 0x21, 0x46}, {"G", 0x22, 0x47}, {"H", 0x23, 0x48},
    {"I", 0x17, 0x49}, {"J", 0x24, 0x4A}, {"K", 0x25, 0x4B}, {"L", 0x26, 0x4C},
    {"M", 0x32, 0x4D}, {"N", 0x31, 0x4E}, {"O", 0x18, 0x4F}, {"P", 0x19, 0x50},
    {"Q", 0x10, 0x51}, {"R", 0x13, 0x52}, {"S", 0x1F, 0x53}, {"T", 0x14, 0x54},
    {"U", 0x16, 0x55}, {"V", 0x2F, 0x56}, {"W", 0x11, 0x57}, {"X", 0x2D, 0x58},
    {"Y", 0x15, 0x59}, {"Z", 0x2C, 0x5A},
    {"1", 0x02, 0x31}, {"2", 0x03, 0x32}, {"3", 0x04, 0x33}, {"4", 0x05, 0x34},
    {"5", 0x06, 0x35}, {"6", 0x07, 0x36}, {"7", 0x08, 0x37}, {"8", 0x09, 0x38},
    {"9", 0x0A, 0x39}, {"0", 0x0B, 0x30},
    {"MINUS", 0x0C, 0xBD}, {"EQUAL", 0x0D, 0xBB},
    {"HOME", 0xE047, VK_HOME}, {"END", 0xE04F, VK_END},
    {"PAGEUP", 0xE049, VK_PRIOR}, {"PAGEDOWN", 0xE051, VK_NEXT},
    {"UP", 0xE048, VK_UP}, {"DOWN", 0xe050, VK_DOWN},
    {"LEFT", 0xe04b, VK_LEFT}, {"RIGHT", 0xe04d, VK_RIGHT},
    {NULL, 0, 0}
};

static WORD LocalNameToScanCode(const char* name) {
    for (int i = 0; localKeyNameTable[i].name != NULL; i++) {
        if (_stricmp(localKeyNameTable[i].name, name) == 0) {
            return localKeyNameTable[i].scanCode;
        }
    }
    return 0;
}

static UINT LocalNameToVkCode(const char* name) {
    for (int i = 0; localKeyNameTable[i].name != NULL; i++) {
        if (_stricmp(localKeyNameTable[i].name, name) == 0) {
            return localKeyNameTable[i].vkCode;
        }
    }
    return 0;
}

static INT_PTR CALLBACK MappingDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowTextW(hDlg, L"按键映射");
            AttachGlassButton(GetDlgItem(hDlg, IDOK));
            AttachGlassButton(GetDlgItem(hDlg, IDCANCEL));
            if (strlen(g_mappingFromText) > 0) {
                SetEditTextUtf8(GetDlgItem(hDlg, IDC_FROM_KEY), g_mappingFromText);
                SetEditTextUtf8(GetDlgItem(hDlg, IDC_TO_KEY), g_mappingToText);
            }
            return (INT_PTR)TRUE;
        }
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN:
            return (INT_PTR)HandleCtlColor((HWND)lParam, (HDC)wParam, msg);
        case WM_DRAWITEM:
            DrawOwnerButton((const DRAWITEMSTRUCT*)lParam);
            return (INT_PTR)TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                GetEditTextUtf8(GetDlgItem(hDlg, IDC_FROM_KEY), g_mappingFromText, sizeof(g_mappingFromText));
                GetEditTextUtf8(GetDlgItem(hDlg, IDC_TO_KEY), g_mappingToText, sizeof(g_mappingToText));

                if (strlen(g_mappingFromText) == 0 || strlen(g_mappingToText) == 0) {
                    MessageBoxW(hDlg, L"请填写触发键和输出键。", L"输入错误", MB_OK | MB_ICONWARNING);
                    return (INT_PTR)TRUE;
                }

                EndDialog(hDlg, IDOK);
                return (INT_PTR)TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return (INT_PTR)TRUE;
            }
            return (INT_PTR)FALSE;
        default:
            return (INT_PTR)FALSE;
    }
}

static bool AddOrEditMapping(HWND hParent, bool isEdit, int selectedIndex) {
    // Get current mappings for edit mode
    strcpy(g_mappingFromText, "");
    strcpy(g_mappingToText, "");

    if (isEdit) {
        int mappingCount;
        const KeyMapping* mappings = KeymapGetAll(&mappingCount);
        ExtractMappingNames(mappings[selectedIndex].name,
                            g_mappingFromText, sizeof(g_mappingFromText),
                            g_mappingToText, sizeof(g_mappingToText));
    }

    // Show the dialog
    INT_PTR result = DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_MAPPING_DIALOG), hParent, MappingDialogProc);

    if (result != IDOK) {
        return false;
    }

    // Find from scan code and to vk code
    WORD scanCode = LocalNameToScanCode(g_mappingFromText);
    UINT targetVk = LocalNameToVkCode(g_mappingToText);

    if (scanCode == 0 || targetVk == 0) {
        MessageBoxW(hParent, L"按键名称无效。\n\n可用示例：H、J、K、L、LEFT、RIGHT、HOME、END、F1。", L"按键名称错误", MB_OK | MB_ICONERROR);
        return false;
    }

    // Check for duplicate from scan code
    if (!isEdit) {
        const KeyMapping* existing = KeymapFindByScanCode(scanCode);
        if (existing != NULL) {
            MessageBoxW(hParent, L"这个触发键已经存在映射，请换一个键。", L"重复映射", MB_OK | MB_ICONWARNING);
            return false;
        }
    }

    char mappingName[128];
    snprintf(mappingName, sizeof(mappingName), "%s->%s", g_mappingFromText, g_mappingToText);

    if (isEdit) {
        // Remove the selected one and add new
        int mappingCount;
        const KeyMapping* currentMappings = KeymapGetAll(&mappingCount);
        KeymapClear();

        for (int i = 0; i < mappingCount; i++) {
            if (i != selectedIndex) {
                KeymapAddMapping(currentMappings[i].scanCode, currentMappings[i].targetVk, strdup(currentMappings[i].name));
            }
        }
        KeymapAddMapping(scanCode, targetVk, strdup(mappingName));
    } else {
        // Add new
        KeymapAddMapping(scanCode, targetVk, strdup(mappingName));
    }

    return true;
}

static void OnOK(HWND hDlg) {
    CapturePathEdits();

    // Update the config
    ConfigSet(&g_workingConfig);

    // If mappings changed, they've already been updated in-place
    // Save the config to file
    ConfigSave(NULL);

    LOG_INFO("Configuration saved from dialog");
    EndDialog(hDlg, IDOK);
}

static void OnCancel(HWND hDlg) {
    // Just close, no changes saved
    EndDialog(hDlg, IDCANCEL);
}
