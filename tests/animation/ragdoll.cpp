#include <aiverselib/animation/RagdollAsset.h>
#include <cassert>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
int main(){
    Model m;m.nodes.resize(4);m.order={0,1,2,3};
    m.nodes[0].name="root";m.nodes[0].children={1};
    m.nodes[1].name="Hips";m.nodes[1].parent=0;m.nodes[1].children={2};
    m.nodes[2].name="Chest";m.nodes[2].parent=1;m.nodes[2].children={3};
    m.nodes[3].name="Head";m.nodes[3].parent=2;
    m.nodes[1].t={0,1,0};m.nodes[2].t={0,1,0};m.nodes[3].t={0,.5f,0};
    auto avatar=HumanoidAvatar::compile(m,{
        {HumanoidBone::Hips,"root/Hips"},{HumanoidBone::Chest,"root/Hips/Chest"},
        {HumanoidBone::Head,"root/Hips/Chest/Head"}});
    auto rag=RagdollAsset::compile(avatar);
    assert(rag.scene()["bodies"].size()==3);
    assert(rag.scene()["joints"].size()==2);
    Pose target;bindPose(m,target);
    std::vector<RagdollBodyPose> bodies;
    for(const auto& bind:rag.bindings())
        if(bind.role==HumanoidBone::Head)bodies.push_back({bind.id,{0,3,0},{}});
    rag.applyBodyPoses(bodies,target);
    assert(target.size()==4);
    return 0;
}
