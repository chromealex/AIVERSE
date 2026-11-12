#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/WeaponData.h>
#include <aiverselib/genre/WeaponPersistence.h>
#include "modules/rts/RtsNative.h"
// Custom native state initialization; the project's command/physics owners consume the policies.
namespace {
// Weapon policies from the same validated settings plus project definitions (materials, frame,
// attachments). The host's fire/reload commands later call stepFeed/stepCharge once per tick in the
// authoritative handler; here only validated initial records are stored, one NativeState key each.
bool weaponConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse;using namespace aiverse::lib::genre;
    if(!weaponSettingsErrors(settings).empty())return false;
    auto defs=readWeaponDefinitions(Json::parse(R"({"materials":[{"id":1,"resistanceMilli":200,"damageFalloffMilli":100},{"id":2,"impenetrable":true}],
        "frame":{"slots":[1],"stats":[{"id":1,"baseMilli":25000,"minimumMilli":0,"maximumMilli":100000}]},
        "attachments":[{"id":10,"slot":1,"modifiers":[{"stat":1,"op":"mul","valueMilli":1200}]}]})"));
    LoadoutState loadout;auto stats=equipAttachment(defs.frame,defs.attachments,loadout,10)?resolveStats(defs.frame,defs.attachments,loadout):std::nullopt;
    auto feedRules=feedConfig(settings);auto charge=chargeConfig(settings);
    auto shot=resolvePenetration(defs.materials,penetrationConfig(settings),{{1,1,{},1_fx,{},{}},{2,2,1_fx,{},{},{}}},stats?stats->at(1):Fixed{},800_fx);
    auto blast=resolveExplosion(explosionConfig(settings),{},{{1,{2_fx,{},{}},{}}},[](const ExplosionTarget&){return 1_fx;});
    auto aim=solveIntercept(aimConfig(settings),{},{20_fx,{},{}},{},50_fx,10_fx);
    if(!stats||!valid(feedRules)||!valid(charge)||!shot||!blast||!aim||!aim->found)return false;
    return storeBytes(state,"genre.weapon.feed",save(makeFeed(feedRules)))&&storeBytes(state,"genre.weapon.charge",save(ChargeState{}))&&storeBytes(state,"genre.weapon.loadout",save(loadout));
}
}
bool libraryGenreConsumerExample(aiverse::game::NativeState& state,const aiverse::Json& settings,const aiverse::Json& story){
    using namespace aiverse::lib::genre;
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(settings);if(!parsed.value)return false;
    if(!valid(motionConfig(*parsed.value))||!valid(matchConfig(*parsed.value)))return false;
    auto graph=readStory(story);auto initial=beginStory(graph,graph.begin()->first,{{1,1}});if(!initial)return false;
    return storeBytes(state,"genre.story",save(*initial))&&storeBytes(state,"genre.motion",save(MotionState{}))&&weaponConsumerExample(state,*parsed.value);
}
