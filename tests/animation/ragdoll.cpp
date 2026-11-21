#include <aiverselib/animation/RagdollAsset.h>
#include "../AlwaysCheck.h"
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
int main(){
    Model m;m.nodes.resize(5);m.order={0,1,2,3,4};
    m.nodes[0].name="root";m.nodes[0].children={1};
    m.nodes[1].name="Hips";m.nodes[1].parent=0;m.nodes[1].children={2};
    m.nodes[2].name="Chest";m.nodes[2].parent=1;m.nodes[2].children={3};
    m.nodes[3].name="Neck";m.nodes[3].parent=2;m.nodes[3].children={4};
    m.nodes[4].name="Head";m.nodes[4].parent=3;
    m.nodes[1].t={0,1,0};m.nodes[2].t={0,1,0};m.nodes[3].t={0,.3f,0};m.nodes[4].t={0,.2f,0};
    auto avatar=HumanoidAvatar::compile(m,{
        {HumanoidBone::Hips,"root/Hips"},{HumanoidBone::Chest,"root/Hips/Chest"},
        {HumanoidBone::Neck,"root/Hips/Chest/Neck"},
        {HumanoidBone::Head,"root/Hips/Chest/Neck/Head"}});
    auto rag=RagdollAsset::compile(avatar);
    AIVERSE_TEST_CHECK(rag.scene()["bodies"].size()==3);
    AIVERSE_TEST_CHECK(rag.scene()["joints"].size()==2);
    Pose target;bindPose(m,target);
    std::vector<RagdollBodyPose> bodies;
    for(const auto& bind:rag.bindings())
        if(bind.role==HumanoidBone::Head)bodies.push_back({bind.id,{0,3,0},{}});
    rag.applyBodyPoses(bodies,target);
    AIVERSE_TEST_CHECK(target.size()==5);
    auto sparse=avatar;
    sparse.nodes[size_t(HumanoidBone::Neck)]=-1;
    const auto sparseRag=RagdollAsset::compile(sparse);
    // Neckless avatars retain a Chest-to-Head torso capsule.
    AIVERSE_TEST_CHECK(sparseRag.scene()["bodies"].size()==3);
    AIVERSE_TEST_CHECK(sparseRag.scene()["joints"].size()==2);
    return 0;
}
