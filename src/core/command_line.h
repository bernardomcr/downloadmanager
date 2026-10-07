#pragma once

#include <string>
#include <vector>

namespace dm {

// Monta uma linha de comando do Windows em que cada argumento chega intacto ao programa
// (regras de aspas e barras do CommandLineToArgvW / runtime do C). UTF-8.
std::string quoteArgument(const std::string& argument);
std::string buildCommandLine(const std::string& program, const std::vector<std::string>& arguments);

}  // namespace dm
