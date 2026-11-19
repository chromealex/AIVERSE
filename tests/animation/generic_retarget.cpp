#include <aiverselib/animation/GenericRetarget.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Host tests do not link the platform runtime, matching animation_graphs.
namespace aiverse::platform { bool readContentFile(const std::string&,std::string&){return false;} }
using namespace aiverse::render;
using namespace aiverse::lib::animation;
static void require(bool b,const char* m) {if(!b){std::fprintf(stderr,"retarget: %s\n",m);std::exit(1);}}
int main() {
    Model a,b;
    a.nodes.resize(2);b.nodes.resize(3);
    a.nodes[0].name="root";a.nodes[0].children={1};
    a.nodes[1].name="arm";a.nodes[1].parent=0;a.nodes[1].t={0,1,0};
    b.nodes[0].name="root";b.nodes[0].children={1,2};
    b.nodes[1].name="weaponArm";b.nodes[1].parent=0;b.nodes[1].t={0,2,0};
    b.nodes[2].name="unused";b.nodes[2].parent=0;
    b.nodes[1].r=quat::axisAngle({0,1,0},.5f);
    auto map=GenericRetargetMap::compile(a,b,{{"root/arm","root/weaponArm"}});
    require(map.pairs().size()==2,"root + alias mapping");
    Pose src,dst;bindPose(a,src);
    src[1].t.x=1.f;src[1].r=quat::axisAngle({0,1,0},1.f);
    src[1].s={1.5f,1,1};src[1].w[0]=.7f;
    map.apply(a,b,src,dst);
    require(std::abs(dst[1].t.x-2.f)<.001f,"bind-length translation ratio");
    const quat expected=qnormalize(b.nodes[1].r*src[1].r);
    require(std::abs(dot(dst[1].r,expected))>.999f,"rotation bind delta");
    require(std::abs(dst[1].s.x-1.f)<.001f,"scale opt-in");
    require(std::abs(dst[1].w[0])<.001f,"morph opt-in");
    require(std::abs(dst[2].t.x)<.001f,"unmapped bone stays bind");
    map.apply(a,b,src,dst,{true,true,true});
    require(std::abs(dst[1].s.x-1.5f)<.001f,"scale copied");
    require(std::abs(dst[1].w[0]-.7f)<.001f,"morph copied");
    bool thrown=false;
    try { Pose wrong;map.apply(a,b,wrong,dst); }
    catch(const std::invalid_argument&) {thrown=true;}
    require(thrown,"wrong source pose rejected");
    require(bonePath(b,1)=="root/weaponArm","stable full bone path");
    std::puts("generic retarget OK");
}
