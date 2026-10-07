#include "core/base64.h"

namespace dm {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string base64Encode(const std::string& data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        const unsigned value = (static_cast<unsigned char>(data[i]) << 16) |
                               (static_cast<unsigned char>(data[i + 1]) << 8) | static_cast<unsigned char>(data[i + 2]);
        out += kAlphabet[(value >> 18) & 63];
        out += kAlphabet[(value >> 12) & 63];
        out += kAlphabet[(value >> 6) & 63];
        out += kAlphabet[value & 63];
    }
    if (i < data.size()) {
        unsigned value = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size()) value |= static_cast<unsigned char>(data[i + 1]) << 8;
        out += kAlphabet[(value >> 18) & 63];
        out += kAlphabet[(value >> 12) & 63];
        out += i + 1 < data.size() ? kAlphabet[(value >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::string> base64Decode(const std::string& text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::string out;
    out.reserve(text.size() / 4 * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int values[4];
        int padding = 0;
        for (int j = 0; j < 4; ++j) {
            const char c = text[i + j];
            if (c == '=' && i + 4 == text.size() && j >= 2) {
                values[j] = 0;
                ++padding;
            } else {
                values[j] = decodeChar(c);
                if (values[j] < 0 || padding > 0) return std::nullopt;
            }
        }
        const unsigned value = (values[0] << 18) | (values[1] << 12) | (values[2] << 6) | values[3];
        out += static_cast<char>((value >> 16) & 0xFF);
        if (padding < 2) out += static_cast<char>((value >> 8) & 0xFF);
        if (padding < 1) out += static_cast<char>(value & 0xFF);
    }
    return out;
}

}  // namespace dm
