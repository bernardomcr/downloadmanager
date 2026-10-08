#include "core/settings.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace dm {
namespace {

// 2: padrão de conexões 8 -> 16.
constexpr int kSettingsVersion = 2;

}  // namespace

std::string serializeSettings(const Settings& settings) {
    std::ostringstream out;
    out << "download-folder=" << settings.downloadFolder << '\n';
    out << "connections=" << settings.connections << '\n';
    out << "language="
        << (settings.language == LanguageSetting::Portuguese ? "pt"
            : settings.language == LanguageSetting::English  ? "en"
                                                             : "auto")
        << '\n';
    out << "close-to-tray=" << (settings.closeToTray ? 1 : 0) << '\n';
    out << "start-with-windows=" << (settings.startWithWindows ? 1 : 0) << '\n';
    out << "notify-on-complete=" << (settings.notifyOnComplete ? 1 : 0) << '\n';
    out << "keep-awake=" << (settings.keepAwake ? 1 : 0) << '\n';
    out << "browser-ask=" << (settings.askForBrowserDownloads ? 1 : 0) << '\n';
    out << "browser-adopt=" << (settings.adoptBrowserDownloads ? 1 : 0) << '\n';
    out << "rules=" << (settings.rulesEnabled ? 1 : 0) << '\n';
    out << "auto-update=" << (settings.autoUpdate ? 1 : 0) << '\n';
    out << "browser-folder=" << (settings.browserFolder ? 1 : 0) << '\n';
    out << "complete-window=" << (settings.showCompleteWindow ? 1 : 0) << '\n';
    out << "speed-unit=" << (settings.speedInBits ? "bits" : "bytes") << '\n';
    out << "settings-version=" << kSettingsVersion << '\n';
    out << "max-downloads=" << settings.maxDownloads << '\n';
    out << "speed-limit-kbps=" << settings.speedLimitKBps << '\n';
    out << "schedule=" << (settings.scheduleEnabled ? 1 : 0) << '\n';
    out << "schedule-start=" << formatTimeOfDay(settings.scheduleStart) << '\n';
    out << "schedule-end=" << formatTimeOfDay(settings.scheduleEnd) << '\n';
    if (!settings.realDebridToken.empty()) out << "real-debrid-token=" << settings.realDebridToken << '\n';
    return out.str();
}

Settings parseSettings(const std::string& text) {
    Settings settings;
    int version = 1;
    bool hasConnections = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        if (key == "download-folder") {
            settings.downloadFolder = value;
        } else if (key == "connections") {
            std::istringstream number(value);
            int connections = 0;
            if (number >> connections) settings.connections = std::clamp(connections, 1, 32);
            hasConnections = true;
        } else if (key == "settings-version") {
            std::istringstream number(value);
            number >> version;
        } else if (key == "browser-folder") {
            settings.browserFolder = value != "0";
        } else if (key == "complete-window") {
            settings.showCompleteWindow = value != "0";
        } else if (key == "speed-unit") {
            settings.speedInBits = value == "bits";
        } else if (key == "language") {
            settings.language = value == "pt"   ? LanguageSetting::Portuguese
                                : value == "en" ? LanguageSetting::English
                                                : LanguageSetting::Automatic;
        } else if (key == "close-to-tray") {
            settings.closeToTray = value != "0";
        } else if (key == "start-with-windows") {
            settings.startWithWindows = value != "0";
        } else if (key == "notify-on-complete") {
            settings.notifyOnComplete = value != "0";
        } else if (key == "keep-awake") {
            settings.keepAwake = value != "0";
        } else if (key == "browser-ask") {
            settings.askForBrowserDownloads = value != "0";
        } else if (key == "browser-adopt") {
            settings.adoptBrowserDownloads = value != "0";
        } else if (key == "rules") {
            settings.rulesEnabled = value != "0";
        } else if (key == "auto-update") {
            settings.autoUpdate = value != "0";
        } else if (key == "max-downloads") {
            std::istringstream number(value);
            int count = 0;
            if (number >> count) settings.maxDownloads = std::clamp(count, 1, 10);
        } else if (key == "speed-limit-kbps") {
            std::istringstream number(value);
            int64_t limit = 0;
            if (number >> limit) settings.speedLimitKBps = std::max<int64_t>(limit, 0);
        } else if (key == "real-debrid-token") {
            settings.realDebridToken = value;
        } else if (key == "schedule") {
            settings.scheduleEnabled = value == "1";
        } else if (key == "schedule-start" || key == "schedule-end") {
            const int minutes = parseTimeOfDay(value);
            if (minutes >= 0) (key == "schedule-start" ? settings.scheduleStart : settings.scheduleEnd) = minutes;
        }
    }
    // Versão 1 gravava sempre "connections=8" (o padrão de então): passa para o padrão novo uma vez.
    if (version < 2 && hasConnections && settings.connections == 8) settings.connections = Settings{}.connections;
    return settings;
}

int parseTimeOfDay(const std::string& text) {
    int hours = 0;
    int minutes = 0;
    char separator = 0;
    std::istringstream in(text);
    if (!(in >> hours >> separator >> minutes) || separator != ':') return -1;
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) return -1;
    return hours * 60 + minutes;
}

std::string formatTimeOfDay(int minutes) {
    minutes = std::clamp(minutes, 0, 24 * 60 - 1);
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d", minutes / 60, minutes % 60);
    return text;
}

}  // namespace dm
