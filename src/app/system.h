#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// Integrações com o Windows usadas pelo app.
namespace app {

// %LOCALAPPDATA%\DownloadManager (criada se não existir).
std::wstring dataDirectory();
// Pasta Downloads do usuário.
std::wstring defaultDownloadFolder();

// Liga/desliga a entrada em HKCU\...\Run que abre o app minimizado na bandeja ao entrar no Windows.
// exePath vazio: o próprio executável (o instalador passa o caminho do app instalado).
void setStartWithWindows(bool enabled, const std::wstring& exePath = {});

// O app em execução é o instalado pelo setup (pasta registrada em "Apps e recursos")?
// Só a cópia instalada se atualiza sozinha; uma cópia solta não vira outra instalação sem perguntar.
bool runningFromInstallation();

void openFile(const std::wstring& path);
// Abre o Explorer com o arquivo selecionado (ou a pasta, se o arquivo não existir).
void showInFolder(const std::wstring& path);
// Lixeira, com desfazer.
bool moveToRecycleBin(const std::vector<std::wstring>& paths);
void copyToClipboard(HWND owner, const std::wstring& text);
// Diálogo do Windows para escolher pasta; vazio se cancelado.
std::wstring chooseFolder(HWND owner, const std::wstring& title, const std::wstring& initial);

// Enquanto ligado, o Windows não suspende por inatividade (a tela ainda pode apagar).
void keepSystemAwake(bool enabled);
bool shutdownComputer();
bool sleepComputer();

int64_t unixNow();
// Data e hora curtas no formato do usuário.
std::wstring formatDateTime(int64_t unixSeconds);

}  // namespace app
