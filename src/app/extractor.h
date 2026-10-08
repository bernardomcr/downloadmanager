#pragma once

#include <string>

namespace app {

// Programa que extrai compactados: o do usuário (NanaZip, 7-Zip ou WinRAR), com a janela de progresso
// dele; sem nenhum instalado, o tar.exe do Windows (escondido).
struct Extractor {
    enum class Kind { None, SevenZip, WinRar, Tar };  // SevenZip vale também para o NanaZip (mesma linha de comando)
    Kind kind = Kind::None;
    std::wstring exe;     // com janela de progresso (NanaZipG, 7zG, WinRAR)
    std::wstring lister;  // de console, para listar o conteúdo (NanaZipC, 7z, Rar/UnRAR, tar)
    std::wstring name;    // "NanaZip", "7-Zip", "WinRAR", "tar"
};

// Para a extensão (sem ponto): primeiro o programa associado a ela no Windows, se for um dos conhecidos;
// depois NanaZip, 7-Zip e WinRAR instalados; por fim o tar.
Extractor findExtractor(const std::wstring& extension);
Extractor findExtractorFor(const std::wstring& archive);

struct ExtractResult {
    bool ok = false;
    std::wstring path;  // pasta criada, ou o item único extraído ao lado do compactado
};

// Extrair inteligente (como o "Smart Extraction" do NanaZip): se o compactado tem um único item na raiz,
// extrai ao lado dele; senão, numa pasta com o nome do arquivo. Nunca sobrescreve nada. Espera terminar.
ExtractResult smartExtract(const std::wstring& archive);

}  // namespace app
