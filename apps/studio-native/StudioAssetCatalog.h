// Asset identities and project-local SQLite lookup for AIVERSE Studio.
// Sidecar .meta files are authoritative. SQLite is a disposable index.
// Never store compiled GPU artifacts or editor selection state in a .meta.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>
#include "engine/data/Json.h"
struct sqlite3;
namespace aiverse::studio {
struct StudioAssetEntry {
    std::string guid;       // 32 lower-case hex chars, persistent across rename/move
    std::string path;       // content-relative, normalized '/'
    std::string importer;   // texture/model/audio/data/folder
    bool directory = false;
    std::int64_t bytes = 0;
    Json settings = Json::object();
};
bool studioValidGuid(const std::string& value);
Json studioDefaultSettings(const std::string& importer);
std::string studioImporterForPath(const std::filesystem::path& path, bool directory);
std::filesystem::path studioMetaPath(const std::filesystem::path& contentFile);
class StudioAssetCatalog {
public:
    explicit StudioAssetCatalog(const std::filesystem::path& project);
    StudioAssetCatalog(const StudioAssetCatalog&) = delete;
    StudioAssetCatalog& operator=(const StudioAssetCatalog&) = delete;
    ~StudioAssetCatalog();
    void rebuild(); // scans content and reconciles .meta -> SQLite in a transaction
    std::vector<StudioAssetEntry> all() const;
    std::optional<StudioAssetEntry> byGuid(const std::string& guid) const;
    std::optional<StudioAssetEntry> byPath(const std::string& path) const;
    StudioAssetEntry patchSettings(const std::string& guid,
                                   const std::string& key,const Json& value);
    const std::filesystem::path& project() const { return project_; }
private:
    void exec(const char* sql) const;
    StudioAssetEntry inspect(const std::filesystem::path& file,bool directory) const;
    std::filesystem::path project_;
    sqlite3* db_ = nullptr;
};
}
