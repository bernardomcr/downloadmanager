#include "core/command_line.h"

namespace dm {

std::string quoteArgument(const std::string& argument) {
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos) return argument;

    std::string quoted = "\"";
    size_t backslashes = 0;
    for (const char c : argument) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            // Barras antes de aspas precisam ser dobradas, e a aspa escapada.
            quoted.append(backslashes * 2 + 1, '\\');
        } else {
            quoted.append(backslashes, '\\');
        }
        backslashes = 0;
        quoted += c;
    }
    // Barras no final, antes da aspa de fechamento, também dobram.
    quoted.append(backslashes * 2, '\\');
    quoted += '"';
    return quoted;
}

std::string buildCommandLine(const std::string& program, const std::vector<std::string>& arguments) {
    std::string line = quoteArgument(program);
    for (const auto& argument : arguments) line += " " + quoteArgument(argument);
    return line;
}

}  // namespace dm
