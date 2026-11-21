#include <aiverselib/gameplay/InputFlow.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <type_traits>
#include <string_view>
using namespace aiverse::lib::gameplay::inputflow;
#define CHECK(x) do { if (!(x)) { std::cerr << "InputFlow failed line " << __LINE__ << ": " #x "\n"; std::abort(); } } while(false)

static bool has(std::span<const ActionEvent> events, std::string_view action, Phase phase) {
    for (const auto& event : events) if (event.action == action && event.phase == phase) return true;
    return false;
}
static bool near(float a,float b) { return std::abs(a-b) < 0.001f; }

static_assert(std::is_same_v<InputFlow,aiverse::lib::gameplay::enhanced::InputSystem>,
              "InputFlow rename must preserve the Enhanced InputSystem alias");

int main() {
    const Control w{Device::Keyboard,1}, a{Device::Keyboard,2}, s{Device::Keyboard,3}, d{Device::Keyboard,4};
    InputFlow inputs;
    CHECK(inputs.defineAction({"Move",ValueType::Axis2D,{Modifier::normalize()}}));
    CHECK(inputs.defineAction({"Fire",ValueType::Boolean}));
    CHECK(inputs.actionDefinition("Fire") &&
          inputs.actionDefinition("Fire")->type==ValueType::Boolean);
    CHECK(inputs.actionDefinition("Missing")==nullptr);
    MappingContext gameplay{"Gameplay"};
    gameplay.bind("Move",w,{Modifier::swapXY()});
    gameplay.bind("Move",s,{Modifier::swapXY(),Modifier::negate()});
    gameplay.bind("Move",a,{Modifier::negate()});
    gameplay.bind("Move",d);
    gameplay.bind("Fire",{Device::MouseButton,0},{},{Trigger::pressed()});
    CHECK(inputs.registerContext(gameplay));
    CHECK(inputs.activateContext("Gameplay",0));
    CHECK(inputs.registeredContexts()==std::vector<std::string>({"Gameplay"}));
    inputs.setControl(w,1);
    CHECK(has(inputs.update(0.016f),"Move",Phase::Started));
    CHECK(near(inputs.state("Move")->value.y,1));
    inputs.setControl(d,1);
    CHECK(has(inputs.update(0.016f),"Move",Phase::Triggered));
    CHECK(near(inputs.state("Move")->value.x,0.707107f));
    CHECK(near(inputs.state("Move")->value.y,0.707107f));
    inputs.setControl(w,0); inputs.setControl(d,0);
    CHECK(has(inputs.update(0.016f),"Move",Phase::Completed));
    CHECK(!inputs.state("Move")->active);
    inputs.setControl({Device::MouseButton,0},1);
    CHECK(has(inputs.update(0.016f),"Fire",Phase::Triggered));
    CHECK(has(inputs.update(0.016f),"Fire",Phase::Ongoing));
    inputs.setControl({Device::MouseButton,0},0);
    CHECK(has(inputs.update(0.016f),"Fire",Phase::Completed));
    CHECK(inputs.rebind("Gameplay",5,{Device::Keyboard,99})); // fifth binding is Fire
    inputs.setControl({Device::MouseButton,0},1);
    CHECK(!has(inputs.update(0.016f),"Fire",Phase::Triggered));
    inputs.setControl({Device::Keyboard,99},1);
    CHECK(has(inputs.update(0.016f),"Fire",Phase::Triggered));
    inputs.cancelAll();
    CHECK(has(inputs.events(),"Fire",Phase::Canceled));
    CHECK(inputs.state("Fire")==nullptr);

    InputFlow timing;
    CHECK(timing.defineAction({"Charge",ValueType::Boolean}));
    CHECK(timing.defineAction({"Tap",ValueType::Boolean}));
    CHECK(timing.defineAction({"Release",ValueType::Boolean}));
    MappingContext timed{"Gameplay"};
    timed.bind("Charge",{Device::Keyboard,10},{},{Trigger::hold(0.2f)});
    timed.bind("Tap",{Device::Keyboard,11},{},{Trigger::tap(0.25f)});
    timed.bind("Release",{Device::Keyboard,12},{},{Trigger::released()});
    CHECK(timing.registerContext(timed));
    CHECK(timing.activateContext("Gameplay"));
    timing.setControl({Device::Keyboard,10},1);
    CHECK(has(timing.update(0.1f),"Charge",Phase::Ongoing));
    CHECK(!has(timing.events(),"Charge",Phase::Triggered));
    CHECK(has(timing.update(0.11f),"Charge",Phase::Triggered));
    CHECK(!has(timing.update(0.1f),"Charge",Phase::Triggered)); // hold is one-shot
    timing.setControl({Device::Keyboard,10},0);
    CHECK(has(timing.update(0.016f),"Charge",Phase::Completed));
    timing.setControl({Device::Keyboard,11},1);
    CHECK(has(timing.update(0.1f),"Tap",Phase::Started));
    timing.setControl({Device::Keyboard,11},0);
    CHECK(has(timing.update(0.01f),"Tap",Phase::Triggered));
    CHECK(has(timing.events(),"Tap",Phase::Completed));
    timing.setControl({Device::Keyboard,11},1);
    timing.setControl({Device::Keyboard,11},0); // tap entirely between updates
    CHECK(has(timing.update(0.01f),"Tap",Phase::Triggered));
    timing.setControl({Device::Keyboard,12},1);
    timing.update(0.01f);
    timing.setControl({Device::Keyboard,12},0);
    CHECK(has(timing.update(0.01f),"Release",Phase::Triggered));

    InputFlow contexts;
    CHECK(contexts.defineAction({"GameplayFire",ValueType::Boolean}));
    CHECK(contexts.defineAction({"MenuAccept",ValueType::Boolean}));
    MappingContext game{"Game"}, menu{"Menu"};
    const Control space{Device::Keyboard,32};
    game.bind("GameplayFire",space);
    menu.bind("MenuAccept",space);
    CHECK(contexts.registerContext(game)); CHECK(contexts.registerContext(menu));
    CHECK(contexts.activateContext("Game",0));
    contexts.setControl(space,1);
    CHECK(has(contexts.update(0.016f),"GameplayFire",Phase::Triggered));
    CHECK(contexts.activateContext("Menu",100));
    CHECK(has(contexts.update(0.016f),"MenuAccept",Phase::Triggered));
    CHECK(!contexts.state("GameplayFire")->active);
    CHECK(has(contexts.events(),"GameplayFire",Phase::Canceled)); // consumed by modal
    contexts.deactivateContext("Menu");
    CHECK(has(contexts.update(0.016f),"GameplayFire",Phase::Started));

    InputFlow chord;
    CHECK(chord.defineAction({"Aim",ValueType::Boolean}));
    CHECK(chord.defineAction({"AltFire",ValueType::Boolean}));
    MappingContext chordContext{"Combat"};
    chordContext.bind("Aim",{Device::MouseButton,1});
    chordContext.bind("AltFire",{Device::Keyboard,70},{},{Trigger::pressed(),Trigger::chord("Aim")});
    CHECK(chord.registerContext(chordContext));
    CHECK(chord.activateContext("Combat"));
    chord.setControl({Device::MouseButton,1},1);
    chord.setControl({Device::Keyboard,70},1);
    CHECK(has(chord.update(0.016f),"AltFire",Phase::Triggered)); // same-frame chord
    chord.setControl({Device::Keyboard,70},0);
    chord.update(0.016f);
    chord.setControl({Device::MouseButton,1},0);
    chord.update(0.016f);
    chord.setControl({Device::Keyboard,70},1);
    CHECK(!has(chord.update(0.016f),"AltFire",Phase::Triggered));

    InputFlow analog;
    CHECK(analog.defineAction({"Steer",ValueType::Axis1D}));
    CHECK(analog.defineAction({"MouseX",ValueType::Axis1D}));
    MappingContext vehicle{"Vehicle"};
    vehicle.bind("Steer",{Device::GamepadAxis,0,1},{Modifier::deadZone(0.2f)});
    vehicle.bind("MouseX",{Device::MouseAxis,0},{Modifier::scale(0.1f)});
    CHECK(analog.registerContext(vehicle));
    CHECK(analog.activateContext("Vehicle"));
    analog.setControl({Device::GamepadAxis,0,1},0.1f);
    analog.update(0.016f);
    CHECK(!analog.state("Steer")->active);
    analog.setControl({Device::GamepadAxis,0,1},0.6f);
    analog.addDelta({Device::MouseAxis,0},3);
    CHECK(has(analog.update(0.016f),"Steer",Phase::Triggered));
    CHECK(near(analog.state("Steer")->value.x,0.5f));
    CHECK(near(analog.state("MouseX")->value.x,0.3f));
    CHECK(has(analog.update(0.016f),"MouseX",Phase::Completed));

    // Raw C++ context registrations must reject bad actions/duplicate explicit IDs;
    // auto IDs must not collide with authored IDs.
    InputFlow registrations;
    CHECK(registrations.defineAction({"Use",ValueType::Boolean}));
    MappingContext repeated{"Repeated"};
    repeated.bind("Use",{Device::Keyboard,90});
    repeated.bind("Use",{Device::Keyboard,91});
    repeated.mappings[0].id = 42;
    repeated.mappings[1].id = 42;
    CHECK(!registrations.registerContext(repeated));
    CHECK(registrations.context("Repeated") == nullptr);
    MappingContext badAction{"BadAction"};
    badAction.bind("Missing",{Device::Keyboard,90});
    CHECK(!registrations.registerContext(badAction));
    MappingContext assigned{"Assigned"};
    assigned.bind("Use",{Device::Keyboard,90});
    assigned.bind("Use",{Device::Keyboard,91});
    assigned.mappings[0].id = 1;
    CHECK(registrations.registerContext(assigned));
    const auto* registered = registrations.context("Assigned");
    CHECK(registered && registered->mappings[0].id == 1 &&
          registered->mappings[1].id == 2);

    // Action modifiers must gate lifecycle as well as the numeric value.
    InputFlow deadzoneActions;
    CHECK(deadzoneActions.defineAction({"Throttle",ValueType::Axis1D,{Modifier::deadZone(0.3f)}}));
    CHECK(deadzoneActions.defineAction({"Disabled",ValueType::Axis1D,{Modifier::scale(0)}}));
    MappingContext deadzoneContext{"Deadzone"};
    deadzoneContext.bind("Throttle",{Device::GamepadAxis,2});
    deadzoneContext.bind("Disabled",{Device::GamepadAxis,3});
    CHECK(deadzoneActions.registerContext(deadzoneContext));
    CHECK(deadzoneActions.activateContext("Deadzone"));
    deadzoneActions.setControl({Device::GamepadAxis,2},0.1f);
    deadzoneActions.setControl({Device::GamepadAxis,3},1.0f);
    CHECK(!has(deadzoneActions.update(0.016f),"Throttle",Phase::Triggered));
    CHECK(!deadzoneActions.state("Throttle")->active);
    CHECK(!has(deadzoneActions.events(),"Disabled",Phase::Started));
    CHECK(!deadzoneActions.state("Disabled")->active);
    deadzoneActions.setControl({Device::GamepadAxis,2},0.6f);
    CHECK(has(deadzoneActions.update(0.016f),"Throttle",Phase::Started));
    CHECK(deadzoneActions.state("Throttle")->active);
    deadzoneActions.setControl({Device::GamepadAxis,2},0.1f);
    CHECK(has(deadzoneActions.update(0.016f),"Throttle",Phase::Completed));
    CHECK(!deadzoneActions.state("Throttle")->active);

    // Same-context collisions can be rejected, swapped or accepted explicitly.
    InputFlow collisions;
    CHECK(collisions.defineAction({"Walk",ValueType::Boolean}));
    CHECK(collisions.defineAction({"Interact",ValueType::Boolean}));
    MappingContext player{"Player"};
    player.bind("Walk",{Device::Keyboard,11},{},{},true);
    player.bind("Interact",{Device::Keyboard,12},{},{},false);
    CHECK(collisions.registerContext(player));
    CHECK(collisions.activateContext("Player",0));
    CHECK(!collisions.rebind("Player",2,{Device::Keyboard,11},RebindPolicy::Reject));
    CHECK(collisions.context("Player")->mappings[1].control.code==12);
    CHECK(collisions.rebind("Player",2,{Device::Keyboard,11},RebindPolicy::Swap));
    CHECK(collisions.context("Player")->mappings[0].control.code==12);
    CHECK(collisions.context("Player")->mappings[1].control.code==11);
    CHECK(collisions.rebind("Player",2,{Device::Keyboard,12},RebindPolicy::Allow));
    const auto conflicts=collisions.conflicts();
    CHECK(conflicts.size()==1);
    CHECK(conflicts[0].higherContext=="Player");
    CHECK(conflicts[0].shadowed);

    // Advanced trigger timings + inverted chord gate. Assertions are executed in CI later.
    InputFlow advanced;
    for(const char* name:{"Double","LongRelease","Repeat","Guard","Aim"})
        CHECK(advanced.defineAction({name,ValueType::Boolean}));
    MappingContext combat{"Advanced"};
    combat.bind("Double",{Device::Keyboard,60},{},{Trigger::doubleTap(0.25f)});
    combat.bind("LongRelease",{Device::Keyboard,61},{},{Trigger::holdAndRelease(0.15f)});
    combat.bind("Repeat",{Device::Keyboard,62},{},{Trigger::pulse(0.1f)});
    combat.bind("Aim",{Device::Keyboard,63});
    combat.bind("Guard",{Device::Keyboard,64},{},{Trigger::pressed(),Trigger::notChord("Aim")});
    CHECK(advanced.registerContext(combat));
    CHECK(advanced.activateContext("Advanced"));
    advanced.setControl({Device::Keyboard,60},1);
    CHECK(!has(advanced.update(0.01f),"Double",Phase::Triggered));
    advanced.setControl({Device::Keyboard,60},0);
    advanced.update(0.01f);
    advanced.setControl({Device::Keyboard,60},1);
    CHECK(has(advanced.update(0.1f),"Double",Phase::Triggered));
    advanced.setControl({Device::Keyboard,61},1);
    advanced.update(0.2f);
    advanced.setControl({Device::Keyboard,61},0);
    CHECK(has(advanced.update(0.01f),"LongRelease",Phase::Triggered));
    advanced.setControl({Device::Keyboard,62},1);
    CHECK(!has(advanced.update(0.05f),"Repeat",Phase::Triggered));
    CHECK(has(advanced.update(0.06f),"Repeat",Phase::Triggered));
    CHECK(!has(advanced.update(0.02f),"Repeat",Phase::Triggered));
    CHECK(has(advanced.update(0.09f),"Repeat",Phase::Triggered));
    advanced.setControl({Device::Keyboard,63},1);
    advanced.setControl({Device::Keyboard,64},1);
    CHECK(!has(advanced.update(0.01f),"Guard",Phase::Triggered));
    advanced.setControl({Device::Keyboard,64},0);
    advanced.setControl({Device::Keyboard,63},0);
    advanced.update(0.01f);
    advanced.setControl({Device::Keyboard,64},1);
    CHECK(has(advanced.update(0.01f),"Guard",Phase::Triggered));

    // A chord may only be satisfied by an actually reachable Action. An
    // input consumed by a higher-priority UI context cannot serve as Aim.
    InputFlow layered;
    CHECK(layered.defineAction({"Aim",ValueType::Boolean}));
    CHECK(layered.defineAction({"AltFire",ValueType::Boolean}));
    CHECK(layered.defineAction({"MenuAccept",ValueType::Boolean}));
    MappingContext lower{"LowerGame"},higher{"HigherMenu"};
    lower.bind("Aim",{Device::Keyboard,83});
    lower.bind("AltFire",{Device::Keyboard,84},{},
               {Trigger::pressed(),Trigger::chord("Aim")});
    higher.bind("MenuAccept",{Device::Keyboard,83});
    CHECK(layered.registerContext(lower));
    CHECK(layered.registerContext(higher));
    CHECK(layered.activateContext("LowerGame",0));
    CHECK(layered.activateContext("HigherMenu",100));
    layered.setControl({Device::Keyboard,83},1);
    layered.setControl({Device::Keyboard,84},1);
    CHECK(!has(layered.update(0.016f),"AltFire",Phase::Triggered));
    CHECK(!layered.state("Aim")->active);

    // Chord providers also respect modifiers applied to the final Action.
    InputFlow gated;
    CHECK(gated.defineAction({"Aim",ValueType::Axis1D,
                              {Modifier::deadZone(0.5f)}}));
    CHECK(gated.defineAction({"AltFire",ValueType::Boolean}));
    MappingContext gates{"Gates"};
    gates.bind("Aim",{Device::GamepadAxis,0});
    gates.bind("AltFire",{Device::Keyboard,84},{},
               {Trigger::pressed(),Trigger::chord("Aim")});
    CHECK(gated.registerContext(gates));
    CHECK(gated.activateContext("Gates"));
    gated.setControl({Device::GamepadAxis,0},0.2f);
    gated.setControl({Device::Keyboard,84},1);
    CHECK(!has(gated.update(0.016f),"AltFire",Phase::Triggered));
    gated.setControl({Device::Keyboard,84},0);
    gated.update(0.016f);
    gated.setControl({Device::GamepadAxis,0},0.8f);
    gated.setControl({Device::Keyboard,84},1);
    CHECK(has(gated.update(0.016f),"AltFire",Phase::Triggered));

    // A recursive chord dependency must follow the provider's own mapping and
    // Action gates, not just the provider's raw physical key level.
    InputFlow chained;
    CHECK(chained.defineAction({"Shift",ValueType::Boolean}));
    CHECK(chained.defineAction({"Stance",ValueType::Boolean,{}, {Trigger::chord("Shift")}}));
    CHECK(chained.defineAction({"Finisher",ValueType::Boolean}));
    MappingContext chainCtx{"Chain"};
    chainCtx.bind("Shift",{Device::Keyboard,110});
    chainCtx.bind("Stance",{Device::Keyboard,111});
    chainCtx.bind("Finisher",{Device::Keyboard,112},{},
                  {Trigger::pressed(),Trigger::chord("Stance")});
    CHECK(chained.registerContext(chainCtx));
    CHECK(chained.activateContext("Chain"));
    chained.setControl({Device::Keyboard,111},1);
    chained.setControl({Device::Keyboard,112},1);
    CHECK(!has(chained.update(0.016f),"Finisher",Phase::Triggered));
    chained.setControl({Device::Keyboard,112},0);
    chained.update(0.016f);
    chained.setControl({Device::Keyboard,110},1);
    chained.setControl({Device::Keyboard,112},1);
    CHECK(has(chained.update(0.016f),"Finisher",Phase::Triggered));

    // A high-priority binding whose mapping Chord is false must not reserve
    // its physical key before a lower-priority provider and its dependent
    // action are evaluated. When the gate is active, it must consume the key.
    InputFlow conditional;
    for (const char* name:{"Modal","MenuAction","Move","FollowUp"})
        CHECK(conditional.defineAction({name,ValueType::Boolean}));
    MappingContext ui{"UI"}, gameCtx{"Game"};
    ui.bind("Modal",{Device::Keyboard,130});
    ui.bind("MenuAction",{Device::Keyboard,131},{},{Trigger::chord("Modal")});
    gameCtx.bind("Move",{Device::Keyboard,131});
    gameCtx.bind("FollowUp",{Device::Keyboard,132},{},
                 {Trigger::pressed(),Trigger::chord("Move")});
    CHECK(conditional.registerContext(ui));
    CHECK(conditional.registerContext(gameCtx));
    CHECK(conditional.activateContext("UI",100));
    CHECK(conditional.activateContext("Game",0));
    conditional.setControl({Device::Keyboard,131},1);
    conditional.setControl({Device::Keyboard,132},1);
    CHECK(has(conditional.update(0.016f),"FollowUp",Phase::Triggered));
    CHECK(conditional.state("Move")->active);
    CHECK(!conditional.state("MenuAction")->active);
    conditional.setControl({Device::Keyboard,131},0);
    conditional.setControl({Device::Keyboard,132},0);
    conditional.update(0.016f);
    conditional.setControl({Device::Keyboard,130},1);
    conditional.setControl({Device::Keyboard,131},1);
    conditional.setControl({Device::Keyboard,132},1);
    CHECK(!has(conditional.update(0.016f),"FollowUp",Phase::Triggered));
    CHECK(conditional.state("MenuAction")->active);
    CHECK(!conditional.state("Move")->active);

    // Cycles involving positive or negative chords must be rejected before
    // mutating live native Action or Context definitions.
    InputFlow cyclic;
    CHECK(cyclic.defineAction({"A",ValueType::Boolean,{}, {Trigger::chord("B")}}));
    CHECK(!cyclic.defineAction({"B",ValueType::Boolean,{}, {Trigger::notChord("A")}}));
    CHECK(cyclic.defineAction({"B",ValueType::Boolean}));
    MappingContext cyclicCtx{"Cycle"};
    cyclicCtx.bind("A",{Device::Keyboard,120});
    cyclicCtx.bind("B",{Device::Keyboard,121},{},{Trigger::chord("A")});
    CHECK(!cyclic.registerContext(cyclicCtx));
    CHECK(cyclic.context("Cycle")==nullptr);
    CHECK(cyclic.registeredContexts().empty());

    // Stage 49: chord interruption pauses the effective action and resets
    // Hold accumulation even when the physical button never went up.
    InputFlow gatedTimers;
    CHECK(gatedTimers.defineAction({"Modifier",ValueType::Boolean}));
    CHECK(gatedTimers.defineAction({"Charge",ValueType::Boolean}));
    MappingContext chargeCtx{"Charging"};
    chargeCtx.bind("Modifier",{Device::Keyboard,140});
    chargeCtx.bind("Charge",{Device::Keyboard,141},{},
                   {Trigger::hold(0.2f),Trigger::chord("Modifier")});
    CHECK(gatedTimers.registerContext(chargeCtx));
    CHECK(gatedTimers.activateContext("Charging"));
    gatedTimers.setControl({Device::Keyboard,141},1);
    CHECK(!has(gatedTimers.update(0.15f),"Charge",Phase::Triggered));
    CHECK(!gatedTimers.state("Charge")->active);
    gatedTimers.setControl({Device::Keyboard,140},1);
    CHECK(!has(gatedTimers.update(0.1f),"Charge",Phase::Triggered));
    CHECK(has(gatedTimers.update(0.11f),"Charge",Phase::Triggered));
    gatedTimers.setControl({Device::Keyboard,140},0);
    CHECK(has(gatedTimers.update(0.1f),"Charge",Phase::Canceled));
    CHECK(!gatedTimers.state("Charge")->active);
    gatedTimers.setControl({Device::Keyboard,140},1);
    CHECK(!has(gatedTimers.update(0.1f),"Charge",Phase::Triggered));
    CHECK(has(gatedTimers.update(0.11f),"Charge",Phase::Triggered));
    CHECK(!has(gatedTimers.update(0.2f),"Charge",Phase::Triggered));

    // An Action-level chord must govern its ACTIVE state and cancel promptly
    // if its provider disappears, independently of the mapping's raw value.
    InputFlow actionGates;
    CHECK(actionGates.defineAction({"Modifier",ValueType::Boolean}));
    CHECK(actionGates.defineAction({"Combo",ValueType::Boolean,{},
                                    {Trigger::chord("Modifier")}}));
    MappingContext actionCtx{"ActionGates"};
    actionCtx.bind("Modifier",{Device::Keyboard,142});
    actionCtx.bind("Combo",{Device::Keyboard,143});
    CHECK(actionGates.registerContext(actionCtx));
    CHECK(actionGates.activateContext("ActionGates"));
    actionGates.setControl({Device::Keyboard,143},1);
    actionGates.update(0.016f);
    CHECK(!actionGates.state("Combo")->active);
    actionGates.setControl({Device::Keyboard,142},1);
    CHECK(has(actionGates.update(0.016f),"Combo",Phase::Started));
    CHECK(actionGates.state("Combo")->active);
    actionGates.setControl({Device::Keyboard,142},0);
    CHECK(has(actionGates.update(0.016f),"Combo",Phase::Canceled));
    CHECK(!actionGates.state("Combo")->active);

    // Re-activating a previously disabled context while a key remains held
    // must not synthesize a new Pressed edge.
    InputFlow restored;
    CHECK(restored.defineAction({"Use",ValueType::Boolean}));
    MappingContext restoreCtx{"Restore"};
    restoreCtx.bind("Use",{Device::Keyboard,144},{},{Trigger::pressed()});
    CHECK(restored.registerContext(restoreCtx));
    CHECK(restored.activateContext("Restore"));
    restored.setControl({Device::Keyboard,144},1);
    CHECK(has(restored.update(0.016f),"Use",Phase::Triggered));
    restored.deactivateContext("Restore");
    restored.update(0.016f);
    CHECK(restored.activateContext("Restore"));
    CHECK(!has(restored.update(0.016f),"Use",Phase::Triggered));
    restored.setControl({Device::Keyboard,144},0);
    restored.update(0.016f);
    restored.setControl({Device::Keyboard,144},1);
    CHECK(has(restored.update(0.016f),"Use",Phase::Triggered));

    // Stage 51: a KeyDown buffered BEFORE context activation is not a new
    // Pressed/DoubleTap/Hold, even when it is still in raw.pressed this frame.
    InputFlow lateContext;
    CHECK(lateContext.defineAction({"MappingPressed",ValueType::Boolean}));
    CHECK(lateContext.defineAction({"ActionPressed",ValueType::Boolean,{},
                                    {Trigger::pressed()}}));
    CHECK(lateContext.defineAction({"ActionHold",ValueType::Boolean,{},
                                    {Trigger::hold(0.1f)}}));
    CHECK(lateContext.defineAction({"Continuous",ValueType::Boolean}));
    MappingContext late{"Late"};
    late.bind("MappingPressed",{Device::Keyboard,150},{},{Trigger::pressed()});
    late.bind("ActionPressed",{Device::Keyboard,151});
    late.bind("ActionHold",{Device::Keyboard,152});
    late.bind("Continuous",{Device::Keyboard,153});
    CHECK(lateContext.registerContext(late));
    for (int code : {150,151,152,153})
        lateContext.setControl({Device::Keyboard,code},1);
    CHECK(lateContext.activateContext("Late"));
    CHECK(!has(lateContext.update(0.2f),"MappingPressed",Phase::Triggered));
    CHECK(!has(lateContext.events(),"ActionPressed",Phase::Triggered));
    CHECK(!has(lateContext.events(),"ActionHold",Phase::Triggered));
    CHECK(has(lateContext.events(),"Continuous",Phase::Triggered));
    for (int code : {150,151,152,153})
        lateContext.setControl({Device::Keyboard,code},0);
    lateContext.update(0.016f);
    for (int code : {150,151,152,153})
        lateContext.setControl({Device::Keyboard,code},1);
    CHECK(has(lateContext.update(0.05f),"MappingPressed",Phase::Triggered));
    CHECK(has(lateContext.events(),"ActionPressed",Phase::Triggered));
    CHECK(!has(lateContext.events(),"ActionHold",Phase::Triggered));
    CHECK(has(lateContext.update(0.06f),"ActionHold",Phase::Triggered));

    // Stage 54: Action-level Chord gating participates in priority
    // consumption, not merely ActionState.active after the lower context
    // has already lost access to the physical button.
    InputFlow actionPriority;
    CHECK(actionPriority.defineAction({"Gate",ValueType::Boolean}));
    CHECK(actionPriority.defineAction({"High",ValueType::Boolean,{},
                                       {Trigger::chord("Gate")}}));
    CHECK(actionPriority.defineAction({"Low",ValueType::Boolean}));
    MappingContext priorityHigh{"HighContext"},priorityLow{"LowContext"};
    priorityHigh.bind("Gate",{Device::Keyboard,160});
    priorityHigh.bind("High",{Device::MouseButton,0});
    priorityLow.bind("Low",{Device::MouseButton,0});
    CHECK(actionPriority.registerContext(priorityHigh));
    CHECK(actionPriority.registerContext(priorityLow));
    CHECK(actionPriority.activateContext("HighContext",100));
    CHECK(actionPriority.activateContext("LowContext",0));
    actionPriority.setControl({Device::MouseButton,0},1);
    CHECK(has(actionPriority.update(0.016f),"Low",Phase::Triggered));
    CHECK(!actionPriority.state("High")->active);
    actionPriority.setControl({Device::Keyboard,160},1);
    CHECK(has(actionPriority.update(0.016f),"High",Phase::Triggered));
    CHECK(!actionPriority.state("Low")->active);
    actionPriority.setControl({Device::Keyboard,160},0);
    CHECK(has(actionPriority.update(0.016f),"Low",Phase::Triggered));
    CHECK(!actionPriority.state("High")->active);

    // Long acyclic Action prerequisite chains must not depend on declaration
    // order or an arbitrary fixed depth of eight actions.
    InputFlow deep;
    MappingContext deepCtx{"Deep"};
    for (int index=0;index<12;++index) {
        Action item;
        item.name="Node"+std::to_string(index);
        if(index<11)item.triggers.push_back(Trigger::chord("Node"+std::to_string(index+1)));
        CHECK(deep.defineAction(item));
        deepCtx.bind(item.name,{Device::Keyboard,180+index});
    }
    CHECK(deep.registerContext(deepCtx));
    CHECK(deep.activateContext("Deep"));
    for(int index=0;index<12;++index)
        deep.setControl({Device::Keyboard,180+index},1);
    CHECK(has(deep.update(0.016f),"Node0",Phase::Triggered));
    CHECK(deep.state("Node0")->active);

    // Review fix 2026-11-21: action-level Tap / Released / HoldAndRelease decide on the
    // release frame even when the mapping itself has no triggers.
    {
        InputFlow level;
        CHECK(level.defineAction({"Tap",ValueType::Boolean,{},{Trigger::tap(0.3f)}}));
        CHECK(level.defineAction({"Up",ValueType::Boolean,{},{Trigger::released()}}));
        CHECK(level.defineAction({"Charge",ValueType::Boolean,{},{Trigger::holdAndRelease(0.2f)}}));
        MappingContext ctx{"Level"};
        ctx.bind("Tap",{Device::Keyboard,200});
        ctx.bind("Up",{Device::Keyboard,201});
        ctx.bind("Charge",{Device::Keyboard,202});
        CHECK(level.registerContext(ctx));
        CHECK(level.activateContext("Level"));
        level.setControl({Device::Keyboard,200},1);
        CHECK(!has(level.update(0.05f),"Tap",Phase::Triggered));
        level.setControl({Device::Keyboard,200},0);
        CHECK(has(level.update(0.05f),"Tap",Phase::Triggered));
        level.setControl({Device::Keyboard,200},1);
        level.update(0.2f); level.update(0.2f);
        level.setControl({Device::Keyboard,200},0);
        CHECK(!has(level.update(0.01f),"Tap",Phase::Triggered));   // held too long
        level.setControl({Device::Keyboard,201},1);
        CHECK(!has(level.update(0.05f),"Up",Phase::Triggered));
        level.setControl({Device::Keyboard,201},0);
        CHECK(has(level.update(0.05f),"Up",Phase::Triggered));
        level.setControl({Device::Keyboard,202},1);
        level.update(0.15f); level.update(0.15f);
        level.setControl({Device::Keyboard,202},0);
        CHECK(has(level.update(0.01f),"Charge",Phase::Triggered));
    }
    // DoubleTap: a triple tap fires once, and a long first press does not arm it.
    {
        InputFlow dbl;
        CHECK(dbl.defineAction({"Double",ValueType::Boolean}));
        MappingContext ctx{"Dbl"};
        ctx.bind("Double",{Device::Keyboard,210},{},{Trigger::doubleTap(0.25f)});
        CHECK(dbl.registerContext(ctx));
        CHECK(dbl.activateContext("Dbl"));
        const Control k{Device::Keyboard,210};
        int fired=0;
        for (int tap=0; tap<3; ++tap) {
            dbl.setControl(k,1); if (has(dbl.update(0.03f),"Double",Phase::Triggered)) ++fired;
            dbl.setControl(k,0); if (has(dbl.update(0.03f),"Double",Phase::Triggered)) ++fired;
        }
        CHECK(fired==1);
        InputFlow slow;
        CHECK(slow.defineAction({"Double",ValueType::Boolean}));
        CHECK(slow.registerContext(ctx));
        CHECK(slow.activateContext("Dbl"));
        slow.setControl(k,1); slow.update(0.5f); slow.update(0.5f);   // long hold, not a tap
        slow.setControl(k,0); slow.update(0.03f);
        slow.setControl(k,1);
        CHECK(!has(slow.update(0.03f),"Double",Phase::Triggered));
    }
    // cancelAll(): Canceled phases reach a consumer that only reads update().
    {
        InputFlow cancel;
        CHECK(cancel.defineAction({"Hold",ValueType::Boolean}));
        MappingContext ctx{"C"};
        ctx.bind("Hold",{Device::Keyboard,220});
        CHECK(cancel.registerContext(ctx));
        CHECK(cancel.activateContext("C"));
        cancel.setControl({Device::Keyboard,220},1);
        CHECK(has(cancel.update(0.016f),"Hold",Phase::Started));
        cancel.cancelAll();
        CHECK(has(cancel.update(0.016f),"Hold",Phase::Canceled));
        CHECK(!has(cancel.update(0.016f),"Hold",Phase::Canceled));
        cancel.setControl({Device::Keyboard,220},1);
        cancel.update(0.016f);
        cancel.cancelAll();
        CHECK(has(cancel.events(),"Hold",Phase::Canceled));        // read right away
        CHECK(!has(cancel.update(0.016f),"Hold",Phase::Canceled)); // not delivered twice
    }

    std::cout << "Enhanced Input action/context/trigger/modifier scenarios passed\n";
}
