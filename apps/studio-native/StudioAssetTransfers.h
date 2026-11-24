// Preview-first, conflict-aware project/content file operations.
// Only trusted native pickers may supply external filesystem paths.
// No browser-supplied absolute path is ever accepted for an import.
#pragma once
#include "apps/studio-native/StudioAssetCatalog.h"
#include <filesystem>
#include <functional>
#include <memory>
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
    std::string replacementGuid,replacementDigest,replacementMetaDigest,replacementSourceDigest;
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
    static StudioTransferPlan rename(const std::filesystem::path& project,
        const std::string& contentPath,const std::string& newName);
    Json describe() const;
    // Internal no-overwrite moves use an all-or-error reversible sequence.
    // The completion hook runs before the move is considered committed, so
    // dependent material writes can fail and trigger filesystem rollback.
    Json apply(const std::string& token,const std::vector<StudioTransferDecision>& decisions,
               const std::function<void()>& beforeCommit = {},
               std::optional<editor::ProjectHistory::MovePlan>* history = nullptr,bool repairResources=false);
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
// Prepared relocation operations are project-relative, never browser input.
struct StudioBatchRelocation {std::string from,to,digest;bool directory=false;};
struct StudioBatchResourceEdit {
    editor::ProjectFileEdit edit; // original project-relative physical path
    std::string destination;      // final project-relative physical path
};
struct StudioBatchAssetPlan {
    std::vector<StudioBatchRelocation> relocations;
    std::vector<editor::ProjectFileEdit> documents;
    std::vector<StudioBatchResourceEdit> movingResources;
};
// Plan ordinary move chains using virtual directional file/tree digests.
// Resource edits retain initial and projected final physical identities.
StudioBatchAssetPlan studioPlanAssetMoveChain(const std::filesystem::path& project,
    const std::vector<editor::ProjectHistory::MovePlan>& ordered,bool redo);
// File-only mixed history: ordinary moves, reversible Trash operations,
// replacement versions and importer settings. No resource graph rewrites.
bool studioCanPlanMixedAssetChain(const std::vector<editor::ProjectHistory::Step>& ordered);
StudioBatchAssetPlan studioPlanMixedAssetChain(const std::filesystem::path& project,
    const std::vector<editor::ProjectHistory::Step>& ordered,bool redo);
// Coordinator supports exact endpoint chains and disjoint document edits.
void studioApplyHistoryBatch(const std::filesystem::path& project,
    const std::vector<StudioBatchRelocation>& relocations,
    const std::vector<editor::ProjectFileEdit>& documents,
    const std::vector<StudioBatchResourceEdit>& movingResources={});
void studioRecoverHistoryBatch(const std::filesystem::path& project);
// Replay a native recorded file or directory move, never a browser-supplied path list.
void studioReplayAssetMove(const std::filesystem::path& project,
                           const editor::ProjectHistory::MovePlan& plan,bool redo,
                           const std::function<void()>& validateReferences = {},StudioBatchAssetPlan* staged=nullptr);
void studioReplayAssetReplacement(const std::filesystem::path& project,
    const editor::ProjectHistory::MovePlan& plan,bool redo,StudioBatchAssetPlan* staged=nullptr);
// Soft-delete one asset to persistent project-local quarantine; never purge it on history eviction.
editor::ProjectHistory::MovePlan studioQuarantineAsset(const std::filesystem::path& project,
    const std::string& path,const std::string& expectedGuid,const std::function<void()>& validateReferences={});
void studioReplayAssetQuarantine(const std::filesystem::path& project,
    const editor::ProjectHistory::MovePlan& plan,bool deleted,const std::function<void()>& validateReferences={},StudioBatchAssetPlan* staged=nullptr);
// Persistent asset timeline, separate from the recovery payload store.
// A durable dirty marker fences interrupted publication; restore only exact
// verified content/material/recovery-slot state, never infer missing history.
class StudioAssetHistoryPublication {
public:
    explicit StudioAssetHistoryPublication(const std::filesystem::path& project);
    ~StudioAssetHistoryPublication();
    StudioAssetHistoryPublication(const StudioAssetHistoryPublication&)=delete;
    StudioAssetHistoryPublication& operator=(const StudioAssetHistoryPublication&)=delete;
    void save(const editor::ProjectHistory& history);
    // Rejected calls preserve the previous checkpoint only if the entire
    // owner timeline and filesystem state are still exactly the same.
    bool cancelUnchanged(const editor::ProjectHistory& history);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
editor::ProjectHistory studioLoadAssetHistoryCheckpoint(const std::filesystem::path& project);
void studioRecoverAssetJournal(const std::filesystem::path& project);
// Recovery browser: list persisted slots; review exact current identity before restore/purge.
// Project-local logical-byte budget. Zero preserves the unlimited legacy policy.
Json studioRecoveryStorage(const std::filesystem::path& project);
Json studioSetRecoveryQuota(const std::filesystem::path& project,int64_t expectedMiB,int64_t quotaMiB);
Json studioAssetTrashList(const std::filesystem::path& project,size_t offset,size_t limit);
std::vector<std::string> studioProtectedTrashIds(const editor::ProjectHistory& history);
Json studioReviewTrashBatch(const std::filesystem::path& project,const std::vector<std::string>& ids,const editor::ProjectHistory& history);
Json studioApplyTrashBatch(const std::filesystem::path& project,const std::vector<std::string>& ids,const std::string& token,const editor::ProjectHistory& history);
Json studioAssetTrashReview(const std::filesystem::path& project,const std::string& id,const std::string& action);
void studioAssetTrashApply(const std::filesystem::path& project,const std::string& id,const std::string& action,const std::string& expectedToken);
std::string studioCreateContentFolder(const std::filesystem::path& project,
                                     const std::string& parent,const std::string& name,
                                     editor::ProjectHistory::MovePlan* history=nullptr);
}
