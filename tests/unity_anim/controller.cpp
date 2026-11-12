// Mecanim-style controller node and clip sidecars (tools/importers/unity_anim targets).
#include "engine/render/AnimationGraph.h"
#include "engine/render/AnimationController.h"
#include "engine/render/ModelClips.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
using namespace aiverse;
using namespace aiverse::render;
namespace aiverse::platform { bool readContentFile(const std::string&, std::string&) { return false; } }
static int checks = 0;
static void check(bool ok, const char* message) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
static bool near(float a, float b, float eps = .003f) { return std::abs(a - b) < eps; }

static AnimChannel track(int node, AnimChannel::Path path, std::vector<float> times, std::vector<float> values) {
    AnimChannel c; c.node = node; c.path = path; c.times = std::move(times); c.values = std::move(values);
    c.comps = path == AnimChannel::Rotation ? 4 : 3;
    return c;
}
static AnimClip clip(const char* name, float duration, std::vector<AnimChannel> ch) {
    AnimClip a; a.name = name; a.duration = duration; a.channels = std::move(ch); return a;
}
// root(0) -> hips(1) -> spine(2) -> arm(3); hips -> leg(4)
static Model model() {
    Model m; m.nodes.resize(5); m.order = {0, 1, 2, 3, 4};
    const char* names[] = {"root", "hips", "spine", "arm", "leg"};
    const int parents[] = {-1, 0, 1, 2, 1};
    for (int i = 0; i < 5; ++i) { m.nodes[size_t(i)].name = names[i]; m.nodes[size_t(i)].parent = parents[i]; if (parents[i] >= 0) m.nodes[size_t(parents[i])].children.push_back(i); }
    using P = AnimChannel;
    m.clips.push_back(clip("Idle", 1, {track(1, P::Translation, {0, 1}, {0, 0, 0, 0, 0, 0})}));
    m.clips.push_back(clip("Walk", 1, {track(1, P::Translation, {0, 1}, {0, 0, 0, 1, 0, 0})}));
    m.clips.push_back(clip("Run", .5f, {track(1, P::Translation, {0, .5f}, {0, 0, 0, 2, 0, 0})}));
    m.clips.push_back(clip("Attack", 1, {track(3, P::Translation, {0, 1}, {0, 0, 0, 0, 1, 0})}));
    m.clips.push_back(clip("Fwd", 1, {track(1, P::Translation, {0, 1}, {0, 0, 1, 0, 0, 1})}));
    m.clips.push_back(clip("Back", 1, {track(1, P::Translation, {0, 1}, {0, 0, -1, 0, 0, -1})}));
    m.clips.push_back(clip("Left", 1, {track(1, P::Translation, {0, 1}, {-1, 0, 0, -1, 0, 0})}));
    m.clips.push_back(clip("Right", 1, {track(1, P::Translation, {0, 1}, {1, 0, 0, 1, 0, 0})}));
    // Additive source: spine rises 0.5 -> 1.5 (first frame 0.5 is the reference); legs move too.
    m.clips.push_back(clip("Breathe", 1, {track(2, P::Translation, {0, 1}, {0, .5f, 0, 0, 1.5f, 0}), track(4, P::Translation, {0, 1}, {0, 0, 0, 0, 3, 0})}));
    m.finalize();
    return m;
}
struct Rig {
    std::shared_ptr<const AnimationGraph> graph;
    AnimationGraphInstance inst;
    Pose pose;
    void build(const Model& m, const Json& j) { graph = AnimationGraph::compile(m, j); inst.reset(graph); }
    void step(float dt) { inst.update(dt); inst.evaluate(pose); }
    const AnimationControllerPlayer& player() { return *inst.controller("animator"); }
};
static Json graphWith(const char* params, const char* controller) {
    std::string s = std::string("{\"parameters\":") + params + ",\"nodes\":[{\"id\":\"animator\",\"kind\":\"controller\",\"controller\":" + controller + "}],\"output\":\"animator\"}";
    return Json::parse(s);
}

int main() {
    const Model m = model();
    Rig r;

    // ---- 1D blend tree: shared normalized clock, duration = weighted average
    r.build(m, graphWith(R"({"Speed":0.5})", R"({"layers":[{"name":"Base","states":[{"name":"Move","motion":{"tree":{"type":"1d","parameter":"Speed",
        "children":[{"motion":{"clip":"Walk"},"threshold":0},{"motion":{"clip":"Run"},"threshold":1}]}}}]}]})"));
    r.step(.375f);
    check(near(r.player().normalizedTime(0), .5f), "1D tree: normalized time over the blended duration (0.75 s)");
    check(near(r.pose[1].t.x, .75f), "1D tree: phase-synchronized children");
    r.inst.parameters().set("Speed", 2.f); r.inst.evaluate(r.pose);
    check(near(r.pose[1].t.x, 1.f), "1D tree: parameter above the last threshold clamps");

    // ---- 2D freeform directional (+ centre motion) and cartesian
    const char* dir = R"({"layers":[{"name":"Base","states":[{"name":"Move","motion":{"tree":{"type":"freeform2d","parameter":"X","parameterY":"Z",
        "children":[{"motion":{"clip":"Idle"},"position":[0,0]},{"motion":{"clip":"Fwd"},"position":[0,1]},{"motion":{"clip":"Back"},"position":[0,-1]},
                    {"motion":{"clip":"Left"},"position":[-1,0]},{"motion":{"clip":"Right"},"position":[1,0]}]}}}]}]})";
    r.build(m, graphWith(R"({"X":0,"Z":1})", dir)); r.step(.1f);
    check(near(r.pose[1].t.z, 1) && near(r.pose[1].t.x, 0), "2D directional: exact sample position");
    r.inst.parameters().set("Z", .5f); r.inst.evaluate(r.pose);
    check(near(r.pose[1].t.z, .5f) && near(r.pose[1].t.x, 0), "2D directional: halfway to the centre");
    r.inst.parameters().set("X", .7071f); r.inst.parameters().set("Z", .7071f); r.inst.evaluate(r.pose);
    check(near(r.pose[1].t.x, r.pose[1].t.z) && r.pose[1].t.x > .4f, "2D directional: diagonal mixes neighbours evenly");
    std::vector<float> w;
    AnimationParameters probe = r.graph->defaults(); probe.set("X", 0.f); probe.set("Z", 0.f);
    r.graph->nodes()[0].controller->weights(*r.graph->nodes()[0].controller->layers()[0].states[0].motion, probe, w);
    check(near(w[0], 1), "2D directional: origin selects the centre motion");
    std::string cart = dir; cart.replace(cart.find("freeform2d"), 10, "cartesian2d");
    r.build(m, graphWith(R"({"X":0.5,"Z":0})", cart.c_str())); r.step(.1f);
    check(near(r.pose[1].t.x, .5f, .02f), "2D cartesian: halfway between centre and right");

    // ---- transitions: conditions, exit time, fixed fade, offset, time parameter
    const char* sm = R"({"layers":[{"name":"Base","default":"Idle","states":[
        {"name":"Idle","motion":{"clip":"Walk"},"transitions":[{"to":"Run","duration":0.2,"conditions":[{"parameter":"Speed","mode":"greater","threshold":0.5}]}]},
        {"name":"Run","motion":{"clip":"Run"},"transitions":[{"to":"Idle","hasExitTime":true,"exitTime":0.75,"duration":0,"offset":0.5}]},
        {"name":"Swing","motion":{"clip":"Attack","loop":false},"timeParameter":"Progress","transitions":[{"to":"Idle","duration":0,"conditions":[{"parameter":"Progress","mode":"less","threshold":0.001}]}]}],
        "anyState":[{"to":"Swing","duration":0,"canTransitionToSelf":false,"conditions":[{"parameter":"Progress","mode":"greater","threshold":0.001}]}]}]})";
    r.build(m, graphWith(R"({"Speed":0,"Progress":0})", sm));
    r.step(.25f);
    check(r.player().state(0) == "Idle" && near(r.pose[1].t.x, .25f), "default state plays");
    r.inst.parameters().set("Speed", 1.f); r.step(.1f);
    check(r.player().state(0) == "Run" && near(r.player().transition(0), 0), "condition starts a transition");
    r.step(.1f);
    check(near(r.player().transition(0), .5f), "fixed duration fade in seconds");
    r.step(.1f);
    check(r.player().transition(0) < 0 && r.player().state(0) == "Run", "transition completes");
    const float before = r.player().normalizedTime(0);
    r.step(.05f);
    check(r.player().state(0) == "Run" && before < .75f, "exit time not reached yet");
    r.step(.2f);
    check(r.player().state(0) == "Idle" && near(r.player().normalizedTime(0), .5f), "exit time fires; offset sets the destination time");
    r.inst.parameters().set("Speed", 0.f); r.inst.parameters().set("Progress", .4f); r.step(.016f);
    check(r.player().state(0) == "Swing" && near(r.pose[3].t.y, .4f), "any-state transition; time parameter drives normalized time");
    r.inst.parameters().set("Progress", .9f); r.step(.016f);
    check(r.player().state(0) == "Swing" && near(r.pose[3].t.y, .9f), "canTransitionToSelf=false keeps the state");
    r.inst.parameters().set("Progress", 0.f); r.step(.016f);
    check(r.player().state(0) == "Idle", "progress reset returns");

    // ---- triggers latch on change and are consumed
    const char* trig = R"({"triggers":["Hit"],"layers":[{"name":"Base","states":[
        {"name":"Idle","motion":{"clip":"Idle"},"transitions":[{"to":"Hurt","conditions":[{"parameter":"Hit","mode":"if"}]}]},
        {"name":"Hurt","motion":{"clip":"Attack","loop":false},"transitions":[{"to":"Idle","hasExitTime":true,"exitTime":1}]}]}]})";
    r.build(m, graphWith(R"({"Hit":0})", trig));
    r.inst.parameters().set("Hit", 1.f); r.step(.01f);
    check(r.player().state(0) == "Hurt" && !r.player().triggerLatched("Hit"), "trigger fires and is consumed");
    r.step(1.1f); r.step(.01f);
    check(r.player().state(0) == "Idle", "held trigger value does not fire again");
    r.inst.parameters().set("Hit", 2.f); r.step(.01f);
    check(r.player().state(0) == "Hurt", "changed trigger value fires again");

    // ---- layers: masked override with weight parameter, empty state passes, additive first-frame delta
    const char* layers = R"({"layers":[
        {"name":"Base","states":[{"name":"Walk","motion":{"clip":"Walk"}}]},
        {"name":"Upper","weight":1,"weightParameter":"upperWeight","mask":{"hips/spine":1,"spine/arm":1},"states":[
            {"name":"None","motion":null,"transitions":[{"to":"Shoot","duration":0,"conditions":[{"parameter":"shoot","mode":"if"}]}]},
            {"name":"Shoot","motion":{"clip":"Attack"}}]},
        {"name":"Breath","blending":"additive","weight":0.5,"states":[{"name":"B","motion":{"clip":"Breathe"}}]}]})";
    r.build(m, graphWith(R"({"upperWeight":1,"shoot":0})", layers));
    r.step(.5f);
    check(near(r.pose[1].t.x, .5f) && near(r.pose[3].t.y, 0), "empty state in an override layer passes the base through");
    check(near(r.pose[2].t.y, .25f) && near(r.pose[4].t.y, .75f), "additive layer adds (clip - first frame) * weight");
    r.inst.parameters().set("shoot", 1.f); r.step(.0f); r.step(.25f);
    check(r.player().state(1) == "Shoot" && near(r.pose[3].t.y, .25f) && near(r.pose[1].t.x, .75f), "masked layer overrides only its bones");
    r.inst.parameters().set("upperWeight", .5f); r.inst.evaluate(r.pose);
    check(near(r.pose[3].t.y, .125f), "layer weight parameter scales the override");

    // ---- exit transition and destination interruption
    const char* exits = R"({"layers":[{"name":"Base","default":"A","states":[
        {"name":"A","motion":{"clip":"Idle"},"transitions":[{"to":"B","duration":0.5,"interruption":"destination","conditions":[{"parameter":"go","mode":"if"}]}]},
        {"name":"B","motion":{"clip":"Walk"},"transitions":[{"to":"C","duration":0.5,"conditions":[{"parameter":"far","mode":"if"}]},{"exit":true,"duration":0,"conditions":[{"parameter":"home","mode":"if"}]}]},
        {"name":"C","motion":{"clip":"Fwd"}}]}]})";
    r.build(m, graphWith(R"({"go":0,"far":0,"home":0})", exits));
    r.inst.parameters().set("go", 1.f); r.step(.01f); r.step(.2f);
    r.inst.parameters().set("far", 1.f); r.step(.01f);
    check(r.player().state(0) == "C" && r.player().transition(0) >= 0, "destination state interrupts a running transition");
    r.step(.6f);
    check(r.player().state(0) == "C" && r.player().transition(0) < 0, "interrupting transition completes");
    r.build(m, graphWith(R"({"go":1,"far":0,"home":0})", exits)); r.step(.01f); r.step(.6f);
    r.inst.parameters().set("home", 1.f); r.inst.parameters().set("go", 0.f); r.step(.01f);
    check(r.player().state(0) == "A", "exit transition returns to the default state");

    // ---- derived inputs from RtsView built-ins
    r.build(m, graphWith(R"({"speed":0,"Speed":0})", R"({"inputs":{"Speed":{"from":"speed","scale":0.5,"max":1}},"layers":[{"name":"Base","states":[{"name":"Move","motion":{"tree":{"type":"1d","parameter":"Speed",
        "children":[{"motion":{"clip":"Idle"},"threshold":0},{"motion":{"clip":"Walk"},"threshold":1}]}}}]}]})"));
    r.inst.parameters().set("speed", 1.f); r.step(.5f);
    check(near(r.inst.parameters().values.at("Speed"), .5f) && near(r.pose[1].t.x, .25f), "input maps speed onto the imported parameter");

    // ---- validation
    auto rejects = [&](const char* params, const char* c, const char* label) {
        bool caught = false;
        try { AnimationGraph::compile(m, graphWith(params, c)); } catch (const std::exception&) { caught = true; }
        check(caught, label);
    };
    rejects("{}", R"({"layers":[{"name":"B","states":[{"name":"S","motion":{"clip":"Nope"}}]}]})", "unknown clip rejected");
    rejects("{}", R"({"layers":[{"name":"B","states":[{"name":"S","speedParameter":"x","motion":null}]}]})", "undeclared parameter rejected");
    rejects("{}", R"({"layers":[{"name":"B","states":[{"name":"S","transitions":[{"to":"T"}]}]}]})", "unknown destination rejected");
    rejects("{}", R"({"layers":[]})", "empty layers rejected");

    // ---- clip sidecar: frame-range split of a take, root motion, keepSource, node paths
    {
        Model s = model();
        s.clips.push_back(clip("Take 001", 4, {track(1, AnimChannel::Translation, {0, 4}, {0, 0, 0, 8, 4, 0})}));
        s.clips.back().sourceStart = 1; s.clips.back().sourceFps = 10;
        const char* side = R"({"format":"aiverse-clips","version":1,"keepSource":false,"clips":[
            {"name":"Part","take":"Take 001","frames":[20,30]},
            {"name":"InPlace","take":"Take 001","seconds":[0,2],"rootMotion":{"node":"root/hips","bakeY":true}},
            {"name":"Missing","take":"Nope"}]})";
        std::vector<std::string> warn;
        applyClipSidecar(s, "", side, [](const std::string&, std::string&) { return false; }, &warn);
        check(s.clips.size() == 2 && s.findClip("Walk") < 0, "keepSource=false keeps only listed clips");
        const AnimClip& part = s.clips[size_t(s.findClip("Part"))];
        Pose p; bindPose(s, p); sampleClip(part, 0, p);
        check(near(part.duration, 1) && near(p[1].t.x, 2) && near(p[1].t.y, 1), "frames convert through the take's fps and start");
        bindPose(s, p); sampleClip(part, 1, p);
        check(near(p[1].t.x, 4), "split end key interpolated");
        const AnimClip& inPlace = s.clips[size_t(s.findClip("InPlace"))];
        bindPose(s, p); sampleClip(inPlace, 2, p);
        check(near(p[1].t.x, 0) && near(p[1].t.y, 2) && near(inPlace.rootSpeed, 2), "root motion: XZ removed, baked Y kept, speed recorded");
        check(warn.size() == 1, "missing take is a warning");
        check(findNodePath(s, "hips/spine/arm") == 3 && findNodePath(s, "spine/leg") < 0 && findNodePath(s, "leg") == 4, "suffix node paths");
    }
    std::printf("Unity animation checks: %d passed\n", checks);
    return 0;
}
