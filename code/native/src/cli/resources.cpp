// Locate the project-local glass database and provide output directory/path helpers.
// Native command preparation uses these paths instead of depending on an old research workspace.
// Keep resource discovery fallbacks and output-directory errors explicit; no optical defaults belong here.

#include "cli_internal.hpp"

namespace lensde { namespace cli { namespace detail {

std::string lensde_default_database() {
    if (const char* configured = std::getenv("LENSDE_DATABASE_ROOT")) {
        if (*configured) return std::string(configured);
    }
    std::string directory;
#ifdef _WIN32
    char executable[32768] = {};
    const DWORD length = GetModuleFileNameA(nullptr, executable, sizeof(executable));
    if (length > 0 && length < sizeof(executable)) {
        directory.assign(executable, length);
        const auto slash = directory.find_last_of("/\\");
        directory = slash == std::string::npos ? "." : directory.substr(0, slash);
    }
#else
    if (const char* current = std::getenv("PWD")) directory = current;
#endif
    if (directory.empty()) directory = ".";
    const std::string fallback = directory + "/../../catalogs/database";
    for (int level = 0; level < 12 && !directory.empty(); ++level) {
        const std::string candidate = directory + "/catalogs/database";
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesA(candidate.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) return candidate;
#else
        struct stat info;
        if (stat(candidate.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) return candidate;
#endif
        const auto slash = directory.find_last_of("/\\");
        if (slash == std::string::npos) break;
        directory.resize(slash);
    }
    return fallback;
}

void ensure_output_directory(const std::string& path) {
    if (path.empty()) {
        return;
    }
#ifdef _WIN32
    auto is_sep = [](char ch) { return ch == '\\' || ch == '/'; };
    auto is_root = [&](const std::string& value) {
        return value.empty() ||
               (value.size() == 2 && value[1] == ':') ||
               (value.size() == 3 && value[1] == ':' && is_sep(value[2])) ||
               (value.size() == 1 && is_sep(value[0]));
    };
    std::string normalized = path;
    for (char& ch : normalized) {
        if (ch == '/') {
            ch = '\\';
        }
    }
    while (!normalized.empty() && is_sep(normalized.back()) && !is_root(normalized)) {
        normalized.pop_back();
    }
    for (std::size_t pos = 0; pos <= normalized.size(); ++pos) {
        if (pos != normalized.size() && !is_sep(normalized[pos])) {
            continue;
        }
        const std::string part = normalized.substr(0, pos);
        if (is_root(part)) {
            continue;
        }
        if (!CreateDirectoryA(part.c_str(), nullptr)) {
            const DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                break;
            }
        }
    }
#else
    mkdir(path.c_str(), 0755);
#endif
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }
    const char last = dir.back();
    if (last == '\\' || last == '/') {
        return dir + name;
    }
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

} } }
