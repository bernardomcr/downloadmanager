#include "app/browser_integration.h"

#include <windows.h>

#include "app/ipc.h"
#include "core/json.h"
#include "util/file_io.h"
#include "util/unicode.h"

namespace app {
namespace {

void setDefaultRegistryValue(const std::wstring& keyPath, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

std::string hostManifest(const std::wstring& hostPath, const char* listKey, const std::wstring& allowed) {
    dm::JsonValue::Object manifest;
    manifest["name"] = dm::toUtf8(kNativeHostName);
    manifest["description"] = "Download Manager";
    manifest["path"] = dm::toUtf8(hostPath);
    manifest["type"] = "stdio";
    manifest[listKey] = dm::JsonValue::Array{dm::JsonValue(dm::toUtf8(allowed))};
    return dm::JsonValue(std::move(manifest)).serialize();
}

}  // namespace

void registerBrowserIntegration(const std::wstring& dataDirectory) {
    wchar_t exe[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    const std::wstring hostPath = dm::joinPath(dm::directoryOf(std::wstring(exe, length)), L"dm-host.exe");
    if (!dm::fileExists(hostPath) || dataDirectory.empty()) return;

    const std::wstring firefoxManifest = dm::joinPath(dataDirectory, L"native-host-firefox.json");
    const std::wstring chromiumManifest = dm::joinPath(dataDirectory, L"native-host-chromium.json");
    dm::writeTextFileAtomically(firefoxManifest, hostManifest(hostPath, "allowed_extensions", kFirefoxExtensionId));
    dm::writeTextFileAtomically(
        chromiumManifest,
        hostManifest(hostPath, "allowed_origins", L"chrome-extension://" + std::wstring(kChromiumExtensionId) + L"/"));

    const std::wstring name = kNativeHostName;
    setDefaultRegistryValue(L"Software\\Mozilla\\NativeMessagingHosts\\" + name, firefoxManifest);
    for (const wchar_t* browser : {L"Software\\Google\\Chrome", L"Software\\Microsoft\\Edge",
                                   L"Software\\BraveSoftware\\Brave-Browser", L"Software\\Chromium"}) {
        setDefaultRegistryValue(std::wstring(browser) + L"\\NativeMessagingHosts\\" + name, chromiumManifest);
    }
}

}  // namespace app
