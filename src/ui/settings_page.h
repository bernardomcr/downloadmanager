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

    std::function<void(const dm::Settings&)> onChanged;

private:
    static INT_PTR CALLBACK dialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void fillControls();
    void notify();

    HWND dialog_ = nullptr;
    dm::Settings settings_;
    bool filling_ = false;
};

}  // namespace ui
