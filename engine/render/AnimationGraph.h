// Programmable pose graphs. Presentation only; the host owns model/program lifetimes.
#pragma once
#include "Animation.h"
#include "AnimationContacts.h"
#include "SurfaceLocomotion.h"
#include "../data/Json.h"
#include <map>
#include <memory>
#include <span>
#include <functional>

namespace aiverse::render {
struct AnimationRigState;
struct AnimationParameters {
    std::shared_ptr<const AnimationEnvironment> environment;
    std::map<std::string, float> values;
    std::map<std::string, float3> vectors;
    bool set(const std::string& name, float value);
    bool set(const std::string& name, float3 value);
};

// A stateless native operation; shared configuration and per-instance state stay separate.
// Callbacks must not access simulation or retain pose/state references. Worker safety is the host's responsibility.
struct AnimationOperation {
    size_t stateFloats = 0; // bounded to 256 per node, initialized to zero on state entry/reset
    std::function<void(const Model&, const Json&)> validate; // compile-time only; may throw
    void (*update)(const Json&, const AnimationParameters&, float, std::span<float>) noexcept = nullptr;
    void (*evaluate)(const Model&, const Json&, const AnimationParameters&, std::span<const float>, Pose&) noexcept = nullptr;
};
using AnimationOperations = std::map<std::string, std::shared_ptr<const AnimationOperation>>;

// Immutable after compilation. Input references must precede their consumers (a bounded DAG).
class AnimationRig;
class MotionDatabase;
class MotionPlayer;
class AnimationController;
class AnimationControllerPlayer;
// Trait implementations are shared and stateless. Callbacks return false to disable a failing instance.
struct AnimationTrait {
    size_t stateFloats = 0;
    std::function<void(const Model&, const Json&)> validate;
    using Update = std::function<bool(const Json&, AnimationParameters&, float, std::span<float>)>;
    using Evaluate = std::function<bool(const Json&, const AnimationParameters&, std::span<const float>, Pose&)>;
    Update preUpdate, postUpdate;
    Evaluate preEvaluate, postEvaluate;
};
using AnimationTraits = std::map<std::string, std::shared_ptr<const AnimationTrait>>;
using AnimationGraphs = std::map<std::string, Json>;

class AnimationGraph {
public:
    enum class Kind { Bind, Clip, Blend, Additive, TwoBoneIK, Native, Stack, Subgraph, Rig, MotionMatching, Controller };
    struct Trait {
        std::shared_ptr<const AnimationTrait> implementation;
        std::shared_ptr<const AnimationRig> rig;
        Json config;
    };
    struct Node {
        std::string id, parameter, target, pole;
        Kind kind = Kind::Bind;
        int a = -1, b = -1, clip = -1, hip = -1, knee = -1, foot = -1;
        float weight = 1, speed = 1;
        bool loop = true;
        std::vector<uint8_t> mask;
        std::shared_ptr<const AnimationOperation> operation;
        Json config;
        std::vector<Trait> traits;
        std::shared_ptr<const AnimationGraph> subgraph;
        std::map<std::string, std::string> bindings;
        std::shared_ptr<const AnimationRig> rig;
        std::shared_ptr<const MotionDatabase> motion;
        std::shared_ptr<const AnimationController> controller;  // kind "controller" (AnimationController.h)

    };
    struct Condition { std::string parameter, op; float value = 0; };
    struct Transition { int to = 0; float fade = .2f, after = 0; std::vector<Condition> conditions; };
    struct State { std::string id; int output = 0; std::vector<Transition> transitions; std::vector<int> program; };
    static std::shared_ptr<const AnimationGraph> compile(const Model& model, const Json& definition, const AnimationOperations& operations = {}, const AnimationTraits& traits = {}, const AnimationGraphs& graphs = {});
    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<State>& states() const { return states_; }
    bool hasContacts() const;
    const AnimationParameters& defaults() const { return defaults_; }
    int initial() const { return initial_; }
    const Model* model() const { return model_; }
private:
    struct CompileContext;
    static std::shared_ptr<const AnimationGraph> compileImpl(const Model&, const Json&, CompileContext&, int);
    const Model* model_ = nullptr;
    std::vector<Node> nodes_;
    std::vector<State> states_;
    AnimationParameters defaults_;
    int initial_ = 0;
};

class AnimationGraphInstance {
public:
    AnimationGraphInstance() = default;
    AnimationGraphInstance(const AnimationGraphInstance&) = delete;
    AnimationGraphInstance& operator=(const AnimationGraphInstance&) = delete;
    AnimationGraphInstance(AnimationGraphInstance&&) = default;
    AnimationGraphInstance& operator=(AnimationGraphInstance&&) = default;
    void reset(std::shared_ptr<const AnimationGraph> graph);
    const AnimationGraph* graph() const { return graph_.get(); }
    AnimationParameters& parameters() { return parameters_; }
    const AnimationParameters& parameters() const { return parameters_; }
    // One transition at most per update. Interrupted fades start from the last evaluated pose.
    void update(float dt);
    void evaluate(Pose& result);
    const std::string& state() const;
    float stateTime() const { return time_; }
    AnimationGraphInstance* child(const std::string& node);
    const MotionPlayer* motion(const std::string& node) const;
    const AnimationControllerPlayer* controller(const std::string& node) const;
    bool hasContacts() const;
    AnimationContactReport contactReport() const;
    SurfaceLocomotionReport locomotionReport() const;
    bool healthy() const { return healthy_; }
    const std::string& error() const { return error_; }
private:
    void evaluateCurrent(Pose& result);
    std::shared_ptr<const AnimationGraph> graph_;
    AnimationParameters parameters_;
    int state_ = 0;
    float time_ = 0, fadeTime_ = 0, fadeDuration_ = 0;
    Pose source_, last_, bind_;
    std::vector<Pose> poses_;
    std::vector<mat4> world_;
    std::vector<std::vector<float>> nativeState_;
    std::vector<double> clocks_;
    struct TraitState { std::vector<float> data; bool enabled=true; std::shared_ptr<AnimationRigState> rig; };
    std::vector<std::vector<TraitState>> traitState_;
    std::vector<std::shared_ptr<AnimationGraphInstance>> children_;
    std::vector<std::shared_ptr<MotionPlayer>> motions_;
    std::vector<std::shared_ptr<AnimationControllerPlayer>> controllers_;
    std::vector<std::shared_ptr<AnimationRigState>> rigs_;
    bool healthy_=true;
    std::string error_;
    void resetNodes();
    void bindChild(size_t node);
    void traitFailure(size_t node, size_t trait);
};
} // namespace aiverse::render
