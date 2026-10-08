#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/rules.h"

namespace ui {

struct AddRequest {
    std::wstring url;
    std::wstring folder;
    std::wstring fileName;
    // Do navegador; não aparecem no diálogo, só seguem junto com o download.
    std::vector<std::pair<std::string, std::string>> headers;
    std::string userAgent;
    // Veio do navegador: o diálogo destaca o arquivo e esconde o link.
    bool fromBrowser = false;

    // Regras de organização, para o "Salvar em" já mostrar a pasta final.
    const std::vector<dm::Rule>* rules = nullptr;
    bool rulesEnabled = false;
    std::wstring defaultFolder;

    // Saída: a pasta é a padrão ou a que a regra escolheu (as regras ainda valem ao concluir).
    bool organize = false;
    // Saída: `url` é um magnet ou o caminho de um arquivo .torrent (vai pelo Real-Debrid).
    bool torrent = false;
};

// Diálogo "Adicionar download". `request` entra com os valores iniciais e sai com o que o usuário confirmou.
bool showAddDialog(HWND owner, AddRequest& request);

// Diálogo "Trocar link". `url` entra com o link atual.
bool showChangeUrlDialog(HWND owner, std::wstring& url);

// Diálogo "Limite de velocidade" de um download. `kilobytesPerSecond` entra com o valor atual (0 = sem limite).
bool showSpeedLimitDialog(HWND owner, int64_t& kilobytesPerSecond);

// Fundo branco para diálogos e para os textos/caixas de seleção dentro deles.
// Devolve o pincel a usar, ou nullptr se a mensagem não for de cor.
INT_PTR whiteBackground(UINT message, WPARAM wParam);

std::wstring windowText(HWND control);
bool isWebUrl(const std::wstring& url);
// Magnet, ou caminho de um arquivo .torrent que existe.
bool isTorrentInput(const std::wstring& text);

// "https://www.site.com/x" -> "site.com"
std::wstring siteOf(const std::wstring& url);

}  // namespace ui
