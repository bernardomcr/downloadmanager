#include "app/debrid_account.h"

#include "engine/http.h"
#include "util/unicode.h"
#include "version.h"

namespace app {

DebridAccountCheck checkDebridAccount(const std::string& token) {
    DebridAccountCheck check;
    dm::HttpSession session(L"DownloadManager/" + dm::toWide(DM_VERSION_STRING));
    const dm::ApiResponse response =
        dm::httpCall(session, L"GET", std::string(dm::kRealDebridApi) + "/user", {{"Authorization", "Bearer " + token}});
    if (response.status == 0) {
        check.networkError = response.error == 0 ? ERROR_WINHTTP_CANNOT_CONNECT : response.error;
        return check;
    }
    check.error = dm::parseDebridError(response.status, response.body);
    if (check.error != dm::DebridError::None) return check;
    const auto user = dm::parseDebridUser(response.body);
    if (!user) {
        check.error = dm::DebridError::Other;
        return check;
    }
    check.user = *user;
    check.ok = true;
    return check;
}

}  // namespace app
