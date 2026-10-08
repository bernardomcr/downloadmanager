#pragma once

#include <cstdint>
#include <string>

namespace dm {

enum class LanguageSetting { Automatic, Portuguese, English };

enum class WhenDone { Nothing, Sleep, Shutdown };

// Configurações do app. Strings em UTF-8.
struct Settings {
    std::string downloadFolder;  // vazio: pasta Downloads do Windows
    int connections = 16;
    LanguageSetting language = LanguageSetting::Automatic;
    bool closeToTray = true;
    bool startWithWindows = true;
    bool notifyOnComplete = true;
    bool keepAwake = true;          // não deixa o PC dormir enquanto baixa
    bool askForBrowserDownloads = true;  // download vindo do navegador abre o diálogo "Adicionar" preenchido
    bool adoptBrowserDownloads = true;   // o que o navegador baixar sozinho aparece em Concluídos (sem mover)
    // O que o navegador baixa sozinho vai para <pasta padrão>\Navegador (Chrome/Edge pela extensão,
    // Firefox pela preferência de pasta do perfil).
    bool browserFolder = true;
    bool rulesEnabled = true;            // aba Regras: organizar os concluídos em subpastas
    bool autoUpdate = true;              // baixa e instala versões novas sozinho
    bool showCompleteWindow = true;      // janela "Download concluído" (Abrir / Abrir pasta / Extrair / Fechar)
    bool speedInBits = false;            // velocidade em Mb/s (bits) em vez de MB/s (bytes)
    // Token da API do Real-Debrid já criptografado pelo app (DPAPI, base64); vazio = não conectado.
    std::string realDebridToken;

    int maxDownloads = 3;           // downloads ao mesmo tempo; o resto espera na fila
    int64_t speedLimitKBps = 0;     // limite total; 0 = sem limite
    bool scheduleEnabled = false;   // só baixa a fila entre scheduleStart e scheduleEnd
    int scheduleStart = 2 * 60;     // minutos do dia
    int scheduleEnd = 8 * 60;

    // Vale só para esta sessão (não é salvo): volta para Nothing depois de executar.
    WhenDone whenDone = WhenDone::Nothing;
};

// "HH:MM" <-> minutos do dia. -1 se inválido.
int parseTimeOfDay(const std::string& text);
std::string formatTimeOfDay(int minutes);

std::string serializeSettings(const Settings& settings);
// Chaves desconhecidas ou inválidas mantêm o valor padrão.
Settings parseSettings(const std::string& text);

}  // namespace dm
