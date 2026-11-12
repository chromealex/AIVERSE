#include <aiverselib/genre/Configured.h>
#include <aiverselib/genre/Persistence.h>
#include <aiverselib/genre/Data.h>
#include <aiverselib/genre/LocomotionModes.h>
#include <aiverselib/genre/CombatConfigured.h>
#include <aiverselib/genre/CombatData.h>
#include <aiverselib/genre/WeaponData.h>
#include <aiverselib/genre/WeaponPersistence.h>
#include <aiverselib/genre/MatchObjectivesConfigured.h>
#include <aiverselib/genre/MatchObjectivesData.h>
#include <aiverselib/genre/SensesConfigured.h>
#include <aiverselib/genre/SensesData.h>
#include <aiverselib/genre/SensesPersistence.h>
#include <aiverselib/genre/NarrativeConfigured.h>
#include <aiverselib/genre/NarrativeSceneData.h>
#include <aiverselib/genre/NarrativeScenePersistence.h>
#include <aiverselib/genre/CinematicConfigured.h>
#include <aiverselib/genre/CinematicData.h>
#include <aiverselib/genre/CinematicPersistence.h>
#include <aiverselib/bots/GenreBotConfig.h>
#include <aiverselib/genre/InteractionConfigured.h>
#include <aiverselib/genre/InteractionData.h>
#include <aiverselib/genre/InteractionPersistence.h>
#include "modules/rts/RtsNative.h"
#include <array>
#include <span>
#include <string>
// Custom native state initialization; the project's command/physics owners consume the policies.
// Combat actions: validated settings + hitbox data, one authoritative tick of hit collection and contact
// resolution, then per-actor state in NativeState. Damage itself stays with the project's combat owner.
static bool libraryCombatConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;using aiverse::Fixed;
    auto dodge=dodgeConfig(settings);auto guard=guardConfig(settings);auto poise=poiseConfig(settings);auto lock=lockOnConfig(settings);
    if(!valid(dodge)||!valid(guard)||!valid(poise)||!valid(lock))return false;
    AttackSet attacks{{1,attackConfig(settings,1)}};
    auto boxes=readHitboxes(aiverse::Json::parse(R"({"attacks":[{"attack":1,"hitboxes":[{"id":1,"from":)"+std::to_string(attacks[1].windup)+
        R"(,"to":)"+std::to_string(attacks[1].windup+attacks[1].active)+R"(,"shape":"sphere","a":[0,1000,1000],"radiusMilli":500}]}]})"),attacks);
    CombatShape torso;torso.kind=ShapeKind::Capsule;torso.b={{},Fixed::fromInt(2),{}};torso.radius=Fixed::fromRatio(2,5);
    CombatBody attacker{1,1,{},{{1,torso}}},defender{2,2,{{{},{},Fixed::fromRatio(6,5)},Fixed::fromRatio(1,2)},{{1,torso}}};
    MeleeState swing;CombatActorState target;auto always=[](auto){return true;};
    for(uint32_t tick=0;tick<=attacks[1].windup;++tick){
        if(!stepMelee(attacks,swing,tick,tick==0?1:0,always)||!stepDodge(dodge,target.dodge,tick,false,[]{return true;})||!stepGuard(guard,target.guard,tick,false)||!stepPoise(poise,target.poise,tick))return false;
        const std::array<LockCandidate,1> seen{{{attacker.id,attacker.pose.position,true}}};
        if(!stepLockOn(lock,target.lock,tick,defender.pose,seen,{tick==0,0}))return false;
        auto hits=collectMeleeHits(attacks,boxes,swing,tick,attacker,std::span<const CombatBody>(&defender,1));if(!hits)return false;
        for(const auto& hit:*hits){
            (void)hit;auto contact=resolveCombatContact(dodge,target.dodge,guard,target.guard,defender.pose,attacker.pose.position,tick);
            if(!applyPoiseHit(poise,target.poise,tick,poise.maximum/4,contact))return false;
        }
    }
    if(swing.hitTargets!=std::vector<int64_t>{defender.id}||target.lock.target!=attacker.id)return false;
    state.values["genre.combat_hash"]=int64_t(combatHash(target)&0x7fffffffffffffffull);
    return storeBytes(state,"genre.combat",save(target))&&storeBytes(state,"genre.combat_swing",save(swing));
}
// Stealth senses: validated settings + disguise/hiding definitions, one authoritative perception tick for a
// guard pair and an intruder (owner LOS/occlusion callbacks), then the versioned state in NativeState.
// Bots read SensesReport::perceived (PerceivedTarget) and searchGoal; movement stays with their controllers.
static bool librarySensesConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;using aiverse::Fixed;using aiverse::Vec3;
    if(!sensesSettingsErrors(settings).empty())return false;
    const auto config=sensesConfig(settings);
    auto defs=readSenseDefinitions(aiverse::Json::parse(R"({"disguises":[{"id":1,"factions":[1],"zones":[2],"blownBy":[1]}],
        "hidingSpots":[{"id":1,"position":[0,0,30000]}]})"));
    SensesInput in;
    SenseAgent guard;guard.id=1;guard.team=1;SenseAgent partner=guard;partner.id=2;partner.position={Fixed::fromInt(4),{},{}};
    in.observers={guard,partner};
    SenseTarget intruder;intruder.id=10;intruder.team=2;intruder.position={{},Fixed::fromRatio(17,10),Fixed::fromInt(6)};intruder.zone=2;intruder.equipDisguise=1;
    intruder.lightMilli=600;intruder.stanceMilli=500;intruder.leaveTrail=true;in.targets={intruder};
    NoiseEvent step;step.id=1;step.source=intruder.id;step.team=intruder.team;step.position=intruder.position;step.radius=Fixed::fromInt(8);step.loudnessMilli=400;in.noises={step};
    in.candidates={{1,{{},{},Fixed::fromInt(30)},1}};
    SensesState senses;
    auto los=[](const SenseAgent&,const SenseTarget&){return true;};          // owner: world raycast eye -> target
    auto occlusion=[](const SenseAgent&,const NoiseEvent&){return int64_t(1000);}; // owner: wall/door attenuation
    auto report=stepSenses(config,defs,senses,0,in,los,occlusion);
    if(!report||report->perceived.empty()||report->perceived.front().target!=intruder.id||!senses.disguises.contains(intruder.id))return false;
    state.values["genre.senses.config"]=int64_t(sensesConfigHash(config)&0x7FFFFFFFFFFFFFFFull);
    state.values["genre.senses_hash"]=int64_t(sensesHash(senses)&0x7FFFFFFFFFFFFFFFull);
    return storeBytes(state,"genre.senses",save(senses));
}
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
// Match objectives: validated settings + map layout, one authoritative tick of a capture zone, payload and
// objective clock, outcomes awarded through the match owner (awardScore), records stored one key each.
// Presence counts come from the project's own spatial query (insideZone here); deaths/pickups likewise.
static bool libraryMatchObjectivesConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;using aiverse::Fixed;
    if(!objectiveSettingsErrors(settings).empty())return false;
    auto layout=readObjectiveLayout(aiverse::Json::parse(R"({"zones":[{"id":1,"kind":"capture","center":[0,0,0],"radiusMilli":4000}],
        "payload":{"path":[[0,0,0],[20000,0,0],[20000,0,20000]],"checkpointsMilli":[20000]}})"));
    auto zoneRules=zoneConfig(settings);auto cartRules=payloadConfig(settings);auto clockRules=objectiveClockConfig(settings);
    auto bombRules=bombConfig(settings);auto spectate=spectatorConfig(settings);
    if(!valid(zoneRules)||!valid(cartRules)||!valid(clockRules)||!valid(bombRules)||!valid(spectate)||!layout.payload)return false;
    const std::array<int64_t,2> teams{1,2};auto match=makeMatch(teams);auto clock=startClock(clockRules,0);auto bomb=makeBomb(1,2,0);auto viewer=makeSpectator(1,1);
    if(!match||!clock||!bomb||!viewer)return false;
    ZoneState zone;auto cart=makePayload(1);uint64_t scoreEvent=0;
    Occupants present;for(const aiverse::Vec3 body:{aiverse::Vec3{},aiverse::Vec3{Fixed::fromInt(1),{},{}}})if(insideZone(layout.zones[0],body))++present[1];
    auto captured=stepZone(zoneRules,zone,layout.zones[0].id,present,0);auto pushed=stepPayload(cartRules,*layout.payload,cart,1,0,0);
    auto timer=stepClock(clockRules,*clock,0,zone.contested||cart.contested);if(!captured||!pushed||!timer)return false;
    ObjectiveEvents outcomes=*captured;outcomes.insert(outcomes.end(),pushed->begin(),pushed->end());
    const ObjectiveScoring points{{ObjectiveEventKind::ZoneCaptured,5},{ObjectiveEventKind::ZoneHoldTick,1},{ObjectiveEventKind::PayloadCheckpoint,3},{ObjectiveEventKind::PayloadDelivered,10}};
    if(!stepMatch(matchConfig(settings),*match,0)||!awardObjectiveEvents(*match,points,outcomes,scoreEvent))return false;
    aiverse::Hasher hash;hashObjective(hash,zone);hashObjective(hash,cart);hashObjective(hash,*clock);
    state.values["genre.objective.hash"]=int64_t(hash.value()&0x7fffffffffffffffull);state.values["genre.objective.score_event"]=int64_t(scoreEvent);
    return storeBytes(state,"genre.objective.zone",save(zone))&&storeBytes(state,"genre.objective.payload",save(cart))&&storeBytes(state,"genre.objective.clock",save(*clock))
        &&storeBytes(state,"genre.objective.bomb",save(*bomb))&&storeBytes(state,"genre.objective.spectator",save(*viewer))&&storeBytes(state,"genre.objective.match",save(*match));
}
// Genre bots: validated settings, one tick of each policy against owner-built perception, bot memory in NativeState.
// A real authoritative handler passes sim.rng() and applies the returned inputs through the same stepMelee/
// stepDodge/stepGuard/stepLockOn/stepFeed path as player commands; bots never write combat or body state.
static bool genreBotConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::bots;using aiverse::Fixed;using aiverse::operator""_fx;namespace g=aiverse::lib::genre;
    if(!genreBotSettingsErrors(settings).empty())return false;
    auto melee=meleeBotConfig(settings);auto shooter=shooterBotConfig(settings);auto squad=squadConfig(settings);auto weights=objectiveWeights(settings);
    aiverse::Random rng(0x9e3779b9u);uint32_t tick=0;
    g::AttackSet attacks{{melee.opener,g::attackConfig(settings,melee.opener)}};
    PerceivedTarget foe;foe.id=2;foe.team=2;foe.position={{},{},Fixed::fromRatio(3,2)};
    MeleeBotState fighter;MeleeBotSelf self{1,1,{},{},{}};
    auto action=stepMeleeBot(melee,attacks,g::dodgeConfig(settings),g::guardConfig(settings),fighter,tick,self,std::span<const PerceivedTarget>(&foe,1),rng);
    auto feed=g::feedConfig(settings);ShooterBotState gunner;foe.position={{},Fixed::fromRatio(8,5),20_fx};
    ShooterBotSelf shooterSelf{1,1,{},{{},Fixed::fromRatio(8,5),{}},{},{},{},g::makeFeed(feed),30,1000};
    auto shot=stepShooterBot(shooter,feed,gunner,tick,shooterSelf,std::span<const PerceivedTarget>(&foe,1),{},rng);
    const std::array<ObjectiveInfo,2> points{{{1,ObjectiveKind::Capture,{{},{},30_fx},0},{2,ObjectiveKind::Hold,{10_fx,{},{}},1}}};
    auto goal=chooseObjective(weights,1,{},points);
    const std::array<SquadMember,3> members{{{11,{}},{12,{1_fx,{},{}}},{13,{2_fx,{},{}}}}};SquadState team;
    std::optional<SquadPlan> plan;
    if(goal&&goal->id)for(const auto& p:points)if(p.id==goal->id)plan=stepSquad(squad,team,tick,p.position,goal->stance,members);
    if(!action||!shot||!goal||!plan||plan->orders.size()!=members.size())return false;
    state.values["bots.genre.hash"]=int64_t((genreBotHash(fighter)^genreBotHash(gunner)^genreBotHash(team))&0x7fffffffffffffffull);
    state.values["bots.genre.objective"]=goal->id;
    return storeBytes(state,"bots.genre.melee",save(fighter))&&storeBytes(state,"bots.genre.shooter",save(gunner))&&storeBytes(state,"bots.genre.squad",save(team));
}
// Narrative scenes: settings-driven config, a tiny social scene (relationship-gated choice, reveal, deferred
// consequence, cue requests, one bark) for one authoritative tick, then the state in NativeState. The legacy story
// JSON loads through the same reader. Cinematic/UI owners consume report cues and bark lines; they never write state.
static bool narrativeSceneConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings,const aiverse::Json& story){
    using namespace aiverse::lib::genre;using C=NarrativeCommandKind;
    if(!narrativeSettingsErrors(settings).empty())return false;const auto config=narrativeConfig(settings);
    const auto legacy=readNarrativeWorld(story,config);
    const auto world=readNarrativeWorld(aiverse::Json::parse(R"({"facts":[{"id":1,"type":"bool","persistent":true}],
        "characters":[{"id":1,"nameKey":"character.guard"}],"consequences":[{"id":1,"effects":[{"meter":"trust","character":1,"add":-10}]}],
        "nodes":[{"id":1,"textKey":"story.gate.halt","speaker":1,"cues":[1],"choices":[
            {"id":1,"textKey":"story.gate.bribe","next":0,"conditions":[{"meter":"trust","character":1,"op":"ge","value":0}],
             "effects":[{"reveal":1,"to":[0,1]},{"schedule":1,"afterScenes":1}],"cues":[2]}]}],
        "scenes":[{"id":1,"entry":1}],"barks":[{"id":1,"trigger":1,"speaker":1,"textKey":"bark.guard.idle"}]})"),config);
    auto scene=beginNarrative(world,config);auto old=beginNarrative(legacy,config);if(!scene||!old)return false;
    NarrativeInput input{{{1,C::StartScene,1},{2,C::Choose,1}},{{1,1,{}}}};
    auto report=stepNarrative(world,config,*scene,0,input);
    if(!report||report->cues.size()!=2||report->barks.size()!=1||!knows(*scene,0,1)||scene->pending.size()!=1)return false;
    state.values["genre.narrative.world"]=int64_t(narrativeWorldHash(world)&0x7fffffffffffffffull);
    state.values["genre.narrative.config"]=int64_t(narrativeConfigHash(config)&0x7fffffffffffffffull);
    state.values["genre.narrative.cue"]=int64_t(report->cues.back().sequence);
    return storeBytes(state,"genre.narrative",save(*scene))&&storeBytes(state,"genre.narrative.legacy",save(*old));
}
// Physical interactions: validated `interaction` settings + interactable definitions, one authoritative tick of
// carry, door, platform and breakable policies against engine/world collision, states in NativeState. These
// kinematic policies are authoritative; a Box3D world may only render the DebrisRequest output.
static bool physicalInteractionConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;using aiverse::Fixed;using aiverse::Vec3;
    if(!interactionSettingsErrors(settings).empty())return false;
    const auto config=interactionConfig(settings);
    const auto defs=readInteractionDefinitions(aiverse::Json::parse(R"({"carryables":[{"id":1,"massMilli":6000,"halfMilli":[200,200,200]}],
        "doors":[{"id":2,"kind":"sliding","panelMin":[3000,0,-100],"panelMax":[5000,2000,100],"slideMilli":[2000,0,0],"openTicks":12,"closeTicks":12}],
        "platforms":[{"id":3,"halfMilli":[1000,250,1000],"speedMilli":1500,"waitTicks":20,"waypoints":[[0,0,8000],[0,4000,8000]]}],
        "breakables":[{"id":4,"health":50,"stages":[{"stage":1,"health":20,"debris":[{"kind":1,"count":2}]}],"brokenDebris":[{"kind":2,"count":4}]}]})"));
    aiverse::world::SpatialWorld world;if(!world.add({1,{{Fixed::fromInt(-20),Fixed::fromInt(-1),Fixed::fromInt(-20)},{Fixed::fromInt(20),Fixed{},Fixed::fromInt(20)}}}))return false;
    const WorldCollision collision(world);const uint32_t tick=0;const auto dt=Fixed::fromRatio(1,60);
    CarryState hands;CarryInput carry;CarrierInput carrier;carrier.carrier=10;carrier.pickUp=1;carry.carriers={carrier};carry.objects={{1,{{},Fixed::fromRatio(1,5),Fixed::fromInt(1)}}};
    DoorState doors;PlatformState platforms;BreakableState breakables;
    if(!addDoor(doors,defs.doors.at(2))||!addPlatform(platforms,defs.platforms.at(3))||!addBreakable(breakables,defs.breakables.at(4)))return false;
    const std::array<DoorCommand,1> open{{{10,2,DoorAction::Open,{},{},false}}};
    const std::array<RiderContact,1> riders{{{10,3}}};
    const std::array<DamageInput,1> hit{{{1,4,1,35,10,{}}}};
    auto held=stepCarry(config,defs.carryables,hands,tick,dt,carry,collision);auto door=stepDoors(config,defs.doors,doors,tick,open,collision);
    auto ride=stepPlatforms(config,defs.platforms,platforms,tick,dt,{},riders);auto broken=stepBreakables(defs.breakables,breakables,tick,hit);
    if(!held||!door||!ride||!broken||hands.held.size()!=1||ride->carry.size()!=1||broken->debris.size()!=1)return false;
    // ride->carry[0].delta goes onto the rider before its stepLocomotion move; debris requests go to presentation.
    aiverse::Hasher hash;hashInteraction(hash,hands);hashInteraction(hash,doors);hashInteraction(hash,platforms);hashInteraction(hash,breakables);
    state.values["genre.interaction.hash"]=int64_t(hash.value()&0x7fffffffffffffffull);
    state.values["genre.interaction.config"]=int64_t(interactionConfigHash(config)&0x7fffffffffffffffull);
    state.values["genre.interaction.defs"]=int64_t(interactionDefinitionsHash(defs)&0x7fffffffffffffffull);
    return storeBytes(state,"genre.interaction.carry",save(hands))&&storeBytes(state,"genre.interaction.doors",save(doors))
        &&storeBytes(state,"genre.interaction.platforms",save(platforms))&&storeBytes(state,"genre.interaction.breakables",save(breakables));
}
// Cinematic gameplay: validated settings + scene JSON, a narrative cue id that starts the scene, two authoritative
// ticks of time domains feeding the scene runner (hit-stop preset on the world domain), versioned state in NativeState.
// Presentation plays SceneEvents and reports line/marker progress back as tick-stamped SceneInput.
static bool libraryCinematicConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings){
    using namespace aiverse::lib::genre;
    if(!cinematicSettingsErrors(settings).empty())return false;
    auto lib=readSceneLibrary(aiverse::Json::parse(R"({"scenes":[{"id":1,"trigger":42,"length":120,"checkpointBefore":true,
        "participants":[{"id":1,"lockInput":true}],
        "cues":[{"id":1,"at":0,"kind":"dialogue","ref":100,"actor":1},{"id":2,"at":30,"kind":"event","ref":7}],
        "syncs":[{"id":1,"at":60,"timeout":240,"wait":[{"kind":"lineFinished","cue":1}]}],
        "qtes":[{"id":1,"at":90,"kind":"press","button":1,"close":45}]}]})"));
    TimeState time;SceneState scene;TimeInput timeIn;timeIn.add={hitStopRequest(settings)};
    SceneInput in;in.options=qteOptions(settings);in.commands={cueTrigger(1,42,{{1,1001}})}; // narrative CueRequest{cueId=42}
    for(uint32_t tick=0;tick<2;++tick){
        auto t=stepTime(lib.time,time,tick,timeIn);if(!t)return false;in.domainSteps=sceneDomainSteps(*t);
        auto r=stepScene(lib,scene,tick,in);if(!r)return false;
        timeIn={};timeIn.add=r->timeAdd;timeIn.cancelTags=r->timeCancelTags;in={};
    }
    if(scene.phase!=ScenePhase::Playing||!inputLocked(scene,1))return false;
    state.values["genre.cinematic_hash"]=int64_t((cinematicHash(scene)^cinematicHash(time))&0x7fffffffffffffffull);
    state.values["genre.cinematic.library"]=int64_t(sceneLibraryHash(lib)&0x7fffffffffffffffull);
    return storeBytes(state,"genre.cinematic.time",save(time))&&storeBytes(state,"genre.cinematic.scene",save(scene));
}
bool libraryPuzzlesConsumerExample(aiverse::game::NativeState& state);   // PuzzlesConsumer.cpp (puzzle-solvers)
// Cards + turn strategy (card-games, tactics-grid, CardBot/TacticsBot): CardsTurnsConsumer.cpp.
bool libraryCardsTurnsConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings);
// Vehicles + sports (vehicle-controllers, sports-rules): VehiclesSportsConsumer.cpp.
bool vehiclesSportsConsumerExample(aiverse::game::NativeState&,const aiverse::lib::gameplay::GameplaySettings&);
// settlement-production + survival-ecology: ProductionSurvivalConsumer.cpp.
bool libraryProductionSurvivalConsumerExample(aiverse::game::NativeState& state,const aiverse::lib::gameplay::GameplaySettings& settings);
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
    if(!storeBytes(state,"genre.story",save(*initial))||!storeBytes(state,"genre.motion",save(MotionState{}))||!storeBytes(state,"genre.locomotion",save(body)))return false;
    if(!libraryCombatConsumerExample(state,*parsed.value))return false;
    if(!libraryMatchObjectivesConsumerExample(state,*parsed.value))return false;
    if(!librarySensesConsumerExample(state,*parsed.value))return false;
    if(!genreBotConsumerExample(state,*parsed.value))return false;
    if(!narrativeSceneConsumerExample(state,*parsed.value,story))return false;
    if(!physicalInteractionConsumerExample(state,*parsed.value))return false;
    if(!libraryCinematicConsumerExample(state,*parsed.value))return false;
    if(!libraryPuzzlesConsumerExample(state))return false;
    if(!libraryCardsTurnsConsumerExample(state,*parsed.value))return false;
    if(!vehiclesSportsConsumerExample(state,*parsed.value))return false;
    if(!libraryProductionSurvivalConsumerExample(state,*parsed.value))return false;
    return weaponConsumerExample(state,*parsed.value);
}
