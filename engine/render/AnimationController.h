// Layered state-machine controller with Mecanim semantics (Unity AnimatorController imported by
// tools/importers/unity_anim): layers (override / additive, weighted node masks), states with
// speed / time / cycle-offset parameters, blend trees (1D, 2D directional / cartesian, direct)
// with one shared normalized clock, transitions (conditions, exit time, fixed / normalized
// duration, offset, any state, exit to default, interruption) and latched triggers.
// Presentation only: floats, never read by the simulation. Used as AnimationGraph node
// {kind:"controller"}; the graph owns parameters, this player owns layer state.
#pragma once
#include "Animation.h"
#include "../data/Json.h"
#include <memory>
#include <string>
#include <vector>

namespace aiverse::render {
struct AnimationParameters;

class AnimationController {
public:
    struct Motion;
    struct Child {
        std::shared_ptr<const Motion> motion; // null: empty slot (bind pose)
        float threshold = 0, x = 0, y = 0, timeScale = 1, cycleOffset = 0;
        std::string direct;
    };
    struct Motion {
        enum class Type { Clip, Tree1D, Directional2D, Cartesian2D, Direct };
        Type type = Type::Clip;
        int clip = -1;
        bool loop = true;
        std::string parameter, parameterY;
        bool normalize = false;          // direct trees: weights divided by their sum
        std::vector<Child> children;     // trees; 1D children sorted by threshold
    };
    enum class Mode { If, IfNot, Greater, Less, Equals, NotEqual };
    struct Condition { std::string parameter; Mode mode = Mode::If; float threshold = 0; };
    enum class Interruption { None, Source, Destination, SourceThenDestination, DestinationThenSource };
    struct Transition {
        int to = -1;                     // destination state; -1 with exit=true: the layer's default
        bool exit = false, hasExitTime = false, fixedDuration = true, canTransitionToSelf = true, orderedInterruption = true;
        float exitTime = 0, duration = 0, offset = 0;
        Interruption interruption = Interruption::None;
        std::vector<Condition> conditions;
    };
    struct State {
        std::string name;
        std::shared_ptr<const Motion> motion;  // null: empty state (contributes nothing)
        float speed = 1, cycleOffset = 0;
        std::string speedParameter, timeParameter, cycleOffsetParameter;
        std::vector<Transition> transitions;
    };
    struct Layer {
        std::string name, weightParameter;
        float weight = 1;
        bool additive = false;
        std::vector<float> mask;         // per model node 0..1; empty = whole model
        std::vector<State> states;
        std::vector<Transition> anyState;
        int defaultState = 0;
    };
    // `parameters` are the graph defaults; every referenced scalar must be declared there.
    // Derived inputs: target = clamp(from * scale + offset, min, max), applied before each update
    // (maps RtsView built-ins such as `speed` onto imported parameter names).
    struct Input { std::string target, from; float scale = 1, offset = 0, min = -1000000, max = 1000000; };
    static std::shared_ptr<const AnimationController> compile(const Model& model, const Json& definition, const AnimationParameters& parameters);
    const std::vector<Layer>& layers() const { return layers_; }
    const std::vector<std::string>& triggers() const { return triggers_; }
    const std::vector<Input>& inputs() const { return inputs_; }
    const Model* model() const { return model_; }
    float duration(const Motion& m, const AnimationParameters& p) const;
    // Blend weights of a tree's direct children for the current parameters (sum 1 unless direct).
    void weights(const Motion& m, const AnimationParameters& p, std::vector<float>& out) const;
    static constexpr size_t kMaxChildren = 64, kMaxDepth = 8;

private:
    const Model* model_ = nullptr;
    std::vector<Layer> layers_;
    std::vector<std::string> triggers_;
    std::vector<Input> inputs_;
};

class AnimationControllerPlayer {
public:
    void reset(std::shared_ptr<const AnimationController> controller);
    // Advances clocks and transitions. Triggers latch on a change to a non-zero value and are
    // consumed by the transition that uses them.
    void update(float dt, AnimationParameters& parameters);
    // Evaluates every layer over `pose` (bind pose for the base layer). Does not advance time.
    void evaluate(Pose& pose, const AnimationParameters& parameters) const;
    // Diagnostics: state name / normalized time of a layer, transition progress (-1: none).
    const std::string& state(size_t layer) const;
    float normalizedTime(size_t layer) const;
    float transition(size_t layer) const;
    bool triggerLatched(const std::string& name) const;

private:
    struct Play { int state = -1; double time = 0; float speed = 1; };   // time: normalized
    struct LayerState {
        Play current, next;              // next.state >= 0 while a transition runs
        float fade = 0, fadeDuration = 0;
        bool frozen = false;             // interrupted: source is the stored snapshot
        Pose snapshot, snapshotReference;
        float snapshotCoverage = 0;
        float weight = 1;
    };
    struct Layered { Pose pose, reference; float coverage = 0; };
    void advance(Play& p, const AnimationController::Layer& layer, float dt, const AnimationParameters& params);
    bool fires(const AnimationController::Transition& t, const Play& p, double previous, const AnimationParameters& params) const;
    void start(size_t layer, const AnimationController::Transition& t, const AnimationParameters& params);
    float stateSeconds(const AnimationController::Layer& layer, int state, const AnimationParameters& params) const;
    bool samplePlay(const AnimationController::Layer& layer, const Play& p, const AnimationParameters& params, Pose& out, bool reference) const;
    void sampleLayer(size_t layer, const AnimationParameters& params, Layered& out) const;
    void sampleMotion(const AnimationController::Motion& m, double normalized, float cycleOffset, const AnimationParameters& params, Pose& out, bool reference, size_t depth) const;
    std::shared_ptr<const AnimationController> controller_;
    std::vector<LayerState> layers_;
    std::vector<float> lastTrigger_;
    std::vector<uint8_t> latched_;
    Pose bind_;
    mutable Layered layered_, transitionPose_;
    mutable std::vector<Pose> stack_;
};
} // namespace aiverse::render
