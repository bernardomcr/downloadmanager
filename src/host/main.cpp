// dm-host.exe: ponte entre a extensão do navegador e o app (Native Messaging).
// O navegador abre este programa e conversa por stdin/stdout: cada mensagem é um tamanho de 4 bytes
// seguido de JSON UTF-8. Os pedidos de download são repassados para a janela do app (WM_COPYDATA);
// se o app estiver fechado, ele é aberto na bandeja.
#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "app/ipc.h"
#include "core/browser_request.h"
#include "core/json.h"
#include "version.h"

namespace {

constexpr uint32_t kMaxMessage = 1024 * 1024;

bool readExactly(void* buffer, size_t size) {
    return std::fread(buffer, 1, size, stdin) == size;
}

void reply(const dm::JsonValue& message) {
    const std::string text = message.serialize();
    const auto length = static_cast<uint32_t>(text.size());
    std::fwrite(&length, sizeof(length), 1, stdout);
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

void replyStatus(bool ok, const char* error = nullptr) {
    dm::JsonValue::Object object;
    object["ok"] = ok;
    object["version"] = DM_VERSION_STRING;
    if (error) object["error"] = error;
    reply(dm::JsonValue(std::move(object)));
}

HWND findApp() {
    return FindWindowW(app::kMainWindowClass, nullptr);
}

// Abre o DownloadManager.exe (da mesma pasta) na bandeja e espera a janela aparecer.
HWND launchApp() {
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    std::wstring exe(path, length);
    exe = exe.substr(0, exe.find_last_of(L'\\') + 1) + L"DownloadManager.exe";
    std::wstring command = L"\"" + exe + L"\" --tray";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr,
                        &startup, &process)) {
        return nullptr;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    for (int attempt = 0; attempt < 100; ++attempt) {  // até 10 s
        if (HWND window = findApp()) return window;
        Sleep(100);
    }
    return nullptr;
}

bool forward(const std::string& json, ULONG_PTR kind = app::kCopyDataBrowserRequest) {
    HWND window = findApp();
    if (!window) window = launchApp();
    if (!window) return false;

    // O navegador está em primeiro plano; deixa o app trazer o diálogo "Adicionar" para frente.
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    AllowSetForegroundWindow(processId);

    COPYDATASTRUCT data{};
    data.dwData = kind;
    data.cbData = static_cast<DWORD>(json.size());
    data.lpData = const_cast<char*>(json.data());
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000,
                               &result) != 0 &&
           result == TRUE;
}

}  // namespace

const char* askStatus(const std::string& token) {
    HWND window = findApp();
    if (!window || token.empty()) return "unknown";
    COPYDATASTRUCT data{};
    data.dwData = app::kCopyDataRequestStatus;
    data.cbData = static_cast<DWORD>(token.size());
    data.lpData = const_cast<char*>(token.data());
    DWORD_PTR result = app::kStatusUnknown;
    if (!SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 5000,
                             &result)) {
        return "unknown";
    }
    switch (result) {
        case app::kStatusWaiting: return "waiting";
        case app::kStatusStarted: return "started";
        case app::kStatusFailed: return "failed";
        case app::kStatusDeclined: return "declined";
        default: return "unknown";
    }
}

int main() {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    for (;;) {
        uint32_t length = 0;
        if (!readExactly(&length, sizeof(length)) || length == 0 || length > kMaxMessage) return 0;
        std::string message(length, '\0');
        if (!readExactly(message.data(), length)) return 0;

        const auto value = dm::parseJson(message);
        const std::string type = value ? value->string("type") : "";
        if (type == "ping") {
            replyStatus(true);
        } else if (type == "add") {
            const auto request = dm::parseBrowserRequest(message);
            if (!request) {
                replyStatus(false, "invalid");
            } else {
                // Repassa a versão validada, não o texto que chegou.
                const bool ok = forward(dm::serializeBrowserRequest(*request));
                replyStatus(ok, ok ? nullptr : "app");
            }
        } else if (type == "status") {
            dm::JsonValue::Object object;
            object["ok"] = true;
            object["status"] = askStatus(value->string("token"));
            reply(dm::JsonValue(std::move(object)));
        } else if (type == "adopt") {
            const auto request = dm::parseAdoptRequest(message);
            if (!request) {
                replyStatus(false, "invalid");
            } else {
                const bool ok = forward(dm::serializeAdoptRequest(*request), app::kCopyDataAdopt);
                replyStatus(ok, ok ? nullptr : "app");
            }
        } else {
            replyStatus(false, "unknown");
        }
    }
}
