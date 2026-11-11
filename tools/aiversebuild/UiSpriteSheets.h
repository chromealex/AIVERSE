#pragma once
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace aiverse::build {
// RCSS spritesheets use pixel coordinates, so resizing their source invalidates the sheet.
inline std::vector<std::string> uiSpriteSheetImages(const std::string& css, const std::string& path) {
    static const std::regex comments(R"(/\*[\s\S]*?\*/)");
    static const std::regex sheets(R"(@spritesheet\s+[^{}]+\{([^{}]*)\})", std::regex::icase);
    static const std::regex source(R"(\bsrc\s*:\s*([^;]+);)", std::regex::icase);
    std::string clean = std::regex_replace(css, comments, "");
    std::vector<std::string> result;
    for (std::sregex_iterator i(clean.begin(), clean.end(), sheets), end; i != end; ++i) {
        std::string body = (*i)[1].str();
        std::smatch match;
        if (!std::regex_search(body, match, source)) continue;
        std::string name = match[1].str();
        auto trim = [&] { auto a = name.find_first_not_of(" \t\r\n\"'"); auto b = name.find_last_not_of(" \t\r\n\"'"); name = a == std::string::npos ? "" : name.substr(a, b-a+1); };
        trim();
        if (name.rfind("url(", 0) == 0 && name.back() == ')') { name = name.substr(4, name.size()-5); trim(); }
        if (name.empty()) continue;
        result.push_back((std::filesystem::path(path).parent_path() / name).lexically_normal().generic_string());
    }
    return result;
}
} // namespace aiverse::build
