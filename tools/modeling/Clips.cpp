#include "Modeling.h"
#include "engine/render/AnimationGraph.h"
#include <cmath>
namespace aiverse::modeling {
namespace {
Json arr(std::initializer_list<float> v) {
    auto a = Json::array();
    for (float f : v)
        a.push(Json::number(double(f), 8));
    return a;
}
void parameters(AnimationGraphInstance &instance, const Json &p) {
    require(p.isNull() || p.isObject(), "parameters must be object");
    for (auto &[key, v] : p.members()) {
        bool ok = v.isArray() ? instance.parameters().set(key, vector3(v, {}, key))
                              : instance.parameters().set(key, number(v, 0, key));
        require(ok, "invalid graph parameter '" + key + "'");
    }
}
} // namespace
Json sample(const Model &m, const Json &request) {
    float t = number(request["time"], 0, "time");
    require(t >= 0 && t <= 600, "sample time must be 0..600");
    Pose pose;
    bindPose(m, pose);
    if (request["graph"].isObject()) {
        AnimationGraphInstance instance;
        instance.reset(AnimationGraph::compile(m, request["graph"]));
        parameters(instance, request["parameters"]);
        int frames = int(std::ceil(t * 60));
        for (int f = 0; f < frames; ++f)
            instance.update(std::min(1.f / 60.f, t - float(f) / 60.f));
        instance.evaluate(pose);
        require(instance.healthy(), instance.error());
    } else {
        int clip = m.findClip(request.getStr("clip"));
        require(clip >= 0 || request.getStr("clip").empty(), "unknown clip");
        if (clip >= 0)
            sampleClip(m.clips[size_t(clip)], t, pose);
    }
    std::vector<mat4> world;
    worldMatrices(m, pose, world);
    auto out = Json::object(), nodes = Json::array();
    for (size_t i = 0; i < pose.size(); ++i) {
        auto &p = pose[i];
        auto j = Json::object();
        j.set("id", Json::string(m.nodes[i].name));
        j.set("translation", arr({p.t.x, p.t.y, p.t.z}));
        j.set("rotation", arr({p.r.x, p.r.y, p.r.z, p.r.w}));
        j.set("scale", arr({p.s.x, p.s.y, p.s.z}));
        auto pos = world[i].transformPoint({});
        j.set("worldPosition", arr({pos.x, pos.y, pos.z}));
        nodes.push(j);
    }
    out.set("nodes", nodes);
    return out;
}
Json bake(const Json &doc, const Json &request) {
    auto built = build(doc);
    auto &m = *built.model;
    float duration = number(request["duration"], 1, "duration");
    int fps = int(request.getInt("fps", 30));
    require(duration > 0 && duration <= 60 && fps >= 1 && fps <= 120, "bake requires duration 0..60 and fps 1..120");
    int frames = int(std::ceil(duration * float(fps))) + 1;
    require(size_t(frames) * m.nodes.size() * 3 <= 100000, "bake key budget exceeded");
    auto graph = request.has("graph") ? request["graph"] : evaluateDocument(doc)["graph"];
    AnimationGraphInstance instance;
    instance.reset(AnimationGraph::compile(m, graph));
    parameters(instance, request["parameters"]);
    auto clip = Json::object(), channels = Json::array(), times = Json::array();
    for (int f = 0; f < frames; ++f)
        times.push(Json::number(double(std::min(duration, float(f) / float(fps))), 8));
    for (auto &n : m.nodes)
        for (auto path : {"translation", "rotation", "scale"}) {
            auto c = Json::object();
            c.set("node", Json::string(n.name));
            c.set("path", Json::string(path));
            c.set("times", times);
            c.set("values", Json::array());
            channels.push(c);
        }
    Pose pose;
    for (int f = 0; f < frames; ++f) {
        if (f)
            instance.update(float(times[size_t(f)].toDouble() - times[size_t(f - 1)].toDouble()));
        instance.evaluate(pose);
        require(instance.healthy(), instance.error());
        for (size_t n = 0; n < pose.size(); ++n) {
            auto &p = pose[n];
            channels.at(n * 3).member("values").push(arr({p.t.x, p.t.y, p.t.z}));
            channels.at(n * 3 + 1).member("values").push(arr({p.r.x, p.r.y, p.r.z, p.r.w}));
            channels.at(n * 3 + 2).member("values").push(arr({p.s.x, p.s.y, p.s.z}));
        }
    }
    clip.set("id", Json::string(request.getStr("clip", "Baked")));
    clip.set("channels", channels);
    return clip;
}
Json robotDocument() {
    auto doc = emptyDocument();
    doc.member("parameters").set("legLength", Json::number(.7));
    doc.set(
        "materials",
        Json::parse(
            R"([{"id":"armor","color":[0.045,0.26,0.35,1],"metallic":0.7,"roughness":0.3},{"id":"steel","color":[0.25,0.29,0.32,1],"metallic":0.85,"roughness":0.35},{"id":"signal","color":[0.95,0.15,0.025,1],"emissive":[0.8,0.08,0.01]}])"));
    auto &nodes = doc.member("nodes");
    nodes.push(Json::parse(
        R"({"id":"body","translation":[0,1.2,0],"material":"armor","geometry":{"kind":"bevelBox","size":[1.4,0.6,1.8],"bevel":0.12}})"));
    nodes.push(Json::parse(
        R"({"id":"turret","parent":"body","translation":[0,0.45,0.15],"material":"steel","geometry":{"kind":"cylinder","radius":0.38,"height":0.35}})"));
    nodes.push(Json::parse(
        R"({"id":"barrel","parent":"turret","translation":[0,0,0.6],"rotation":[0.70710678,0,0,0.70710678],"material":"steel","geometry":{"kind":"cylinder","radius":0.09,"height":1.0}})"));
    nodes.push(Json::parse(
        R"({"id":"muzzle","parent":"barrel","translation":[0,0.51,0],"material":"signal","geometry":{"kind":"sphere","radius":0.095,"rings":6,"segments":12}})"));
    for (int side : {-1, 1})
        for (int leg = 0; leg < 3; ++leg) {
            std::string name = std::string(side < 0 ? "left" : "right") + std::to_string(leg);
            auto hip = Json::object();
            hip.set("id", Json::string(name));
            hip.set("parent", Json::string("body"));
            hip.set("translation", arr({float(side) * .7f, 0, float(leg - 1) * .65f}));
            hip.set("material", Json::string("steel"));
            hip.set("geometry", Json::parse(R"({"kind":"sphere","radius":0.16,"segments":12,"rings":6})"));
            nodes.push(hip);
            auto upper = Json::parse(
                R"({"material":"armor","geometry":{"kind":"bevelBox","size":[0.8,0.2,0.24],"bevel":0.04}})");
            upper.set("id", Json::string(name + ".upper"));
            upper.set("parent", Json::string(name));
            upper.set("translation", arr({float(side) * .35f, -.125f, 0}));
            auto q = quat::axisAngle({0, 0, 1}, float(side) * -.34f);
            upper.set("rotation", arr({q.x, q.y, q.z, q.w}));
            nodes.push(upper);
            auto knee = Json::parse(
                R"({"material":"steel","geometry":{"kind":"sphere","radius":0.13,"segments":12,"rings":6}})");
            knee.set("id", Json::string(name + ".knee"));
            knee.set("parent", Json::string(name));
            knee.set("translation", arr({float(side) * .7f, -.25f, 0}));
            nodes.push(knee);
            auto shin = Json::parse(
                R"({"material":"steel","geometry":{"kind":"cylinder","radius":0.075,"height":{"param":"legLength"}},"translation":[0,{"param":"legLength","scale":-0.5},0]})");
            shin.set("id", Json::string(name + ".shin"));
            shin.set("parent", Json::string(name + ".knee"));
            nodes.push(shin);
            auto foot = Json::parse(
                R"({"material":"armor","geometry":{"kind":"bevelBox","size":[0.32,0.13,0.4],"bevel":0.04},"translation":[0,{"param":"legLength","scale":-1},0]})");
            foot.set("id", Json::string(name + ".foot"));
            foot.set("parent", Json::string(name + ".knee"));
            nodes.push(foot);
        }
    for (auto kind : {"Idle", "Walk", "Attack", "Death"}) {
        auto clip = Json::object(), tracks = Json::array();
        clip.set("id", Json::string(kind));
        for (int side : {-1, 1})
            for (int leg = 0; leg < 3; ++leg) {
                std::string name = std::string(side < 0 ? "left" : "right") + std::to_string(leg);
                auto c = Json::object(), times = Json::array(), values = Json::array();
                c.set("node", Json::string(name));
                c.set("path", Json::string("rotation"));
                for (int frame = 0; frame <= 24; ++frame) {
                    float t = float(frame) / 24;
                    float phase = float((leg + (side > 0 ? 1 : 0)) % 2) * 3.14159265f;
                    float angle = std::string(kind) == "Walk"     ? std::sin(t * 6.2831853f + phase) * .55f
                                  : std::string(kind) == "Death"  ? t * 1.4f
                                  : std::string(kind) == "Attack" ? std::sin(t * 3.14159265f) * .4f
                                                                  : std::sin(t * 6.2831853f) * .04f;
                    auto q = quat::axisAngle({0, 0, 1}, angle * float(side));
                    times.push(Json::number(double(t), 8));
                    values.push(arr({q.x, q.y, q.z, q.w}));
                }
                c.set("times", times);
                c.set("values", values);
                tracks.push(c);
            }
        clip.set("channels", tracks);
        doc.member("clips").push(clip);
    }
    return doc;
}
} // namespace aiverse::modeling
