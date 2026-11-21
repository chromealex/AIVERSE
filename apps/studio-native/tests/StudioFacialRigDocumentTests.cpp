#include "apps/studio-native/StudioFacialRigDocument.h"
#include "tests/AlwaysCheck.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace aiverse;
using namespace aiverse::studio;

int main() {
    namespace fs=std::filesystem;
    const Json valid=Json::parse(R"json({
      "version":1,
      "morphs":[
        {"parameter":"jawOpen","node":"Face","target":"jawOpen",
         "gain":1,"bias":0,"halfLife":0.06,"blend":"override"},
        {"parameter":"eyeBlinkLeft","node":"Face","target":"eyeBlinkLeft",
         "blend":"additive"}],
      "rig":[{"bone":"Eye.L","op":"aim","target":"lookAt","axis":[0,0,1],
               "weightParameter":"lookWeight","weight":1}]
    })json");
    std::string error;
    AIVERSE_TEST_CHECK(validStudioFacialRigDraft(valid,&error));
    AIVERSE_TEST_CHECK(error.empty());
    AIVERSE_TEST_CHECK(validStudioFacialRigName("human_face-v1"));
    AIVERSE_TEST_CHECK(!validStudioFacialRigName("../evil"));
    AIVERSE_TEST_CHECK(!validStudioFacialRigName("nested/folder"));
    AIVERSE_TEST_CHECK(!validStudioFacialRigName("bad.json"));

    auto duplicate=valid;
    duplicate.member("morphs").push(Json::parse(R"json(
      {"parameter":"other","node":"Face","target":"jawOpen"})json"));
    AIVERSE_TEST_CHECK(!validStudioFacialRigDraft(duplicate,&error));
    auto invalid=valid;
    invalid.member("morphs").at(0).set("halfLife",Json::number(int64_t(-1)));
    AIVERSE_TEST_CHECK(!validStudioFacialRigDraft(invalid,&error));
    invalid=valid;
    invalid.member("rig").at(0).set("target",Json::string(""));
    AIVERSE_TEST_CHECK(!validStudioFacialRigDraft(invalid,&error));
    invalid=valid;
    invalid.set("version",Json::number(int64_t(2)));
    AIVERSE_TEST_CHECK(!validStudioFacialRigDraft(invalid,&error));

    const fs::path root=fs::temp_directory_path()/
        ("aiverse-studio-facial-rig-contract-"+std::to_string(::getpid()));
    std::error_code ec;
    if(!fs::create_directory(root))
        throw std::runtime_error("Studio facial rig fixture directory already exists");
    try {
        const Json saved=saveStudioFacialRig(root.string(),"hero",valid);
        AIVERSE_TEST_CHECK(saved["name"].str()=="hero");
        const Json names=listStudioFacialRigs(root.string());
        AIVERSE_TEST_CHECK(names["rigs"].isArray()&&names["rigs"].size()==1);
        AIVERSE_TEST_CHECK(names["rigs"][0].str()=="hero");
        const Json loaded=readStudioFacialRig(root.string(),"hero");
        AIVERSE_TEST_CHECK(loaded["rig"].dump(-1)==valid.dump(-1));

        bool escaped=false;
        try {(void)saveStudioFacialRig(root.string(),"../wrong",valid);}
        catch(const std::exception&) {escaped=true;}
        AIVERSE_TEST_CHECK(escaped);
        AIVERSE_TEST_CHECK(!fs::exists(root/"wrong.json"));

        fs::create_directory(root/"forbidden");
        escaped=false;
        try {(void)readStudioFacialRig(root.string(),"forbidden");}
        catch(const std::exception&) {escaped=true;}
        AIVERSE_TEST_CHECK(escaped);

        // Runtime reimport channel validation needs MotionWeave, not a
        // Studio fake Model/GPU. Schema acceptance alone is not a pass.
        fs::remove_all(root,ec);
    }catch(...) {
        fs::remove_all(root,ec);
        throw;
    }
    std::cout<<"Studio facial rig authoring contract OK\n";
}
