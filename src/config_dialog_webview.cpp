#include "config_dialog.h"

extern "C" {
#include "config.h"
#include "keymap.h"
#include "logger.h"
}

#include <windows.h>
#include <shlobj.h>
#include <objbase.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "WebView2.h"

struct MappingItem {
    std::wstring from;
    std::wstring to;
};

struct PreparedMapping {
    WORD scanCode;
    UINT targetVk;
    std::string name;
};

struct ConfigWebViewWindow;

static const wchar_t* kWindowClass = L"PowerCapslockConfigWebView";
static HMODULE g_webView2Loader = NULL;

typedef HRESULT(STDAPICALLTYPE* CreateCoreWebView2EnvironmentWithOptionsFn)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);

static std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (size <= 0) {
        size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, NULL, 0);
        if (size <= 0) {
            return L"";
        }
        std::wstring result(size - 1, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, result.data(), size);
        return result;
    }
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

static std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return "";
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
    if (size <= 0) {
        return "";
    }
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, NULL, NULL);
    return result;
}

static std::wstring GetExeDir(void) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash != NULL) {
        *slash = L'\0';
    }
    return path;
}

static std::wstring GetEnvString(const wchar_t* name) {
    DWORD size = GetEnvironmentVariableW(name, NULL, 0);
    if (size == 0) {
        return L"";
    }
    std::wstring value(size, L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), size);
    if (written == 0 || written >= size) {
        return L"";
    }
    value.resize(written);
    return value;
}

static std::wstring ReadUtf8File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return L"<!doctype html><meta charset=\"utf-8\"><body style=\"font-family:Microsoft YaHei UI;padding:32px\">配置页面资源加载失败。</body>";
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0 || fileSize.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(file);
        return L"<!doctype html><meta charset=\"utf-8\"><body style=\"font-family:Microsoft YaHei UI;padding:32px\">配置页面资源大小异常。</body>";
    }

    std::string bytes((size_t)fileSize.QuadPart, '\0');
    DWORD totalRead = 0;
    if (!bytes.empty() && !ReadFile(file, bytes.data(), (DWORD)bytes.size(), &totalRead, NULL)) {
        CloseHandle(file);
        return L"<!doctype html><meta charset=\"utf-8\"><body style=\"font-family:Microsoft YaHei UI;padding:32px\">配置页面资源读取失败。</body>";
    }
    CloseHandle(file);
    bytes.resize(totalRead);
    return Utf8ToWide(bytes);
}

static std::wstring JsonEscape(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size() + 8);
    for (wchar_t c : text) {
        switch (c) {
            case L'\\': result += L"\\\\"; break;
            case L'"': result += L"\\\""; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

static std::wstring JsonReadStringAt(const std::wstring& json, size_t quote) {
    std::wstring value;
    bool escaped = false;
    for (size_t i = quote + 1; i < json.size(); i++) {
        wchar_t c = json[i];
        if (escaped) {
            switch (c) {
                case L'n': value += L'\n'; break;
                case L'r': value += L'\r'; break;
                case L't': value += L'\t'; break;
                default: value += c; break;
            }
            escaped = false;
            continue;
        }
        if (c == L'\\') {
            escaped = true;
            continue;
        }
        if (c == L'"') {
            break;
        }
        value += c;
    }
    return value;
}

static std::wstring JsonGetString(const std::wstring& json, const wchar_t* key) {
    std::wstring pattern = L"\"";
    pattern += key;
    pattern += L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos) {
        return L"";
    }
    size_t colon = json.find(L':', keyPos + pattern.size());
    if (colon == std::wstring::npos) {
        return L"";
    }
    size_t quote = json.find(L'"', colon + 1);
    if (quote == std::wstring::npos) {
        return L"";
    }
    return JsonReadStringAt(json, quote);
}

static std::vector<MappingItem> JsonGetMappings(const std::wstring& json) {
    std::vector<MappingItem> mappings;
    size_t keyPos = json.find(L"\"mappings\"");
    if (keyPos == std::wstring::npos) {
        return mappings;
    }
    size_t arrayStart = json.find(L'[', keyPos);
    size_t arrayEnd = json.find(L']', arrayStart);
    if (arrayStart == std::wstring::npos || arrayEnd == std::wstring::npos) {
        return mappings;
    }

    size_t cursor = arrayStart;
    while (cursor < arrayEnd) {
        size_t objectStart = json.find(L'{', cursor);
        if (objectStart == std::wstring::npos || objectStart > arrayEnd) {
            break;
        }
        size_t objectEnd = json.find(L'}', objectStart);
        if (objectEnd == std::wstring::npos || objectEnd > arrayEnd) {
            break;
        }
        std::wstring object = json.substr(objectStart, objectEnd - objectStart + 1);
        MappingItem item = {JsonGetString(object, L"from"), JsonGetString(object, L"to")};
        if (!item.from.empty() && !item.to.empty()) {
            mappings.push_back(item);
        }
        cursor = objectEnd + 1;
    }
    return mappings;
}

static std::wstring UpperAscii(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') ? (wchar_t)(c - L'a' + L'A') : c;
    });
    return value;
}

static void ExtractMappingName(const char* name, std::wstring& from, std::wstring& to) {
    std::string text = name != NULL ? name : "";
    size_t arrow = text.find("->");
    if (arrow == std::string::npos) {
        from = UpperAscii(Utf8ToWide(text));
        to = from;
        return;
    }
    from = UpperAscii(Utf8ToWide(text.substr(0, arrow)));
    to = UpperAscii(Utf8ToWide(text.substr(arrow + 2)));
}

static std::vector<MappingItem> CurrentMappings(void) {
    std::vector<MappingItem> result;
    int count = 0;
    const KeyMapping* mappings = KeymapGetAll(&count);
    for (int i = 0; i < count; i++) {
        std::wstring from;
        std::wstring to;
        ExtractMappingName(mappings[i].name, from, to);
        result.push_back({from, to});
    }
    return result;
}

static std::wstring MappingSignature(const std::vector<MappingItem>& mappings) {
    std::wstring result;
    for (const MappingItem& item : mappings) {
        result += UpperAscii(item.from);
        result += L"->";
        result += UpperAscii(item.to);
        result += L";";
    }
    return result;
}

static std::wstring BuildInitJson(void) {
    const Config* config = ConfigGet();
    std::wstring json = L"{\"type\":\"init\",\"logPath\":\"";
    json += JsonEscape(Utf8ToWide(config->logDirPath));
    json += L"\",\"modelPath\":\"";
    json += JsonEscape(Utf8ToWide(config->modelDirPath));
    json += L"\",\"mappings\":[";

    std::vector<MappingItem> mappings = CurrentMappings();
    for (size_t i = 0; i < mappings.size(); i++) {
        if (i > 0) {
            json += L",";
        }
        json += L"{\"from\":\"";
        json += JsonEscape(UpperAscii(mappings[i].from));
        json += L"\",\"to\":\"";
        json += JsonEscape(UpperAscii(mappings[i].to));
        json += L"\"}";
    }
    json += L"]}";
    return json;
}

static void CopyPathToConfig(const std::wstring& source, char* dest, size_t destSize) {
    std::string utf8 = WideToUtf8(source);
    snprintf(dest, destSize, "%s", utf8.c_str());
}

static std::wstring BrowseFolder(HWND owner, const wchar_t* title) {
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl == NULL) {
        return L"";
    }
    wchar_t path[MAX_PATH];
    path[0] = L'\0';
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return path;
}

struct ConfigWebViewWindow {
    HWND hwnd = NULL;
    HWND parent = NULL;
    ICoreWebView2Controller* controller = NULL;
    ICoreWebView2* webview = NULL;
    EventRegistrationToken messageToken = {};
    std::wstring html;
    std::wstring initialMappingSignature;
    std::wstring testAction;
    std::wstring testBrowseLogPath;
    std::wstring testBrowseModelPath;
    bool saved = false;
    bool mappingsChanged = false;
    bool testAutomationStarted = false;

    void Resize(void) {
        if (controller == NULL || hwnd == NULL) {
            return;
        }
        RECT bounds;
        GetClientRect(hwnd, &bounds);
        controller->put_Bounds(bounds);
    }

    void PostJson(const std::wstring& json) {
        if (webview != NULL) {
            webview->PostWebMessageAsJson(json.c_str());
        }
    }

    void PostError(const wchar_t* message) {
        std::wstring json = L"{\"type\":\"error\",\"message\":\"";
        json += JsonEscape(message);
        json += L"\"}";
        PostJson(json);
    }

    void MaybeRunTestAutomation(void) {
        if (testAction.empty() || testAutomationStarted || webview == NULL) {
            return;
        }
        testAutomationStarted = true;

        std::wstring script =
            std::wstring(L"(function(){\n") +
            L"const action=\"" + JsonEscape(testAction) + L"\";\n" +
            L"const logPath=\"" + JsonEscape(testBrowseLogPath) + L"\";\n" +
            L"const modelPath=\"" + JsonEscape(testBrowseModelPath) + L"\";\n" +
            L"function fail(message){try{chrome.webview.postMessage(JSON.stringify({type:'test-error',message:String(message)}));}catch(e){}}\n" +
            L"function by(id){const el=document.getElementById(id);if(!el)throw new Error('missing '+id);return el;}\n" +
            L"function later(fn){setTimeout(function(){try{fn();}catch(e){fail(e&&e.message?e.message:e);}},120);}\n" +
            L"function rows(){return Array.from(document.querySelectorAll('#mappingRows tr'));}\n" +
            L"function rowFor(from){return rows().find(row=>row.cells[0].textContent.trim()===from);}\n" +
            L"function hasMapping(from,to){return rows().some(row=>row.cells[0].textContent.trim()===from&&row.cells[1].textContent.trim()===to);}\n" +
            L"function mappingTab(){document.querySelector('[data-tab=\"mappings\"]').click();}\n" +
            L"function waitValue(id,expected,next,start){try{if(!expected||by(id).value===expected){later(next);return;}if(Date.now()-start>3000)throw new Error(id+' did not receive browse path');setTimeout(function(){waitValue(id,expected,next,start);},60);}catch(e){fail(e&&e.message?e.message:e);}}\n" +
            L"function addMapping(next){by('addMapping').click();later(function(){by('fromKey').value='Q';by('toKey').value='B';by('confirmMapping').click();later(function(){if(!hasMapping('Q','B'))throw new Error('Q->B was not added');next();});});}\n" +
            L"function editMapping(next){const row=rowFor('Q');if(!row)throw new Error('Q mapping missing before edit');row.click();later(function(){by('editMapping').click();later(function(){by('fromKey').value='Q';by('toKey').value='C';by('confirmMapping').click();later(function(){if(!hasMapping('Q','C'))throw new Error('Q->C was not edited');next();});});});}\n" +
            L"function deleteMapping(next){const row=rowFor('Q');if(!row)throw new Error('Q mapping missing before delete');row.click();later(function(){by('removeMapping').click();later(function(){if(rowFor('Q'))throw new Error('Q mapping was not deleted');next();});});}\n" +
            L"function save(){later(function(){by('save').click();});}\n" +
            L"function run(){if(action==='add'){by('browseLog').click();waitValue('logPath',logPath,function(){by('browseModel').click();waitValue('modelPath',modelPath,function(){mappingTab();later(function(){addMapping(save);});},Date.now());},Date.now());return;}mappingTab();later(function(){if(action==='edit')editMapping(save);else if(action==='delete')deleteMapping(save);else throw new Error('unknown action '+action);});}\n" +
            L"function waitReady(){try{if(!by('mappingRows').children.length){setTimeout(waitReady,60);return;}run();}catch(e){fail(e&&e.message?e.message:e);}}\n" +
            L"waitReady();\n" +
            L"})();";
        webview->ExecuteScript(script.c_str(), NULL);
    }

    void ReleaseWebView(void) {
        if (webview != NULL) {
            if (messageToken.value != 0) {
                webview->remove_WebMessageReceived(messageToken);
            }
            webview->Release();
            webview = NULL;
        }
        if (controller != NULL) {
            controller->Close();
            controller->Release();
            controller = NULL;
        }
    }
};

class HandlerBase {
public:
    HandlerBase() : refCount(1) {}
    virtual ~HandlerBase() {}

    ULONG STDMETHODCALLTYPE AddRefImpl(void) {
        return (ULONG)InterlockedIncrement(&refCount);
    }

    ULONG STDMETHODCALLTYPE ReleaseImpl(void) {
        ULONG count = (ULONG)InterlockedDecrement(&refCount);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT QueryInterfaceImpl(REFIID riid, void** ppv, IUnknown* self, const IID& iid) {
        if (ppv == NULL) {
            return E_POINTER;
        }
        *ppv = NULL;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid)) {
            *ppv = self;
            self->AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

private:
    volatile LONG refCount;
};

class WebMessageHandler : public ICoreWebView2WebMessageReceivedEventHandler, public HandlerBase {
public:
    explicit WebMessageHandler(ConfigWebViewWindow* owner) : owner(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        return QueryInterfaceImpl(riid, ppv, static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this),
                                  IID_ICoreWebView2WebMessageReceivedEventHandler);
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return AddRefImpl(); }
    ULONG STDMETHODCALLTYPE Release(void) override { return ReleaseImpl(); }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR raw = NULL;
        if (FAILED(args->TryGetWebMessageAsString(&raw)) || raw == NULL) {
            return S_OK;
        }
        std::wstring message(raw);
        CoTaskMemFree(raw);

        std::wstring type = JsonGetString(message, L"type");
        if (type == L"ready") {
            owner->PostJson(BuildInitJson());
            owner->MaybeRunTestAutomation();
            return S_OK;
        }
        if (type == L"test-error") {
            DestroyWindow(owner->hwnd);
            return S_OK;
        }
        if (type == L"cancel") {
            DestroyWindow(owner->hwnd);
            return S_OK;
        }
        if (type == L"browse") {
            std::wstring target = JsonGetString(message, L"target");
            std::wstring path;
            if (target == L"log" && !owner->testBrowseLogPath.empty()) {
                path = owner->testBrowseLogPath;
            } else if (target == L"model" && !owner->testBrowseModelPath.empty()) {
                path = owner->testBrowseModelPath;
            } else {
                path = BrowseFolder(owner->hwnd, target == L"log" ? L"选择日志目录" : L"选择模型目录");
            }
            if (!path.empty()) {
                std::wstring json = L"{\"type\":\"path\",\"target\":\"";
                json += JsonEscape(target);
                json += L"\",\"path\":\"";
                json += JsonEscape(path);
                json += L"\"}";
                owner->PostJson(json);
            }
            return S_OK;
        }
        if (type == L"save") {
            Save(message);
            return S_OK;
        }
        return S_OK;
    }

private:
    ConfigWebViewWindow* owner;

    void Save(const std::wstring& message) {
        std::wstring logPath = JsonGetString(message, L"logPath");
        std::wstring modelPath = JsonGetString(message, L"modelPath");
        std::vector<MappingItem> mappings = JsonGetMappings(message);
        if (mappings.empty()) {
            owner->PostError(L"至少需要保留一条映射。");
            return;
        }

        std::vector<PreparedMapping> prepared;
        std::set<WORD> usedScanCodes;
        for (const MappingItem& item : mappings) {
            std::string from = WideToUtf8(UpperAscii(item.from));
            std::string to = WideToUtf8(UpperAscii(item.to));
            WORD scanCode = ConfigKeyNameToScanCode(from.c_str());
            UINT targetVk = ConfigKeyNameToVkCode(to.c_str());
            if (scanCode == 0 || targetVk == 0) {
                owner->PostError(L"按键名称无效，请检查映射表。");
                return;
            }
            if (usedScanCodes.find(scanCode) != usedScanCodes.end()) {
                owner->PostError(L"每个原始按键只能配置一次，请删除重复映射。");
                return;
            }
            usedScanCodes.insert(scanCode);
            std::string name = from + "->" + to;
            prepared.push_back({scanCode, targetVk, name});
        }

        Config updated = *ConfigGet();
        CopyPathToConfig(logPath, updated.logDirPath, sizeof(updated.logDirPath));
        CopyPathToConfig(modelPath, updated.modelDirPath, sizeof(updated.modelDirPath));
        ConfigSet(&updated);

        KeymapClear();
        for (const PreparedMapping& item : prepared) {
            KeymapAddMapping(item.scanCode, item.targetVk, _strdup(item.name.c_str()));
        }

        if (!ConfigSave(NULL)) {
            owner->PostError(L"配置保存失败，请检查目录权限。");
            return;
        }

        owner->saved = true;
        owner->mappingsChanged = MappingSignature(mappings) != owner->initialMappingSignature;
        DestroyWindow(owner->hwnd);
    }
};

class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, public HandlerBase {
public:
    explicit ControllerCompletedHandler(ConfigWebViewWindow* owner) : owner(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        return QueryInterfaceImpl(riid, ppv, static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this),
                                  IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler);
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return AddRefImpl(); }
    ULONG STDMETHODCALLTYPE Release(void) override { return ReleaseImpl(); }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller* result) override {
        if (FAILED(errorCode) || result == NULL) {
            MessageBoxW(owner->hwnd, L"WebView 创建失败。", L"PowerCapslock 设置", MB_OK | MB_ICONERROR);
            DestroyWindow(owner->hwnd);
            return S_OK;
        }

        owner->controller = result;
        owner->controller->AddRef();
        owner->controller->get_CoreWebView2(&owner->webview);

        EventRegistrationToken token = {};
        WebMessageHandler* handler = new WebMessageHandler(owner);
        owner->webview->add_WebMessageReceived(handler, &token);
        handler->Release();
        owner->messageToken = token;

        owner->Resize();
        owner->webview->NavigateToString(owner->html.c_str());
        return S_OK;
    }

private:
    ConfigWebViewWindow* owner;
};

class EnvironmentCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, public HandlerBase {
public:
    explicit EnvironmentCompletedHandler(ConfigWebViewWindow* owner) : owner(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        return QueryInterfaceImpl(riid, ppv, static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this),
                                  IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler);
    }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return AddRefImpl(); }
    ULONG STDMETHODCALLTYPE Release(void) override { return ReleaseImpl(); }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment* result) override {
        if (FAILED(errorCode) || result == NULL) {
            MessageBoxW(owner->hwnd, L"WebView2 运行时不可用，请安装 Microsoft Edge WebView2 Runtime。", L"PowerCapslock 设置", MB_OK | MB_ICONERROR);
            DestroyWindow(owner->hwnd);
            return S_OK;
        }

        ControllerCompletedHandler* handler = new ControllerCompletedHandler(owner);
        HRESULT hr = result->CreateCoreWebView2Controller(owner->hwnd, handler);
        if (FAILED(hr)) {
            handler->Release();
            MessageBoxW(owner->hwnd, L"WebView 控制器创建失败。", L"PowerCapslock 设置", MB_OK | MB_ICONERROR);
            DestroyWindow(owner->hwnd);
        }
        return S_OK;
    }

private:
    ConfigWebViewWindow* owner;
};

static LRESULT CALLBACK ConfigWebViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ConfigWebViewWindow* state = (ConfigWebViewWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            state = (ConfigWebViewWindow*)cs->lpCreateParams;
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
            return TRUE;
        }
        case WM_SIZE:
            if (state != NULL) {
                state->Resize();
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            if (state != NULL) {
                state->ReleaseWebView();
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterConfigWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) {
        return;
    }
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = ConfigWebViewWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(instance, MAKEINTRESOURCE(101));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);
    registered = true;
}

static void CenterWindow(HWND hwnd, HWND parent) {
    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) {
        return;
    }
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    RECT anchor;
    RECT work;
    bool hasAnchor = false;
    bool hasWorkArea = false;

    if (parent != NULL && IsWindow(parent) && IsWindowVisible(parent) && !IsIconic(parent)) {
        hasAnchor = GetWindowRect(parent, &anchor) &&
                    anchor.right > anchor.left &&
                    anchor.bottom > anchor.top;
        HMONITOR monitor = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (monitor != NULL && GetMonitorInfoW(monitor, &mi)) {
            work = mi.rcWork;
            hasWorkArea = true;
        }
    }

    if (!hasAnchor) {
        POINT cursor;
        if (!GetCursorPos(&cursor)) {
            cursor.x = 0;
            cursor.y = 0;
        }
        HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (monitor != NULL && GetMonitorInfoW(monitor, &mi)) {
            work = mi.rcWork;
            hasWorkArea = true;
        } else {
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            hasWorkArea = true;
        }
        anchor = work;
        hasAnchor = true;
    } else if (!hasWorkArea) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        hasWorkArea = true;
    }

    int x = anchor.left + ((anchor.right - anchor.left) - width) / 2;
    int y = anchor.top + ((anchor.bottom - anchor.top) - height) / 2;

    if (hasWorkArea) {
        int maxX = work.right - width;
        int maxY = work.bottom - height;
        if (x < work.left) {
            x = work.left;
        } else if (x > maxX) {
            x = maxX;
        }
        if (y < work.top) {
            y = work.top;
        } else if (y > maxY) {
            y = maxY;
        }
        if (width > work.right - work.left) {
            x = work.left;
        }
        if (height > work.bottom - work.top) {
            y = work.top;
        }
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + ((work.right - work.left) - width) / 2;
        y = work.top + ((work.bottom - work.top) - height) / 2;
    }

    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

static CreateCoreWebView2EnvironmentWithOptionsFn LoadCreateEnvironmentFunction(void) {
    if (g_webView2Loader == NULL) {
        std::wstring loaderPath = GetExeDir() + L"\\WebView2Loader.dll";
        g_webView2Loader = LoadLibraryW(loaderPath.c_str());
        if (g_webView2Loader == NULL) {
            g_webView2Loader = LoadLibraryW(L"WebView2Loader.dll");
        }
    }
    if (g_webView2Loader == NULL) {
        return NULL;
    }
    return (CreateCoreWebView2EnvironmentWithOptionsFn)GetProcAddress(g_webView2Loader, "CreateCoreWebView2EnvironmentWithOptions");
}

extern "C" bool ShowConfigDialog(HWND hParent) {
    HINSTANCE instance = GetModuleHandleW(NULL);
    RegisterConfigWindowClass(instance);

    HRESULT coInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool shouldUninitialize = SUCCEEDED(coInit);

    ConfigWebViewWindow state;
    state.parent = hParent;
    state.initialMappingSignature = MappingSignature(CurrentMappings());
    state.html = ReadUtf8File(GetExeDir() + L"\\resources\\config_ui.html");
    state.testAction = GetEnvString(L"POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_ACTION");
    state.testBrowseLogPath = GetEnvString(L"POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_LOG_PATH");
    state.testBrowseModelPath = GetEnvString(L"POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_MODEL_PATH");

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClass,
        L"PowerCapslock 设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        980,
        720,
        hParent,
        NULL,
        instance,
        &state);

    if (hwnd == NULL) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return false;
    }

    CenterWindow(hwnd, hParent);
    if (hParent != NULL) {
        EnableWindow(hParent, FALSE);
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    CreateCoreWebView2EnvironmentWithOptionsFn createEnvironment = LoadCreateEnvironmentFunction();
    if (createEnvironment == NULL) {
        MessageBoxW(hwnd, L"缺少 WebView2Loader.dll，无法打开配置页面。", L"PowerCapslock 设置", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
    } else {
        std::wstring userData = GetExeDir() + L"\\webview2-user-data";
        EnvironmentCompletedHandler* handler = new EnvironmentCompletedHandler(&state);
        HRESULT hr = createEnvironment(NULL, userData.c_str(), NULL, handler);
        if (FAILED(hr)) {
            handler->Release();
            MessageBoxW(hwnd, L"WebView2 初始化失败。", L"PowerCapslock 设置", MB_OK | MB_ICONERROR);
            DestroyWindow(hwnd);
        }
    }

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hParent != NULL) {
        EnableWindow(hParent, TRUE);
        SetActiveWindow(hParent);
    }
    if (shouldUninitialize) {
        CoUninitialize();
    }

    return state.saved && state.mappingsChanged;
}
