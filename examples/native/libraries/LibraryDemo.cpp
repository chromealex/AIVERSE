#include "modules/rts/RtsNative.h"
#include <aiverselib/Graphs.h>
#include <aiverselib/Behavior.h>
#include <aiverselib/Logistics.h>
#include <array>
#include <stdexcept>
#include <aiverselib/gameplay/Configured.h>
#include <aiverselib/gameplay/Tutorial.h>
#include <aiverselib/gameplay/NativeCommands.h>

bool libraryBotConsumerExample(aiverse::game::NativeState&);
bool libraryGenreConsumerExample(aiverse::game::NativeState&,const aiverse::Json&,const aiverse::Json&);
bool librarySystemsConsumerExample(aiverse::game::NativeState&);
bool librarySystemsTickExample(aiverse::Simulation&,aiverse::game::NativeState&);

namespace {
class LibraryDemo final : public aiverse::game::NativeModule {
public:
    void setup(aiverse::game::RtsModule&,aiverse::Simulation&,aiverse::game::NativeState& state,
               const aiverse::Json& config) const override {
        using namespace aiverse::lib::gameplay;
        auto document=config;
        if(document.size()==0) document=aiverse::Json::parse("{\"schemaVersion\":1}");
        auto parsed=readGameplaySettings(document);
        if(!parsed.value)throw std::runtime_error("library_demo: "+parsed.errors.front());
        const auto& options=*parsed.value;
        if(!librarySystemsConsumerExample(state))throw std::runtime_error("workshop initialization failed");
        auto story=aiverse::Json::parse(R"({"nodes":[{"id":1,"textKey":"demo.story","choices":[{"id":1,"textKey":"demo.finish","next":0}]}]})");
        if(!libraryBotConsumerExample(state)||!libraryGenreConsumerExample(state,document,story))throw std::runtime_error("genre initialization failed");
        state.values["cast_ticks"]=options.abilities.castTicks;
        state.values["cooldown_ticks"]=options.abilities.cooldownTicks;
        state.values["ready_tick"]=0;
        state.values["finish_tick"]=0;
        state.values["casting"]=0;
        state.values["casts_finished"]=0;
        state.values["tutorial_step"]=0;
        state.values["tutorial_progress"]=0;
        state.values["tutorial_complete"]=0;
        const auto paths=aiverse::lib::bfs({{1},{2},{}},0);
        state.values["route_length"]=static_cast<std::int64_t>(paths->distance[2]);
        state.values["phase"]=0;
        auto& stock=state.arrays["stock"]; stock={10,5};
        const std::array recipe{aiverse::lib::Ingredient{0,3},aiverse::lib::Ingredient{1,2}};
        state.values["recipe_paid"]=aiverse::lib::consume(stock,recipe) ? 1 : 0;
    }
    void tick(aiverse::game::RtsModule&,aiverse::Simulation& sim,aiverse::game::NativeState& state) const override {
        using namespace aiverse::lib::gameplay;
        if(!librarySystemsTickExample(sim,state))return;
        AbilityConfig ability;ability.target=TargetKind::None;
        ability.castTicks=state.values["cast_ticks"];ability.cooldownTicks=state.values["cooldown_ticks"];
        AbilityState cast{state.values["ready_tick"],state.values["finish_tick"],state.values["casting"]!=0};
        TutorialState tutorial{state.values["tutorial_step"],state.values["tutorial_progress"],state.values["tutorial_complete"]!=0};
        const std::array steps{TutorialStep{0,1,8,-1,0,true,{}}};
        if(completeAbility(cast,sim.tick())) {
            ++state.values["casts_finished"];
            tutorialEvent(steps,tutorial,1);
        }
        if(!cast.casting)beginAbility(ability,cast,{TargetKind::None,0,0,true,true},sim.tick());
        state.values["ready_tick"]=cast.readyTick;state.values["finish_tick"]=cast.finishTick;state.values["casting"]=cast.casting;
        state.values["tutorial_step"]=tutorial.step;state.values["tutorial_progress"]=tutorial.progress;state.values["tutorial_complete"]=tutorial.complete;
        if (sim.tick()%sim.tickRate()!=0) return;
        constexpr std::array transitions{aiverse::lib::Transition{0,1,1},aiverse::lib::Transition{1,1,0}};
        // The only persistent FSM state lives in NativeState, never the module instance.
        aiverse::lib::transition(state.values["phase"],transitions,1);
    }
};
}
std::unique_ptr<aiverse::game::NativeModule> createLibraryDemo() { return std::make_unique<LibraryDemo>(); }
