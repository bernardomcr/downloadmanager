#pragma once

#include <string>

#include "engine/download_task.h"

namespace i18n {

// Mensagem traduzida para o motivo de falha de um download.
std::wstring describeError(dm::DownloadError error, unsigned long detail);

}  // namespace i18n
