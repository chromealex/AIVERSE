// Stage 108 deferred: generic material inheritance and animated graph
// source policy are not defined in RTS or FPS gameplay code.
#include "engine/view/AppearanceAuthoringService.h"
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse;
    using view::AppearanceAuthoringService;
    const Json library=Json::parse(R"({
        "base":{"color":[0.2,0.3,0.4,1],"metallic":0.4,
                "roughness":0.9,"thinTransmission":0.35,"shadowDiffusion":0.125,"sssStrength":0.6,
                "sssScale":0.4,"sssThickness":0.06,"sssColor":[1,0.4,0.2],"alpha":"opaque"},
        "paint":{"base":"base","roughness":0.25,"team":"tint"}
    })");
    std::vector<std::string> warnings;
    auto warn=[&](const std::string& message){warnings.push_back(message);};
    Json variant=Json::object();
    variant.set("use",Json::string("paint"));
    variant.set("roughness",Json::number(0.5));
    variant.set("texture",Json::string("textures/paint.png"));
    const auto flat=AppearanceAuthoringService::resolveMaterial(
        library,variant,warn);
    AIVERSE_TEST_CHECK(flat.getStr("team")=="tint");
    AIVERSE_TEST_CHECK(flat["metallic"].toDouble()==0.4);
    AIVERSE_TEST_CHECK(flat["roughness"].toDouble()==0.5);
    render::Material material;
    material.name="body";
    int textureCalls=0;
    AppearanceAuthoringService::applyMaterial(
        material,library,variant,
        [&](const std::string& path) {
            AIVERSE_TEST_CHECK(path=="textures/paint.png");
            ++textureCalls;
            return 7;
        },warn);
    AIVERSE_TEST_CHECK(material.name=="body");
    AIVERSE_TEST_CHECK(material.baseTex==7);
    AIVERSE_TEST_CHECK(textureCalls==1);
    AIVERSE_TEST_CHECK(material.roughness==0.5f);
    AIVERSE_TEST_CHECK(material.shadowDiffusion==0.125f);
    AIVERSE_TEST_CHECK(material.thinTransmission==0.35f);
    AIVERSE_TEST_CHECK(material.sssStrength==0.6f && material.sssScale==0.4f);
    AIVERSE_TEST_CHECK(material.sssThickness==0.06f);
    AIVERSE_TEST_CHECK(material.sssColor.y==0.4f);
    AIVERSE_TEST_CHECK(material.team==render::TeamMode::Tint);
    AIVERSE_TEST_CHECK(warnings.empty());
    const auto missing=AppearanceAuthoringService::resolveMaterial(
        library,Json::string("not_found"),warn);
    AIVERSE_TEST_CHECK(missing.isObject());
    AIVERSE_TEST_CHECK(!warnings.empty());

    render::Model model;
    bool failed=false;
    try {
        AppearanceAuthoringService::compileGraph(model,
            Json::string("graphs/missing.json"),
            [](const std::string&,std::string&) {return false;});
    }catch(const std::runtime_error&) {failed=true;}
    AIVERSE_TEST_CHECK(failed);
}
