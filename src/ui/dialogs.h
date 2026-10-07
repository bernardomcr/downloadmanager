#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace ui {

struct AddRequest {
    std::wstring url;
    std::wstring folder;
    std::wstring fileName;
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

}  // namespace ui
