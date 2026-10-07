#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dm {

// Regra de organização "SE → ENTÃO", aplicada quando um download termina.
// Condições vazias valem para tudo; as preenchidas precisam bater todas.
struct Rule {
    enum class Kind { Any, File, Video };

    std::string name;
    bool enabled = true;

    // SE
    std::vector<std::string> extensions;  // sem ponto, minúsculas: "zip", "rar"
    std::vector<std::string> sites;       // "youtube.com" vale também para "www.youtube.com"
    std::string nameContains;             // trecho do nome do arquivo (sem diferenciar maiúsculas)
    int64_t minSize = 0;                  // bytes; 0 = sem mínimo
    int64_t maxSize = 0;                  // bytes; 0 = sem máximo
    Kind kind = Kind::Any;

    // ENTÃO
    std::string folder;                   // relativa à pasta padrão (ex.: "Compactados") ou absoluta
    bool extract = false;                 // compactados: extrair numa pasta com o nome do arquivo
    bool deleteArchive = false;           // depois de extrair com sucesso
    bool openFile = false;
    bool openFolder = false;
};

// O que se sabe do download na hora de aplicar as regras. UTF-8.
struct DownloadFacts {
    std::string url;
    std::string fileName;
    int64_t size = -1;
    bool isVideo = false;
};

// Primeira regra ligada que bate (a ordem da lista importa), ou nullptr.
const Rule* matchRule(const std::vector<Rule>& rules, const DownloadFacts& facts);

// Regras de fábrica (equivalentes às categorias do IDM). `portuguese` escolhe os nomes das pastas.
std::vector<Rule> defaultRules(bool portuguese);

std::string serializeRules(const std::vector<Rule>& rules);
std::vector<Rule> parseRules(const std::string& text);

// Pasta final: absoluta ("C:\\x", "\\\\servidor\\x") fica como está; relativa vai dentro de `base`.
std::string resolveRuleFolder(const std::string& folder, const std::string& base);

// "zip, .RAR ,7z" -> {"zip", "rar", "7z"}; também serve para sites.
std::vector<std::string> splitList(const std::string& text);
std::string joinList(const std::vector<std::string>& items);

// Extensão do arquivo em minúsculas, sem ponto ("arquivo.tar.gz" -> "gz").
std::string fileExtension(const std::string& fileName);

}  // namespace dm
