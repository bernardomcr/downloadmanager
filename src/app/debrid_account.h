#pragma once

#include <windows.h>

#include <string>

#include "core/debrid.h"

namespace app {

// Resultado de conferir um token do Real-Debrid (GET /user).
struct DebridAccountCheck {
    bool ok = false;
    dm::DebridError error = dm::DebridError::None;
    DWORD networkError = 0;  // != 0: não chegou ao Real-Debrid
    dm::DebridUser user;
};

// Bloqueia (rede): chamar fora da thread da interface.
DebridAccountCheck checkDebridAccount(const std::string& token);

}  // namespace app
