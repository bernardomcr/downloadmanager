#pragma once

#include <windows.h>

#include <functional>

#include "core/settings.h"

namespace ui {

// Aba "Configurações": um diálogo filho embutido na janela principal. Cada mudança é aplicada na hora.
class SettingsPage {
public:
    HWND create(HWND parent);
    HWND handle() const { return dialog_; }

    void setSettings(const dm::Settings& settings);
    // Reaplica os textos depois de trocar o idioma.
    void applyTexts();

    // Linha da versão/atualização e o texto do botão ("Procurar agora" ou "Atualizar agora").
    void setUpdateStatus(const std::wstring& text, bool updateReady);

    std::function<void(const dm::Settings&)> onChanged;
    std::function<void()> onUpdateButton;

private:
    static INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void fillControls();
    void notify();
    void setTime(int controlId, int minutes);
    int readTime(int controlId) const;

    HWND dialog_ = nullptr;
    dm::Settings settings_;
    bool filling_ = false;
    bool updateReady_ = false;
};

}  // namespace ui
