#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/LocomotionModes.h>
#include "modules/rts/RtsNative.h"
// Custom native state initialization; the project's command/physics owners consume the policies.
bool libraryGenreConsumerExample(aiverse::game::NativeState& state,const aiverse::Json& settings,const aiverse::Json& story){
    using namespace aiverse::lib::genre;
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(settings);if(!parsed.value)return false;
    if(!valid(motionConfig(*parsed.value))||!valid(matchConfig(*parsed.value)))return false;
    auto graph=readStory(story);auto initial=beginStory(graph,graph.begin()->first,{{1,1}});if(!initial)return false;
    // Mode machine (slopes, ledges, ladders, swimming, climbing, mantle): one grounded spawn tick.
    // The physics owner fills LocomotionInput::env every tick; bind the config hash in the save envelope.
    const auto modes=locomotionModeConfig(*parsed.value);if(!valid(modes))return false;
    LocomotionState body;LocomotionInput spawn;spawn.env.grounded=true;
    if(!stepLocomotion(modes,body,spawn,0,aiverse::Fixed::fromRatio(1,60)))return false;
    state.values["genre.locomotion.config"]=int64_t(locomotionConfigHash(modes)&0x7FFFFFFFFFFFFFFFull);
    return storeBytes(state,"genre.story",save(*initial))&&storeBytes(state,"genre.motion",save(MotionState{}))&&storeBytes(state,"genre.locomotion",save(body));
}
