#pragma once

#include <windows.h>

// Contrato entre o app e o dm-host.exe (ponte com a extensão do navegador).
namespace app {

inline constexpr const wchar_t* kMainWindowClass = L"DownloadManager.MainWindow";
// WM_COPYDATA com este dwData: o lpData é uma mensagem "add" da extensão, em JSON UTF-8.
inline constexpr ULONG_PTR kCopyDataBrowserRequest = 0x444D4144;  // 'DMAD'
// lpData: token do pedido (texto). A resposta (LRESULT) é um BrowserRequestStatus.
inline constexpr ULONG_PTR kCopyDataRequestStatus = 0x444D5354;   // 'DMST'
// lpData: mensagem "adopt" em JSON: arquivo que o navegador terminou de baixar.
inline constexpr ULONG_PTR kCopyDataAdopt = 0x444D414F;           // 'DMAO'

// Postada pelo instalador/atualizador: o app salva tudo e fecha (sem perguntar nada).
inline constexpr UINT kMessageQuit = WM_APP + 40;

enum BrowserRequestStatus : LRESULT {
    kStatusUnknown = 0,   // token desconhecido: o navegador assume
    kStatusWaiting = 1,   // diálogo aberto ou conectando
    kStatusStarted = 2,   // o app já está recebendo (ou aceitou na fila): o navegador pode largar
    kStatusFailed = 3,    // o app não conseguiu: o navegador continua e o app adota no fim
    kStatusDeclined = 4,  // o usuário cancelou no diálogo: o navegador também cancela
};

// Identidade da ponte para os navegadores.
inline constexpr const wchar_t* kNativeHostName = L"com.bernardomcr.downloadmanager";
inline constexpr const wchar_t* kFirefoxExtensionId = L"downloadmanager@bernardomcr.github.io";
// Derivado da "key" em extension/manifest.json (Chrome/Edge/Brave instalados sem a loja).
inline constexpr const wchar_t* kChromiumExtensionId = L"ofgflamfinkbmejdomanbildiknfpeeh";

}  // namespace app
