#include "apps/studio-native/StudioPbrMaterialPreview.h"
#include "tests/AlwaysCheck.h"
#include <cmath>
#include <iostream>
#include "apps/editor/core/MaterialOps.h"
#include "apps/editor/core/Workspace.h"
#include <filesystem>
#include <chrono>
#include <limits>
using namespace aiverse;
using namespace aiverse::studio;
int main() {
    auto material=studioPbrPreviewMaterial(Json::parse(R"({
        "color":[0.25,0.5,0.9,0.8],"metallic":0.95,"roughness":0.15,
        "emissive":[1,0.1,0.0],"emissiveStrength":2.5,"alpha":"blend",
        "doubleSided":true,"castShadow":false,"shadowDiffusion":0.125,
        "thinTransmission":0.35,"sssStrength":0.6,"sssScale":0.4,"sssThickness":0.06,"sssColor":[1,0.4,0.2]
    })"),"BlueSteel");
    AIVERSE_TEST_CHECK(material.name=="BlueSteel");
    AIVERSE_TEST_CHECK(material.shadowDiffusion==.125f);
    AIVERSE_TEST_CHECK(material.thinTransmission==.35f);
    AIVERSE_TEST_CHECK(material.sssStrength==.6f && material.sssScale==.4f);
    AIVERSE_TEST_CHECK(material.sssThickness==.06f);
    AIVERSE_TEST_CHECK(material.sssColor.z==.2f);
    AIVERSE_TEST_CHECK(material.metallic>.9f && material.roughness<.2f);
    AIVERSE_TEST_CHECK(material.baseColor.x==.25f && material.baseColor.w==.8f);
    AIVERSE_TEST_CHECK(material.emissiveStrength==2.5f);
    AIVERSE_TEST_CHECK(material.alpha==render::AlphaMode::Blend && material.transparent);
    AIVERSE_TEST_CHECK(material.doubleSided && !material.castShadow);
    material=studioPbrPreviewMaterial(Json::parse(R"({
      "metallic":500,"roughness":-2,"emissiveStrength":9999,"alpha":"mask","cutoff":0.34
    })"),"Clamped");
    AIVERSE_TEST_CHECK(material.metallic==1 && material.roughness>=.04f);
    AIVERSE_TEST_CHECK(material.emissiveStrength==100);
    AIVERSE_TEST_CHECK(material.alpha==render::AlphaMode::Mask);
    AIVERSE_TEST_CHECK(std::abs(material.alphaCutoff-.34f)<.001f);
    material=studioPbrPreviewMaterial(Json::object(),"Default");
    AIVERSE_TEST_CHECK(material.shading==render::ShadingModel::Pbr);
    AIVERSE_TEST_CHECK(material.shadowDiffusion==0);
    AIVERSE_TEST_CHECK(material.sssStrength==0);
    AIVERSE_TEST_CHECK(material.sssThickness==0);
    AIVERSE_TEST_CHECK(material.thinTransmission==0);
    AIVERSE_TEST_CHECK(material.vertexColors==false);
    AIVERSE_TEST_CHECK(material.roughness>=0 && material.roughness<=1);
    AIVERSE_TEST_CHECK(editor::checkMaterial(Json::parse(R"({"shadowDiffusion":0.125})")).empty());
    AIVERSE_TEST_CHECK(!editor::checkMaterial(Json::parse(R"({"shadowDiffusion":-0.1})")).empty());
    AIVERSE_TEST_CHECK(!editor::checkMaterial(Json::parse(R"({"shadowDiffusion":1.1})")).empty());
    AIVERSE_TEST_CHECK(!editor::checkMaterial(Json::parse(R"({"shadowDiffusion":"soft"})")).empty());
    AIVERSE_TEST_CHECK(editor::checkMaterial(Json::parse(R"({"sssStrength":0.6,"sssScale":0.4,"sssThickness":0.06,"sssColor":[1,0.4,0.2]})")).empty());
    AIVERSE_TEST_CHECK(editor::checkMaterial(Json::parse(R"({"thinTransmission":0.35,"sssStrength":0.6,"sssScale":0.4,"sssColor":[1,0.4,0.2]})")).empty());
    AIVERSE_TEST_CHECK(!editor::checkMaterial(Json::parse(R"({"sssColor":"red"})")).empty());
    AIVERSE_TEST_CHECK(!editor::checkMaterial(Json::parse(R"({"thinTransmission":1.1})")).empty());
    const auto folder=std::filesystem::temp_directory_path()/("aiverse_diffusion_material_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    {
        editor::Workspace workspace;
        workspace.project.dir=folder.string();
        editor::saveMaterialLibrary(workspace,Json::parse(R"({"fabric":{"shadowDiffusion":0.125,"roughness":0.9,"thinTransmission":0.35,"sssStrength":0.6,"sssScale":0.4,"sssThickness":0.06,"sssColor":[1,0.4,0.2]}})"),"test SSS save");
        const auto reloaded=editor::loadMaterialLibrary(workspace);
        AIVERSE_TEST_CHECK(reloaded["fabric"]["shadowDiffusion"].toDouble()==.125);
        AIVERSE_TEST_CHECK(studioPbrPreviewMaterial(reloaded["fabric"],"fabric").shadowDiffusion==.125f);
        AIVERSE_TEST_CHECK(reloaded["fabric"]["sssStrength"].toDouble()==.6);
        AIVERSE_TEST_CHECK(reloaded["fabric"]["sssThickness"].toDouble()==.06);
        AIVERSE_TEST_CHECK(studioPbrPreviewMaterial(reloaded["fabric"],"fabric").thinTransmission==.35f);
        AIVERSE_TEST_CHECK(studioPbrPreviewMaterial(reloaded["fabric"],"fabric").sssColor.y==.4f);
    }
    std::filesystem::remove_all(folder);
    std::cout<<"Studio PBR material adapter PASS\n";
    return 0;
}
