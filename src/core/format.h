#pragma once

#include <cstdint>
#include <string>

namespace dm {

// "1,5 MB", "820 KB"... (unidades de 1024, uma casa decimal; separador decimal conforme o idioma)
std::string formatBytes(int64_t bytes, char decimalSeparator = ',');
// "3,2 MB/s"; bits: "25,6 Mb/s" (unidades de 1000, como a velocidade da internet é anunciada)
std::string formatSpeed(double bytesPerSecond, char decimalSeparator = ',', bool bits = false);
// "1:02:03", "4:05", "0:09"; vazio quando desconhecido
std::string formatDuration(int64_t seconds);

}  // namespace dm
