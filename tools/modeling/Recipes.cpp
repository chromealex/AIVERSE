#include "Modeling.h"
#include <algorithm>
#include <cmath>
namespace aiverse::modeling {
namespace {
Json arr(std::initializer_list<float> v) {
    auto a = Json::array();
    for (auto f : v)
        a.push(Json::number(double(f), 8));
    return a;
}
float distanceSegment(float3 p, float3 a, float3 b) {
    auto ab = b - a;
    float d = dot(ab, ab);
    float t = d > 1e-8f ? std::clamp(dot(p - a, ab) / d, 0.f, 1.f) : 0;
    return length(p - (a + ab * t));
}
} // namespace
Json autoWeights(const Json &doc, const Json &request) {
    auto next = doc;
    auto node = request.getStr("node"), skin = request.getStr("skin");
    require(!node.empty() && !skin.empty(), "auto weights needs node and skin");
    int index = -1;
    for (size_t i = 0; i < next["nodes"].size(); ++i)
        if (next["nodes"][i].getStr("id") == node) {
            index = int(i);
            auto &n = next.member("nodes").at(i);
            n.set("skin", Json::string(skin));
            n.erase("weights");
        }
    require(index >= 0, "unknown node");
    auto built = build(next);
    auto &m = *built.model;
    auto &n = m.nodes[size_t(index)];
    require(n.mesh >= 0 && n.skin >= 0, "auto weights requires skinned mesh");
    auto &p = m.meshes[size_t(n.mesh)].primitives[0];
    require(p.vertexCount <= 100000, "auto weights limited to 100000 vertices");
    auto &s = m.skins[size_t(n.skin)];
    auto rows = Json::array();
    for (uint32_t i = 0; i < p.vertexCount; ++i) {
        auto pos = m.vertices[p.firstVertex + i].pos;
        std::vector<std::pair<float, int>> nearest;
        for (size_t j = 0; j < s.joints.size(); ++j) {
            int joint = s.joints[j], parent = m.nodes[size_t(joint)].parent;
            auto b = m.bindWorld[size_t(joint)].transformPoint({});
            auto a = parent >= 0 ? m.bindWorld[size_t(parent)].transformPoint({}) : b;
            float dist = distanceSegment(pos, a, b);
            nearest.push_back({dist, int(j)});
        }
        std::sort(nearest.begin(), nearest.end());
        auto row = Json::array();
        float sum = 0;
        size_t count = std::min(size_t(4), nearest.size());
        for (size_t k = 0; k < count; ++k)
            sum += 1 / (nearest[k].first * nearest[k].first + .0001f);
        for (size_t k = 0; k < count; ++k) {
            auto pair = Json::array();
            pair.push(Json::number(int64_t(nearest[k].second)));
            pair.push(Json::number(double((1 / (nearest[k].first * nearest[k].first + .0001f)) / sum), 8));
            row.push(pair);
        }
        rows.push(row);
    }
    next.member("nodes").at(size_t(index)).set("weights", rows);
    return next;
}
Json generateClip(const Json &doc, const Json &request) {
    auto built = build(doc);
    auto &m = *built.model;
    float duration = number(request["duration"], 1, "duration");
    int fps = int(request.getInt("fps", 30));
    require(duration > 0 && duration <= 60 && fps >= 1 && fps <= 120, "generator requires duration 0..60, fps 1..120");
    auto tracks = request["tracks"];
    require(tracks.isArray() && tracks.size() > 0 && tracks.size() <= 512, "tracks must contain 1..512 definitions");
    int frames = int(std::ceil(duration * float(fps))) + 1;
    require(size_t(frames) * tracks.size() <= 100000, "generated key budget exceeded");
    auto clip = Json::object(), channels = Json::array();
    clip.set("id", Json::string(request.getStr("clip", "Generated")));
    for (auto &spec : tracks.items()) {
        auto name = spec.getStr("node"), path = spec.getStr("path", "rotation"), wave = spec.getStr("wave", "sine");
        int ni = m.findNode(name);
        require(ni >= 0, "unknown generator node");
        require(path == "rotation" || path == "translation" || path == "scale", "invalid generated channel path");
        require(wave == "sine" || wave == "spin" || wave == "fold" || wave == "recoil", "unknown motion generator");
        auto axis = vector3(spec["axis"], {0, 0, 1}, "axis");
        require(length(axis) > 1e-6f, "generator axis is zero");
        axis = normalize(axis);
        float amp = number(spec["amplitude"], .3f, "amplitude"), frequency = number(spec["frequency"], 1, "frequency"),
              phase = number(spec["phase"], 0, "phase"), offset = number(spec["offset"], 0, "offset");
        auto c = Json::object(), times = Json::array(), values = Json::array();
        c.set("node", Json::string(name));
        c.set("path", Json::string(path));
        auto &bind = m.nodes[size_t(ni)];
        for (int f = 0; f < frames; ++f) {
            float t = std::min(duration, float(f) / float(fps)), x = t / duration;
            float value = wave == "sine"   ? std::sin(6.2831853f * (t * frequency + phase))
                          : wave == "spin" ? (t * frequency + phase) * 6.2831853f
                          : wave == "fold" ? x * x * (3 - 2 * x)
                                           : std::sin(std::min(x * 4, 1.f) * 3.14159265f);
            value = offset + amp * value;
            times.push(Json::number(double(t), 8));
            if (path == "rotation") {
                auto q = bind.r * quat::axisAngle(axis, value);
                values.push(arr({q.x, q.y, q.z, q.w}));
            } else {
                auto p = (path == "scale" ? bind.s : bind.t) + axis * value;
                values.push(arr({p.x, p.y, p.z}));
            }
        }
        c.set("times", times);
        c.set("values", values);
        channels.push(c);
    }
    clip.set("channels", channels);
    return clip;
}
Json duplicateParts(const Json &doc, const Json &request) {
    auto next = doc;
    auto root = request.getStr("node"), prefix = request.getStr("prefix");
    require(!prefix.empty(), "duplicate needs prefix");
    auto built = build(doc);
    int rootId = built.model->findNode(root);
    require(rootId >= 0, "unknown duplicate root");
    std::vector<bool> chosen(built.model->nodes.size());
    for (int i : built.model->order)
        chosen[size_t(i)] = i == rootId || (built.model->nodes[size_t(i)].parent >= 0 &&
                                            chosen[size_t(built.model->nodes[size_t(i)].parent)]);
    for (size_t i = 0; i < chosen.size(); ++i)
        if (chosen[i]) {
            auto n = doc["nodes"][i];
            require(!n.has("skin"), "duplicate skinned subtree requires explicit skin remapping");
            n.set("id", Json::string(prefix + n.getStr("id")));
            if (int(i) != rootId)
                n.set("parent", Json::string(prefix + n.getStr("parent")));
            else if (request.has("translation"))
                n.set("translation", request["translation"]);
            next.member("nodes").push(n);
        }
    return next;
}
} // namespace aiverse::modeling
