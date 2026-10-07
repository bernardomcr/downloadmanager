#pragma once

#include <string>

namespace app {

// Registra o dm-host.exe (mesma pasta do app) como ponte de Native Messaging para Firefox, Chrome,
// Edge e Brave, no registro do usuário (HKCU). Chamado a cada início: corrige o caminho se o app mudou de pasta.
// Não faz nada se o dm-host.exe não estiver junto do app.
void registerBrowserIntegration(const std::wstring& dataDirectory);
// Desinstalação: tira a ponte dos navegadores (e o .xpi registrado no Firefox).
void unregisterBrowserIntegration();

}  // namespace app
