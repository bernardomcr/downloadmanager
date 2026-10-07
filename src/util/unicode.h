#pragma once

#include <string>

namespace dm {

// Conversões UTF-8 <-> UTF-16 (Windows usa UTF-16 nas APIs "W"; o núcleo portátil usa UTF-8).
std::wstring toWide(const std::string& utf8);
std::string toUtf8(const std::wstring& wide);

}  // namespace dm
