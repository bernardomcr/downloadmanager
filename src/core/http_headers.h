#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dm {

struct ContentRange {
    int64_t first = 0;
    int64_t last = 0;
    int64_t total = -1;  // -1 quando o servidor responde "*"
};

// "bytes 0-99/1000" -> {0, 99, 1000}
std::optional<ContentRange> parseContentRange(const std::string& header);

// Nome do arquivo em Content-Disposition (prefere filename*=UTF-8''...). Vazio se não houver.
std::string fileNameFromContentDisposition(const std::string& header);

// Último trecho do caminho da URL, sem query, decodificado. Vazio se não houver.
std::string fileNameFromUrl(const std::string& url);

// Remove caracteres proibidos no Windows, nomes reservados (CON, NUL...) e limita o tamanho.
std::string sanitizeFileName(const std::string& name);

std::string percentDecode(const std::string& text);

}  // namespace dm
