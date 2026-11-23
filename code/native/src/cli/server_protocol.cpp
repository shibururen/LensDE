// Parse quoted request lines and serve repeated native commands through the ready/done protocol.
// Each request owns its argv storage and delegates to a fresh single-command context.
// Preserve quoting, sentinel framing, per-request exception handling, and sequential response order.

#include "cli_internal.hpp"

namespace lensde { namespace cli {
using namespace detail;

std::vector<std::string> split_server_command_line(const std::string& line) {
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    char quote_char = '\0';
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (in_quotes && ch == '\\' && i + 1 < line.size() &&
            (line[i + 1] == quote_char || line[i + 1] == '\\')) {
            current.push_back(line[i + 1]);
            ++i;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            if (in_quotes && ch == quote_char) {
                in_quotes = false;
                quote_char = '\0';
            } else if (!in_quotes) {
                in_quotes = true;
                quote_char = ch;
            } else {
                current.push_back(ch);
            }
            continue;
        }
        if (!in_quotes && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        args.push_back(current);
    }
    return args;
}

constexpr const char* kServerReady = "__OPTILAND_SERVER_READY__";
constexpr const char* kServerDone = "__OPTILAND_SERVER_DONE__";

int run_server_mode() {
    std::cout << kServerReady << "\n" << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        if (line == "__quit__" || line == "quit" || line == "exit") {
            break;
        }
        std::vector<std::string> owned;
        owned.push_back("optiland_server");
        std::vector<std::string> parsed = split_server_command_line(line);
        owned.insert(owned.end(), parsed.begin(), parsed.end());
        std::vector<char*> command_argv;
        command_argv.reserve(owned.size());
        for (std::string& value : owned) {
            command_argv.push_back(value.data());
        }
        int code = 0;
        try {
            code = run_single_command(static_cast<int>(command_argv.size()), command_argv.data());
        } catch (const std::exception& exc) {
            std::cerr << "Server command failed: " << exc.what() << "\n";
            code = 99;
        } catch (...) {
            std::cerr << "Server command failed with an unknown exception\n";
            code = 99;
        }
        std::cout << kServerDone << " " << code << "\n" << std::flush;
    }
    return 0;
}

} }
