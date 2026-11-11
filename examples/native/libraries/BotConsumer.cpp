#include <aiverselib/bots/Rts.h>
#include <aiverselib/bots/Persistence.h>
#include <aiverselib/bots/Waypoints.h>
#include "modules/rts/RtsNative.h"
// Engine-specific bot hosts call the adapters before stepping; this TU verifies project-native access.
bool libraryBotConsumerExample(aiverse::game::NativeState& state) {
    using namespace aiverse::lib::bots;
    auto rts=rtsConfig(aiverse::Json::parse(R"({"preset":"rush"})"));
    auto fps=fpsConfig(aiverse::Json::parse(R"({"preset":"survivor"})"));
    auto route=readWaypoints(aiverse::Json::parse("[]"));
    return rts.firstAttackArmy==4&&fps.retreatHealth==60&&route.empty()&&storeBytes(state,"bots.fps",saveFps({}));
}
