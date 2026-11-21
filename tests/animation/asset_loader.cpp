#include <aiverselib/animation/AnimationAssetLoader.h>
#include "../AlwaysCheck.h"
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(){
    Model m;m.nodes.resize(1);m.nodes[0].name="root";m.order={0};
    AnimClip c;c.name="Shoot";c.duration=.7f;
    m.clips.push_back(c);
    auto asset=compileMontageAsset(m,Json::parse(R"({
        "model":"models/hero.glb","slot":"UpperBody",
        "blendIn":0.1,"blendOut":0.2,
        "sections":[{"name":"Shot","clip":"Shoot","begin":0,"end":0.7,
                     "next":-1,"notifies":[{"time":0.3,"name":"shot_fx"}]}]
    })"));
    AIVERSE_TEST_CHECK(asset && asset->sections.size()==1);
    AIVERSE_TEST_CHECK(asset->sections[0].notifies[0].time>.29f);
    bool failed=false;
    try{compileMontageAsset(m,Json::parse(R"({"sections":[{"clip":"Missing","begin":0,"end":1}]})"));}catch(const std::exception&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    return 0;
}
