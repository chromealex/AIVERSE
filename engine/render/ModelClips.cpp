#include "ModelClips.h"
#include "Animation.h"
#include "../data/Json.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace aiverse::render {
namespace {
std::vector<std::string> split(const std::string& path) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= path.size()) {
        size_t b = path.find('/', a);
        if (b == std::string::npos) b = path.size();
        if (b > a) out.push_back(path.substr(a, b - a));
        a = b + 1;
    }
    return out;
}
std::string normalizePath(const std::string& p) {   // "a/b/../c" -> "a/c" (pack and asset readers expect clean paths)
    std::vector<std::string> out;
    const bool abs = !p.empty() && p[0] == '/';
    for (auto& s : split(p)) {
        if (s == ".") continue;
        if (s == ".." && !out.empty() && out.back() != "..") out.pop_back();
        else out.push_back(s);
    }
    std::string r = abs ? "/" : "";
    for (size_t i = 0; i < out.size(); ++i) r += (i ? "/" : "") + out[i];
    return r;
}
std::string nodePath(const Model& m, int n) {
    std::string p;
    for (; n >= 0; n = m.nodes[size_t(n)].parent) p = p.empty() ? m.nodes[size_t(n)].name : m.nodes[size_t(n)].name + "/" + p;
    return p;
}
// One channel value at time t (local sampler: Animation.cpp keeps its own private).
NodePose sampleOne(const AnimChannel& c, float t, const NodePose& rest) {
    AnimClip tmp;
    tmp.channels.push_back(c);
    tmp.channels[0].node = 0;
    Pose p{rest};
    sampleClip(tmp, t, p, 1);
    return p[0];
}
void push(AnimChannel& c, const NodePose& p) {
    switch (c.path) {
        case AnimChannel::Translation: c.values.insert(c.values.end(), {p.t.x, p.t.y, p.t.z}); break;
        case AnimChannel::Scale: c.values.insert(c.values.end(), {p.s.x, p.s.y, p.s.z}); break;
        case AnimChannel::Rotation: c.values.insert(c.values.end(), {p.r.x, p.r.y, p.r.z, p.r.w}); break;
        case AnimChannel::Weights: for (int k = 0; k < c.comps && k < kMaxMorphTargets; ++k) c.values.push_back(p.w[k]); break;
    }
}
quat twistAbout(quat q, float3 axis) {   // swing-twist decomposition: rotation of q about `axis`
    const float d = q.x * axis.x + q.y * axis.y + q.z * axis.z;
    quat t{axis.x * d, axis.y * d, axis.z * d, q.w};
    const float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z + t.w * t.w);
    return len < 1e-8f ? quat{} : quat{t.x / len, t.y / len, t.z / len, t.w / len};
}
void rootMotion(Model& m, AnimClip& clip, const Json& j, std::vector<std::string>* warnings) {
    const int node = findNodePath(m, j.getStr("node"));
    if (node < 0) { if (warnings) warnings->push_back(m.path + ": clip '" + clip.name + "': root motion node '" + j.getStr("node") + "' not found"); return; }
    const bool bakeXZ = j.getBool("bakeXZ", false), bakeY = j.getBool("bakeY", false), bakeRot = j.getBool("bakeRotation", false);
    // "Up" of the motion in the node's parent space (the model is Y up).
    const int parent = m.nodes[size_t(node)].parent;
    float3 up{0, 1, 0};
    if (parent >= 0 && size_t(parent) < m.bindWorld.size()) {
        const quat pr = rotationOf(m.bindWorld[size_t(parent)]);
        const quat inv{-pr.x, -pr.y, -pr.z, pr.w};
        up = normalize(inv.rotate(up));
    }
    for (auto& c : clip.channels) {
        if (c.node != node || c.times.empty() || c.interp == AnimChannel::CubicSpline) continue;
        if (c.path == AnimChannel::Translation && !(bakeXZ && bakeY)) {
            const float3 v0{c.values[0], c.values[1], c.values[2]};
            for (size_t k = 0; k < c.times.size(); ++k) {
                float3 v{c.values[k * 3], c.values[k * 3 + 1], c.values[k * 3 + 2]};
                const float3 d = v - v0;
                const float3 vertical = up * dot(d, up), horizontal = d - vertical;
                if (k + 1 == c.times.size() && clip.duration > 0) clip.rootSpeed = length(horizontal) / clip.duration;
                if (!bakeXZ) v = v - horizontal;
                if (!bakeY) v = v - vertical;
                c.values[k * 3] = v.x; c.values[k * 3 + 1] = v.y; c.values[k * 3 + 2] = v.z;
            }
        }
        if (c.path == AnimChannel::Rotation && !bakeRot) {
            const quat q0{c.values[0], c.values[1], c.values[2], c.values[3]};
            const quat q0i{-q0.x, -q0.y, -q0.z, q0.w};
            for (size_t k = 0; k < c.times.size(); ++k) {
                const quat q{c.values[k * 4], c.values[k * 4 + 1], c.values[k * 4 + 2], c.values[k * 4 + 3]};
                const quat tw = twistAbout(q * q0i, up);
                const quat r = qnormalize(quat{-tw.x, -tw.y, -tw.z, tw.w} * q);
                c.values[k * 4] = r.x; c.values[k * 4 + 1] = r.y; c.values[k * 4 + 2] = r.z; c.values[k * 4 + 3] = r.w;
            }
        }
    }
}
// Channels of an external clip model retargeted onto `m` by node path (suffix match).
bool bindExternal(const Model& m, const Model& src, const AnimClip& in, AnimClip& out, std::string& missing) {
    std::map<int, int> map;
    out.channels.clear();
    int unbound = 0;
    for (const auto& c : in.channels) {
        if (c.node < 0) continue;
        auto it = map.find(c.node);
        if (it == map.end()) {
            it = map.emplace(c.node, findNodePath(m, nodePath(src, c.node))).first;
            if (it->second < 0 && ++unbound <= 3) missing += (missing.empty() ? "" : ", ") + nodePath(src, c.node);
        }
        if (it->second < 0) continue;
        AnimChannel k = c;
        k.node = it->second;
        out.channels.push_back(std::move(k));
    }
    out.duration = in.duration;
    if (unbound > 3) missing += ", +" + std::to_string(unbound - 3) + " more";
    return !out.channels.empty();
}
} // namespace

int findNodePath(const Model& m, const std::string& path) {
    const auto parts = split(path);
    if (parts.empty()) return -1;
    // Pass 0: the node's ancestor chain ends with every segment. Pass 1: the path has extra leading
    // segments above the model's root (an Animator placed on a parent of the model instance).
    for (int pass = 0; pass < 2; ++pass)
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            if (m.nodes[i].name != parts.back()) continue;
            int n = m.nodes[i].parent;
            size_t k = parts.size() - 1;
            bool ok = true;
            while (k > 0) {
                if (n < 0) { ok = pass == 1; break; }
                if (m.nodes[size_t(n)].name != parts[k - 1]) { ok = false; break; }
                n = m.nodes[size_t(n)].parent; --k;
            }
            if (ok && (pass == 0 ? k == 0 : true)) return int(i);
        }
    return -1;
}

AnimClip sliceClip(const AnimClip& src, float start, float end) {
    AnimClip out;
    out.name = src.name;
    out.sourceStart = src.sourceStart; out.sourceFps = src.sourceFps;
    end = std::max(end, start);
    out.duration = end - start;
    const NodePose rest{};
    for (const auto& c : src.channels) {
        if (c.times.empty()) continue;
        AnimChannel k;
        k.node = c.node; k.path = c.path; k.comps = c.comps;
        k.interp = c.interp == AnimChannel::Step ? AnimChannel::Step : AnimChannel::Linear;
        std::vector<float> times{start};
        if (c.interp == AnimChannel::CubicSpline) {   // resampled: tangents would need rescaling per segment
            const int n = std::max(1, int(std::ceil((end - start) * 30)));
            for (int i = 1; i <= n; ++i) times.push_back(start + (end - start) * float(i) / float(n));
        } else {
            for (float t : c.times) if (t > start + 1e-6f && t < end - 1e-6f) times.push_back(t);
            if (end > start) times.push_back(end);
        }
        for (float t : times) {
            k.times.push_back(t - start);
            push(k, sampleOne(c, t, rest));
        }
        out.channels.push_back(std::move(k));
    }
    return out;
}

void applyClipSidecar(Model& m, const std::string& dir, const std::string& text, const FileReader& read, std::vector<std::string>* warnings) {
    auto warn = [&](const std::string& s) { if (warnings) warnings->push_back(m.path + ".clips.json: " + s); };
    Json j;
    try { j = Json::parse(text); } catch (const std::exception& e) { warn(e.what()); return; }
    if (!j.isObject() || !j["clips"].isArray()) { warn("expected {\"clips\": [...]}"); return; }
    std::vector<AnimClip> made;
    std::map<std::string, std::unique_ptr<Model>> files;
    for (const auto& cj : j["clips"].items()) {
        const std::string name = cj.getStr("name");
        if (name.empty()) { warn("clip without a name skipped"); continue; }
        AnimClip clip;
        if (cj.has("file")) {
            const std::string file = normalizePath(dir + cj.getStr("file"));
            auto it = files.find(file);
            if (it == files.end()) {
                std::unique_ptr<Model> src;
                try { std::vector<std::string> w; src = loadModel(file, read, &w); }
                catch (const std::exception& e) { warn(std::string("clip '") + name + "': " + e.what()); }
                it = files.emplace(file, std::move(src)).first;
            }
            if (!it->second) continue;
            const Model& src = *it->second;
            const int ci = cj.has("clip") ? src.findClip(cj.getStr("clip")) : (src.clips.empty() ? -1 : 0);
            if (ci < 0) { warn("clip '" + name + "': no clip '" + cj.getStr("clip") + "' in " + file); continue; }
            std::string missing;
            if (!bindExternal(m, src, src.clips[size_t(ci)], clip, missing)) { warn("clip '" + name + "': no node of " + file + " exists in the model (" + missing + ")"); continue; }
            if (!missing.empty()) warn("clip '" + name + "': nodes not in the model: " + missing);
        } else {
            const int ti = m.findClip(cj.getStr("take"));
            if (ti < 0) { warn("clip '" + name + "': take '" + cj.getStr("take") + "' not found"); continue; }
            const AnimClip& take = m.clips[size_t(ti)];
            clip = take;
            const Json& fr = cj["frames"];
            const Json& sec = cj["seconds"];
            if (fr.isArray() && fr.size() == 2) {
                const float fps = cj.has("fps") ? float(cj["fps"].toDouble(30)) : (take.sourceFps > 0 ? take.sourceFps : 30.f);
                clip = sliceClip(take, float(fr[0].toDouble()) / fps - take.sourceStart, float(fr[1].toDouble()) / fps - take.sourceStart);
            } else if (sec.isArray() && sec.size() == 2) {
                clip = sliceClip(take, float(sec[0].toDouble()), float(sec[1].toDouble()));
            }
        }
        clip.name = name;
        if (cj["rootMotion"].isObject()) rootMotion(m, clip, cj["rootMotion"], warnings);
        made.push_back(std::move(clip));
    }
    if (!j.getBool("keepSource", true)) m.clips.clear();
    for (auto& c : made) {
        auto same = std::find_if(m.clips.begin(), m.clips.end(), [&](const AnimClip& x) { return x.name == c.name; });
        if (same != m.clips.end()) *same = std::move(c);
        else m.clips.push_back(std::move(c));
    }
}

bool applyClipSidecar(Model& m, const std::string& path, const FileReader& read, std::vector<std::string>* warnings) {
    std::string text;
    if (!read(path + ".clips.json", text)) return false;
    const size_t slash = path.find_last_of("/\\");
    applyClipSidecar(m, slash == std::string::npos ? std::string() : path.substr(0, slash + 1), text, read, warnings);
    return true;
}
} // namespace aiverse::render
