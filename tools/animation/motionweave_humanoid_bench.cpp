// MotionWeave per-character CPU cost on a 53-bone procedural humanoid.
// Host-only, opt-in (AIVERSE_BUILD_TOOLS); timings are machine-dependent and
// deliberately not part of CTest.
//
//   aiverse_motionweave_humanoid_bench [--counts 1,10,100,1000] [--gaits 24]
//                                      [--scenarios clip,blend,blend-ik,mm,mm-ik]
//                                      [--repeats 3] [--no-phases]
//
// Scenarios (one shared graph per scenario, per-actor parameters differ):
//   clip      Walk_IP clip only
//   blend     walk/run blend (weight per actor)
//   blend-ik  blend + rig with two twoBoneIK leg units
//   mm        motion matching over Idle/Walk/Run/Stop/TurnLeft + N gaits
//   mm-ik     motion matching + the same leg IK rig
// For each actor count it reports MotionWeave::update (sequential) and the
// persistent MotionWeaveScheduler, as ms/frame and us/character, median of
// --repeats runs. --phases (default on) micro-benchmarks the building blocks.
// Only uses graph features available on origin/main so the same source can
// measure the engine before and after an optimisation.
#include "tests/animation/HumanoidLocomotionFixture.h"
#include "engine/animation/MotionWeave.h"
#include "engine/render/AnimationMotion.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::motionweave;
namespace fx = aiverse::motionweave_fixture;
namespace aiverse::platform {
bool readContentFile(const std::string&, std::string&) { return false; }
}

namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out; std::stringstream in(s); std::string item;
    while (std::getline(in, item, ',')) if (!item.empty()) out.push_back(item);
    return out;
}
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

std::string ikRig(const std::string& input) {
    return R"({"id":"ik","kind":"rig","input":")" + input + R"(","units":[
      {"op":"twoBoneIK","bone":"thighL","knee":"shinL","end":"footL","target":"footTargetL","pole":"kneePoleL"},
      {"op":"twoBoneIK","bone":"thighR","knee":"shinR","end":"footR","target":"footTargetR","pole":"kneePoleR"}]})";
}
std::string graphJson(const std::string& scenario, int gaits) {
    const std::string params = R"("parameters":{"runBlend":0.5,"footTargetL":[0.1,0.09,0.2],"footTargetR":[-0.1,0.09,-0.15],
      "kneePoleL":[0.1,0.5,2],"kneePoleR":[-0.1,0.5,2],"traj1":[0,0,0.4],"traj2":[0,0,0.8]})";
    if (scenario == "clip") return "{" + params + R"(,"nodes":[{"id":"walk","kind":"clip","clip":"Walk_IP"}],"output":"walk"})";
    const std::string blend = R"({"id":"walk","kind":"clip","clip":"Walk_IP"},{"id":"run","kind":"clip","clip":"Run_IP"},
      {"id":"loco","kind":"blend","a":"walk","b":"run","parameter":"runBlend"})";
    if (scenario == "blend") return "{" + params + R"(,"nodes":[)" + blend + R"(],"output":"loco"})";
    if (scenario == "blend-ik") return "{" + params + R"(,"nodes":[)" + blend + "," + ikRig("loco") + R"(],"output":"ik"})";
    std::string clips = R"("Idle","Walk","Run","Stop","TurnLeft")";
    for (int i = 0; i < gaits; ++i) { char b[24]; std::snprintf(b, sizeof b, ",\"Gait%02d\"", i); clips += b; }
    const std::string mm = R"({"id":"mm","kind":"motionMatching","database":{"root":"root","clips":[)" + clips +
        R"(],"bones":["footL","footR","hips"],"trajectory":[{"time":0.3,"parameter":"traj1"},{"time":0.6,"parameter":"traj2"}],
        "fps":30,"searchInterval":0.1,"blend":0.2,"stripRoot":true}})";
    if (scenario == "mm") return "{" + params + R"(,"nodes":[)" + mm + R"(],"output":"mm"})";
    return "{" + params + R"(,"nodes":[)" + mm + "," + ikRig("mm") + R"(],"output":"ik"})";
}

void makeActors(World& w, const Model& model, const std::shared_ptr<const AnimationGraph>& graph, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const Entity e = w.create();
        auto& asset = w.add<AnimationAssetComponent>(e);
        asset.model = &model; asset.graph = graph;
        w.add<AnimationPlayerComponent>(e);
        w.add<AnimationPoseComponent>(e);
        auto& in = w.add<AnimationInputsComponent>(e);
        const float u = fx::frac(float(i) * .618034f);
        in.overrides.set("runBlend", u);
        const float speed = 1.f + 2.4f * u, turn = (fx::frac(float(i) * .3819f) - .5f) * 1.2f;
        in.overrides.set("traj1", fx::yawQ(turn * .3f).rotate({0, 0, speed * .3f}));
        in.overrides.set("traj2", fx::yawQ(turn * .6f).rotate({0, 0, speed * .6f}));
        in.overrides.set("footTargetL", float3{.1f, .09f + .03f * u, .25f - .4f * u});
        in.overrides.set("footTargetR", float3{-.1f, .09f, -.2f + .35f * u});
        w.add<AnimationPhaseSeedComponent>(e, AnimationPhaseSeedComponent{u});
    }
}

struct Row { std::string scenario; size_t actors = 0; double serialMs = 0, parallelMs = 0; size_t workers = 0; };

Row measure(const std::string& scenario, const Model& model, const std::shared_ptr<const AnimationGraph>& graph, size_t actors, int repeats) {
    const size_t frames = actors <= 1 ? 3000 : actors <= 10 ? 1200 : actors <= 100 ? 240 : 60;
    std::vector<double> serial, parallel; size_t workers = 0;
    for (int r = 0; r < repeats; ++r) {
        World a, b; makeActors(a, model, graph, actors); makeActors(b, model, graph, actors);
        MotionWeaveScheduler scheduler;
        for (int warm = 0; warm < 20; ++warm) { MotionWeave::update(a, 1.f / 60.f); MotionWeave::update(b, 1.f / 60.f, scheduler); }
        auto t0 = Clock::now();
        for (size_t f = 0; f < frames; ++f) MotionWeave::update(a, 1.f / 60.f);
        auto t1 = Clock::now();
        MotionWeaveScheduleStats stats;
        for (size_t f = 0; f < frames; ++f) stats = MotionWeave::update(b, 1.f / 60.f, scheduler);
        auto t2 = Clock::now();
        serial.push_back(ms(t0, t1) / double(frames));
        parallel.push_back(ms(t1, t2) / double(frames));
        workers = stats.workerThreads;
    }
    return {scenario, actors, median(serial), median(parallel), workers};
}

template <class Fn> double perCallUs(int iterations, Fn&& fn) {
    for (int i = 0; i < iterations / 10 + 1; ++i) fn(i);
    std::vector<double> runs;
    for (int r = 0; r < 5; ++r) {
        auto t0 = Clock::now();
        for (int i = 0; i < iterations; ++i) fn(i);
        runs.push_back(ms(t0, Clock::now()) * 1000.0 / iterations);
    }
    return median(runs);
}

volatile float gSink = 0;

void phases(const Model& model, const fx::HumanoidBones& b, int gaits) {
    Pose pose, other; std::vector<mat4> world;
    bindPose(model, pose); bindPose(model, other);
    const AnimClip& walk = model.clips[size_t(model.findClip("Walk_IP"))];
    const AnimClip& run = model.clips[size_t(model.findClip("Run_IP"))];
    std::printf("\n## Building blocks (single thread, %zu bones, us per call, median of 5)\n\n", model.nodes.size());
    std::printf("| block | us/call |\n|---|---:|\n");
    std::printf("| bindPose | %.3f |\n", perCallUs(20000, [&](int) { bindPose(model, pose); gSink = gSink + pose[3].t.y; }));
    std::printf("| sampleClip (53 rot + 2 trans channels) | %.3f |\n", perCallUs(20000, [&](int i) { sampleClip(walk, float(i % 60) / 60.f, pose); gSink = gSink + pose[3].r.x; }));
    std::printf("| worldMatrices | %.3f |\n", perCallUs(20000, [&](int) { worldMatrices(model, pose, world); gSink = gSink + world[20].at(0, 3); }));
    worldMatrices(model, pose, world);
    std::printf("| solveTwoBoneIK (one leg) | %.3f |\n", perCallUs(20000, [&](int i) {
        solveTwoBoneIK(model, pose, world, b.thigh[0], b.shin[0], b.foot[0], {.1f, .09f, .1f + .001f * float(i % 50)}, {.1f, .5f, 2}, 1);
        gSink = gSink + pose[size_t(b.shin[0])].r.x; }));
    // Graph-level costs: instance update+evaluate per scenario.
    for (const char* scenario : {"clip", "blend", "blend-ik", "mm", "mm-ik"}) {
        auto graph = AnimationGraph::compile(model, Json::parse(graphJson(scenario, gaits)));
        AnimationGraphInstance inst; inst.reset(graph);
        inst.parameters().set("runBlend", .4f);
        inst.parameters().set("traj1", float3{.1f, 0, .6f});
        inst.parameters().set("traj2", float3{.3f, 0, 1.2f});
        std::printf("| graph %s: update+evaluate | %.3f |\n", scenario, perCallUs(6000, [&](int) { inst.update(1.f / 60.f); inst.evaluate(pose); gSink = gSink + pose[5].r.y; }));
        if (std::string(scenario) == "mm") {
            const auto& db = *graph->nodes()[0].motion;
            std::vector<float> query; db.features(1, .3f, query);
            float cost = 0;
            std::printf("| MotionDatabase::search (%zu frames x %zu dims) | %.3f |\n", db.frames.size(), db.weights.size(),
                        perCallUs(3000, [&](int i) { query[0] += (i & 1) ? .001f : -.001f; gSink = gSink + float(db.search(query, 1, .3f, cost)); }));
            std::printf("| MotionDatabase::features (query extraction) | %.3f |\n", perCallUs(3000, [&](int i) { db.features(1, float(i % 30) / 30.f, query); gSink = gSink + query[0]; }));
        }
    }
    (void)run; (void)other;
}
} // namespace

int main(int argc, char** argv) {
    std::vector<size_t> counts{1, 10, 100, 1000};
    std::vector<std::string> scenarios{"clip", "blend", "blend-ik", "mm", "mm-ik"};
    int repeats = 3, gaits = 24; bool runPhases = true;
    for (int i = 1; i < argc; ++i) {
        const std::string o = argv[i];
        if (o == "--counts" && i + 1 < argc) { counts.clear(); for (auto& c : split(argv[++i])) counts.push_back(size_t(std::stoul(c))); }
        else if (o == "--scenarios" && i + 1 < argc) scenarios = split(argv[++i]);
        else if (o == "--repeats" && i + 1 < argc) repeats = std::max(1, std::stoi(argv[++i]));
        else if (o == "--gaits" && i + 1 < argc) gaits = std::clamp(std::stoi(argv[++i]), 0, 40);
        else if (o == "--no-phases") runPhases = false;
        else { std::fprintf(stderr, "usage: %s [--counts 1,10,100,1000] [--scenarios ...] [--repeats N] [--gaits N] [--no-phases]\n", argv[0]); return 2; }
    }
    fx::HumanoidBones bones;
    Model model = fx::makeHumanoid(&bones);
    fx::addGaitVariants(model, bones, gaits);
    std::printf("# MotionWeave humanoid CPU bench: %zu bones, %zu clips, %d gait variants\n\n", model.nodes.size(), model.clips.size(), gaits);
    std::printf("| scenario | actors | sequential ms/frame | us/char | scheduler ms/frame | us/char | workers |\n|---|---:|---:|---:|---:|---:|---:|\n");
    for (const auto& s : scenarios) {
        auto graph = AnimationGraph::compile(model, Json::parse(graphJson(s, gaits)));
        for (size_t n : counts) {
            const Row r = measure(s, model, graph, n, repeats);
            std::printf("| %s | %zu | %.4f | %.2f | %.4f | %.2f | %zu |\n", s.c_str(), n, r.serialMs, r.serialMs * 1000 / double(n),
                        r.parallelMs, r.parallelMs * 1000 / double(n), r.workers);
            std::fflush(stdout);
        }
    }
    if (runPhases) phases(model, bones, gaits);
    return 0;
}
