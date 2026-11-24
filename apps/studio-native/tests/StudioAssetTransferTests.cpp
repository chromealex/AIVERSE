#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "tests/TestCheck.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs=std::filesystem;
using aiverse::studio::StudioAssetCatalog;
using aiverse::studio::StudioTransferDecision;
using aiverse::studio::StudioTransferPlan;

void write(const fs::path& path,const std::string& text){
    std::ofstream file(path,std::ios::binary);
    AIVERSE_TEST_CHECK(file);
    file<<text;
}
std::string read(const fs::path& path){
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
int main(){
    const fs::path temp=fs::temp_directory_path()/("studio-transfer-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path project=temp/"Project", external=temp/"External";
    fs::create_directories(project/"content"/"Models");
    fs::create_directories(project/"content"/"Other");
    fs::create_directories(external);
    try{
        const std::string folder=aiverse::studio::studioCreateContentFolder(project,"Models","Vehicles");
        AIVERSE_TEST_CHECK(folder=="Models/Vehicles");
        write(project/"content"/"Models"/"Ship.glb","original");
        write(external/"Ship.glb","replacement");
        write(external/"Drone.glb","new");
        StudioAssetCatalog db(project);
        db.rebuild();
        const auto original=db.byPath("Models/Ship.glb");
        AIVERSE_TEST_CHECK(original);
        const auto stableGuid=original->guid;

        // Preview name collision, skip by default and leave content unchanged.
        auto skip=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        AIVERSE_TEST_CHECK(skip.describe()["items"][0].getStr("conflict")=="name");
        auto skipped=skip.apply(skip.token(),{{0,"skip"}});
        AIVERSE_TEST_CHECK(skipped["files"].size()==0);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship.glb")=="original");

        // Keep both: no existing file touched, fresh GUID for the copy.
        auto both=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        auto copied=both.apply(both.token(),{{0,"keep-both"}});
        AIVERSE_TEST_CHECK(copied["files"].size()==1);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship (1).glb")=="replacement");
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Ship (1).glb")->guid!=stableGuid);

        // Replace is explicit and preserves destination sidecar/GUID.
        auto replace=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        auto replaced=replace.apply(replace.token(),{{0,"replace"}});
        AIVERSE_TEST_CHECK(replaced["files"].size()==1);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship.glb")=="replacement");
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Ship.glb")->guid==stableGuid);

        // A GUID lookup follows internal file+meta move to a different folder.
        const auto material=project/"materials.json";
        const std::string materialBefore=R"({"materials":{"test":{"texture":"content/Models/Ship.glb"}}})";
        const std::string materialAfter=R"({"materials":{"test":{"texture":"content/Other/Ship.glb"}}})";
        write(material,materialBefore);
        auto move=StudioTransferPlan::internal(project,"Other",{"Models/Ship.glb"});
        AIVERSE_TEST_CHECK(move.describe()["items"][0].getStr("conflict").empty());
        std::optional<aiverse::editor::ProjectHistory::MovePlan> history;
        const auto moved=move.apply(move.token(),{},[&]{write(material,materialAfter);},&history);
        AIVERSE_TEST_CHECK(moved["files"].size()==1);
        db.rebuild();
        AIVERSE_TEST_CHECK(!db.byPath("Models/Ship.glb"));
        AIVERSE_TEST_CHECK(db.byGuid(stableGuid)->path=="Other/Ship.glb");
        AIVERSE_TEST_CHECK(history&&history->files.size()==2);
        const auto metaBytes=read(project/"content"/"Other"/"Ship.glb.meta");
        write(project/"content"/"Models"/"Ship.glb","collision");
        bool undoRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*history,false);}catch(const std::runtime_error&){undoRefused=true;}
        AIVERSE_TEST_CHECK(undoRefused&&read(project/"content"/"Models"/"Ship.glb")=="collision");
        fs::remove(project/"content"/"Models"/"Ship.glb");
        write(material,materialAfter+"\n");
        undoRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*history,false);}catch(const std::runtime_error&){undoRefused=true;}
        AIVERSE_TEST_CHECK(undoRefused&&read(material)==materialAfter+"\n");
        write(material,materialAfter);
        write(project/"content"/"Other"/"Ship.glb","different");
        undoRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*history,false);}catch(const std::runtime_error&){undoRefused=true;}
        AIVERSE_TEST_CHECK(undoRefused&&read(project/"content"/"Other"/"Ship.glb")=="different");
        write(project/"content"/"Other"/"Ship.glb","replacement");
        aiverse::studio::studioReplayAssetMove(project,*history,false);
        AIVERSE_TEST_CHECK(read(material)==materialBefore&&read(project/"content"/"Models"/"Ship.glb")=="replacement");
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship.glb.meta")==metaBytes);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(stableGuid)->path=="Models/Ship.glb");
        aiverse::studio::studioReplayAssetMove(project,*history,true);
        AIVERSE_TEST_CHECK(read(material)==materialAfter&&read(project/"content"/"Other"/"Ship.glb.meta")==metaBytes);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(stableGuid)->path=="Other/Ship.glb");
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));

        // A cloned external sidecar is a GUID conflict even without a name collision.
        write(external/"Clone.glb","clone");
        fs::copy_file(project/"content"/"Other"/"Ship.glb.meta",
                      external/"Clone.glb.meta");
        auto guidConflict=StudioTransferPlan::external(project,"Models",{external/"Clone.glb"});
        AIVERSE_TEST_CHECK(guidConflict.describe()["items"][0].getStr("conflict")=="guid");
        auto fixed=guidConflict.apply(guidConflict.token(),{{0,"new-guid"}});
        AIVERSE_TEST_CHECK(fixed["files"].size()==1);
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Clone.glb")->guid!=stableGuid);

        // Reject a stale preview token before modifying anything.
        auto bad=StudioTransferPlan::external(project,"Models",{external/"Drone.glb"});
        bool rejected=false;
        try{(void)bad.apply("incorrect-token",{});}
        catch(const std::runtime_error&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Models"/"Drone.glb"));

        // Copy a genuine nested external directory without trusting its .meta.
        fs::create_directories(external/"Textures"/"Rocks");
        write(external/"Textures"/"Rocks"/"Stone.png","pixels");
        auto folderPlan=StudioTransferPlan::external(project,"Models",{external/"Textures"});
        auto importedFolder=folderPlan.apply(folderPlan.token(),{});
        AIVERSE_TEST_CHECK(importedFolder["files"].size()==1);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Textures"/"Rocks"/"Stone.png")=="pixels");
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Textures/Rocks/Stone.png"));
        // A failing dependent document write must revert all internal file
        // and metadata moves, not leave a partially committed project.
        write(project/"content"/"Models"/"Rollback.png","rollback-pixels");
        db.rebuild();
        const auto rollbackGuid=db.byPath("Models/Rollback.png")->guid;
        auto failMove=StudioTransferPlan::internal(project,"Other",{"Models/Rollback.png"});
        bool hookFailed=false;
        try {
            (void)failMove.apply(failMove.token(),{},[] {
                throw std::runtime_error("injected material write failure");
            });
        } catch (const std::runtime_error&) { hookFailed=true; }
        AIVERSE_TEST_CHECK(hookFailed);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Rollback.png")=="rollback-pixels");
        AIVERSE_TEST_CHECK(fs::exists(project/"content"/"Models"/"Rollback.png.meta"));
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Other"/"Rollback.png"));
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byGuid(rollbackGuid)->path=="Models/Rollback.png");

        // Successful follow-up proves a failed move leaves the project usable.
        auto retryMove=StudioTransferPlan::internal(project,"Other",{"Models/Rollback.png"});
        const auto retry=retryMove.apply(retryMove.token(),{});
        AIVERSE_TEST_CHECK(retry["issues"].size()==0);
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byGuid(rollbackGuid)->path=="Other/Rollback.png");

        // Destructive internal replace remains forbidden until it has
        // a complete backup protocol, even if the UI is bypassed.
        write(project/"content"/"Models"/"Rollback.png","collision");
        db.rebuild();
        auto unsafe=StudioTransferPlan::internal(project,"Other",{"Models/Rollback.png"});
        bool replaceRejected=false;
        try { (void)unsafe.apply(unsafe.token(),{{0,"replace"}}); }
        catch (const std::runtime_error&) { replaceRejected=true; }
        AIVERSE_TEST_CHECK(replaceRejected);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Rollback.png")=="collision");
        AIVERSE_TEST_CHECK(read(project/"content"/"Other"/"Rollback.png")=="rollback-pixels");

        // A large payload is represented by a digest, not retained bytes.
        fs::create_directories(project/"content"/"LargeTree"/"Empty");
        const auto large=project/"content"/"LargeTree"/"Large.bin";
        {
            std::ofstream out(large,std::ios::binary);
            const std::string chunk(1024*1024,'x');
            for(int i=0;i<17;++i)out.write(chunk.data(),chunk.size());
        }
        db.rebuild();
        const auto treeGuid=db.byPath("LargeTree")->guid;
        const auto largeGuid=db.byPath("LargeTree/Large.bin")->guid;
        auto tree=StudioTransferPlan::internal(project,"Other",{"LargeTree"});
        std::optional<aiverse::editor::ProjectHistory::MovePlan> treeHistory;
        const auto treeResult=tree.apply(tree.token(),{}, {},&treeHistory);
        AIVERSE_TEST_CHECK(treeResult["files"].size()==1&&treeHistory&&treeHistory->files.size()==2);
        AIVERSE_TEST_CHECK(treeHistory->files[0].directory&&treeHistory->files[0].bytes.empty()&&treeHistory->files[0].digest.size()==64);
        aiverse::editor::ProjectHistory stack;
        stack.recordMove(std::move(*treeHistory),"Tree move");
        AIVERSE_TEST_CHECK(stack.canUndo()&&stack.bytes()<1024*1024);
        fs::create_directory(project/"content"/"Other"/"LargeTree"/"Unexpected");
        bool treeRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*stack.next(false).move,false);}
        catch(const std::runtime_error&){treeRefused=true;}
        AIVERSE_TEST_CHECK(treeRefused);
        fs::remove(project/"content"/"Other"/"LargeTree"/"Unexpected");
        aiverse::studio::studioReplayAssetMove(project,*stack.next(false).move,false);stack.advance(false);
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byGuid(treeGuid)->path=="LargeTree"&&db.byGuid(largeGuid)->path=="LargeTree/Large.bin");
        AIVERSE_TEST_CHECK(fs::is_directory(project/"content"/"LargeTree"/"Empty"));
        aiverse::studio::studioReplayAssetMove(project,*stack.next(true).move,true);stack.advance(true);
        auto rename=StudioTransferPlan::rename(project,"Other/LargeTree","RenamedTree");
        std::optional<aiverse::editor::ProjectHistory::MovePlan> renameHistory;
        AIVERSE_TEST_CHECK(rename.describe()["items"][0].getStr("to")=="Other/RenamedTree");
        AIVERSE_TEST_CHECK(rename.apply(rename.token(),{}, {},&renameHistory)["files"].size()==1&&renameHistory);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(largeGuid)->path=="Other/RenamedTree/Large.bin");
        aiverse::studio::studioReplayAssetMove(project,*renameHistory,false);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(largeGuid)->path=="Other/LargeTree/Large.bin");
        bool extensionRefused=false;
        try{(void)StudioTransferPlan::rename(project,"Other/Ship.glb","Ship.png");}
        catch(const std::runtime_error&){extensionRefused=true;}
        AIVERSE_TEST_CHECK(extensionRefused);
        // Saved maps other than the open map are checked for path references.
        fs::create_directories(project/"maps");
        write(project/"maps"/"other.json",R"({"model":"content/Other/LargeTree/Large.bin"})");
        bool referenceRefused=false;
        try{aiverse::studio::studioCheckMapAssetPaths(project,aiverse::Json::object(),aiverse::Json::object(),aiverse::Json::object(),{{"Other/LargeTree",true}});}
        catch(const std::runtime_error&){referenceRefused=true;}
        AIVERSE_TEST_CHECK(referenceRefused);
        // Soft deletion retains the complete folder and sidecars outside content.
        fs::create_directories(project/"content"/"TrashTree");
        write(project/"content"/"TrashTree"/"Item.png","pixels");
        db.rebuild();
        const auto trashGuid=db.byPath("TrashTree")->guid;
        const auto childGuid=db.byPath("TrashTree/Item.png")->guid;
        auto deleted=aiverse::studio::studioQuarantineAsset(project,"TrashTree",trashGuid);
        AIVERSE_TEST_CHECK(deleted.quarantine&&deleted.files.size()==2);
        const auto backup=project/deleted.files[0].to;
        AIVERSE_TEST_CHECK(fs::exists(backup/"Item.png")&&!fs::exists(project/"content"/"TrashTree"));
        db.rebuild(false);AIVERSE_TEST_CHECK(!db.byGuid(trashGuid)&&!db.byGuid(childGuid));
        fs::create_directory(project/"content"/"TrashTree");
        bool collisionDelete=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,deleted,false);}catch(const std::runtime_error&){collisionDelete=true;}
        AIVERSE_TEST_CHECK(collisionDelete&&fs::exists(backup/"Item.png"));
        fs::remove(project/"content"/"TrashTree");
        aiverse::studio::studioReplayAssetQuarantine(project,deleted,false);
        db.rebuild(false);
        AIVERSE_TEST_CHECK(db.byGuid(childGuid)->path=="TrashTree/Item.png");
        bool referenced=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,deleted,true,[]{throw std::runtime_error("Referenced");});}
        catch(const std::runtime_error&){referenced=true;}
        AIVERSE_TEST_CHECK(referenced&&fs::exists(project/"content"/"TrashTree"/"Item.png"));
        aiverse::studio::studioReplayAssetQuarantine(project,deleted,true);
        AIVERSE_TEST_CHECK(fs::exists(backup/"Item.png"));
        aiverse::editor::ProjectHistory deletedHistory;
        deletedHistory.recordMove(std::move(deleted),"Delete tree");deletedHistory.reset();
        AIVERSE_TEST_CHECK(fs::exists(backup/"Item.png")); // history eviction must not purge recoverable data
        // A non-replacing import publishes the entire staged batch with sidecars.
        fs::create_directories(external/"BatchTree"/"Empty");
        write(external/"BatchTree"/"Child.png","child pixels");
        write(external/"Batch.png","batch pixels");
        auto batch=StudioTransferPlan::external(project,"Other",{external/"Batch.png",external/"BatchTree"});
        std::optional<aiverse::editor::ProjectHistory::MovePlan> imported;
        const auto batchResult=batch.apply(batch.token(),{}, {},&imported);
        AIVERSE_TEST_CHECK(batchResult["files"].size()==2&&imported&&imported->created&&imported->quarantine&&imported->files.size()==4);
        db.rebuild(false);
        const auto importedGuid=db.byPath("Other/Batch.png")->guid;
        const auto importedChildGuid=db.byPath("Other/BatchTree/Child.png")->guid;
        const auto importedMeta=read(project/"content"/"Other"/"Batch.png.meta");
        fs::remove(external/"Batch.png");fs::remove_all(external/"BatchTree");
        bool importRefused=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,*imported,true,[]{throw std::runtime_error("Referenced");});}
        catch(const std::runtime_error&){importRefused=true;}
        AIVERSE_TEST_CHECK(importRefused&&fs::exists(project/"content"/"Other"/"Batch.png"));
        aiverse::studio::studioReplayAssetQuarantine(project,*imported,true);
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Other"/"Batch.png")&&!fs::exists(project/"content"/"Other"/"BatchTree"));
        db.rebuild(false);AIVERSE_TEST_CHECK(!db.byGuid(importedGuid)&&!db.byGuid(importedChildGuid));
        // A conflict in the last item must refuse the whole batch.
        fs::create_directory(project/"content"/"Other"/"BatchTree");
        bool batchRefused=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,*imported,false);}catch(const std::runtime_error&){batchRefused=true;}
        AIVERSE_TEST_CHECK(batchRefused&&!fs::exists(project/"content"/"Other"/"Batch.png"));
        fs::remove(project/"content"/"Other"/"BatchTree");
        aiverse::studio::studioReplayAssetQuarantine(project,*imported,false);
        db.rebuild(false);
        AIVERSE_TEST_CHECK(db.byGuid(importedGuid)->path=="Other/Batch.png"&&db.byGuid(importedChildGuid)->path=="Other/BatchTree/Child.png");
        AIVERSE_TEST_CHECK(read(project/"content"/"Other"/"Batch.png")=="batch pixels");
        AIVERSE_TEST_CHECK(read(project/"content"/"Other"/"Batch.png.meta")==importedMeta);
        AIVERSE_TEST_CHECK(fs::is_directory(project/"content"/"Other"/"BatchTree"/"Empty"));
        aiverse::editor::ProjectHistory::MovePlan createdFolder;
        AIVERSE_TEST_CHECK(aiverse::studio::studioCreateContentFolder(project,"Other","NewFolder",&createdFolder)=="Other/NewFolder");
        db.rebuild(false);const auto createdGuid=db.byPath("Other/NewFolder")->guid;
        aiverse::studio::studioReplayAssetQuarantine(project,createdFolder,true);
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Other"/"NewFolder"));
        aiverse::studio::studioReplayAssetQuarantine(project,createdFolder,false);
        db.rebuild(false);AIVERSE_TEST_CHECK(db.byGuid(createdGuid)->path=="Other/NewFolder");
        // Replace a target without touching its GUID/settings or reading the
        // external source again during undo/redo.
        write(project/"content"/"Other"/"Replace.png","old pixels");
        write(external/"Replace.png","new pixels");
        db.rebuild();
        const auto replaceGuid=db.byPath("Other/Replace.png")->guid;
        const auto live=project/"content"/"Other"/"Replace.png";
        const auto sidecar=fs::path(live.string()+".meta");
        const auto originalSidecar=read(sidecar);
        auto staleReplace=StudioTransferPlan::external(project,"Other",{external/"Replace.png"});
        AIVERSE_TEST_CHECK(staleReplace.describe()["items"][0].getBool("replacementAvailable"));
        write(live,"external pixels");
        bool replaceConflict=false;
        std::optional<aiverse::editor::ProjectHistory::MovePlan> replaceHistory;
        try{(void)staleReplace.apply(staleReplace.token(),{{0,"replace"}},{},&replaceHistory);}
        catch(const std::runtime_error&){replaceConflict=true;}
        AIVERSE_TEST_CHECK(replaceConflict&&read(live)=="external pixels"&&!replaceHistory);
        write(live,"old pixels");
        auto replacePlan=StudioTransferPlan::external(project,"Other",{external/"Replace.png"});
        const auto replacement=replacePlan.apply(replacePlan.token(),{{0,"replace"}},{},&replaceHistory);
        AIVERSE_TEST_CHECK(replacement["files"].size()==1&&replaceHistory&&replaceHistory->replacement);
        AIVERSE_TEST_CHECK(read(live)=="new pixels"&&read(sidecar)==originalSidecar);
        db.rebuild(false);AIVERSE_TEST_CHECK(db.byGuid(replaceGuid)->path=="Other/Replace.png");
        fs::remove(external/"Replace.png");
        const auto oldBackup=project/replaceHistory->files[0].to;
        const auto newBackup=project/replaceHistory->files[1].to;
        AIVERSE_TEST_CHECK(read(oldBackup)=="old pixels"&&!fs::exists(newBackup));
        write(sidecar,originalSidecar+"\n");
        replaceConflict=false;
        try{aiverse::studio::studioReplayAssetReplacement(project,*replaceHistory,false);}
        catch(const std::runtime_error&){replaceConflict=true;}
        AIVERSE_TEST_CHECK(replaceConflict&&read(live)=="new pixels"&&read(oldBackup)=="old pixels");
        write(sidecar,originalSidecar);
        aiverse::studio::studioReplayAssetReplacement(project,*replaceHistory,false);
        AIVERSE_TEST_CHECK(read(live)=="old pixels"&&read(newBackup)=="new pixels"&&!fs::exists(oldBackup));
        aiverse::studio::studioReplayAssetReplacement(project,*replaceHistory,true);
        AIVERSE_TEST_CHECK(read(live)=="new pixels"&&read(oldBackup)=="old pixels");
        AIVERSE_TEST_CHECK(read(sidecar)==originalSidecar);
        // A mixed creation/replacement batch is refused before any publication.
        write(external/"Replace.png","another version");
        write(external/"Fresh.png","fresh");
        auto mixed=StudioTransferPlan::external(project,"Other",{external/"Replace.png",external/"Fresh.png"});
        replaceConflict=false;
        try{(void)mixed.apply(mixed.token(),{{0,"replace"},{1,"import"}},{},&replaceHistory);}
        catch(const std::runtime_error&){replaceConflict=true;}
        AIVERSE_TEST_CHECK(replaceConflict&&read(live)=="new pixels"&&!fs::exists(project/"content"/"Other"/"Fresh.png"));
        std::cout<<"PASS studio conflict resolution and GUID-preserving asset transfers\n";
    }catch(...){fs::remove_all(temp);throw;}
    fs::remove_all(temp);
}
