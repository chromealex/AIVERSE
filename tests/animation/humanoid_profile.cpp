#include <aiverselib/animation/HumanoidProfile.h>
#include "../AlwaysCheck.h"
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
int main() {
    Model model;model.nodes.resize(3);model.order={0,1,2};
    model.nodes[0].name="Armature";model.nodes[0].children={1};
    model.nodes[1].name="mixamorig:Hips";model.nodes[1].parent=0;model.nodes[1].children={2};
    model.nodes[2].name="mixamorig:Head";model.nodes[2].parent=1;
    auto suggested=suggestHumanoidRoles(model);
    AIVERSE_TEST_CHECK(suggested.count(HumanoidBone::Hips));
    AIVERSE_TEST_CHECK(suggested.count(HumanoidBone::Head));
    auto doc=humanoidProfileJson("hero.fbx",suggested);
    auto roles=readHumanoidProfile(Json::parse(doc.dump()));
    AIVERSE_TEST_CHECK(roles.at(HumanoidBone::Head)==suggested.at(HumanoidBone::Head));
    auto avatar=HumanoidAvatar::compile(model,roles);
    AIVERSE_TEST_CHECK(avatar.node(HumanoidBone::Hips)==1);
    return 0;
}
