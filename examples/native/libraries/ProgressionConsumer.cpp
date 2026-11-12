#include <aiverselib/genre/PerksConfigured.h>
#include <aiverselib/genre/PerksData.h>
#include <aiverselib/genre/PerksPersistence.h>
#include "modules/rts/RtsNative.h"
// match-progression: battle deck from the match start payload, one kill -> xp orb -> pickup -> level-up offer ->
// choice, and the three persisted states (perks, orbs, summons) in NativeState. Stat values are read back with
// evaluateStat where the owner computes damage/speed; recruits and summons are spawned by the owner.
bool libraryProgressionConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse;using namespace aiverse::lib::genre;
    if(!perkSettingsErrors(settings).empty())return false;
    const auto perk=perkConfig(settings);const auto pick=pickupConfig(settings);const auto summon=summonConfig(settings);
    const auto defs=readPerkDefinitions(Json::parse(R"({"units":[{"id":1,"hero":true},{"id":2}],
        "perks":[{"id":1,"key":"armor","effects":[{"stat":"incomingDamage","op":"multiply","value":900}]}],
        "xp":{"toNext":[3,6]},"offers":{"default":["recruit","playerPerk"]}})"));
    const auto deck=readBattleDeck(Json::parse(R"({"player":1,"slots":[{"unit":1},{"unit":2}]})"));
    PerkState perks;seedPerks(perks,0x5EEDu);if(!addPlayer(defs,perks,deck))return false;
    PickupState orbs;SummonState allies;if(!addSummoner(summon,allies,1,0))return false;
    if(!stepPickups(pick,orbs,1,{{{0,3,{}}},{}}))return false;
    auto got=stepPickups(pick,orbs,2+pick.landing,{{},{{1,{},{}}}});if(!got||got->collected.empty())return false;
    for(auto [player,xp]:got->collected)if(!grantXp(defs,perk,perks,player,xp,true))return false;
    if(!openOffer(defs,perk,perks,1))return false;const auto& p=perks.players.at(1);
    auto choice=botOfferChoice(defs,perk,p,false);if(!choice||!selectOffer(defs,perk,perks,1,p.offer.revision,*choice,2))return false;
    state.values["genre.progression.config"]=int64_t((perkDefinitionsHash(defs)^progressionConfigHash(perk,pick,summon))&0x7FFFFFFFFFFFFFFFull);
    state.values["genre.progression_hash"]=int64_t((progressionHash(perks)^progressionHash(orbs)^progressionHash(allies))&0x7FFFFFFFFFFFFFFFull);
    return storeBytes(state,"genre.perks",save(perks))&&storeBytes(state,"genre.pickups",save(orbs))&&storeBytes(state,"genre.summons",save(allies));
}
