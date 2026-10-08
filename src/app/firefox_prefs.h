#pragma once

#include <string>

namespace app {

// Pasta onde o Firefox salva downloads, em todos os perfis do usuário (vale a partir da próxima vez que
// o Firefox abrir). Extensões não conseguem mudar isso; o app grava um bloco marcado no user.js de cada
// perfil. folder vazio: volta para a pasta Downloads padrão.
void setFirefoxDownloadFolder(const std::wstring& folder);

// Texto do bloco do user.js (sem efeitos colaterais; usado e testado à parte).
std::string firefoxPrefsBlock(const std::wstring& folder);
// Troca (ou acrescenta) o bloco do app num user.js existente.
std::string replaceFirefoxPrefsBlock(const std::string& userJs, const std::string& block);

}  // namespace app
