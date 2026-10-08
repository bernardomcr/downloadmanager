#pragma once

#include <windows.h>

#include <string>

// Visual do app: fundo branco, tipografia limpa, um azul de destaque e formas arredondadas (GDI+,
// que já vem no Windows: anti-aliasing sem dependências).
namespace ui::theme {

constexpr COLORREF kBackground = RGB(255, 255, 255);
constexpr COLORREF kText = RGB(17, 24, 39);
constexpr COLORREF kTextSecondary = RGB(107, 114, 128);
constexpr COLORREF kTextTertiary = RGB(156, 163, 175);
constexpr COLORREF kHairline = RGB(234, 236, 240);
constexpr COLORREF kAccent = RGB(37, 99, 235);
constexpr COLORREF kAccentHover = RGB(29, 78, 216);
constexpr COLORREF kAccentPressed = RGB(30, 64, 175);
constexpr COLORREF kAccentSoft = RGB(238, 242, 255);
constexpr COLORREF kNeutralSoft = RGB(243, 244, 246);
constexpr COLORREF kRowHover = RGB(246, 248, 251);
constexpr COLORREF kRowSelected = RGB(235, 241, 254);
constexpr COLORREF kDanger = RGB(220, 38, 38);
constexpr COLORREF kDebrid = RGB(109, 40, 217);      // etapa no servidor do Real-Debrid
constexpr COLORREF kDebridSoft = RGB(243, 237, 255);
constexpr COLORREF kSuccess = RGB(22, 163, 74);

// Barra de progresso: trilho e preenchimento (dois tons: gradiente sutil) por estado.
constexpr COLORREF kTrack = RGB(237, 239, 243);
struct BarColors {
    COLORREF from;
    COLORREF to;
};
constexpr BarColors kBarActive{RGB(59, 130, 246), RGB(37, 99, 235)};
constexpr BarColors kBarRemote{RGB(167, 139, 250), RGB(124, 58, 237)};  // ainda no Real-Debrid
constexpr BarColors kBarPaused{RGB(203, 207, 214), RGB(176, 182, 191)};
constexpr BarColors kBarQueued{RGB(214, 219, 227), RGB(214, 219, 227)};
constexpr BarColors kBarFailed{RGB(248, 113, 113), RGB(220, 38, 38)};

// Liga/desliga o GDI+ (uma vez, em wWinMain).
void startup();
void shutdown();

// "Segoe UI Variable Text" no Windows 11; "Segoe UI" no 10.
const wchar_t* fontFace();
HFONT createFont(int points, int weight, UINT dpi);

// Formas suaves num HDC (coordenadas em pixels).
void fillRoundRect(HDC dc, const RECT& rect, int radius, COLORREF color);
void fillRoundRectGradient(HDC dc, const RECT& rect, int radius, COLORREF from, COLORREF to);
void fillRect(HDC dc, const RECT& rect, COLORREF color);

// Janela com a barra de título branca e borda discreta (Windows 11; no 10 não muda nada).
void applyWindowFrame(HWND window);

}  // namespace ui::theme
