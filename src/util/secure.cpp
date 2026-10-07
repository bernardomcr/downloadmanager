#include "util/secure.h"

#include <windows.h>
#include <dpapi.h>

#include "core/base64.h"

namespace dm {

std::string protectForCurrentUser(const std::string& plain) {
    if (plain.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"DownloadManager", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    std::string encrypted(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return base64Encode(encrypted);
}

std::optional<std::string> unprotectForCurrentUser(const std::string& encoded) {
    if (encoded.empty()) return std::string{};
    const auto encrypted = base64Decode(encoded);
    if (!encrypted) return std::nullopt;
    DATA_BLOB input{static_cast<DWORD>(encrypted->size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(encrypted->data()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::nullopt;
    }
    std::string plain(reinterpret_cast<const char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return plain;
}

}  // namespace dm
