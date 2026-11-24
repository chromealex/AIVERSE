#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "apps/studio-native/StudioAssetFileDigest.h"
#include "engine/pack/Sha256.h"
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

        // S6g: expose exact map path candidates before applying a move.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            fs::create_directories(project/"maps"/".aiverse-cache");
            write(project/"maps"/"other.json",R"({"items":[{"model":"Models/Ship.glb"},{"model":"content/Missing.png"},{"note":"prefix content/Models/Ship.glb"}]})");
            write(project/"maps"/".aiverse-cache"/"ignored.json","not JSON");
            const auto current=Json::parse(R"({"a/b~c":"content/Models/Ship.glb"})");
            const auto manifest=Json::parse(R"({"maps":[{"file":"maps/other.json"}]})");
            const auto refs=studioCollectProjectAssetReferences(project,Json::object(),Json::object(),current,manifest,db.all());
            AIVERSE_TEST_CHECK(refs.size()==3);
            AIVERSE_TEST_CHECK(refs[0].document=="current map"&&refs[0].field=="/a~1b~0c"&&refs[0].guid==stableGuid&&!refs[0].writable);
            AIVERSE_TEST_CHECK(refs[1].document=="maps/other.json"&&refs[1].field=="/items/0/model"&&refs[1].guid==stableGuid);
            AIVERSE_TEST_CHECK(refs[2].path=="content/Missing.png"&&refs[2].guid.empty());
            auto preview=StudioTransferPlan::internal(project,"Other",{"Models/Ship.glb"});
            const auto impact=studioMoveReferenceImpact(preview.describe(),refs);
            AIVERSE_TEST_CHECK(impact.size()==2&&!impact[0].getBool("writable")&&!impact[1].getBool("writable"));
            bool refused=false;
            try{studioCheckMapAssetPaths(project,current,Json::object(),manifest,{{"Models/Ship.glb",false}});}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused);
            fs::remove_all(project/"maps");
        }

        // S6g.2: actual glTF resource slots, local relative paths and both edge directions.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            write(project/"content"/"Models"/"Scene.gltf",R"({"buffers":[{"uri":"../Other/mesh.bin"},{"uri":"data:application/octet-stream;base64,AA=="}],"images":[{"uri":"../Other/paint%20red.png"},{"uri":"missing.png"},{"uri":"https://example.com/a.png"},{"uri":"../../escape.png"},{"bufferView":0}],"extras":{"uri":"ignored.png"}})");
            write(project/"content"/"Other"/"mesh.bin","mesh");
            write(project/"content"/"Other"/"paint red.png","paint");
            db.rebuild();
            const auto refs=studioCollectGltfReferences(project,studioReferenceGuids(db.all()));
            AIVERSE_TEST_CHECK(refs.size()==3);
            AIVERSE_TEST_CHECK(refs[0].path=="content/Other/mesh.bin"&&refs[0].field=="/buffers/0/uri"&&!refs[0].writable);
            AIVERSE_TEST_CHECK(refs[1].path=="content/Other/paint red.png"&&refs[1].guid==db.byPath("Other/paint red.png")->guid);
            AIVERSE_TEST_CHECK(refs[2].path=="content/Models/missing.png"&&refs[2].guid.empty());
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Models/Scene.gltf",false).size()==3);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Other",true).size()==2);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Other/mesh.bin",false).size()==1);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Other/mesh",false).empty());
            const auto preview=StudioTransferPlan::internal(project,"Models",{"Other/mesh.bin"});
            AIVERSE_TEST_CHECK(studioMoveReferenceImpact(preview.describe(),refs).size()==1);
            bool refused=false;
            try{studioCheckGltfAssetPaths(project,{{"Other/mesh.bin",false}});}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused);
            AIVERSE_TEST_CHECK(studioGltfResourcePath("content/Models/Scene.gltf","%2e%2e/%2e%2e/escape.png").empty());
            for(const auto& path:{"Models/Scene.gltf","Other/mesh.bin","Other/paint red.png"}){
                fs::remove(project/"content"/path);fs::remove(studioMetaPath(project/"content"/path));
            }
            db.rebuild(false);
        }

        // S6g.3: shortest paths, shared descendants, cycle-closing edge and depth cap.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            const std::vector<StudioAssetReference> edges={
                {"content/A","glTF resource","/0","content/B","",false},
                {"content/A","glTF resource","/1","content/C","",false},
                {"content/B","glTF resource","/0","content/D","",false},
                {"content/C","glTF resource","/0","content/D","",false},
                {"content/D","glTF resource","/0","content/A","",false},
                {"maps/map.json","map","/0","content/B","",false}};
            const auto visits=studioTraverseAssetDependencies(edges,"A",false,false,16);
            AIVERSE_TEST_CHECK(visits.size()==5);
            size_t cycles=0;for(const auto& v:visits){cycles+=v.cycle?1:0;if(v.edge==4)AIVERSE_TEST_CHECK(v.depth==3);}
            AIVERSE_TEST_CHECK(cycles==1);
            AIVERSE_TEST_CHECK(studioTraverseAssetDependencies(edges,"A",false,false,1).size()==2);
            const auto incoming=studioTraverseAssetDependencies(edges,"D",false,true,16);
            AIVERSE_TEST_CHECK(incoming.size()==6);
            const StudioAssetReference ref{"content/Pack/model.gltf","glTF resource","/images/0/uri","content/Pack/tex.png","",false,"tex.png"};
            AIVERSE_TEST_CHECK(studioGltfRelocationSafe(ref,{{"content/Pack","content/Renamed",true}}));
            AIVERSE_TEST_CHECK(studioGltfRelocationSafe(ref,{{"content/Pack/model.gltf","content/Pack/renamed.gltf",false}}));
            AIVERSE_TEST_CHECK(!studioGltfRelocationSafe(ref,{{"content/Pack/tex.png","content/Pack/renamed.png",false}}));
            AIVERSE_TEST_CHECK(!studioGltfRelocationSafe(ref,{{"content/Pack/model.gltf","content/Else/model.gltf",false}}));
            fs::create_directories(project/"content"/"Pack");
            write(project/"content"/"Pack"/"model.gltf",R"({"images":[{"uri":"tex.png"}]})");
            write(project/"content"/"Pack"/"tex.png","texture");db.rebuild();
            auto rename=StudioTransferPlan::rename(project,"Pack","Renamed");
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            AIVERSE_TEST_CHECK(studioMoveReferenceImpact(rename.describe(),studioCollectGltfReferences(project,{})).size()==0);
            (void)rename.apply(rename.token(),{},{},&recorded);
            AIVERSE_TEST_CHECK(recorded&&fs::exists(project/"content"/"Renamed"/"tex.png"));
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(fs::exists(project/"content"/"Pack"/"model.gltf"));
            fs::remove_all(project/"content"/"Pack");fs::remove(studioMetaPath(project/"content"/"Pack"));db.rebuild(false);
        }

        // S6h.1: persisted quota blocks growth, not restoration or explicit purge.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            write(project/"content"/"Quota.bin",std::string(2*1024*1024,'q'));
            db.rebuild();
            const auto guid=db.byPath("Quota.bin")->guid;
            AIVERSE_TEST_CHECK(studioRecoveryStorage(project).getInt("quotaMiB")==0);
            AIVERSE_TEST_CHECK(studioSetRecoveryQuota(project,0,1).getInt("quotaMiB")==1);
            bool refused=false;
            try{(void)studioQuarantineAsset(project,"Quota.bin",guid);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&fs::exists(project/"content"/"Quota.bin"));
            AIVERSE_TEST_CHECK(studioSetRecoveryQuota(project,1,4).getInt("quotaMiB")==4);
            auto plan=studioQuarantineAsset(project,"Quota.bin",guid);
            AIVERSE_TEST_CHECK(studioRecoveryStorage(project).getInt("trashBytes")>=2*1024*1024);
            AIVERSE_TEST_CHECK(studioSetRecoveryQuota(project,4,1).getBool("overQuota"));
            studioReplayAssetQuarantine(project,plan,false);
            AIVERSE_TEST_CHECK(fs::exists(project/"content"/"Quota.bin")&&!studioRecoveryStorage(project).getBool("overQuota"));
            refused=false;try{(void)studioSetRecoveryQuota(project,4,0);}catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&studioRecoveryStorage(project).getInt("quotaMiB")==1);
            (void)studioSetRecoveryQuota(project,1,0);
            fs::remove(project/"content"/"Quota.bin");fs::remove(studioMetaPath(project/"content"/"Quota.bin"));
            db.rebuild(false);
        }

        // S6h.2: protected history, exact batch review, changed payload refusal.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            fs::create_directories(project/"content"/"Cleanup");
            write(project/"content"/"Cleanup"/"a.bin","folder payload");
            write(project/"content"/"Purge.bin","purge payload");db.rebuild();
            auto folder=studioQuarantineAsset(project,"Cleanup",db.byPath("Cleanup")->guid);
            auto file=studioQuarantineAsset(project,"Purge.bin",db.byPath("Purge.bin")->guid);
            const auto folderId=fs::path(folder.files[0].to).parent_path().filename().string();
            const auto fileId=fs::path(file.files[0].to).parent_path().filename().string();
            editor::ProjectHistory protectedHistory;
            auto keep=folder;protectedHistory.recordMove(std::move(keep),"Protected folder");
            AIVERSE_TEST_CHECK(studioProtectedTrashIds(protectedHistory).size()==1);
            bool refused=false;
            try{(void)studioReviewTrashBatch(project,{folderId,fileId},protectedHistory);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused);
            editor::ProjectHistory empty;
            const auto plan=studioReviewTrashBatch(project,{folderId,fileId},empty);
            AIVERSE_TEST_CHECK(plan["items"].size()==2&&plan.getInt("bytes")>27);
            write(project/file.files[0].to,"changed payload");
            refused=false;try{(void)studioApplyTrashBatch(project,{folderId,fileId},plan.getStr("token"),empty);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&fs::exists(project/folder.files[0].to));
            write(project/file.files[0].to,"purge payload");
            const auto refreshed=studioReviewTrashBatch(project,{folderId,fileId},empty);
            const auto applied=studioApplyTrashBatch(project,{folderId,fileId},refreshed.getStr("token"),empty);
            AIVERSE_TEST_CHECK(applied.getBool("ok")&&applied["removed"].size()==2);
            AIVERSE_TEST_CHECK(!fs::exists(project/folder.files[0].to)&&!fs::exists(project/file.files[0].to));
        }

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
        // Recovery listing is independent of the session-local history.
        const auto trashId=[](const fs::path& payload){return payload.parent_path().filename().string();};
        const auto deletedId=trashId(backup);
        const auto replacementId=trashId(oldBackup);
        auto page=aiverse::studio::studioAssetTrashList(project,0,100);
        AIVERSE_TEST_CHECK(page["total"].integer()>=2&&!page.getBool("recoveryPending"));
        bool listedDelete=false,listedVersion=false;
        for(const auto& row:page["items"].items()){
            if(row.getStr("id")==deletedId)listedDelete=row.getStr("kind")=="deleted";
            if(row.getStr("id")==replacementId)listedVersion=row.getStr("kind")=="replacement";
        }
        AIVERSE_TEST_CHECK(listedDelete&&listedVersion);
        auto restore=aiverse::studio::studioAssetTrashReview(project,deletedId,"restore");
        AIVERSE_TEST_CHECK(restore.getStr("guid")==trashGuid&&!restore.getStr("token").empty());
        bool trashRefused=false;
        try{aiverse::studio::studioAssetTrashApply(project,deletedId,"restore","stale");}
        catch(const std::runtime_error&){trashRefused=true;}
        AIVERSE_TEST_CHECK(trashRefused&&fs::exists(backup));
        aiverse::studio::studioAssetTrashApply(project,deletedId,"restore",restore.getStr("token"));
        db.rebuild(false);AIVERSE_TEST_CHECK(db.byGuid(childGuid)->path=="TrashTree/Item.png");
        AIVERSE_TEST_CHECK(!fs::exists(backup));
        auto versionReview=aiverse::studio::studioAssetTrashReview(project,replacementId,"restore");
        write(live,"changed after review");
        trashRefused=false;
        try{aiverse::studio::studioAssetTrashApply(project,replacementId,"restore",versionReview.getStr("token"));}
        catch(const std::runtime_error&){trashRefused=true;}
        AIVERSE_TEST_CHECK(trashRefused&&read(live)=="changed after review"&&fs::exists(oldBackup));
        write(live,"new pixels");
        versionReview=aiverse::studio::studioAssetTrashReview(project,replacementId,"restore");
        aiverse::studio::studioAssetTrashApply(project,replacementId,"restore",versionReview.getStr("token"));
        AIVERSE_TEST_CHECK(read(live)=="old pixels"&&read(sidecar)==originalSidecar&&!fs::exists(oldBackup));
        page=aiverse::studio::studioAssetTrashList(project,0,100);
        std::string outgoingId;
        for(const auto& row:page["items"].items())
            if(row.getStr("kind")=="replacement"&&row.getStr("path")=="Other/Replace.png")outgoingId=row.getStr("id");
        AIVERSE_TEST_CHECK(!outgoingId.empty());
        const auto purge=aiverse::studio::studioAssetTrashReview(project,outgoingId,"purge");
        trashRefused=false;
        try{aiverse::studio::studioAssetTrashApply(project,outgoingId,"purge",versionReview.getStr("token"));}
        catch(const std::runtime_error&){trashRefused=true;}
        AIVERSE_TEST_CHECK(trashRefused&&read(live)=="old pixels");
        aiverse::studio::studioAssetTrashApply(project,outgoingId,"purge",purge.getStr("token"));
        AIVERSE_TEST_CHECK(read(live)=="old pixels"&&read(sidecar)==originalSidecar);
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-asset-trash"/outgoingId));
        // Persist a mixed owner timeline without retaining payload bytes in memory.
        {
            using namespace aiverse::studio;using aiverse::editor::ProjectHistory;
            const auto p=temp/"Persistent";
            fs::create_directories(p/"content");
            write(p/"content"/"Image.png","original image");
            StudioAssetCatalog index(p);index.rebuild();
            const auto imageGuid=index.byPath("Image.png")->guid;
            ProjectHistory timeline;
            {
                StudioAssetHistoryPublication publication(p);
                index.patchSettings(imageGuid,"compression",aiverse::Json::string("high"),&timeline);
                ProjectHistory::MovePlan folderPlan;
                studioCreateContentFolder(p,"","Folder",&folderPlan);
                timeline.recordMove(std::move(folderPlan),"Create");
                auto move=StudioTransferPlan::internal(p,"Folder",{"Image.png"});
                std::optional<ProjectHistory::MovePlan> movePlan;
                (void)move.apply(move.token(),{}, {},&movePlan);
                AIVERSE_TEST_CHECK(movePlan);timeline.recordMove(std::move(*movePlan),"Move");
                write(external/"Image.png","replacement image");
                auto replace=StudioTransferPlan::external(p,"Folder",{external/"Image.png"});
                (void)replace.apply(replace.token(),{{0,"replace"}},{},&movePlan);
                AIVERSE_TEST_CHECK(movePlan&&movePlan->replacement);
                timeline.recordMove(std::move(*movePlan),"Replace");
                auto removal=studioQuarantineAsset(p,"Folder/Image.png",imageGuid);
                timeline.recordMove(std::move(removal),"Delete");
                // The cursor and the redo branch must both survive reopening.
                studioReplayAssetQuarantine(p,*timeline.next(false).move,false);
                timeline.advance(false);publication.save(timeline);
            }
            auto restored=studioLoadAssetHistoryCheckpoint(p);
            AIVERSE_TEST_CHECK(restored.steps().size()==5&&restored.position()==4&&restored.canRedo());
            AIVERSE_TEST_CHECK(restored.versionToken()!=timeline.versionToken());
            {
                StudioAssetHistoryPublication publication(p);
                AIVERSE_TEST_CHECK(publication.cancelUnchanged(restored));
            }
            // Verify the persisted plan can actually drive the native owners.
            {
                StudioAssetHistoryPublication publication(p);
                studioReplayAssetQuarantine(p,*restored.next(true).move,true);restored.advance(true);
                AIVERSE_TEST_CHECK(!fs::exists(p/"content"/"Folder"/"Image.png"));
                studioReplayAssetQuarantine(p,*restored.next(false).move,false);restored.advance(false);
                studioReplayAssetReplacement(p,*restored.next(false).move,false);restored.advance(false);
                studioReplayAssetMove(p,*restored.next(false).move,false);restored.advance(false);
                AIVERSE_TEST_CHECK(read(p/"content"/"Image.png")=="original image");
                publication.save(restored);
            }
            auto reopened=studioLoadAssetHistoryCheckpoint(p);
            AIVERSE_TEST_CHECK(reopened.position()==2&&reopened.steps().size()==5&&reopened.canRedo());
            // Unexpected content must never reactivate an old history.
            write(p/"content"/"Image.png","external edit");
            bool rejected=false;
            try{(void)studioLoadAssetHistoryCheckpoint(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected);
            write(p/"content"/"Image.png","original image");
            AIVERSE_TEST_CHECK(studioLoadAssetHistoryCheckpoint(p).position()==2);
            // Simulate interruption after the marker, before checkpoint commit.
            {StudioAssetHistoryPublication interrupted(p);}
            rejected=false;
            try{(void)studioLoadAssetHistoryCheckpoint(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected&&fs::exists(p/"content"/"Image.png"));
            {StudioAssetHistoryPublication publication(p);publication.save(reopened);}
            AIVERSE_TEST_CHECK(studioLoadAssetHistoryCheckpoint(p).steps().size()==5);
            // Changing a retained replacement slot invalidates the checkpoint.
            const auto& replacementPlan=*reopened.steps()[3].move;
            const auto retained=p/replacementPlan.files[1].to;
            AIVERSE_TEST_CHECK(fs::exists(retained));write(retained,"damaged backup");
            rejected=false;
            try{(void)studioLoadAssetHistoryCheckpoint(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected);
        }
        // Coordinated filesystem publication: a replacement chain and a
        // disjoint quarantine pair share an outer journal with project bytes.
        {
            using namespace aiverse;using namespace aiverse::studio;
            const auto p=temp/"Coordinated";
            fs::create_directories(p/"content");
            write(p/"game.json","before");write(p/"content"/"Version.png","old");
            write(p/"content"/"Delete.png","delete me");
            StudioAssetCatalog index(p);index.rebuild();
            const auto deleteGuid=index.byPath("Delete.png")->guid;
            write(external/"Version.png","new");
            std::optional<editor::ProjectHistory::MovePlan> replacement;
            auto replace=StudioTransferPlan::external(p,"",{external/"Version.png"});
            (void)replace.apply(replace.token(),{{0,"replace"}},{},&replacement);
            AIVERSE_TEST_CHECK(replacement&&replacement->replacement);
            const auto deletion=studioQuarantineAsset(p,"Delete.png",deleteGuid);
            StudioBatchAssetPlan staged;
            studioReplayAssetReplacement(p,*replacement,false,&staged);
            studioReplayAssetQuarantine(p,deletion,false,{},&staged);
            AIVERSE_TEST_CHECK(staged.relocations.size()==4);
            AIVERSE_TEST_CHECK(read(p/"content"/"Version.png")=="new"&&!fs::exists(p/"content"/"Delete.png"));
            staged.documents.push_back({p/"game.json","before","after"});
            const auto folder=p/".aiverse-cache"/"studio-history-batch";
            auto manifest=[&]{
                Json body=Json::object(),moves=Json::array(),documents=Json::array();
                for(const auto& m:staged.relocations){
                    Json row=Json::object();row.set("from",Json::string(m.from));row.set("to",Json::string(m.to));
                    row.set("digest",Json::string(m.digest));row.set("directory",Json::fromBool(m.directory));moves.push(std::move(row));
                }
                for(const auto& f:staged.documents){
                    Json row=Json::object();row.set("path",Json::string(f.path.lexically_relative(p).generic_string()));
                    row.set("before",Json::string(f.before));row.set("after",Json::string(f.after));documents.push(std::move(row));
                }
                body.set("moves",std::move(moves));body.set("documents",std::move(documents));
                const auto payload=body.dump(-1);
                const auto sum=pack::sha256(payload.data(),payload.size());const auto hash=pack::toHex(sum.data(),sum.size());
                Json envelope=Json::object();envelope.set("version",Json::number(int64_t(1)));
                envelope.set("payload",Json::string(payload));envelope.set("sha256",Json::string(hash));return envelope.dump(-1);
            };
            const auto envelope=manifest();
            for(size_t phase=0;phase<=staged.relocations.size();++phase){
                fs::create_directory(folder);write(folder/"pending.json",envelope);
                for(size_t i=0;i<phase;++i){const auto& m=staged.relocations[i];fs::rename(p/m.from,p/m.to);}
                if(phase==staged.relocations.size())write(p/"game.json","after");
                studioRecoverHistoryBatch(p);
                AIVERSE_TEST_CHECK(!fs::exists(folder)&&read(p/"game.json")=="before");
                AIVERSE_TEST_CHECK(read(p/"content"/"Version.png")=="new"&&!fs::exists(p/"content"/"Delete.png"));
            }
            // External bytes must stop the entire rollback, not overwrite them.
            fs::create_directory(folder);write(folder/"pending.json",envelope);
            for(const auto& m:staged.relocations)fs::rename(p/m.from,p/m.to);
            write(p/"game.json","external");
            bool rejected=false;
            try{studioRecoverHistoryBatch(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected&&read(p/"game.json")=="external"&&read(p/"content"/"Version.png")=="old");
            write(p/"game.json","after");studioRecoverHistoryBatch(p);
            AIVERSE_TEST_CHECK(read(p/"game.json")=="before"&&read(p/"content"/"Version.png")=="new");
            studioApplyHistoryBatch(p,staged.relocations,staged.documents);
            AIVERSE_TEST_CHECK(read(p/"game.json")=="after"&&read(p/"content"/"Version.png")=="old");
            AIVERSE_TEST_CHECK(read(p/"content"/"Delete.png")=="delete me"&&!fs::exists(folder));
            index.rebuild(false);AIVERSE_TEST_CHECK(index.byGuid(deleteGuid)->path=="Delete.png");
        }
        std::cout<<"PASS studio conflict resolution and GUID-preserving asset transfers\n";
    }catch(...){fs::remove_all(temp);throw;}
    fs::remove_all(temp);
}
