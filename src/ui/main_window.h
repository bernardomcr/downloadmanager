#pragma once

#include <windows.h>

#include <array>

namespace ui {

// Janela principal: barra de abas + botão "Adicionar" no topo e uma página por aba.
class MainWindow {
public:
    static constexpr const wchar_t* kClassName = L"DownloadManager.MainWindow";

    bool create(HINSTANCE instance, int showCommand);

private:
    enum Page { kDownloads, kCompleted, kRules, kSettings, kPageCount };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createControls();
    void applyDpi(UINT dpi);
    void layout();
    void showPage(int page);
    void onAddClicked();

    int scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tabs_ = nullptr;
    HWND addButton_ = nullptr;
    std::array<HWND, kPageCount> pages_{};
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr;
    UINT dpi_ = 96;
    int currentPage_ = kDownloads;
};

}  // namespace ui
