// AIVERSE Enhanced Input: presentation-side action mapping, independent of game genres.
// See docs/enhanced-input-agent-handoff.md. No simulation or platform dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::lib::gameplay::enhanced {

enum class ValueType : std::uint8_t { Boolean, Axis1D, Axis2D, Axis3D };
enum class Device : std::uint8_t {
    Keyboard, MouseButton, MouseAxis, MouseWheel, GamepadButton, GamepadAxis, Touch
};

// Code is the platform::Key, mouse button/axis or platform::PadButton/PadAxis ordinal.
// Device index is normally 0; 0..3 identify gamepads.
struct Control {
    Device device = Device::Keyboard;
    int code = 0;
    int deviceIndex = 0;
    auto operator<=>(const Control&) const = default;
};

struct Value {
    float x = 0, y = 0, z = 0;
    float length() const { return std::sqrt(x*x + y*y + z*z); }
    bool actuated(float epsilon = 0.00001f) const { return length() > epsilon; }
    Value& operator+=(const Value& other) {
        x += other.x; y += other.y; z += other.z; return *this;
    }
};

enum class ModifierKind : std::uint8_t { Scale, Negate, DeadZone, Swizzle, Normalize };
struct Modifier {
    ModifierKind kind = ModifierKind::Scale;
    Value scalar{1, 1, 1};
    float threshold = 0.1f;
    std::array<int, 3> swizzle{0, 1, 2};

    static Modifier scale(float x, float y = 1, float z = 1) {
        Modifier m; m.scalar = {x,y,z}; return m;
    }
    static Modifier negate() { Modifier m; m.kind = ModifierKind::Negate; return m; }
    static Modifier deadZone(float threshold) {
        Modifier m; m.kind = ModifierKind::DeadZone; m.threshold = threshold; return m;
    }
    // For a 1D key feeding Axis2D Y: swizzle({1,0,2}).
    static Modifier swapXY() {
        Modifier m; m.kind = ModifierKind::Swizzle; m.swizzle = {1,0,2}; return m;
    }
    static Modifier normalize() { Modifier m; m.kind = ModifierKind::Normalize; return m; }
};

inline Value applyModifiers(Value v, std::span<const Modifier> modifiers) {
    for (const auto& m : modifiers) {
        switch (m.kind) {
        case ModifierKind::Scale:
            v.x *= m.scalar.x; v.y *= m.scalar.y; v.z *= m.scalar.z; break;
        case ModifierKind::Negate:
            v.x = -v.x; v.y = -v.y; v.z = -v.z; break;
        case ModifierKind::DeadZone: {
            const float len = v.length(), threshold = std::clamp(m.threshold, 0.0f, 0.9999f);
            if (len <= threshold) v = {};
            else if (len > 0) {
                const float scaled = std::clamp((len - threshold) / (1.0f - threshold), 0.0f, 1.0f);
                const float multiplier = scaled / len;
                v.x *= multiplier; v.y *= multiplier; v.z *= multiplier;
            }
            break;
        }
        case ModifierKind::Swizzle: {
            const std::array<float,3> a{v.x,v.y,v.z};
            // Unreal-style swizzle: destination component i receives source component swizzle[i].
            // For 1D -> Y use {1,0,2}: {0,x,0}.
            v = {a[std::clamp(m.swizzle[0],0,2)],
                 a[std::clamp(m.swizzle[1],0,2)],
                 a[std::clamp(m.swizzle[2],0,2)]};
            break;
        }
        case ModifierKind::Normalize: {
            const float len = v.length();
            if (len > 1.0f) { v.x /= len; v.y /= len; v.z /= len; }
            break;
        }
        }
    }
    return v;
}

enum class TriggerKind : std::uint8_t { Down, Pressed, Released, Hold, Tap, Chord, DoubleTap, HoldAndRelease, Pulse, NotChord };
struct Trigger {
    TriggerKind kind = TriggerKind::Down;
    float seconds = 0;
    std::string chordAction{};

    static Trigger down() { return {}; }
    static Trigger pressed() { return {TriggerKind::Pressed}; }
    static Trigger released() { return {TriggerKind::Released}; }
    static Trigger hold(float seconds) { return {TriggerKind::Hold, seconds}; }
    static Trigger tap(float seconds) { return {TriggerKind::Tap, seconds}; }
    static Trigger chord(std::string action) { return {TriggerKind::Chord, 0, std::move(action)}; }
    static Trigger doubleTap(float window) { return {TriggerKind::DoubleTap, window}; }
    static Trigger holdAndRelease(float seconds) { return {TriggerKind::HoldAndRelease, seconds}; }
    static Trigger pulse(float interval) { return {TriggerKind::Pulse, interval}; }
    static Trigger notChord(std::string action) { return {TriggerKind::NotChord,0,std::move(action)}; }
};

enum class Phase : std::uint8_t { Started, Ongoing, Triggered, Completed, Canceled };
struct ActionEvent {
    std::string action;
    Phase phase = Phase::Started;
    Value value;
};
struct ActionState {
    Value value;
    bool active = false;
    bool triggered = false;
};

struct Action {
    std::string name;
    ValueType type = ValueType::Boolean;
    std::vector<Modifier> modifiers{};
    std::vector<Trigger> triggers{};
};

struct Mapping {
    std::uint64_t id = 0;   // Unique inside its context; 0 = assign at registration.
    std::string action;
    Control control;
    std::vector<Modifier> modifiers;
    std::vector<Trigger> triggers;
    bool consume = true;
};
enum class RebindPolicy : std::uint8_t { Allow, Reject, Swap };

// A collision is descriptive, not an error: shared controls may be intentional.
// Ordered by the same context priority/name order as dispatch.
struct BindingConflict {
    Control control;
    std::string higherContext, lowerContext;
    std::string higherAction, lowerAction;
    std::uint64_t higherMappingId=0, lowerMappingId=0;
    bool shadowed=false; // the earlier active binding consumes the control
};

struct MappingContext {
    std::string name;
    std::vector<Mapping> mappings{};

    void bind(std::string action, Control control,
              std::vector<Modifier> modifiers = {}, std::vector<Trigger> triggers = {},
              bool consume = true) {
        mappings.push_back({0, std::move(action), control,
                            std::move(modifiers), std::move(triggers), consume});
    }
};

namespace detail {
// Chord / NotChord dependencies must be acyclic, including mapping-level edges.
// This makes negative gates and chained dependencies order-independent.
inline bool chordGraphAcyclic(const std::map<std::string,Action>& actions,
                             const std::map<std::string,MappingContext>& contexts) {
    std::map<std::string,std::set<std::string>> edges;
    auto add = [&](const std::string& from, const std::vector<Trigger>& triggers) {
        auto& dependencies = edges[from];
        for (const auto& trigger : triggers)
            if (trigger.kind == TriggerKind::Chord || trigger.kind == TriggerKind::NotChord)
                dependencies.insert(trigger.chordAction);
    };
    for (const auto& [name, action] : actions) add(name,action.triggers);
    for (const auto& [name, context] : contexts) {
        (void)name;
        for (const auto& mapping : context.mappings) add(mapping.action,mapping.triggers);
    }
    std::map<std::string,int> marks;
    auto visit = [&](auto&& self, const std::string& name) -> bool {
        const int mark=marks[name];
        if (mark==1) return false;
        if (mark==2) return true;
        marks[name]=1;
        for (const auto& dependency : edges[name])
            if (edges.contains(dependency) && !self(self,dependency)) return false;
        marks[name]=2;
        return true;
    };
    for (const auto& [name, dependencies] : edges) {
        (void)dependencies;
        if (!visit(visit,name)) return false;
    }
    return true;
}
} // namespace detail

class InputFlow {
public:
    // Defining an action or context with an existing name replaces it and resets its runtime state.
    bool defineAction(Action action) {
        if (action.name.empty()) return false;
        // Stage candidate configuration: invalid cycles leave the live system intact.
        auto proposed=actions_;
        proposed[action.name]=action;
        if (!detail::chordGraphAcyclic(proposed,contexts_)) return false;
        const std::string name = action.name;
        actions_[name] = std::move(action);
        actionRuntime_.erase(name);
        states_.erase(name);
        return true;
    }
    bool registerContext(MappingContext context) {
        if (context.name.empty()) return false;
        std::uint64_t next = 1;
        std::set<std::uint64_t> ids;
        for (const auto& mapping : context.mappings) {
            if (mapping.action.empty() || !actions_.contains(mapping.action)) return false;
            if (mapping.id && !ids.insert(mapping.id).second) return false;
            if (mapping.id >= next) {
                if (mapping.id == UINT64_MAX) return false;
                next = mapping.id + 1;
            }
        }
        for (auto& mapping : context.mappings) {
            if (!mapping.id) {
                if (next == UINT64_MAX) return false;
                mapping.id = next++;
            }
        }
        auto proposed=contexts_;
        proposed[context.name]=context;
        if (!detail::chordGraphAcyclic(actions_,proposed)) return false;
        const std::string name = context.name;
        contexts_[name] = std::move(context);
        eraseContextRuntime(name);
        if (active_.contains(name)) seedHeldMappings(name);
        return true;
    }
    bool activateContext(const std::string& name, int priority = 0) {
        if (!contexts_.contains(name)) return false;
        const bool firstActivation = !active_.contains(name);
        active_[name] = priority;
        if (firstActivation) seedHeldMappings(name);
        return true;
    }
    void deactivateContext(const std::string& name) {
        active_.erase(name);
        eraseContextRuntime(name);
    }
    void clearContexts() {
        active_.clear();
        mappingRuntime_.clear();
    }
    bool isContextActive(const std::string& name) const { return active_.contains(name); }
    // Read-only declaration access for editor/host trigger-aware presentation.
    const Action* actionDefinition(const std::string& name) const {
        const auto it=actions_.find(name);
        return it==actions_.end()?nullptr:&it->second;
    }
    const MappingContext* context(const std::string& name) const {
        const auto it = contexts_.find(name);
        return it == contexts_.end() ? nullptr : &it->second;
    }
    // Runtime rebinding is scoped to a mapping id and leaves all other contexts untouched.
    bool rebind(const std::string& contextName, std::uint64_t id, Control control,
                RebindPolicy policy = RebindPolicy::Allow) {
        auto it = contexts_.find(contextName);
        if (it == contexts_.end()) return false;
        Mapping* target=nullptr;
        std::vector<Mapping*> colliding;
        for(auto& mapping:it->second.mappings){
            if(mapping.id==id)target=&mapping;
            else if(mapping.control==control)colliding.push_back(&mapping);
        }
        if(!target)return false;
        if(policy==RebindPolicy::Reject && !colliding.empty())return false;
        if(policy==RebindPolicy::Swap && colliding.size()>1)return false;
        const Control previous=target->control;
        if(policy==RebindPolicy::Swap && colliding.size()==1){
            Mapping* other=colliding.front();
            other->control=previous;
            mappingRuntime_.erase({contextName,other->id});
        }
        target->control=control;
        mappingRuntime_.erase({contextName,id});
        if (active_.contains(contextName)) seedHeldMappings(contextName);
        return true;
    }

    // Optional editor/user-facing collision report, using actual dispatch precedence.
    // Disabled contexts are ignored by default because they cannot shadow an action.
    std::vector<BindingConflict> conflicts(bool activeOnly=true) const {
        struct Bound { std::string context,action; std::uint64_t id; Control control;
                       bool consume; int priority; };
        std::vector<Bound> ordered;
        for(const auto& [name,context]:contexts_){
            const auto active=active_.find(name);
            if(activeOnly && active==active_.end())continue;
            for(const auto& mapping:context.mappings)
                ordered.push_back({name,mapping.action,mapping.id,mapping.control,mapping.consume,
                                   active==active_.end()?0:active->second});
        }
        std::stable_sort(ordered.begin(),ordered.end(),[](const Bound& a,const Bound& b){
            if(a.priority!=b.priority)return a.priority>b.priority;
            return a.context<b.context;
        });
        std::vector<BindingConflict> result;
        for(std::size_t i=0;i<ordered.size();++i)
            for(std::size_t j=i+1;j<ordered.size();++j)
                if(ordered[i].control==ordered[j].control)
                    result.push_back({ordered[i].control,ordered[i].context,ordered[j].context,
                                      ordered[i].action,ordered[j].action,ordered[i].id,
                                      ordered[j].id,ordered[i].consume});
        return result;
    }

    void setControl(Control control, float value) {
        if (!std::isfinite(value)) value = 0;
        auto& sample = controls_[control];
        const bool before = std::abs(sample.value) > kEpsilon;
        const bool after = std::abs(value) > kEpsilon;
        sample.pressed |= !before && after;
        sample.released |= before && !after;
        sample.value = value;
        sample.transient = false;
    }
    // Relative mouse axes / wheel: deltas accumulate and are automatically cleared after update.
    void addDelta(Control control, float amount) {
        if (!std::isfinite(amount)) return;
        auto& sample = controls_[control];
        const bool before = std::abs(sample.value) > kEpsilon;
        sample.value += amount;
        sample.pressed |= !before && std::abs(sample.value) > kEpsilon;
        sample.transient = true;
    }

    // Release only a physical device category while preserving keyboard,
    // pointer, touch and gamepad controls owned by the same local player.
    // The next update() emits Canceled for affected Actions that no longer
    // have another live mapping; unchanged devices remain active.
    void cancelDevices(std::initializer_list<Device> devices, int deviceIndex = -1) {
        const std::set<Device> removed(devices.begin(),devices.end());
        auto matches = [&](Control key) {
            return removed.contains(key.device) &&
                   (deviceIndex < 0 || key.deviceIndex == deviceIndex);
        };
        for (auto it=controls_.begin(); it!=controls_.end();) {
            if (matches(it->first)) it=controls_.erase(it);
            else ++it;
        }
        for (const auto& [contextName, context] : contexts_)
            for (const auto& mapping : context.mappings)
                if (matches(mapping.control)) {
                    interruptedActions_.insert(mapping.action);
                    mappingRuntime_.erase({contextName,mapping.id});
                }
    }

    // Use on global focus loss. Emits Canceled and drops all held devices.
    void cancelAll() {
        // Canceled phases stay visible through events() right away and are
        // also handed out by the next update() unless events() was read first,
        // so a consumer that only reads update()'s return value never loses them.
        for (const auto& [name, state] : states_) {
            if (state.active) pendingCanceled_.push_back({name, Phase::Canceled, {}});
        }
        events_ = pendingCanceled_;
        eventsRead_ = false;
        controls_.clear();
        mappingRuntime_.clear();
        actionRuntime_.clear();
        states_.clear();
        interruptedActions_.clear();
    }

    std::span<const ActionEvent> update(float deltaSeconds) {
        events_.clear();
        if (!eventsRead_) events_ = pendingCanceled_;
        pendingCanceled_.clear();
        eventsRead_ = false;
        const float dt = std::isfinite(deltaSeconds) ? std::max(0.0f, deltaSeconds) : 0.0f;

        // Stable processing: higher context priority first; equal priorities lexicographic by name.
        std::vector<std::pair<std::string,int>> ordered(active_.begin(), active_.end());
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });

        // Pass 1: snapshot candidate controls in context priority order.
        // Do not advance trigger timers or emit phases during this preview.
        struct PreviewBinding { const Mapping* mapping; Value value; };
        std::map<std::string,std::vector<PreviewBinding>> candidates;
        std::set<Control> previewConsumed;
        bool hasConditionalChords=false;
        for(const auto& [actionName,definition]:actions_) {
            (void)actionName;
            for(const auto& trigger:definition.triggers)
                if(trigger.kind==TriggerKind::Chord ||
                   trigger.kind==TriggerKind::NotChord)hasConditionalChords=true;
        }
        auto rebuildCandidates = [&](const std::map<std::string,bool>* levels) {
            candidates.clear();
            previewConsumed.clear();
            for (const auto& [name,priority] : ordered) {
                (void)priority;
                for (const auto& mapping : contexts_.at(name).mappings) {
                    if (!actions_.contains(mapping.action) ||
                        previewConsumed.contains(mapping.control)) continue;
                    if (!levels) {
                        for (const auto& trigger : mapping.triggers)
                            if (trigger.kind==TriggerKind::Chord ||
                                trigger.kind==TriggerKind::NotChord) hasConditionalChords=true;
                    }
                    // A gated consuming binding should only reserve its control
                    // when the prerequisite Action levels actually satisfy it.
                    if (levels && (!chordSatisfied(mapping.triggers,*levels) ||
                                   !chordSatisfied(actions_.at(mapping.action).triggers,*levels)))
                        continue;
                    const float raw = sample(mapping.control).value;
                    const Value v = applyModifiers({raw,0,0},mapping.modifiers);
                    if (!v.actuated()) continue;
                    candidates[mapping.action].push_back({&mapping,v});
                    if (mapping.consume) previewConsumed.insert(mapping.control);
                }
            }
        };
        rebuildCandidates(nullptr);

        // Pass 1b: recursively resolve same-frame Action levels through the
        // acyclic Chord/NotChord graph, including mapping and Action gates.
        // Timing triggers affect events, not held chord prerequisite levels.
        std::map<std::string,bool> chordLevels;
        std::map<std::string,int> visiting;
        auto resolve = [&](auto&& self, const std::string& actionName) -> bool {
            const auto action=actions_.find(actionName);
            if (action==actions_.end()) return false;
            if (visiting[actionName]==2) return chordLevels[actionName];
            if (visiting[actionName]==1) return false; // defensive native-state guard
            visiting[actionName]=1;
            auto gatesSatisfied = [&](std::span<const Trigger> triggers) {
                for (const auto& trigger : triggers) {
                    if (trigger.kind!=TriggerKind::Chord &&
                        trigger.kind!=TriggerKind::NotChord) continue;
                    const bool prerequisite=self(self,trigger.chordAction);
                    if (trigger.kind==TriggerKind::Chord && !prerequisite) return false;
                    if (trigger.kind==TriggerKind::NotChord && prerequisite) return false;
                }
                return true;
            };
            Value aggregate{};
            if (const auto it=candidates.find(actionName); it!=candidates.end())
                for (const auto& candidate : it->second)
                    if (gatesSatisfied(candidate.mapping->triggers))
                        aggregate+=candidate.value;
            const Value value=constrain(applyModifiers(aggregate,action->second.modifiers),
                                         action->second.type);
            const bool enabled=value.actuated() && gatesSatisfied(action->second.triggers);
            chordLevels[actionName]=enabled;
            visiting[actionName]=2;
            return enabled;
        };
        auto resolveLevels = [&] {
            chordLevels.clear();
            visiting.clear();
            for (const auto& [name,action] : actions_) {
                (void)action;
                resolve(resolve,name);
            }
        };
        resolveLevels();

        // Reconcile conditional consumption with transitive prerequisites. The
        // initial pass is deliberately conservative (raw priority ordering);
        // each subsequent pass only reserves controls for satisfied mapping
        // gates. Stop on convergence. In a priority/consumption feedback loop
        // with no fixed point, fall back deterministically to the conservative
        // initial snapshot rather than depend on iteration count or name order.
        if (hasConditionalChords) {
            std::vector<std::map<std::string,bool>> seen{chordLevels};
            bool settled=false;
            // A valid acyclic chain may need more than eight transitions.
            // The action count bounds propagation depth; cap malformed
            // native graphs to avoid unbounded presentation-frame work.
            const int maxPasses=int(std::min<std::size_t>(actions_.size()+1,512));
            for (int pass=0; pass<maxPasses; ++pass) {
                const auto prior=chordLevels;
                rebuildCandidates(&prior);
                resolveLevels();
                if (chordLevels==prior) { settled=true; break; }
                if (std::find(seen.begin(),seen.end(),chordLevels)!=seen.end()) break;
                seen.push_back(chordLevels);
            }
            if (!settled) {
                rebuildCandidates(nullptr);
                resolveLevels();
            }
        }

        std::map<Control,bool> consumed;
        struct Aggregate { Value value; bool active = false, triggered = false,
                           began = false, interrupted = false,
                           // an eligible mapping lets action-level triggers decide this
                           // frame: it fired, or it has no triggers of its own and its
                           // control was down / pressed / released (Tap, Released, ...)
                           permit = false; };
        std::map<std::string,Aggregate> result;
        std::set<std::string> presentedActions;

        for (const auto& [name,priority] : ordered) {
            (void)priority;
            for (const auto& mapping : contexts_.at(name).mappings) {
                const auto actionIt = actions_.find(mapping.action);
                if (actionIt == actions_.end()) continue;
                presentedActions.insert(mapping.action);
                auto& runtime = mappingRuntime_[{name,mapping.id}];
                const Raw raw = sample(mapping.control);
                const Value physicalValue = applyModifiers({raw.value,0,0}, mapping.modifiers);
                const bool physicalDown = physicalValue.actuated();
                const bool blocked = consumed[mapping.control];
                const bool chordOK = chordSatisfied(mapping.triggers, chordLevels) &&
                    chordSatisfied(actionIt->second.triggers, chordLevels);
                const bool eligible = !blocked && chordOK;
                const Value converted = eligible ? physicalValue : Value{};
                const bool down = converted.actuated();
                const bool suppressed = runtime.suppressUntilRelease;
                const bool pressed = eligible && !suppressed &&
                    (raw.pressed || (physicalDown && !runtime.wasDown));
                const bool released = eligible && !suppressed &&
                    (raw.released || (!physicalDown && runtime.wasDown));
                const bool tapWithinFrame = pressed && released;
                // A disabled mapping must not age a Hold/Pulse timer behind a
                // modal or a chord, nor fire a phantom Pressed on re-entry.
                bool fired = false;
                if (eligible && !suppressed) {
                    fired = evaluateTriggers(mapping.triggers, runtime, down, pressed, released,
                                             tapWithinFrame, dt, chordLevels);
                    if (mapping.triggers.empty() ? (down || pressed || released) : fired)
                        result[mapping.action].permit = true;
                } else if (eligible && suppressed) {
                    // Context activated after physical KeyDown: continuous Down
                    // is visible, but timed/edge triggers wait for a fresh press.
                    resetTiming(runtime);
                    runtime.wasDown = physicalDown;
                    const bool continuous = std::all_of(mapping.triggers.begin(),
                        mapping.triggers.end(), [](const Trigger& t) {
                            return t.kind==TriggerKind::Down ||
                                   t.kind==TriggerKind::Chord ||
                                   t.kind==TriggerKind::NotChord;
                        });
                    fired = down && continuous;
                } else {
                    if (physicalDown) result[mapping.action].interrupted = true;
                    resetTiming(runtime);
                    runtime.wasDown = physicalDown;
                }
                if (!physicalDown) runtime.suppressUntilRelease = false;
                if (down && chordOK) {
                    auto& aggregate = result[mapping.action];
                    aggregate.active = true;
                    aggregate.value += converted;
                }
                if (pressed && chordOK) result[mapping.action].began = true;
                if (fired) result[mapping.action].triggered = true;
                if (mapping.consume && chordOK && (down || pressed || released))
                    consumed[mapping.control] = true;
            }
        }

        for (const auto& [name, action] : actions_) {
            const Aggregate aggregate = result[name];
            auto& runtime = actionRuntime_[name];
            // Action-level modifiers and axis dimension constraints run BEFORE
            // checking whether the resulting action is active or triggered.
            // An axis dead zone or scalar-zero must not emit a phantom Triggered.
            Value value = constrain(applyModifiers(aggregate.value, action.modifiers), action.type);
            const bool sourceDown = value.actuated();
            const bool chordOK = chordSatisfied(action.triggers, chordLevels);
            const bool actionDown = sourceDown && chordOK;
            const bool suppressed = runtime.suppressUntilRelease;
            const bool press = chordOK && !suppressed &&
                ((sourceDown && !runtime.wasDown) ||
                 (aggregate.began && (!aggregate.active || sourceDown)));
            const bool release = chordOK && !suppressed && !sourceDown && runtime.wasDown;
            bool triggered = aggregate.triggered && (!aggregate.active || sourceDown);
            if (!chordOK) {
                resetTiming(runtime);
                runtime.wasDown = sourceDown;
                triggered = false;
            } else if (suppressed) {
                resetTiming(runtime);
                runtime.wasDown = sourceDown;
                const bool continuous = std::all_of(action.triggers.begin(),
                    action.triggers.end(), [](const Trigger& t) {
                        return t.kind==TriggerKind::Down ||
                               t.kind==TriggerKind::Chord ||
                               t.kind==TriggerKind::NotChord;
                    });
                triggered = triggered && sourceDown && continuous;
            } else if (!action.triggers.empty()) {
                const bool actionGate = evaluateTriggers(action.triggers, runtime, sourceDown,
                                                         press, release, press && release, dt, chordLevels);
                // Mapping-level "fired" is Down for trigger-less mappings, which is false
                // on a release frame: action-level Tap / Released / HoldAndRelease would
                // never fire. Trigger-less mappings only feed the value; action triggers decide.
                triggered = aggregate.permit && actionGate;
            } else {
                runtime.wasDown = sourceDown;
                if (!sourceDown) { runtime.seconds = 0; runtime.holdFired = false; }
            }
            if (!sourceDown) runtime.suppressUntilRelease = false;

            const auto previous = states_[name];
            const bool quickPulse = !actionDown && aggregate.began && triggered;
            if (!chordOK) value = {};
            if ((actionDown && !previous.active) || quickPulse)
                events_.push_back({name, Phase::Started, value});
            if (triggered) events_.push_back({name, Phase::Triggered, value});
            else if (actionDown) events_.push_back({name, Phase::Ongoing, value});
            if ((!actionDown && previous.active) || quickPulse) {
                const bool interrupted = !chordOK || aggregate.interrupted ||
                    interruptedActions_.contains(name) || !presentedActions.contains(name);
                events_.push_back({name, interrupted ? Phase::Canceled :
                    (previous.triggered || triggered ? Phase::Completed : Phase::Canceled), {}});
            }
            states_[name] = {value, actionDown, triggered || (actionDown && previous.triggered)};
        }

        for (auto& [key, raw] : controls_) {
            (void)key;
            raw.pressed = raw.released = false;
            if (raw.transient) { raw.value = 0; raw.transient = false; }
        }
        interruptedActions_.clear();
        return events_;
    }

    const ActionState* state(const std::string& name) const {
        const auto it = states_.find(name);
        return it == states_.end() ? nullptr : &it->second;
    }
    std::span<const ActionEvent> events() const { eventsRead_ = true; return events_; }
    // Enumerate every declared context (including inactive UI/vehicle/modal contexts) for Settings UI.
    std::vector<std::string> registeredContexts() const {
        std::vector<std::string> names;
        names.reserve(contexts_.size());
        for (const auto& [name,context] : contexts_) {
            (void)context;
            names.push_back(name);
        }
        return names;
    }
    std::vector<std::string> activeContexts() const {
        std::vector<std::string> out;
        out.reserve(active_.size());
        for (const auto& entry : active_) out.push_back(entry.first);
        return out;
    }

private:
    static constexpr float kEpsilon = 0.00001f;
    struct Raw { float value = 0; bool pressed = false, released = false, transient = false; };
    struct Runtime {
        bool wasDown=false, holdFired=false, releaseArmed=false, skipNextArm=false;
        bool suppressUntilRelease=false;
        float seconds=0, sinceRelease=1000000.0f, lastPulse=0;
    };

    static void resetTiming(Runtime& runtime) {
        runtime.seconds=0;
        runtime.holdFired=false;
        runtime.releaseArmed=false;
        runtime.skipNextArm=false;
        runtime.sinceRelease=1000000.0f;
        runtime.lastPulse=0;
    }
    void seedHeldMappings(const std::string& name) {
        const auto it=contexts_.find(name);
        if (it==contexts_.end()) return;
        for (const auto& binding : it->second.mappings) {
            const Raw raw=sample(binding.control);
            if (raw.transient) continue;
            // A held control predating this context is not a new key press.
            auto& runtime=mappingRuntime_[{name,binding.id}];
            runtime.wasDown=applyModifiers({raw.value,0,0},binding.modifiers).actuated();
            runtime.suppressUntilRelease=runtime.wasDown;
            if (runtime.wasDown && !states_[binding.action].active) {
                auto& actionRuntime=actionRuntime_[binding.action];
                actionRuntime.wasDown=true;
                actionRuntime.suppressUntilRelease=true;
                resetTiming(actionRuntime);
            }
        }
    }

    Raw sample(Control key) const {
        const auto it = controls_.find(key);
        return it == controls_.end() ? Raw{} : it->second;
    }
    static bool chordSatisfied(std::span<const Trigger> triggers, const std::map<std::string,bool>& levels) {
        for (const auto& trigger : triggers) {
            if (trigger.kind == TriggerKind::Chord &&
                (!levels.contains(trigger.chordAction) || !levels.at(trigger.chordAction)))
                return false;
            if (trigger.kind == TriggerKind::NotChord &&
                levels.contains(trigger.chordAction) && levels.at(trigger.chordAction))
                return false;
        }
        return true;
    }
    static bool evaluateTriggers(std::span<const Trigger> triggers, Runtime& runtime,
                                 bool down, bool pressed, bool released, bool quick,
                                 float dt, const std::map<std::string,bool>& levels) {
        const float elapsedBeforeRelease = runtime.seconds;
        if (pressed && !runtime.wasDown) {
            runtime.seconds=0; runtime.holdFired=false; runtime.lastPulse=0;
        }
        if (down)runtime.seconds+=dt;
        if (!down)runtime.sinceRelease=std::min(1000000.0f,runtime.sinceRelease+dt);
        const float held = quick ? dt : (down ? runtime.seconds : elapsedBeforeRelease);

        bool matched = triggers.empty() ? down : true;
        for (const auto& trigger : triggers) {
            bool ok = false;
            switch (trigger.kind) {
            case TriggerKind::Down: ok = down; break;
            case TriggerKind::Pressed: ok = pressed; break;
            case TriggerKind::Released: ok = released; break;
            case TriggerKind::Hold:
                ok = down && !runtime.holdFired && runtime.seconds >= std::max(0.0f,trigger.seconds);
                break;
            case TriggerKind::Tap:
                ok = released && (runtime.wasDown || quick) &&
                     held <= std::max(0.0f,trigger.seconds);
                break;
            case TriggerKind::Chord:
                ok = levels.contains(trigger.chordAction) && levels.at(trigger.chordAction);
                break;
            case TriggerKind::NotChord:
                ok = !levels.contains(trigger.chordAction) || !levels.at(trigger.chordAction);
                break;
            case TriggerKind::DoubleTap:
                ok = pressed && runtime.releaseArmed &&
                     runtime.sinceRelease<=std::max(0.0f,trigger.seconds);
                break;
            case TriggerKind::HoldAndRelease:
                ok = released && (runtime.wasDown || quick) &&
                     held>=std::max(0.0f,trigger.seconds);
                break;
            case TriggerKind::Pulse:
                ok = down && (runtime.seconds-runtime.lastPulse)>=
                     std::max(0.01f,trigger.seconds);
                break;
            }
            matched = matched && ok;
        }
        if (matched) for (const auto& trigger : triggers) {
            if (trigger.kind == TriggerKind::Hold)runtime.holdFired=true;
            if (trigger.kind == TriggerKind::Pulse)runtime.lastPulse=runtime.seconds;
            if (trigger.kind == TriggerKind::DoubleTap) {
                // A completed double tap consumes its pair: the release of the second
                // press does not arm a third press (triple tap fires once, not twice).
                runtime.releaseArmed=false;
                runtime.skipNextArm=true;
            }
        }
        if(released){
            // The first press must itself be a tap: a long hold followed by a quick
            // press is not a double tap.
            bool arm=!runtime.skipNextArm;
            for (const auto& trigger : triggers)
                if (trigger.kind == TriggerKind::DoubleTap && held > std::max(0.0f,trigger.seconds))
                    arm=false;
            runtime.skipNextArm=false;
            runtime.releaseArmed=arm;
            runtime.sinceRelease=0;
        }
        runtime.wasDown = down;
        if (!down) { runtime.seconds = 0; runtime.holdFired = false; }
        return matched;
    }
    static Value constrain(Value value, ValueType type) {
        switch (type) {
        case ValueType::Boolean:
            return {value.actuated() ? 1.0f : 0.0f, 0, 0};
        case ValueType::Axis1D:
            return {value.x, 0, 0};
        case ValueType::Axis2D:
            return {value.x, value.y, 0};
        case ValueType::Axis3D:
            return value;
        }
        return {};
    }
    void eraseContextRuntime(const std::string& name) {
        for (auto it = mappingRuntime_.begin(); it != mappingRuntime_.end(); )
            if (it->first.first == name) it = mappingRuntime_.erase(it);
            else ++it;
    }

    std::map<std::string,Action> actions_;
    std::map<std::string,MappingContext> contexts_;
    std::map<std::string,int> active_;
    std::map<Control,Raw> controls_;
    std::map<std::pair<std::string,std::uint64_t>,Runtime> mappingRuntime_;
    std::map<std::string,Runtime> actionRuntime_;
    std::map<std::string,ActionState> states_;
    std::set<std::string> interruptedActions_;
    std::vector<ActionEvent> events_;
    std::vector<ActionEvent> pendingCanceled_;   // cancelAll() phases not yet returned by update()
    mutable bool eventsRead_ = false;
};

// Compatibility alias: callers using the old Enhanced Input type keep working.
// New native clients should include aiverselib/gameplay/InputFlow.h and use
// aiverse::lib::gameplay::inputflow::InputFlow.
using InputSystem = InputFlow;

} // namespace aiverse::lib::gameplay::enhanced
