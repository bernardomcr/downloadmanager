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
void setStartWithWindows(bool enabled);

void openFile(const std::wstring& path);
// Abre o Explorer com o arquivo selecionado (ou a pasta, se o arquivo não existir).
void showInFolder(const std::wstring& path);
// Lixeira, com desfazer.
bool moveToRecycleBin(const std::vector<std::wstring>& paths);
void copyToClipboard(HWND owner, const std::wstring& text);
// Diálogo do Windows para escolher pasta; vazio se cancelado.
std::wstring chooseFolder(HWND owner, const std::wstring& title, const std::wstring& initial);

int64_t unixNow();
// Data e hora curtas no formato do usuário.
std::wstring formatDateTime(int64_t unixSeconds);

}  // namespace app
