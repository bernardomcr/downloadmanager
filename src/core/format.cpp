#include "core/format.h"

#include <cstdio>

namespace dm {

std::string formatBytes(int64_t bytes, char decimalSeparator) {
    static constexpr const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    if (bytes < 0) return "?";
    if (bytes < 1024) return std::to_string(bytes) + " B";

    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f %s", value, kUnits[unit]);
    std::string text = buffer;
    if (const size_t dot = text.find('.'); dot != std::string::npos) text[dot] = decimalSeparator;
    return text;
}

std::string formatSpeed(double bytesPerSecond, char decimalSeparator) {
    if (bytesPerSecond < 0) bytesPerSecond = 0;
    return formatBytes(static_cast<int64_t>(bytesPerSecond), decimalSeparator) + "/s";
}

std::string formatDuration(int64_t seconds) {
    if (seconds < 0) return "";
    char buffer[32];
    const long long hours = seconds / 3600;
    const long long minutes = (seconds / 60) % 60;
    const long long secs = seconds % 60;
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld:%02lld", hours, minutes, secs);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld:%02lld", minutes, secs);
    }
    return buffer;
}

}  // namespace dm
