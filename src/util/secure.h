#pragma once

#include <optional>
#include <string>

namespace dm {

// Criptografa com a conta do Windows (DPAPI): só o mesmo usuário, no mesmo PC, consegue ler.
// Usado para guardar cookies dos downloads em disco. Resultado em base64.
std::string protectForCurrentUser(const std::string& plain);
std::optional<std::string> unprotectForCurrentUser(const std::string& encoded);

}  // namespace dm
