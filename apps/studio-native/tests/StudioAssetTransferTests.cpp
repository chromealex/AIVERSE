#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetTransfers.h"
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
        auto move=StudioTransferPlan::internal(project,"Other",{"Models/Ship.glb"});
        AIVERSE_TEST_CHECK(move.describe()["items"][0].getStr("conflict").empty());
        const auto moved=move.apply(move.token(),{});
        AIVERSE_TEST_CHECK(moved["files"].size()==1);
        db.rebuild();
        AIVERSE_TEST_CHECK(!db.byPath("Models/Ship.glb"));
        AIVERSE_TEST_CHECK(db.byGuid(stableGuid)->path=="Other/Ship.glb");

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

        std::cout<<"PASS studio conflict resolution and GUID-preserving asset transfers\n";
    }catch(...){fs::remove_all(temp);throw;}
    fs::remove_all(temp);
}
