#pragma once

#include <string>
#include <vector>

namespace dm {

// Compactado que o app sabe extrair ("zip", "rar", "7z", "tar", "gz"...), pela extensão sem ponto.
bool isArchiveExtension(const std::string& extension);

// Caminhos das entradas a partir da saída de listagem:
//  sevenZipSlt = true: "7z l -slt" (linhas "Path = ..."; o cabeçalho antes de "----------" é do próprio arquivo);
//  false: um caminho por linha ("tar -tf", "rar lb").
std::vector<std::string> parseArchiveListing(const std::string& output, bool sevenZipSlt);

// Nomes distintos no primeiro nível ("pasta/a.txt" e "pasta/b.txt" -> {"pasta"}).
std::vector<std::string> topLevelNames(const std::vector<std::string>& paths);

}  // namespace dm
