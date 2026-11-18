#include "engine/platform/InputFlow.h"
#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstdlib>
#include <iostream>
namespace p=aiverse::platform;
namespace ei=aiverse::lib::gameplay::inputflow;
namespace conf=aiverse::platform::inputflow_config;
#define CHECK(x) do { if(!(x)){ std::cerr<<"InputFlow JSON failed line "<<__LINE__<<": " #x "\n";std::abort();} } while(false)

int main() {
    const auto data=aiverse::Json::parse(R"JSON({
       "version":1,
       "actions":[
          {"name":"Move","type":"axis2D","modifiers":[{"type":"normalize"}]},
          {"name":"Aim","type":"bool"},
          {"name":"Fire","type":"bool"}
       ],
       "contexts":[{
          "name":"Gameplay","priority":10,"active":true,
          "mappings":[
             {"id":3,"action":"Move","control":{"device":"keyboard","code":"W"},
              "modifiers":[{"type":"swizzle","order":[1,0,2]}]},
             {"id":4,"action":"Move","control":{"device":"gamepadAxis","code":"LY"},
              "modifiers":[{"type":"deadzone","threshold":0.13},{"type":"negate"},{"type":"swizzle","order":[1,0,2]}]},
             {"id":5,"action":"Aim","control":{"device":"mouseButton","code":"right"}},
             {"id":6,"action":"Fire","control":{"device":"mouseButton","code":"left"},
              "triggers":[{"type":"pressed"},{"type":"chord","action":"Aim"}]}
          ]
       }]
    })JSON");
    std::string err;
    conf::Asset asset;
    CHECK(conf::readAsset(data,asset,&err));
    CHECK(asset.actions.size()==3&&asset.contexts.size()==1);
    CHECK(asset.contexts[0].context.mappings[0].control.code==int(p::Key::W));
    CHECK(asset.contexts[0].context.mappings[1].modifiers[0].threshold>0.12f);
    const auto serialized=conf::writeAsset(asset);
    conf::Asset duplicate;
    CHECK(conf::readAsset(aiverse::Json::parse(serialized.dump()),duplicate,&err));
    CHECK(duplicate.contexts[0].context.mappings[0].id==3);
    ei::InputFlow input;
    CHECK(conf::installAsset(input,duplicate,&err));
    input.setControl({ei::Device::Keyboard,int(p::Key::W)},1);
    input.update(0.016f);
    CHECK(input.state("Move")&&input.state("Move")->value.y==1);
    input.setControl({ei::Device::Keyboard,int(p::Key::W)},0);
    input.update(0.016f);
    CHECK(!input.state("Move")->active);
    input.setControl({ei::Device::MouseButton,0},1);
    input.update(0.016f);
    CHECK(!input.state("Fire")->triggered);
    input.setControl({ei::Device::MouseButton,1},1);
    input.setControl({ei::Device::MouseButton,0},0);
    input.update(0.016f);
    input.setControl({ei::Device::MouseButton,0},1);
    input.update(0.016f);
    CHECK(input.state("Fire")->triggered);

    const auto profileData=aiverse::Json::parse(R"JSON({
       "version":1,"rebindings":[
         {"context":"Gameplay","id":3,"control":{"device":"keyboard","code":"Up"}}
       ]
    })JSON");
    conf::Profile profile;
    CHECK(conf::readProfile(profileData,profile,&err));
    CHECK(conf::applyProfile(input,profile,&err));
    CHECK(input.context("Gameplay")->mappings[0].control.code==int(p::Key::Up));
    CHECK(conf::writeProfile(profile).dump()==profileData.dump());

    const auto bad=aiverse::Json::parse(R"({"version":2,"actions":[],"contexts":[]})");
    CHECK(!conf::readAsset(bad,duplicate,&err));
    CHECK(!err.empty());
    // Full assets may use forward references, but a transitive chord / inverted
    // chord loop is invalid. On failure the output Asset must stay unchanged.
    const auto beforeCycle=conf::writeAsset(duplicate).dump();
    const auto cyclicAsset=aiverse::Json::parse(R"JSON({
       "version":1,
       "actions":[
          {"name":"A","type":"bool","triggers":[{"type":"chord","action":"B"}]},
          {"name":"B","type":"bool","triggers":[{"type":"notChord","action":"A"}]}
       ],
       "contexts":[{"name":"Cycle","priority":0,"active":true,"mappings":[
          {"id":1,"action":"A","control":{"device":"keyboard","code":"A"}},
          {"id":2,"action":"B","control":{"device":"keyboard","code":"B"}}
       ]}]
    })JSON");
    CHECK(!conf::readAsset(cyclicAsset,duplicate,&err));
    CHECK(err.find("cyclic")!=std::string::npos);
    CHECK(conf::writeAsset(duplicate).dump()==beforeCycle);
    const auto dupBindings=aiverse::Json::parse(R"JSON({
        "version":1,"rebindings":[
          {"context":"Gameplay","id":1,"control":{"device":"keyboard","code":"A"}},
          {"context":"Gameplay","id":1,"control":{"device":"keyboard","code":"B"}}
        ]
    })JSON");
    CHECK(!conf::readProfile(dupBindings,profile,&err));
    const auto unknown=aiverse::Json::parse(R"JSON({
        "version":1,"rebindings":[
          {"context":"Gameplay","id":999,"control":{"device":"keyboard","code":"B"}}
        ]
    })JSON");
    CHECK(conf::readProfile(unknown,profile,&err));
    const int before=input.context("Gameplay")->mappings[0].control.code;
    CHECK(!conf::applyProfile(input,profile,&err));
    CHECK(input.context("Gameplay")->mappings[0].control.code==before);
    const auto invalidCode=aiverse::Json::parse(R"JSON({
        "version":1,"rebindings":[
          {"context":"Gameplay","id":3,"control":{"device":"gamepadAxis","code":"NotAnAxis"}}
        ]
    })JSON");
    CHECK(!conf::readProfile(invalidCode,profile,&err));
    // Both built-in presets can be authored, serialized and installed with stable mapping IDs.
    for(const auto& preset:{p::inputflow_presets::fpsReference(),p::inputflow_presets::rtsReference()}){
        CHECK(!preset.actions.empty()&&!preset.contexts.empty());
        conf::Asset decoded;
        CHECK(conf::readAsset(conf::writeAsset(preset),decoded,&err));
        CHECK(decoded.contexts.size()==preset.contexts.size());
        for(std::size_t j=0;j<decoded.contexts.size();++j){
            CHECK(decoded.contexts[j].context.mappings.size()==preset.contexts[j].context.mappings.size());
            for(std::size_t i=0;i<decoded.contexts[j].context.mappings.size();++i)
                CHECK(decoded.contexts[j].context.mappings[i].id==i+1);
        }
        ei::InputFlow runtime;
        CHECK(conf::installAsset(runtime,decoded,&err));
        CHECK(runtime.registeredContexts().size()==preset.contexts.size());
    }
    // Host profile persistence: disk roundtrip, invalid writes do not replace prior state.
    const auto unique=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto file=std::filesystem::temp_directory_path()/
        ("aiverse-input-profile-"+std::to_string(unique)+".json");
    conf::Profile profileOnDisk;
    CHECK(conf::saveProfileFile(file,conf::Profile{{{"Gameplay",3,{ei::Device::Keyboard,int(p::Key::Up)}}}},&err));
    CHECK(conf::loadProfileFile(file,profileOnDisk,&err));
    CHECK(profileOnDisk.rebindings.size()==1);
    CHECK(profileOnDisk.rebindings[0].control.code==int(p::Key::Up));
    conf::Profile invalidProfile=profileOnDisk;
    invalidProfile.rebindings[0].control.code=999999; // unknown keyboard
    CHECK(!conf::saveProfileFile(file,invalidProfile,&err));
    conf::Profile afterFailure;
    CHECK(conf::loadProfileFile(file,afterFailure,&err));
    CHECK(afterFailure.rebindings[0].control.code==int(p::Key::Up));
    // Project-authored assets use the same non-destructive persistence primitive.
    const auto assetFile=std::filesystem::path(file.string()+".actions");
    CHECK(conf::saveAssetFile(assetFile,asset,&err));
    conf::Asset diskAsset;
    CHECK(conf::loadAssetFile(assetFile,diskAsset,&err));
    CHECK(diskAsset.actions.size()==3 && diskAsset.contexts.size()==1);
    conf::Asset invalidAsset=diskAsset;
    invalidAsset.contexts[0].context.mappings[0].action="MissingAction";
    CHECK(!conf::saveAssetFile(assetFile,invalidAsset,&err));
    conf::Asset afterAssetFailure;
    CHECK(conf::loadAssetFile(assetFile,afterAssetFailure,&err));
    CHECK(afterAssetFailure.contexts[0].context.mappings[0].action=="Move");
    // Per-user store keeps player zero's legacy filename and isolates other
    // local players. A Swap persists both mapping IDs across a reload.
    conf::UserProfiles users;
    const auto userRoot=std::filesystem::path(file.string()+".users");
    users.setScope(userRoot,"demo-project");
    ei::InputFlow p0,p1;
    CHECK(conf::installAsset(p0,p::inputflow_presets::fpsReference(),&err));
    p1=p0;
    CHECK(users.load(0,p0,&err)&&users.load(1,p1,&err));
    CHECK(users.path(0)!=users.path(1));
    const int originalCode=p1.context("FPS")->mappings[0].control.code;
    const int alternateCode=p1.context("FPS")->mappings[1].control.code;
    CHECK(users.rebind(1,p1,"FPS",1,{ei::Device::Keyboard,alternateCode},
                       ei::RebindPolicy::Swap,&err));
    CHECK(p1.context("FPS")->mappings[0].control.code==alternateCode);
    CHECK(p1.context("FPS")->mappings[1].control.code==originalCode);
    ei::InputFlow reloaded;
    CHECK(conf::installAsset(reloaded,p::inputflow_presets::fpsReference(),&err));
    conf::UserProfiles nextRun;
    nextRun.setScope(userRoot,"demo-project");
    CHECK(nextRun.load(1,reloaded,&err));
    CHECK(reloaded.context("FPS")->mappings[0].control.code==alternateCode);
    CHECK(reloaded.context("FPS")->mappings[1].control.code==originalCode);
    CHECK(!nextRun.profile(0));
    CHECK(nextRun.reset(1,reloaded,p0,&err));
    CHECK(reloaded.context("FPS")->mappings[0].control.code==originalCode);
    std::error_code cleanup;
    std::filesystem::remove_all(userRoot,cleanup);
    std::filesystem::remove(assetFile,cleanup);
    std::filesystem::remove(file,cleanup);
    std::cout<<"InputFlow config/schema/profile roundtrip passed\n";
}
