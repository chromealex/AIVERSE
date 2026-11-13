#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace aiverse::build {
// Asset packs and native modules can carry their original license files. Keep these
// outside content.rpak too, including when packaging rules exclude text files.
inline void collectProjectNotices(const std::filesystem::path& project,
                                  const std::filesystem::path& out,
                                  const std::filesystem::path& destination) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(project); it != fs::recursive_directory_iterator(); ++it) {
        const auto name = it->path().filename().string();
        if (it->is_directory()) {
            if (name == "Builds" || name == "build" || name == "logs" || name == "node_modules" ||
                (!name.empty() && name[0] == '.') || it->path().lexically_normal() == out.lexically_normal())
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file() || it->is_symlink()) continue;
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        auto legalName = [&](const std::string& base) {
            return upper == base || upper.rfind(base + ".", 0) == 0 || upper.rfind(base + "-", 0) == 0 || upper.rfind(base + "_", 0) == 0;
        };
        if (legalName("LICENSE") || legalName("LICENCE") || legalName("COPYING") || legalName("NOTICE") ||
            legalName("OFL") || legalName("LICENSES") || legalName("NOTICES") || upper == "THIRD_PARTY_NOTICES.TXT") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    std::string text = "Game asset and native-module notices supplied with this project.\n";
    for (const auto& file : files) {
        std::ifstream f(file, std::ios::binary);
        std::stringstream content; content << f.rdbuf();
        if (!f || content.str().find_first_not_of(" \t\r\n") == std::string::npos)
            throw std::runtime_error("Missing or empty project license: " + file.string());
        text += "\n=== " + fs::relative(file, project).generic_string() + " ===\n" + content.str() + "\n";
    }
    fs::create_directories(destination.parent_path());
    std::ofstream f(destination, std::ios::binary); f << text;
    if (!f) throw std::runtime_error("Cannot write project notices: " + destination.string());
}
} // namespace aiverse::build
