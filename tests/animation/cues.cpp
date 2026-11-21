#include <aiverselib/animation/AnimationEventDispatch.h>
#include "../AlwaysCheck.h"
using namespace aiverse;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(){
    World w;Entity e=w.create();
    w.add<AnimationAssetComponent>(e);
    auto& events=w.add<AnimationGraphNotifiesComponent>(e);
    events.events={{"run","step",.75f,1},{"run","land",.25f,1}};
    auto all=collectAnimationCues(w);
    AIVERSE_TEST_CHECK(all.size()==2 && all[0].name=="land" && all[1].name=="step");
    int seen=0;dispatchAnimationCues(w,[&](const AnimationCue&){++seen;});
    AIVERSE_TEST_CHECK(seen==2);
    return 0;
}
