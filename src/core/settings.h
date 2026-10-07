#pragma once

#include <string>

namespace dm {

enum class LanguageSetting { Automatic, Portuguese, English };

// Configurações do app. Strings em UTF-8.
struct Settings {
    std::string downloadFolder;  // vazio: pasta Downloads do Windows
    int connections = 8;
    LanguageSetting language = LanguageSetting::Automatic;
    bool closeToTray = true;
    bool startWithWindows = true;
    bool notifyOnComplete = true;
};

std::string serializeSettings(const Settings& settings);
// Chaves desconhecidas ou inválidas mantêm o valor padrão.
Settings parseSettings(const std::string& text);

}  // namespace dm
