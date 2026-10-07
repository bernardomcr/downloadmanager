#include "util/secure.h"

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>

#include <vector>

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

std::string sha256OfFile(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<unsigned char> buffer(1 << 16);
        DWORD read = 0;
        bool ok = true;
        while (ok && ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
            ok = BCryptHashData(hash, buffer.data(), read, 0) == 0;
        }
        unsigned char digest[32];
        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
            static constexpr char kHex[] = "0123456789abcdef";
            for (unsigned char byte : digest) {
                result += kHex[byte >> 4];
                result += kHex[byte & 15];
            }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return result;
}

}  // namespace dm
