#include "app/updater.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdlib>
#include <vector>

#include "core/update.h"
#include "engine/http.h"
#include "util/file_io.h"
#include "util/secure.h"
#include "util/unicode.h"
#include "version.h"

namespace app {
namespace {

constexpr const char* kLatestReleaseUrl = "https://api.github.com/repos/bernardomcr/downloadmanager/releases/latest";
constexpr int64_t kCheckInterval = 24 * 60 * 60;
constexpr size_t kMaxSmallDownload = 1 << 20;
constexpr int64_t kMaxSetupSize = 200 << 20;

int64_t unixTime() {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const uint64_t ticks = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
}

// Teste: DM_UPDATE_URL aponta para um servidor local no lugar do GitHub.
std::string latestReleaseUrl(bool& overridden) {
    wchar_t value[1024];
    const DWORD length = GetEnvironmentVariableW(L"DM_UPDATE_URL", value, 1024);
    overridden = length > 0 && length < 1024;
    return overridden ? dm::toUtf8(std::wstring(value, length)) : kLatestReleaseUrl;
}

std::vector<dm::HttpHeader> headers() {
    return {{"Accept", "application/vnd.github+json"}};
}

// GET de um arquivo pequeno (JSON, .sha256) para a memória.
bool fetchText(const dm::HttpSession& session, const std::string& url, std::string& body) {
    dm::HttpRequest request;
    DWORD error = 0;
    if (!request.send(session, url, headers(), -1, -1, error) || request.response().status != 200) return false;
    char buffer[16384];
    for (;;) {
        const int64_t read = request.read(buffer, sizeof(buffer), error);
        if (read < 0) return false;
        if (read == 0) return true;
        body.append(buffer, static_cast<size_t>(read));
        if (body.size() > kMaxSmallDownload) return false;
    }
}

bool fetchFile(const dm::HttpSession& session, const std::string& url, const std::wstring& path) {
    dm::HttpRequest request;
    DWORD error = 0;
    if (!request.send(session, url, {}, -1, -1, error) || request.response().status != 200) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::vector<char> buffer(1 << 16);
    int64_t total = 0;
    bool ok = true;
    for (;;) {
        const int64_t read = request.read(buffer.data(), buffer.size(), error);
        if (read <= 0) {
            ok = read == 0;
            break;
        }
        DWORD written = 0;
        total += read;
        if (total > kMaxSetupSize || !WriteFile(file, buffer.data(), static_cast<DWORD>(read), &written, nullptr)) {
            ok = false;
            break;
        }
    }
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

dm::Version currentVersion() {
    return *dm::parseVersion(DM_VERSION_STRING);
}

}  // namespace

Updater::Updater(std::wstring dataDirectory)
    : directory_(dm::joinPath(dataDirectory, L"update")), setupPath_(dm::joinPath(directory_, L"DownloadManager-Setup.exe")) {
    // Instalador baixado antes: vale se for de uma versão mais nova que esta (senão já foi instalado).
    if (const auto text = dm::readTextFile(dm::joinPath(directory_, L"ready.txt"))) {
        const size_t space = text->find(' ');
        const auto version = dm::parseVersion(text->substr(0, space));
        if (version && *version > currentVersion() && space != std::string::npos && dm::fileExists(setupPath_)) {
            version_ = text->substr(0, space);
            checksum_ = dm::parseChecksumFile(text->substr(space + 1));
            state_ = checksum_.empty() ? State::Idle : State::Ready;
        }
    }
    if (state_ != State::Ready) {
        DeleteFileW(setupPath_.c_str());
        DeleteFileW(dm::joinPath(directory_, L"ready.txt").c_str());
        DeleteFileW(dm::joinPath(directory_, L"attempted.txt").c_str());
    }
}

Updater::~Updater() {
    if (worker_.joinable()) worker_.join();
}

std::wstring Updater::availableVersion() const {
    std::lock_guard lock(mutex_);
    return dm::toWide(version_);
}

void Updater::checkIfDue(bool force) {
    const State state = state_;
    if (state == State::Checking || state == State::Downloading || state == State::Ready) return;
    if (!force) {
        const auto text = dm::readTextFile(dm::joinPath(directory_, L"last-check.txt"));
        const int64_t last = text ? std::atoll(text->c_str()) : 0;
        const int64_t now = unixTime();
        if (last > 0 && last <= now && now - last < kCheckInterval) return;
    }
    if (worker_.joinable()) worker_.join();
    state_ = State::Checking;
    worker_ = std::thread(&Updater::run, this);
}

void Updater::setFailed() {
    DeleteFileW(setupPath_.c_str());
    state_ = State::Failed;
}

void Updater::run() {
    SHCreateDirectoryExW(nullptr, directory_.c_str(), nullptr);
    dm::writeTextFileAtomically(dm::joinPath(directory_, L"last-check.txt"), std::to_string(unixTime()));

    bool overridden = false;
    const std::string url = latestReleaseUrl(overridden);
    dm::HttpSession session(L"DownloadManager/" + dm::toWide(DM_VERSION_STRING));
    std::string body;
    if (!fetchText(session, url, body)) return setFailed();
    const auto release = dm::parseLatestRelease(body, overridden);
    if (!release) return setFailed();
    if (!(release->version > currentVersion())) {
        state_ = State::Current;
        return;
    }
    {
        std::lock_guard lock(mutex_);
        version_ = dm::formatVersion(release->version);
    }

    state_ = State::Downloading;
    std::string checksumText;
    if (!fetchText(session, release->checksumUrl, checksumText)) return setFailed();
    const std::string expected = dm::parseChecksumFile(checksumText);
    if (expected.empty() || !fetchFile(session, release->setupUrl, setupPath_)) return setFailed();
    if (dm::sha256OfFile(setupPath_) != expected) return setFailed();  // corrompido ou alterado no caminho

    {
        std::lock_guard lock(mutex_);
        checksum_ = expected;
        dm::writeTextFileAtomically(dm::joinPath(directory_, L"ready.txt"), version_ + " " + expected);
    }
    state_ = State::Ready;
}

bool Updater::alreadyAttempted() const {
    const auto text = dm::readTextFile(dm::joinPath(directory_, L"attempted.txt"));
    std::lock_guard lock(mutex_);
    return text && *text == version_;
}

bool Updater::launchInstaller(const wchar_t* relaunch, bool silent) {
    if (state_ != State::Ready) return false;
    std::string checksum;
    std::string version;
    {
        std::lock_guard lock(mutex_);
        checksum = checksum_;
        version = version_;
    }
    if (dm::sha256OfFile(setupPath_) != checksum) {
        setFailed();
        return false;
    }
    dm::writeTextFileAtomically(dm::joinPath(directory_, L"attempted.txt"), version);
    std::wstring arguments = silent ? L"/silencioso" : L"";
    if (silent && relaunch) arguments += std::wstring(L" ") + relaunch;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", setupPath_.c_str(), arguments.c_str(),
                                                   directory_.c_str(), SW_SHOWNORMAL)) > 32;
}

}  // namespace app
