#pragma once

#include <string>

namespace app {

// Programa que extrai compactados: o do usuário (NanaZip, 7-Zip ou WinRAR), com a janela de progresso
// dele; sem nenhum instalado, o tar.exe do Windows (escondido).
struct Extractor {
    enum class Kind { None, SevenZip, WinRar, Tar };  // SevenZip vale também para o NanaZip (mesma linha de comando)
    Kind kind = Kind::None;
    std::wstring exe;
    std::wstring name;  // "NanaZip", "7-Zip", "WinRAR", "tar"
};

// Para a extensão (sem ponto): primeiro o programa associado a ela no Windows, se for um dos conhecidos;
// depois NanaZip, 7-Zip e WinRAR instalados; por fim o tar.
Extractor findExtractor(const std::wstring& extension);

// Extrai `archive` dentro de `destination` (criada se preciso) e espera terminar. true = sucesso.
bool extractArchive(const std::wstring& archive, const std::wstring& destination);

}  // namespace app
