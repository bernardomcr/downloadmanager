#pragma once

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "core/video.h"

namespace app {
class VideoTools;
}

namespace ui {

struct VideoRequest {
    std::wstring url;
    std::wstring folder;
    std::wstring title;  // nome sugerido (ex.: título da aba, para streams); vazio = título do site
    std::vector<std::pair<std::string, std::string>> headers;  // Cookie, Referer...
    std::string userAgent;
};

struct VideoChoice {
    struct Item {
        std::wstring url;
        std::wstring title;
    };
    std::vector<Item> items;  // um vídeo, ou os marcados da playlist
    dm::VideoFormat format;
    bool subtitles = false;
    std::wstring folder;
    bool downloadAsFile = false;  // não era vídeo: baixar o link como arquivo comum
};

// Prepara o yt-dlp/ffmpeg se preciso, analisa o link e deixa escolher qualidade e itens.
bool showVideoDialog(HWND owner, app::VideoTools& tools, const VideoRequest& request, VideoChoice& choice);

}  // namespace ui
