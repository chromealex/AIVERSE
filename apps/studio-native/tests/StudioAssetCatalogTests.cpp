#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioContentDirectory.h"
#include "tests/TestCheck.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs=std::filesystem;
using aiverse::studio::StudioAssetCatalog;
int main(){
    const auto dir=fs::temp_directory_path()/("aiverse-asset-index-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir/"content"/"Models");
    std::ofstream(dir/"content"/"Models"/"Ship.glb")<<"model";
    std::ofstream(dir/"content"/"Steel.png")<<"image";
    try{
        StudioAssetCatalog db(dir);db.rebuild();
        const auto rows=db.all();
        AIVERSE_TEST_CHECK(rows.size()==3); // Models folder + Ship.glb + Steel.png
        for(const auto& e:rows){
            AIVERSE_TEST_CHECK(e.guid.size()==32);
            AIVERSE_TEST_CHECK(e.path.find(".meta")==std::string::npos);
            const auto lookup=db.byGuid(e.guid);
            AIVERSE_TEST_CHECK(lookup && lookup->path==e.path);
        }
        const auto ship=db.byPath("Models/Ship.glb");
        AIVERSE_TEST_CHECK(ship && ship->importer=="model");
        const auto texture=db.byPath("Steel.png");
        AIVERSE_TEST_CHECK(texture && texture->importer=="texture");
        AIVERSE_TEST_CHECK(texture->settings.getStr("compression")=="auto");
        AIVERSE_TEST_CHECK(fs::exists(dir/"content"/"Steel.png.meta"));
        const auto retainedGuid=texture->guid;
        const auto updated=db.patchSettings(retainedGuid,"compression",aiverse::Json::string("high"));
        AIVERSE_TEST_CHECK(updated.guid==retainedGuid);
        AIVERSE_TEST_CHECK(updated.settings.getStr("compression")=="high");
        AIVERSE_TEST_CHECK(db.byGuid(retainedGuid)->settings.getStr("compression")=="high");
        bool settingRejected=false;
        try{(void)db.patchSettings(retainedGuid,"maxSize",aiverse::Json::number(int64_t(123)));}
        catch(const std::runtime_error&){settingRejected=true;}
        AIVERSE_TEST_CHECK(settingRejected);
        AIVERSE_TEST_CHECK(db.byGuid(retainedGuid)->settings.getStr("compression")=="high");
        const auto platform=db.patchSettings(retainedGuid,"glesFormat",aiverse::Json::string("etc2"));
        AIVERSE_TEST_CHECK(platform.settings["platformFormats"].getStr("gles")=="etc2");
        AIVERSE_TEST_CHECK(fs::exists(dir/"content"/"Models.meta"));
        // Moving a file and its sidecar preserves GUID; only SQLite path changes.
        fs::rename(dir/"content"/"Models"/"Ship.glb",dir/"content"/"Moved.glb");
        fs::rename(dir/"content"/"Models"/"Ship.glb.meta",dir/"content"/"Moved.glb.meta");
        db.rebuild();
        AIVERSE_TEST_CHECK(!db.byPath("Models/Ship.glb"));
        AIVERSE_TEST_CHECK(db.byGuid(ship->guid)->path=="Moved.glb");
        // Cloned .meta with same GUID triggers a fatal collision, not a silent identity rewrite.
        fs::copy_file(dir/"content"/"Moved.glb",dir/"content"/"Other.glb");
        fs::copy_file(dir/"content"/"Moved.glb.meta",dir/"content"/"Other.glb.meta");
        bool collision=false;
        try{db.rebuild();}catch(const std::runtime_error&){collision=true;}
        AIVERSE_TEST_CHECK(collision);
        AIVERSE_TEST_CHECK(db.byGuid(ship->guid)->path=="Moved.glb"); // index rolled back/stays valid
        fs::remove(dir/"content"/"Other.glb");fs::remove(dir/"content"/"Other.glb.meta");
        // Bad GUID never silently replaces user's broken metadata.
        std::ofstream(dir/"content"/"bad.png")<<"data";
        std::ofstream(dir/"content"/"bad.png.meta")<<R"({"guid":"bogus","importer":"texture","settings":{}})";
        bool invalid=false;
        try{db.rebuild();}catch(const std::runtime_error&){invalid=true;}
        AIVERSE_TEST_CHECK(invalid);
        std::cout<<"PASS studio asset .meta GUID + SQLite catalog\n";
    }catch(...){fs::remove_all(dir);throw;}
    fs::remove_all(dir);
}
