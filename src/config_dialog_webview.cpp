#include "config_dialog.h"

extern "C" {
#include "config.h"
#include "keymap.h"
#include "logger.h"
#include "audio.h"
#include "voice.h"
#include "voice_prompt.h"
#include "action.h"
#include "action_builtin.h"
#include "hook.h"
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

struct ActionItem {
    std::wstring trigger;
    int type;  // 0=key_mapping, 1=builtin, 2=command
    std::wstring param;
};

struct PreparedAction {
    char trigger[16];
    WORD scanCode;
    ActionType type;
    char param[256];
};

struct ConfigWebViewWindow;

static const wchar_t* kWindowClass = L"PowerCapslockConfigWebView";
static HMODULE g_webView2Loader = NULL;
static ConfigWebViewWindow* g_activeWindow = NULL;

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

static int JsonGetInt(const std::wstring& json, const wchar_t* key) {
    std::wstring pattern = L"\"";
    pattern += key;
    pattern += L"\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::wstring::npos) {
        return 0;
    }
    size_t colon = json.find(L':', keyPos + pattern.size());
    if (colon == std::wstring::npos) {
        return 0;
    }
    // Skip whitespace
    size_t numStart = colon + 1;
    while (numStart < json.size() && (json[numStart] == L' ' || json[numStart] == L'\t')) {
        numStart++;
    }
    if (numStart >= json.size()) {
        return 0;
    }
    return _wtoi(json.c_str() + numStart);
}

static std::vector<ActionItem> JsonGetActions(const std::wstring& json) {
    std::vector<ActionItem> actions;
    size_t keyPos = json.find(L"\"actions\"");
    if (keyPos == std::wstring::npos) {
        return actions;
    }
    size_t arrayStart = json.find(L'[', keyPos);
    size_t arrayEnd = json.find(L']', arrayStart);
    if (arrayStart == std::wstring::npos || arrayEnd == std::wstring::npos) {
        return actions;
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
        ActionItem item;
        item.trigger = JsonGetString(object, L"trigger");
        item.type = JsonGetInt(object, L"type");
        item.param = JsonGetString(object, L"param");
        if (!item.trigger.empty()) {
            actions.push_back(item);
        }
        cursor = objectEnd + 1;
    }
    return actions;
}

static std::wstring UpperAscii(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') ? (wchar_t)(c - L'a' + L'A') : c;
    });
    return value;
}

static std::vector<ActionItem> CurrentActions(void) {
    std::vector<ActionItem> result;
    int count = ActionGetCount();
    for (int i = 0; i < count; i++) {
        const Action* action = ActionGet(i);
        if (action != NULL) {
            ActionItem item;
            item.trigger = UpperAscii(Utf8ToWide(action->trigger));
            item.type = (int)action->type;
            item.param = UpperAscii(Utf8ToWide(action->param));
            result.push_back(item);
        }
    }
    return result;
}

static std::wstring ActionSignature(const std::vector<ActionItem>& actions) {
    std::wstring result;
    for (const ActionItem& item : actions) {
        result += UpperAscii(item.trigger);
        result += L"|";
        result += std::to_wstring(item.type);
        result += L"|";
        result += UpperAscii(item.param);
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
    json += L"\",\"actions\":[";

    std::vector<ActionItem> actions = CurrentActions();
    for (size_t i = 0; i < actions.size(); i++) {
        if (i > 0) {
            json += L",";
        }
        json += L"{\"trigger\":\"";
        json += JsonEscape(UpperAscii(actions[i].trigger));
        json += L"\",\"type\":";
        json += std::to_wstring(actions[i].type);
        json += L",\"param\":\"";
        json += JsonEscape(UpperAscii(actions[i].param));
        json += L"\"}";
    }
    json += L"],\"builtins\":[";

    int builtinCount = 0;
    const char** builtins = BuiltinGetList(&builtinCount);
    for (int i = 0; i < builtinCount; i++) {
        if (i > 0) {
            json += L",";
        }
        json += L"{\"name\":\"";
        json += JsonEscape(Utf8ToWide(builtins[i]));
        json += L"\",\"displayName\":\"";
        json += JsonEscape(Utf8ToWide(BuiltinGetDisplayName(builtins[i])));
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
    std::wstring initialActionSignature;
    std::wstring testAction;
    std::wstring testBrowseLogPath;
    std::wstring testBrowseModelPath;
    bool testSkipModelLoad = false;
    bool saved = false;
    bool actionsChanged = false;
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

    void NotifyKeyCaptured(const char* keyName, WORD scanCode) {
        std::wstring json = L"{\"type\":\"key_captured\",\"keyName\":\"";
        json += JsonEscape(Utf8ToWide(keyName));
        json += L"\",\"scanCode\":";
        json += std::to_wstring(scanCode);
        json += L"}";
        PostJson(json);
    }

    void SendActionsToWebView(void) {
        std::wstring json = L"{\"type\":\"actions_update\",\"actions\":[";
        std::vector<ActionItem> actions = CurrentActions();
        for (size_t i = 0; i < actions.size(); i++) {
            if (i > 0) {
                json += L",";
            }
            json += L"{\"trigger\":\"";
            json += JsonEscape(UpperAscii(actions[i].trigger));
            json += L"\",\"type\":";
            json += std::to_wstring(actions[i].type);
            json += L",\"param\":\"";
            json += JsonEscape(UpperAscii(actions[i].param));
            json += L"\"}";
        }
        json += L"]}";
        PostJson(json);
    }

    void SendBuiltinListToWebView(void) {
        std::wstring json = L"{\"type\":\"builtins_update\",\"builtins\":[";
        int builtinCount = 0;
        const char** builtins = BuiltinGetList(&builtinCount);
        for (int i = 0; i < builtinCount; i++) {
            if (i > 0) {
                json += L",";
            }
            json += L"{\"name\":\"";
            json += JsonEscape(Utf8ToWide(builtins[i]));
            json += L"\",\"displayName\":\"";
            json += JsonEscape(Utf8ToWide(BuiltinGetDisplayName(builtins[i])));
            json += L"\"}";
        }
        json += L"]}";
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
            L"function save(){later(function(){by('save').click();});}\n" +
            L"function run(){by('browseLog').click();}\n" +
            L"function waitReady(){try{if(!document.querySelector('#actionRows')){setTimeout(waitReady,60);return;}run();}catch(e){fail(e&&e.message?e.message:e);}}\n" +
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
        if (type == L"start_capture") {
            std::wstring captureType = JsonGetString(message, L"captureType");
            CaptureMode mode = CAPTURE_MODE_TRIGGER;
            if (captureType == L"output") {
                mode = CAPTURE_MODE_OUTPUT;
            }
            HookSetCaptureMode(mode);
            LOG_INFO("[Config] Started capture mode: %d", mode);
            return S_OK;
        }
        if (type == L"stop_capture") {
            HookSetCaptureMode(CAPTURE_MODE_NONE);
            HookClearCapturedKey();
            LOG_INFO("[Config] Stopped capture mode");
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
        std::vector<ActionItem> actionItems = JsonGetActions(message);
        if (actionItems.empty()) {
            owner->PostError(L"至少需要保留一条动作配置。");
            return;
        }

        std::vector<PreparedAction> prepared;
        std::set<WORD> usedScanCodes;
        for (const ActionItem& item : actionItems) {
            std::string trigger = WideToUtf8(UpperAscii(item.trigger));
            std::string param = WideToUtf8(item.param);
            WORD scanCode = ConfigKeyNameToScanCode(trigger.c_str());
            if (scanCode == 0) {
                owner->PostError(L"触发键名称无效，请检查配置表。");
                return;
            }
            if (usedScanCodes.find(scanCode) != usedScanCodes.end()) {
                owner->PostError(L"每个触发键只能配置一次，请删除重复配置。");
                return;
            }
            // Validate param based on type
            if (item.type == ACTION_TYPE_KEY_MAPPING) {
                UINT targetVk = ConfigKeyNameToVkCode(param.c_str());
                if (targetVk == 0) {
                    owner->PostError(L"目标按键名称无效，请检查配置表。");
                    return;
                }
            } else if (item.type == ACTION_TYPE_BUILTIN) {
                // Check if builtin exists
                int builtinCount = 0;
                const char** builtins = BuiltinGetList(&builtinCount);
                bool found = false;
                for (int i = 0; i < builtinCount; i++) {
                    if (_stricmp(builtins[i], param.c_str()) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    owner->PostError(L"内置功能名称无效，请检查配置表。");
                    return;
                }
            }
            // For ACTION_TYPE_COMMAND, param can be any string

            usedScanCodes.insert(scanCode);
            PreparedAction pa;
            strncpy(pa.trigger, trigger.c_str(), sizeof(pa.trigger) - 1);
            pa.trigger[sizeof(pa.trigger) - 1] = '\0';
            pa.scanCode = scanCode;
            pa.type = (ActionType)item.type;
            strncpy(pa.param, param.c_str(), sizeof(pa.param) - 1);
            pa.param[sizeof(pa.param) - 1] = '\0';
            prepared.push_back(pa);
        }

        Config updated = *ConfigGet();
        std::string modelPathUtf8 = WideToUtf8(modelPath);
        bool modelPathChanged = modelPathUtf8 != updated.modelDirPath;
        bool modelLoadedNow = false;
        VoiceModelCheckResult modelStatus;
        ZeroMemory(&modelStatus, sizeof(modelStatus));

        if (modelPathChanged && !owner->testSkipModelLoad) {
            HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
            if (!VoiceReloadModel(modelPathUtf8.c_str(), &modelStatus)) {
                if (oldCursor != NULL) {
                    SetCursor(oldCursor);
                }
                const wchar_t* reason = modelStatus.reason[0] != L'\0'
                    ? modelStatus.reason
                    : L"模型不可用，请确认目录中包含 model.onnx 和 tokens.txt。";
                MessageBoxW(owner->hwnd, reason, L"PowerCapslock - 模型不可用", MB_OK | MB_ICONWARNING);
                owner->PostError(reason);
                return;
            }
            if (oldCursor != NULL) {
                SetCursor(oldCursor);
            }
            modelLoadedNow = true;
            updated.voiceInputEnabled = true;
            updated.voiceInputAsked = true;
        }

        CopyPathToConfig(logPath, updated.logDirPath, sizeof(updated.logDirPath));
        CopyPathToConfig(modelPath, updated.modelDirPath, sizeof(updated.modelDirPath));
        ConfigSet(&updated);

        // Update actions
        ActionResetToDefaults();  // Clear all and start fresh
        for (const PreparedAction& item : prepared) {
            Action action;
            strncpy(action.trigger, item.trigger, sizeof(action.trigger) - 1);
            action.trigger[sizeof(action.trigger) - 1] = '\0';
            action.scanCode = item.scanCode;
            action.type = item.type;
            strncpy(action.param, item.param, sizeof(action.param) - 1);
            action.param[sizeof(action.param) - 1] = '\0';
            ActionAdd(&action);
        }

        if (!ConfigSave(NULL)) {
            owner->PostError(L"配置保存失败，请检查目录权限。");
            return;
        }

        owner->saved = true;
        owner->actionsChanged = ActionSignature(actionItems) != owner->initialActionSignature;

        if (modelLoadedNow) {
            bool audioReady = AudioInit();
            bool promptReady = VoicePromptInit();
            std::wstring modelName = Utf8ToWide(modelStatus.modelName[0] != '\0' ? modelStatus.modelName : "SenseVoice-Small");
            std::wstring message = modelName + L" 模型加载成功。\n\n无需重启程序，语音输入现在可以使用。";
            if (!audioReady || !promptReady) {
                message = modelName + L" 模型加载成功。\n\n"
                          L"但语音输入运行环境没有完全准备好：";
                if (!audioReady) {
                    message += L"\n- 音频录制模块初始化失败，请检查麦克风权限或默认输入设备。";
                }
                if (!promptReady) {
                    message += L"\n- 语音提示窗口初始化失败。";
                }
            }
            MessageBoxW(owner->hwnd, message.c_str(), L"PowerCapslock - 模型加载成功", MB_OK | MB_ICONINFORMATION);
        }

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

// Check for captured keys and notify webview
void CheckCapturedKey(void) {
    if (g_activeWindow == NULL || g_activeWindow->webview == NULL) {
        return;
    }
    if (!HookIsCaptureMode()) {
        return;
    }
    char keyName[32];
    WORD scanCode;
    if (HookGetCapturedKey(keyName, sizeof(keyName), &scanCode)) {
        g_activeWindow->NotifyKeyCaptured(keyName, scanCode);
        HookClearCapturedKey();
        HookSetCaptureMode(CAPTURE_MODE_NONE);
    }
}

static LRESULT CALLBACK ConfigWebViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ConfigWebViewWindow* state = (ConfigWebViewWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            state = (ConfigWebViewWindow*)cs->lpCreateParams;
            state->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
            g_activeWindow = state;
            return TRUE;
        }
        case WM_SIZE:
            if (state != NULL) {
                state->Resize();
            }
            return 0;
        case WM_TIMER:
            CheckCapturedKey();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            if (state != NULL) {
                if (g_activeWindow == state) {
                    g_activeWindow = NULL;
                }
                HookSetCaptureMode(CAPTURE_MODE_NONE);
                HookClearCapturedKey();
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
    state.initialActionSignature = ActionSignature(CurrentActions());
    state.html = ReadUtf8File(GetExeDir() + L"\\resources\\config_ui.html");
    state.testAction = GetEnvString(L"POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_ACTION");
    state.testBrowseLogPath = GetEnvString(L"POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_LOG_PATH");
    state.testBrowseModelPath = GetEnvString(L"POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_MODEL_PATH");
    state.testSkipModelLoad = !GetEnvString(L"POWERCAPSLOCK_CONFIG_WEBVIEW_TEST_SKIP_MODEL_LOAD").empty();

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

    // Set timer to check for captured keys
    SetTimer(hwnd, 1, 100, NULL);

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

    return state.saved && state.actionsChanged;
}
