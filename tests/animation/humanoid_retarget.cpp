#include <aiverselib/animation/HumanoidRetarget.h>
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
int main() {
    Model src,dst;
    for(auto* m:{&src,&dst}) {
        m->nodes.resize(3);m->order={0,1,2};
        m->nodes[0].name="root";m->nodes[0].children={1};
        m->nodes[1].parent=0;m->nodes[1].children={2};
        m->nodes[2].parent=1;
    }
    src.nodes[1].name="Hips";src.nodes[2].name="Head";
    dst.nodes[1].name="Pelvis";dst.nodes[2].name="Cranium";
    src.nodes[1].t={0,1,0};src.nodes[2].t={0,1,0};
    dst.nodes[1].t={0,2,0};dst.nodes[2].t={0,2,0};
    dst.nodes[1].r=quat::axisAngle({0,1,0},0.5f);
    dst.nodes[2].r=quat::axisAngle({0,1,0},0.25f);
    auto sa=HumanoidAvatar::compile(src,{
        {HumanoidBone::Hips,"root/Hips"},
        {HumanoidBone::Head,"root/Hips/Head"}});
    auto da=HumanoidAvatar::compile(dst,{
        {HumanoidBone::Hips,"root/Pelvis"},
        {HumanoidBone::Head,"root/Pelvis/Cranium"}});
    auto retarget=HumanoidRetargeter::compile(sa,da);
    Pose in,out;bindPose(src,in);
    in[2].r=quat::axisAngle({1,0,0},0.7f);
    in[1].t.x=0.3f;
    retarget.evaluate(in,out);
    assert(out.size()==3);
    assert(std::abs(out[1].t.x-0.3f)<.001f); // no foot rig: neutral scale 1
    assert(std::abs(dot(out[2].r,qnormalize(dst.nodes[2].r*in[2].r)))>.999f); // preserves target bind orientation
    Pose bindDst;bindPose(dst,bindDst);
    bindPose(src,in);retarget.evaluate(in,out);
    assert(std::abs(dot(out[1].r,bindDst[1].r))>.999f);
    return 0;
}
