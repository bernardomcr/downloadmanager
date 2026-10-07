#pragma once

#include <optional>
#include <string>

namespace dm {

std::string base64Encode(const std::string& data);
std::optional<std::string> base64Decode(const std::string& text);

}  // namespace dm
