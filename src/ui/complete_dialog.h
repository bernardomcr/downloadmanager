#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace ui {

// Caminho atual do arquivo de um download (as regras podem movê-lo logo depois de concluir). Vazio: sumiu.
using PathLookup = std::function<std::wstring(uint64_t id)>;

// Janela "Download concluído" no canto da tela, sem roubar o foco: Abrir, Abrir pasta, Extrair (só para
// compactados, com o extrair inteligente do programa do usuário) e Fechar. Várias ficam empilhadas.
// extractNow: já começa extraindo (menu "Extrair" da aba Concluídos).
void showCompleteWindow(uint64_t id, PathLookup lookup, bool extractNow = false);

// Teclado (Tab, Enter, Esc) nas janelas abertas. true se a mensagem foi tratada.
bool completeWindowMessage(MSG& message);

}  // namespace ui
