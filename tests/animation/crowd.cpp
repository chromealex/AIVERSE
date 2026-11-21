#include <aiverselib/animation/AnimationCrowdSystems.h>
#include "../AlwaysCheck.h"
using namespace aiverse;
using namespace aiverse::lib::animation;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(){
    // GPU textures require a real Device; validate the opt-in no-GPU guard.
    World world;Entity e=world.create();
    auto& state=world.add<AnimationCrowdComponent>(e);
    world.add<AnimationCrowdSampleComponent>(e);
    stepAnimationCrowd(world,.1f);
    AIVERSE_TEST_CHECK(!world.get<AnimationCrowdSampleComponent>(e).valid);
    state.speed=1.f;
    stepAnimationCrowd(world,.1f);
    AIVERSE_TEST_CHECK(!world.get<AnimationCrowdSampleComponent>(e).valid);
    return 0;
}
