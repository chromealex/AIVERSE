#include <aiverselib/animation/RootMotionBaker.h>
#include <cassert>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(){
    Model model;model.nodes.resize(1);model.nodes[0].name="root";model.order={0};
    AnimClip clip;clip.name="Walk";clip.duration=1;
    AnimChannel c;c.node=0;c.times={0,1};c.values={0,0,0,2,0,0};
    clip.channels.push_back(c);
    auto fixed=bakeRootMotion(model,clip,0,3,60,true);
    assert(fixed.frames.size()==60);
    assert(fixed.frames[0].local.x.raw>0);
    RootMotionTracksResource all;all.clips.push_back(fixed);
    auto read=decodeRootMotionTracks(Json::parse(encodeRootMotionTracks(all).dump()));
    assert(read.clips[0].id==3 && read.clips[0].frames.size()==60);
    int64_t accumulated=0;
    for(auto& frame:read.clips[0].frames)accumulated+=frame.local.x.raw;
    assert(accumulated>Fixed::kOne*19/10 && accumulated<Fixed::kOne*21/10);
    return 0;
}
