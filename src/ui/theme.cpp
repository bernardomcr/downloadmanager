#include "ui/theme.h"

#include <dwmapi.h>

#include <algorithm>

// gdiplus.h usa min/max sem std:: (o projeto define NOMINMAX).
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <objidl.h>
#include <gdiplus.h>

namespace ui::theme {
namespace {

ULONG_PTR g_gdiplusToken = 0;

Gdiplus::Color color(COLORREF value) {
    return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
}

void roundRectPath(Gdiplus::GraphicsPath& path, const RECT& rect, int radius) {
    const float x = static_cast<float>(rect.left);
    const float y = static_cast<float>(rect.top);
    const float width = static_cast<float>(rect.right - rect.left);
    const float height = static_cast<float>(rect.bottom - rect.top);
    const float diameter = std::min({static_cast<float>(radius) * 2.0f, width, height});
    if (diameter <= 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, width, height));
        return;
    }
    path.AddArc(x, y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(x + width - diameter, y + height - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(x, y + height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

}  // namespace

void startup() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
}

void shutdown() {
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    g_gdiplusToken = 0;
}

const wchar_t* fontFace() {
    static const wchar_t* face = [] {
        // Fonte que não existe cai em outra qualquer: confere o nome que o Windows realmente usou.
        LOGFONTW wanted{};
        wanted.lfHeight = -12;
        wanted.lfCharSet = DEFAULT_CHARSET;
        lstrcpynW(wanted.lfFaceName, L"Segoe UI Variable Text", LF_FACESIZE);
        HFONT font = CreateFontIndirectW(&wanted);
        HDC dc = GetDC(nullptr);
        HGDIOBJ old = SelectObject(dc, font);
        wchar_t actual[LF_FACESIZE] = L"";
        GetTextFaceW(dc, LF_FACESIZE, actual);
        SelectObject(dc, old);
        ReleaseDC(nullptr, dc);
        DeleteObject(font);
        return lstrcmpiW(actual, L"Segoe UI Variable Text") == 0 ? L"Segoe UI Variable Text" : L"Segoe UI";
    }();
    return face;
}

HFONT createFont(int points, int weight, UINT dpi) {
    LOGFONTW font{};
    font.lfHeight = -MulDiv(points, static_cast<int>(dpi), 72);
    font.lfWeight = weight;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(font.lfFaceName, fontFace(), LF_FACESIZE);
    return CreateFontIndirectW(&font);
}

void fillRect(HDC dc, const RECT& rect, COLORREF value) {
    SetDCBrushColor(dc, value);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void fillRoundRect(HDC dc, const RECT& rect, int radius, COLORREF value) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    roundRectPath(path, rect, radius);
    Gdiplus::SolidBrush brush(color(value));
    graphics.FillPath(&brush, &path);
}

void fillRoundRectGradient(HDC dc, const RECT& rect, int radius, COLORREF from, COLORREF to) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::GraphicsPath path;
    roundRectPath(path, rect, radius);
    const Gdiplus::RectF bounds(static_cast<float>(rect.left) - 1.0f, static_cast<float>(rect.top),
                                static_cast<float>(rect.right - rect.left) + 2.0f,
                                static_cast<float>(rect.bottom - rect.top));
    Gdiplus::LinearGradientBrush brush(bounds, color(from), color(to), Gdiplus::LinearGradientModeHorizontal);
    graphics.FillPath(&brush, &path);
}

void applyWindowFrame(HWND window) {
    // DWMWA_CAPTION_COLOR (35) e DWMWA_BORDER_COLOR (34): Windows 11.
    const COLORREF caption = kBackground;
    DwmSetWindowAttribute(window, 35, &caption, sizeof(caption));
    const COLORREF border = RGB(218, 221, 226);
    DwmSetWindowAttribute(window, 34, &border, sizeof(border));
}

}  // namespace ui::theme
