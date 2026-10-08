#include "i18n/errors.h"

#include <cwchar>

#include "i18n/strings.h"
#include "util/unicode.h"

namespace i18n {

std::wstring describeDebridError(dm::DebridError error) {
    switch (error) {
        case dm::DebridError::None: return {};
        case dm::DebridError::NotConnected: return tr(Str::ErrDebridNotConnected);
        case dm::DebridError::BadToken: return tr(Str::ErrDebridBadToken);
        case dm::DebridError::NotPremium: return tr(Str::ErrDebridNotPremium);
        case dm::DebridError::TorrentFailed: return tr(Str::ErrDebridTorrentFailed);
        case dm::DebridError::TooManyTorrents: return tr(Str::ErrDebridTooMany);
        case dm::DebridError::Unavailable: return tr(Str::ErrDebridUnavailable);
        default: return tr(Str::ErrDebridOther);
    }
}

std::wstring describeError(dm::DownloadError error, unsigned long detail, const std::string& text) {
    Str id;
    switch (error) {
        case dm::DownloadError::None: return {};
        case dm::DownloadError::InvalidUrl: id = Str::ErrInvalidUrl; break;
        case dm::DownloadError::NameNotResolved: id = Str::ErrNameNotResolved; break;
        case dm::DownloadError::CannotConnect: id = Str::ErrCannotConnect; break;
        case dm::DownloadError::Timeout: id = Str::ErrTimeout; break;
        case dm::DownloadError::ConnectionLost: id = Str::ErrConnectionLost; break;
        case dm::DownloadError::SecureConnection: id = Str::ErrSecureConnection; break;
        case dm::DownloadError::HttpStatus: id = Str::ErrHttpStatus; break;
        case dm::DownloadError::LinkExpired: id = Str::ErrLinkExpired; break;
        case dm::DownloadError::ServerChanged: id = Str::ErrServerChanged; break;
        case dm::DownloadError::DiskFull: id = Str::ErrDiskFull; break;
        case dm::DownloadError::AccessDenied: id = Str::ErrAccessDenied; break;
        case dm::DownloadError::FileSystem: id = Str::ErrFileSystem; break;
        case dm::DownloadError::Protected: id = Str::ErrProtected; break;
        case dm::DownloadError::WebPage: id = Str::ErrWebPage; break;
        case dm::DownloadError::Debrid: return describeDebridError(static_cast<dm::DebridError>(detail));
        case dm::DownloadError::ToolFailed: {
            wchar_t buffer[512];
            std::swprintf(buffer, 512, tr(Str::ErrToolFailed), dm::toWide(text.substr(0, 400)).c_str());
            return buffer;
        }
        default: id = Str::ErrNetwork; break;
    }
    wchar_t buffer[256];
    std::swprintf(buffer, 256, tr(id), detail);
    return buffer;
}

}  // namespace i18n
