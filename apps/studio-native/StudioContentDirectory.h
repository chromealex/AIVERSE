// AIVERSE Studio: project/content is the ONLY root of the HTML5 Assets browser.
// This is an editor read-only listing, NOT a general project filesystem API.
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace aiverse::studio {

struct StudioContentAsset {
    std::string path; // relative to project/content, never project-relative
    std::int64_t bytes = 0;
};

inline std::filesystem::path studioContentRoot(const std::filesystem::path& projectDirectory) {
    namespace fs = std::filesystem;
    std::error_code error;
    if (projectDirectory.empty() || !fs::is_directory(projectDirectory, error)) {
        throw std::runtime_error("Cannot access the AIVERSE project directory");
    }

    const fs::path content = projectDirectory / "content";
    error.clear();
    const fs::file_status status = fs::symlink_status(content, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::runtime_error("Cannot inspect project/content: " + error.message());
    }
    if (fs::is_symlink(status)) {
        throw std::runtime_error("project/content must be a real directory, not a symbolic link");
    }
    if (fs::exists(status)) {
        if (!fs::is_directory(status)) {
            throw std::runtime_error("project/content exists but is not a directory");
        }
    } else {
        error.clear();
        fs::create_directory(content, error);
        if (error) {
            throw std::runtime_error("Cannot create project/content: " + error.message());
        }
    }
    error.clear();
    const fs::file_status after = fs::symlink_status(content, error);
    if (error || fs::is_symlink(after) || !fs::is_directory(after)) {
        throw std::runtime_error("project/content is not a safe directory");
    }
    return content;
}

inline std::vector<StudioContentAsset> studioContentFiles(
    const std::filesystem::path& projectDirectory, std::size_t limit = 2000) {
    namespace fs = std::filesystem;
    const fs::path root = studioContentRoot(projectDirectory);
    std::vector<StudioContentAsset> result;
    if (limit == 0) return result;
    result.reserve(std::min(limit, std::size_t(256)));

    std::error_code error;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
    if (error) throw std::runtime_error("Cannot enumerate project/content: " + error.message());
    const fs::recursive_directory_iterator end;
    for (; it != end && result.size() < limit; it.increment(error)) {
        if (error) {
            throw std::runtime_error("Error while listing project/content: " + error.message());
        }
        // Use symlink_status, NOT is_regular_file(): file symlinks can point
        // outside project/content even when directory symlinks are not followed.
        std::error_code fileError;
        const fs::file_status status = it->symlink_status(fileError);
        if (fileError || !fs::is_regular_file(status)) continue;
        const std::string name=it->path().filename().string();
        if(it->path().extension()==".meta" ||
           name.find(".aiverse-backup-")!=std::string::npos ||
           name.find(".aiverse-meta-update")!=std::string::npos ||
           name.find(".aiverse-tmp")!=std::string::npos)continue;
        const fs::path relative = it->path().lexically_relative(root);
        if (relative.empty() || relative.is_absolute()) continue;

        bool safe = true;
        for (const fs::path& component : relative) {
            if (component.empty() || component == "." || component == "..") {
                safe = false;
                break;
            }
        }
        if (!safe) continue;
        const std::uintmax_t fileSize = it->file_size(fileError);
        if (fileError) continue;
        result.push_back({
            relative.generic_string(),
            static_cast<std::int64_t>(std::min(fileSize,
                static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())))
        });
    }
    if (error) throw std::runtime_error("Cannot complete project/content scan: " + error.message());
    std::sort(result.begin(), result.end(),
              [](const StudioContentAsset& a, const StudioContentAsset& b) { return a.path < b.path; });
    return result;
}

} // namespace aiverse::studio
