#pragma once

#include <string>

#include "core/debrid.h"
#include "engine/task.h"

namespace i18n {

// Mensagem traduzida para o motivo de falha de um download.
// `text`: mensagem da ferramenta (UTF-8), usada em ToolFailed.
std::wstring describeError(dm::DownloadError error, unsigned long detail, const std::string& text = {});

std::wstring describeDebridError(dm::DebridError error);

}  // namespace i18n
