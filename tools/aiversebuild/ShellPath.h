#pragma once
#include <filesystem>
#include <string>

namespace aiverse::build {

inline std::string quote(const std::filesystem::path& p) {
#if defined(_WIN32)
    return "\"" + p.string() + "\"";
#else
    std::string s = p.string(), o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
#endif
}

} // namespace aiverse::build
