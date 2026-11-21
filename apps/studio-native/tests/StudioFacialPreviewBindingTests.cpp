#include "apps/studio-native/StudioFacialPreviewBinding.h"
#include "tests/AlwaysCheck.h"
#include "apps/studio-native/StudioHumanLookDocument.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
using namespace aiverse;
using namespace aiverse::studio;
namespace fs=std::filesystem;
int main() {
    const fs::path project=fs::temp_directory_path()/
        ("aiverse-facial-preview-bind-"+std::to_string(::getpid()));
    AIVERSE_TEST_CHECK(fs::create_directory(project));
    try {
        const fs::path content=project/"content";
        AIVERSE_TEST_CHECK(fs::create_directory(content));
        {
            std::ofstream file(content/"face.glb",std::ios::binary);
            file<<"glTF"; // identity test only; NOT a valid imported GPU model
        }
        {
            std::ofstream file(content/"wrong.png",std::ios::binary);
            file<<"png";
        }
        StudioAssetCatalog assets(project);
        assets.rebuild();
        const auto model=assets.byPath("face.glb");
        const auto image=assets.byPath("wrong.png");
        AIVERSE_TEST_CHECK(model.has_value() && image.has_value());
        StudioFacialPreviewBinding preview;
        const Json rig=Json::parse(R"({"version":1,"morphs":[
            {"parameter":"jawOpen","node":"Face","target":"jawOpen"},
            {"parameter":"eyeBlinkLeft","node":"Face","target":"eyeBlinkLeft"}
            ],"rig":[{"bone":"Eye.L","op":"aim","target":"lookAt",
              "axis":[0,0,1],"weightParameter":"gazeWeight"}]})");
        const Json info=preview.bind(assets,model->guid,rig);
        AIVERSE_TEST_CHECK(info["bound"].boolean());
        AIVERSE_TEST_CHECK(info["guid"].str()==model->guid);
        AIVERSE_TEST_CHECK(info["path"].str()=="face.glb");
        AIVERSE_TEST_CHECK(!info["runtimeReady"].boolean() &&
                           !info["runtimeVerified"].boolean());
        AIVERSE_TEST_CHECK(info["rendererChannelCapacity"].integer()==64);
        preview.setGpuStatus(true,{});
        AIVERSE_TEST_CHECK(preview.describe()["runtimeReady"].boolean());
        AIVERSE_TEST_CHECK(!preview.describe()["runtimeVerified"].boolean());
        preview.setGpuStatus(false,"Native viewport hidden");
        AIVERSE_TEST_CHECK(!preview.describe()["runtimeReady"].boolean());
        const auto generation=uint64_t(info["revision"].integer());
        Json scalar=Json::object();
        scalar.set("jawOpen",Json::number(0.8,3));
        AIVERSE_TEST_CHECK(preview.controls(generation,scalar,Json::object())
            ["values"]["jawOpen"].toDouble()>0.79);
        bool rejected=false;
        Json invalidScalar=Json::object();
        invalidScalar.set("jawOpen",Json::number(int64_t(2)));
        try {(void)preview.controls(generation,invalidScalar,Json::object());}
        catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        AIVERSE_TEST_CHECK(preview.describe()["values"]["jawOpen"].toDouble()>0.79);
        AIVERSE_TEST_CHECK(preview.describe()["vectors"]["lookAt"].isArray());

        Json gaze=Json::object(),coordinates=Json::array();
        coordinates.push(Json::number(int64_t(2)));
        coordinates.push(Json::number(int64_t(1)));
        coordinates.push(Json::number(int64_t(3)));
        gaze.set("lookAt",std::move(coordinates));
        const Json changed=preview.controls(generation,Json::object(),gaze);
        AIVERSE_TEST_CHECK(changed["vectors"]["lookAt"][size_t(0)].toDouble()>1.9);

        Json invalidGaze=Json::object(),invalidCoordinates=Json::array();
        invalidCoordinates.push(Json::number(int64_t(0)));
        invalidCoordinates.push(Json::number(int64_t(1000001)));
        invalidCoordinates.push(Json::number(int64_t(2)));
        invalidGaze.set("lookAt",std::move(invalidCoordinates));
        rejected=false;
        try {(void)preview.controls(generation,invalidScalar,invalidGaze);}
        catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        AIVERSE_TEST_CHECK(preview.describe()["values"]["jawOpen"].toDouble()>0.79);
        AIVERSE_TEST_CHECK(preview.describe()["vectors"]["lookAt"][size_t(0)].toDouble()>1.9);

        Json undeclared=Json::object();
        undeclared.set("unknown",Json::number(0.2,3));
        rejected=false;
        try {(void)preview.controls(generation,undeclared,Json::object());}
        catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        rejected=false;
        try {(void)preview.bind(assets,image->guid,rig);}
        catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected && preview.describe()["guid"].str()==model->guid);
        rejected=false;
        try {(void)preview.controls(generation+1,scalar,Json::object());}
        catch(const std::exception&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        vivid::HumanAppearance look;look.values["skin.roughness"]=.8f;
        const auto saved=saveStudioHumanLook(project.string(),"matte",look.toJson());
        AIVERSE_TEST_CHECK(readStudioHumanLook(project.string(),"matte")["look"]==saved["look"]);
        AIVERSE_TEST_CHECK(listStudioHumanLooks(project.string())["looks"].size()==1);
        rejected=false;try{saveStudioHumanLook(project.string(),"../escape",look.toJson());}catch(...){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        preview.bindAppearance(assets,model->guid,preview.revision(),look.toJson());
        AIVERSE_TEST_CHECK(preview.appearance().has_value() && !preview.appearanceOnly());
        AIVERSE_TEST_CHECK(preview.document()==rig); // keep MotionWeave rig when editing appearance
        rejected=false;try{preview.bindAppearance(assets,model->guid,0,look.toJson());}catch(...){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        preview.reset();
        preview.bindAppearance(assets,model->guid,preview.revision(),look.toJson());
        AIVERSE_TEST_CHECK(preview.appearanceOnly());
        preview.reset();
        AIVERSE_TEST_CHECK(!preview.appearance().has_value());
        AIVERSE_TEST_CHECK(!preview.describe()["bound"].boolean());
        fs::remove_all(project);
    }catch(...) {
        std::error_code ec;
        fs::remove_all(project,ec);
        throw;
    }
}
