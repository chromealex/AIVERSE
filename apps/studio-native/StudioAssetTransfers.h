// Preview-first, conflict-aware project/content file operations.
// Only trusted native pickers may supply external filesystem paths.
// No browser-supplied absolute path is ever accepted for an import.
#pragma once
#include "apps/studio-native/StudioAssetCatalog.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
namespace aiverse::studio {
enum class StudioTransferType { Move, External };
struct StudioTransferEntry {
    int id=0;
    std::filesystem::path source;
    std::filesystem::path destination;
    std::string relativeSource;
    std::string relativeDestination;
    std::string name;
    std::string guid;
    std::string conflict; // "", "name", "guid", "name+guid", "invalid-meta"
    bool directory=false;
};
struct StudioTransferDecision {
    int id=0;
    std::string action; // import, skip, keep-both, replace, new-guid
};
class StudioTransferPlan {
public:
    static StudioTransferPlan external(const std::filesystem::path& project,
        const std::string& destination,const std::vector<std::filesystem::path>& nativePickerFiles);
    static StudioTransferPlan internal(const std::filesystem::path& project,
        const std::string& destination,const std::vector<std::string>& contentPaths);
    Json describe() const;
    // Internal no-overwrite moves use an all-or-error reversible sequence.
    // The completion hook runs before the move is considered committed, so
    // dependent material writes can fail and trigger filesystem rollback.
    Json apply(const std::string& token,const std::vector<StudioTransferDecision>& decisions,
               const std::function<void()>& beforeCommit = {});
    const std::string& token()const{return token_;}
private:
    static StudioTransferPlan plan(const std::filesystem::path& project,
        const std::string& destination,StudioTransferType type,
        const std::vector<std::filesystem::path>& sources);
    std::filesystem::path project_;
    StudioTransferType type_=StudioTransferType::Move;
    std::string token_;
    std::vector<StudioTransferEntry> entries_;
};
std::string studioCreateContentFolder(const std::filesystem::path& project,
                                     const std::string& parent,const std::string& name);
}
