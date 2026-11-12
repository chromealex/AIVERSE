// aiverse_clipinfo: loads a model the way the game does (loader + "<model>.clips.json") and prints
// JSON for importers and agents: node paths / bind TRS, clips, a sampled clip pose, and a dry run of
// an animation graph (e.g. an imported Unity controller) with parameters. Host tool, no GPU.
//
//   aiverse_clipinfo models/hero.fbx [--nodes] [--sample CLIP TIME]
//                    [--graph animations/hero.json [--set Speed=0.5 ...] [--run 1.5] [--dt 0.0333]]
#include "engine/render/AnimationController.h"
#include "engine/render/AnimationGraph.h"
#include "engine/render/AnimationAsset.h"
#include "engine/render/ModelClips.h"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace aiverse;
using namespace aiverse::render;
namespace aiverse::platform { bool readContentFile(const std::string&, std::string&) { return false; } }

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
static std::string nodePath(const Model& m, int n) {
    std::string p;
    for (; n >= 0; n = m.nodes[size_t(n)].parent) p = p.empty() ? m.nodes[size_t(n)].name : m.nodes[size_t(n)].name + "/" + p;
    return p;
}
static Json vec(std::initializer_list<float> v) { Json a = Json::array(); for (float x : v) a.push(Json::number(double(x), 5)); return a; }
static Json poseJson(const Model& m, const Pose& p) {
    Json a = Json::array();
    for (size_t i = 0; i < p.size(); ++i) {
        Json n = Json::object();
        n.set("path", Json::string(nodePath(m, int(i))));
        n.set("t", vec({p[i].t.x, p[i].t.y, p[i].t.z}));
        n.set("r", vec({p[i].r.x, p[i].r.y, p[i].r.z, p[i].r.w}));
        n.set("s", vec({p[i].s.x, p[i].s.y, p[i].s.z}));
        a.push(std::move(n));
    }
    return a;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: aiverse_clipinfo MODEL [--nodes] [--sample CLIP TIME] [--graph FILE [--set k=v] [--run S] [--dt S]]\n"); return 2; }
    std::string modelPath = argv[1], sampleClipName, graphPath;
    float sampleTime = 0, run = 0, dt = 1.f / 30;
    bool nodes = false;
    std::vector<std::pair<std::string, float>> sets;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); } return argv[++i]; };
        if (a == "--nodes") nodes = true;
        else if (a == "--sample") { sampleClipName = next(); sampleTime = std::stof(next()); }
        else if (a == "--graph") graphPath = next();
        else if (a == "--run") run = std::stof(next());
        else if (a == "--dt") dt = std::stof(next());
        else if (a == "--set") { std::string kv = next(); auto eq = kv.find('='); if (eq == std::string::npos) return 2; sets.push_back({kv.substr(0, eq), std::stof(kv.substr(eq + 1))}); }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    Json out = Json::object();
    out.set("model", Json::string(modelPath));
    std::vector<std::string> warnings;
    std::unique_ptr<Model> m;
    try { m = loadModel(modelPath, readFile, &warnings); }
    catch (const std::exception& e) { out.set("error", Json::string(e.what())); std::printf("%s\n", out.dump().c_str()); return 1; }
    Json w = Json::array(); for (auto& s : warnings) w.push(Json::string(s)); out.set("warnings", std::move(w));
    out.set("nodeCount", Json::number(int64_t(m->nodes.size())));
    if (nodes) { Pose bind; bindPose(*m, bind); out.set("nodes", poseJson(*m, bind)); }
    Json clips = Json::array();
    for (const auto& c : m->clips) {
        Json j = Json::object();
        j.set("name", Json::string(c.name));
        j.set("duration", Json::number(double(c.duration), 4));
        j.set("channels", Json::number(int64_t(c.channels.size())));
        if (c.rootSpeed > 0) j.set("rootSpeed", Json::number(double(c.rootSpeed), 4));
        if (c.sourceFps > 0) { j.set("sourceStart", Json::number(double(c.sourceStart), 4)); j.set("sourceFps", Json::number(double(c.sourceFps), 3)); }
        clips.push(std::move(j));
    }
    out.set("clips", std::move(clips));
    int rc = 0;
    if (!sampleClipName.empty()) {
        const int ci = m->findClip(sampleClipName);
        if (ci < 0) { out.set("sampleError", Json::string("no clip '" + sampleClipName + "'")); rc = 1; }
        else { Pose p; bindPose(*m, p); sampleClip(m->clips[size_t(ci)], sampleTime, p); out.set("sample", poseJson(*m, p)); }
    }
    if (!graphPath.empty()) {
        Json g = Json::object();
        try {
            std::string text;
            if (!readFile(graphPath, text)) throw std::runtime_error(graphPath + ": not found");
            auto def = parseAnimationAsset(text);
            auto graph = AnimationGraph::compile(*m, def);
            AnimationGraphInstance inst; inst.reset(graph);
            for (auto& [k, v] : sets) if (!inst.parameters().set(k, v)) throw std::runtime_error("cannot set parameter '" + k + "'");
            for (float t = 0; t < run; t += dt) inst.update(dt);
            Pose p; inst.evaluate(p);
            g.set("ok", Json::fromBool(true));
            g.set("state", Json::string(inst.state()));
            Json layers = Json::array();
            for (size_t i = 0; i < graph->nodes().size(); ++i) {
                const auto& n = graph->nodes()[i];
                if (!n.controller) continue;
                const auto* pl = inst.controller(n.id);
                for (size_t l = 0; l < n.controller->layers().size(); ++l) {
                    Json lj = Json::object();
                    lj.set("node", Json::string(n.id));
                    lj.set("layer", Json::string(n.controller->layers()[l].name));
                    lj.set("state", Json::string(pl->state(l)));
                    lj.set("time", Json::number(double(pl->normalizedTime(l)), 4));
                    lj.set("transition", Json::number(double(pl->transition(l)), 3));
                    layers.push(std::move(lj));
                }
            }
            g.set("layers", std::move(layers));
            if (nodes) g.set("pose", poseJson(*m, p));
        } catch (const std::exception& e) { g.set("ok", Json::fromBool(false)); g.set("error", Json::string(e.what())); rc = 1; }
        out.set("graph", std::move(g));
    }
    std::printf("%s\n", out.dump().c_str());
    return rc;
}
