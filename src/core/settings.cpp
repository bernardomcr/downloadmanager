#include "core/settings.h"

#include <algorithm>
#include <sstream>

namespace dm {

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
    return out.str();
}

Settings parseSettings(const std::string& text) {
    Settings settings;
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
        }
    }
    return settings;
}

}  // namespace dm
