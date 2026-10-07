#pragma once

#include <string>

#include "engine/task.h"

namespace i18n {

// Mensagem traduzida para o motivo de falha de um download.
// `text`: mensagem da ferramenta (UTF-8), usada em ToolFailed.
std::wstring describeError(dm::DownloadError error, unsigned long detail, const std::string& text = {});

}  // namespace i18n
