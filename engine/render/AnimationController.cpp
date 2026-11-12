#include "AnimationController.h"
#include "AnimationGraph.h"
#include "ModelClips.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::render {
namespace {
using C = AnimationController;
[[noreturn]] void bad(const std::string& s) { throw std::runtime_error("controller: " + s); }
float num(const Json& j, float fallback) {
    if (j.isNull()) return fallback;
    if (j.type() == Json::Type::Bool) return j.boolean() ? 1.f : 0.f;
    if (!j.isNumber()) bad("expected a number");
    const double n = j.toDouble();
    if (!std::isfinite(n) || std::abs(n) > 1000000) bad("number outside finite range +/-1000000");
    return float(n);
}
float value(const AnimationParameters& p, const std::string& name, float fallback) {
    if (name.empty()) return fallback;
    auto i = p.values.find(name);
    return i == p.values.end() || !std::isfinite(i->second) ? fallback : i->second;
}
void blendPose(Pose& a, const Pose& b, float w) {
    if (w <= 0) return;
    if (w >= 1) { a = b; return; }
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        auto& x = a[i]; const auto& y = b[i];
        x.t = x.t + (y.t - x.t) * w; x.s = x.s + (y.s - x.s) * w; x.r = nlerp(x.r, y.r, w);
        for (int k = 0; k < kMaxMorphTargets; ++k) x.w[k] += (y.w[k] - x.w[k]) * w;
    }
}
float signedAngle(float ax, float ay, float bx, float by) {
    if ((ax == 0 && ay == 0) || (bx == 0 && by == 0)) return 0;
    return std::atan2(ax * by - ay * bx, ax * bx + ay * by);
}

struct Compiler {
    const Model& model;
    const AnimationParameters& params;
    std::vector<std::string>& triggers;
    void scalar(const std::string& name, const char* what) {
        if (!name.empty() && !params.values.count(name)) bad(std::string(what) + ": undeclared parameter '" + name + "'");
    }
    std::shared_ptr<const C::Motion> motion(const Json& j, size_t depth) {
        if (j.isNull()) return nullptr;
        if (!j.isObject()) bad("motion must be an object or null");
        if (depth > C::kMaxDepth) bad("blend tree depth exceeds 8");
        auto m = std::make_shared<C::Motion>();
        if (j.has("clip")) {
            m->type = C::Motion::Type::Clip;
            m->clip = model.findClip(j.getStr("clip"));
            if (m->clip < 0) bad("unknown clip '" + j.getStr("clip") + "'");
            m->loop = j.getBool("loop", true);
            return m;
        }
        const Json& t = j["tree"];
        if (!t.isObject()) bad("motion needs 'clip' or 'tree'");
        const std::string type = t.getStr("type", "1d");
        if (type == "1d") m->type = C::Motion::Type::Tree1D;
        else if (type == "simple2d" || type == "freeform2d") m->type = C::Motion::Type::Directional2D;
        else if (type == "cartesian2d") m->type = C::Motion::Type::Cartesian2D;
        else if (type == "direct") m->type = C::Motion::Type::Direct;
        else bad("unknown blend tree type '" + type + "'");
        m->parameter = t.getStr("parameter"); m->parameterY = t.getStr("parameterY");
        m->normalize = t.getBool("normalize", false);
        if (m->type != C::Motion::Type::Direct) scalar(m->parameter, "blend tree");
        if (m->type == C::Motion::Type::Directional2D || m->type == C::Motion::Type::Cartesian2D) scalar(m->parameterY, "blend tree");
        if (!t["children"].isArray() || t["children"].size() > C::kMaxChildren) bad("blend tree children must be an array of at most 64");
        for (const auto& c : t["children"].items()) {
            C::Child ch;
            ch.motion = motion(c["motion"], depth + 1);
            ch.threshold = num(c["threshold"], 0);
            if (c["position"].isArray() && c["position"].size() == 2) { ch.x = num(c["position"][0], 0); ch.y = num(c["position"][1], 0); }
            ch.timeScale = num(c["timeScale"], 1);
            if (!(ch.timeScale > 0)) bad("child timeScale must be positive");
            ch.cycleOffset = num(c["cycleOffset"], 0);
            ch.direct = c.getStr("directParameter");
            if (m->type == C::Motion::Type::Direct) scalar(ch.direct, "direct blend");
            m->children.push_back(std::move(ch));
        }
        if (m->type == C::Motion::Type::Tree1D)
            std::stable_sort(m->children.begin(), m->children.end(), [](const C::Child& a, const C::Child& b) { return a.threshold < b.threshold; });
        return m;
    }
    C::Mode mode(const std::string& s) {
        if (s == "if") return C::Mode::If;
        if (s == "ifNot") return C::Mode::IfNot;
        if (s == "greater") return C::Mode::Greater;
        if (s == "less") return C::Mode::Less;
        if (s == "equals") return C::Mode::Equals;
        if (s == "notEqual") return C::Mode::NotEqual;
        bad("unknown condition mode '" + s + "'");
    }
    C::Transition transition(const Json& j, const std::vector<std::string>& names) {
        C::Transition t;
        t.exit = j.getBool("exit", false);
        if (!t.exit) {
            const std::string to = j.getStr("to");
            auto it = std::find(names.begin(), names.end(), to);
            if (it == names.end()) bad("unknown destination state '" + to + "'");
            t.to = int(it - names.begin());
        }
        t.hasExitTime = j.getBool("hasExitTime", false);
        t.exitTime = num(j["exitTime"], 0);
        t.duration = num(j["duration"], 0);
        t.fixedDuration = j.getBool("fixedDuration", true);
        t.offset = num(j["offset"], 0);
        t.canTransitionToSelf = j.getBool("canTransitionToSelf", true);
        t.orderedInterruption = j.getBool("orderedInterruption", true);
        if (t.duration < 0 || t.exitTime < 0) bad("negative transition time");
        const std::string in = j.getStr("interruption", "none");
        t.interruption = in == "source" ? C::Interruption::Source : in == "destination" ? C::Interruption::Destination :
            in == "sourceThenDestination" ? C::Interruption::SourceThenDestination :
            in == "destinationThenSource" ? C::Interruption::DestinationThenSource : C::Interruption::None;
        if (in != "none" && t.interruption == C::Interruption::None) bad("unknown interruption '" + in + "'");
        if (!j["conditions"].isNull() && (!j["conditions"].isArray() || j["conditions"].size() > 32)) bad("conditions must be an array of at most 32");
        for (const auto& c : j["conditions"].items()) {
            C::Condition k{c.getStr("parameter"), mode(c.getStr("mode", "if")), num(c["threshold"], 0)};
            if (k.parameter.empty()) bad("empty condition parameter");
            scalar(k.parameter, "condition");
            t.conditions.push_back(std::move(k));
        }
        return t;
    }
};
} // namespace

std::shared_ptr<const AnimationController> AnimationController::compile(const Model& model, const Json& j, const AnimationParameters& params) {
    if (!j.isObject()) bad("expected an object");
    auto c = std::make_shared<AnimationController>();
    c->model_ = &model;
    for (const auto& t : j["triggers"].items()) {
        if (!t.isString() || !params.values.count(t.str())) bad("trigger '" + (t.isString() ? t.str() : std::string("?")) + "' must be a declared parameter");
        c->triggers_.push_back(t.str());
    }
    Compiler cc{model, params, c->triggers_};
    if (j.has("inputs") && !j["inputs"].isObject()) bad("inputs must be an object");
    for (const auto& [target, ij] : j["inputs"].members()) {
        Input in;
        in.target = target; in.from = ij.getStr("from");
        if (in.from.empty()) bad("input '" + target + "': missing 'from'");
        cc.scalar(target, "input"); cc.scalar(in.from, "input");
        in.scale = num(ij["scale"], 1); in.offset = num(ij["offset"], 0);
        in.min = num(ij["min"], -1000000); in.max = num(ij["max"], 1000000);
        if (in.min > in.max) bad("input '" + target + "': min > max");
        c->inputs_.push_back(std::move(in));
    }
    if (!j["layers"].isArray() || j["layers"].size() == 0 || j["layers"].size() > 16) bad("layers must contain 1..16 entries");
    for (const auto& lj : j["layers"].items()) {
        Layer L;
        L.name = lj.getStr("name");
        L.weight = std::clamp(num(lj["weight"], 1), 0.f, 1.f);
        L.weightParameter = lj.getStr("weightParameter"); cc.scalar(L.weightParameter, "layer weight");
        const std::string blending = lj.getStr("blending", "override");
        if (blending != "override" && blending != "additive") bad("layer '" + L.name + "': blending must be override or additive");
        L.additive = blending == "additive";
        if (lj.has("mask")) {
            if (!lj["mask"].isObject()) bad("layer '" + L.name + "': mask must map node paths to weights");
            L.mask.assign(model.nodes.size(), std::clamp(num(lj["maskDefault"], 0), 0.f, 1.f));
            for (const auto& [path, w] : lj["mask"].members()) {
                const int n = findNodePath(model, path);
                if (n >= 0) L.mask[size_t(n)] = std::clamp(num(w, 1), 0.f, 1.f); // unknown paths: masked bones absent from this model
            }
        }
        if (!lj["states"].isArray() || lj["states"].size() == 0 || lj["states"].size() > 128) bad("layer '" + L.name + "': states must contain 1..128 entries");
        std::vector<std::string> names;
        for (const auto& sj : lj["states"].items()) {
            const std::string n = sj.getStr("name");
            if (n.empty() || std::find(names.begin(), names.end(), n) != names.end()) bad("layer '" + L.name + "': empty or duplicate state '" + n + "'");
            names.push_back(n);
        }
        for (const auto& sj : lj["states"].items()) {
            State s;
            s.name = sj.getStr("name");
            s.motion = cc.motion(sj["motion"], 0);
            s.speed = num(sj["speed"], 1);
            s.cycleOffset = num(sj["cycleOffset"], 0);
            s.speedParameter = sj.getStr("speedParameter"); cc.scalar(s.speedParameter, "state speed");
            s.timeParameter = sj.getStr("timeParameter"); cc.scalar(s.timeParameter, "state time");
            s.cycleOffsetParameter = sj.getStr("cycleOffsetParameter"); cc.scalar(s.cycleOffsetParameter, "state cycle offset");
            if (!sj["transitions"].isNull() && (!sj["transitions"].isArray() || sj["transitions"].size() > 64)) bad("state '" + s.name + "': at most 64 transitions");
            for (const auto& tj : sj["transitions"].items()) s.transitions.push_back(cc.transition(tj, names));
            L.states.push_back(std::move(s));
        }
        if (!lj["anyState"].isNull() && (!lj["anyState"].isArray() || lj["anyState"].size() > 64)) bad("anyState: at most 64 transitions");
        for (const auto& tj : lj["anyState"].items()) L.anyState.push_back(cc.transition(tj, names));
        const std::string def = lj.getStr("default", names[0]);
        auto it = std::find(names.begin(), names.end(), def);
        if (it == names.end()) bad("layer '" + L.name + "': unknown default state '" + def + "'");
        L.defaultState = int(it - names.begin());
        c->layers_.push_back(std::move(L));
    }
    return c;
}

namespace {
void computeWeights(const C::Motion& m, const AnimationParameters& p, float* w) {
    const size_t n = m.children.size();
    std::fill(w, w + n, 0.f);
    if (n == 0) return;
    if (m.type == C::Motion::Type::Direct) {
        float sum = 0;
        for (size_t i = 0; i < n; ++i) { w[i] = std::max(0.f, value(p, m.children[i].direct, 0)); sum += w[i]; }
        if (m.normalize && sum > 0) for (size_t i = 0; i < n; ++i) w[i] /= sum;
        return;
    }
    const float x = value(p, m.parameter, 0);
    if (m.type == C::Motion::Type::Tree1D) {
        if (n == 1 || x <= m.children[0].threshold) { w[0] = 1; return; }
        if (x >= m.children[n - 1].threshold) { w[n - 1] = 1; return; }
        for (size_t i = 0; i + 1 < n; ++i) {
            const float a = m.children[i].threshold, b = m.children[i + 1].threshold;
            if (x >= a && x <= b) {
                const float t = b > a ? (x - a) / (b - a) : 0;
                w[i] = 1 - t; w[i + 1] = t; return;
            }
        }
        return;
    }
    // Gradient band interpolation (Johansen 2009): cartesian, or polar for directional trees.
    const float y = value(p, m.parameterY, 0);
    const bool polar = m.type == C::Motion::Type::Directional2D;
    const float pm = std::sqrt(x * x + y * y);
    float sum = 0;
    for (size_t i = 0; i < n; ++i) {
        const auto& ci = m.children[i];
        float h = 1;
        for (size_t k = 0; k < n; ++k) {
            if (k == i) continue;
            const auto& ck = m.children[k];
            float ux, uy, vx, vy;
            if (polar) {
                const float mi = std::sqrt(ci.x * ci.x + ci.y * ci.y), mk = std::sqrt(ck.x * ck.x + ck.y * ck.y);
                const float avg = (mi + mk) * 0.5f;
                if (avg <= 0) continue;
                constexpr float kAngle = 2.f;
                ux = (mk - mi) / avg; uy = signedAngle(ci.x, ci.y, ck.x, ck.y) * kAngle;
                vx = (pm - mi) / avg; vy = signedAngle(ci.x, ci.y, x, y) * kAngle;
            } else {
                ux = ck.x - ci.x; uy = ck.y - ci.y; vx = x - ci.x; vy = y - ci.y;
            }
            const float len = ux * ux + uy * uy;
            if (len <= 1e-12f) continue;
            h = std::min(h, 1 - (vx * ux + vy * uy) / len);
        }
        w[i] = std::max(0.f, h);
        sum += w[i];
    }
    if (sum > 0) { for (size_t i = 0; i < n; ++i) w[i] /= sum; return; }
    size_t best = 0; float bd = 1e30f;
    for (size_t i = 0; i < n; ++i) {
        const float d = (m.children[i].x - x) * (m.children[i].x - x) + (m.children[i].y - y) * (m.children[i].y - y);
        if (d < bd) { bd = d; best = i; }
    }
    w[best] = 1;
}
float clipSeconds(const Model& model, int clip) {
    return clip >= 0 ? std::max(model.clips[size_t(clip)].duration, 1.f / 1000) : 1.f;
}
} // namespace

void AnimationController::weights(const Motion& m, const AnimationParameters& p, std::vector<float>& out) const {
    out.assign(m.children.size(), 0);
    if (!out.empty()) computeWeights(m, p, out.data());
}

float AnimationController::duration(const Motion& m, const AnimationParameters& p) const {
    if (m.type == Motion::Type::Clip) return clipSeconds(*model_, m.clip);
    float w[kMaxChildren];
    computeWeights(m, p, w);
    float d = 0, sum = 0;
    for (size_t i = 0; i < m.children.size(); ++i) {
        if (w[i] <= 0) continue;
        const auto& c = m.children[i];
        d += w[i] * (c.motion ? duration(*c.motion, p) : 1.f) / c.timeScale;
        sum += w[i];
    }
    return sum > 0 ? std::max(d / sum, 1.f / 1000) : 1.f;
}

// ---------------------------------------------------------------- player

void AnimationControllerPlayer::reset(std::shared_ptr<const AnimationController> c) {
    controller_ = std::move(c);
    layers_.clear(); lastTrigger_.clear(); latched_.clear(); bind_.clear();
    if (!controller_) return;
    bindPose(*controller_->model(), bind_);
    layers_.resize(controller_->layers().size());
    for (size_t i = 0; i < layers_.size(); ++i) layers_[i].current.state = controller_->layers()[i].defaultState;
    lastTrigger_.assign(controller_->triggers().size(), 0);
    latched_.assign(controller_->triggers().size(), 0);
    stack_.assign(2 * (AnimationController::kMaxDepth + 2), bind_);
}

const std::string& AnimationControllerPlayer::state(size_t layer) const {
    static const std::string none;
    if (!controller_ || layer >= layers_.size()) return none;
    const auto& s = layers_[layer];
    const int st = s.next.state >= 0 ? s.next.state : s.current.state;
    return st >= 0 ? controller_->layers()[layer].states[size_t(st)].name : none;
}
float AnimationControllerPlayer::normalizedTime(size_t layer) const {
    if (layer >= layers_.size()) return 0;
    const auto& s = layers_[layer];
    return float(s.next.state >= 0 ? s.next.time : s.current.time);
}
float AnimationControllerPlayer::transition(size_t layer) const {
    if (layer >= layers_.size() || layers_[layer].next.state < 0) return -1;
    const auto& s = layers_[layer];
    return s.fadeDuration > 0 ? std::clamp(s.fade / s.fadeDuration, 0.f, 1.f) : 1.f;
}
bool AnimationControllerPlayer::triggerLatched(const std::string& name) const {
    if (!controller_) return false;
    const auto& t = controller_->triggers();
    for (size_t i = 0; i < t.size(); ++i) if (t[i] == name) return latched_[i] != 0;
    return false;
}

float AnimationControllerPlayer::stateSeconds(const AnimationController::Layer& L, int state, const AnimationParameters& p) const {
    if (state < 0) return 1;
    const auto& s = L.states[size_t(state)];
    return s.motion ? controller_->duration(*s.motion, p) : 1.f;
}

void AnimationControllerPlayer::advance(Play& p, const AnimationController::Layer& L, float dt, const AnimationParameters& params) {
    if (p.state < 0) return;
    const auto& s = L.states[size_t(p.state)];
    if (!s.timeParameter.empty()) { p.time = value(params, s.timeParameter, 0); return; }
    const float speed = s.speed * (s.speedParameter.empty() ? 1.f : value(params, s.speedParameter, 1));
    p.speed = speed;
    p.time += double(dt) * speed / stateSeconds(L, p.state, params);
    if (!std::isfinite(p.time) || std::abs(p.time) > 1e6) p.time = 0;
}

bool AnimationControllerPlayer::fires(const AnimationController::Transition& t, const Play& p, double previous, const AnimationParameters& params) const {
    if (!t.hasExitTime && t.conditions.empty()) return false; // Unity ignores such transitions
    if (t.hasExitTime) {
        const double e = t.exitTime, now = p.time;
        bool ready;
        if (e >= 1) ready = now >= e;
        else ready = std::floor(previous - e) + 1 + e <= now; // crossed k + e for some loop k
        if (!ready) return false;
    }
    const auto& trig = controller_->triggers();
    for (const auto& c : t.conditions) {
        float v = value(params, c.parameter, 0);
        for (size_t i = 0; i < trig.size(); ++i) if (trig[i] == c.parameter) v = latched_[i] ? 1.f : 0.f;
        bool ok = false;
        switch (c.mode) {
            case AnimationController::Mode::If: ok = v != 0; break;
            case AnimationController::Mode::IfNot: ok = v == 0; break;
            case AnimationController::Mode::Greater: ok = v > c.threshold; break;
            case AnimationController::Mode::Less: ok = v < c.threshold; break;
            case AnimationController::Mode::Equals: ok = std::lround(v) == std::lround(c.threshold); break;
            case AnimationController::Mode::NotEqual: ok = std::lround(v) != std::lround(c.threshold); break;
        }
        if (!ok) return false;
    }
    return true;
}

void AnimationControllerPlayer::start(size_t li, const AnimationController::Transition& t, const AnimationParameters& params) {
    const auto& L = controller_->layers()[li];
    auto& s = layers_[li];
    const auto& trig = controller_->triggers();
    for (const auto& c : t.conditions)
        for (size_t i = 0; i < trig.size(); ++i) if (trig[i] == c.parameter) latched_[i] = 0;
    Play to;
    to.state = t.exit ? L.defaultState : t.to;
    to.time = t.offset;
    advance(to, L, 0, params); // time-parameter states start at their parameter
    const int from = s.next.state >= 0 ? s.next.state : s.current.state;
    const float seconds = t.fixedDuration ? t.duration : t.duration * stateSeconds(L, from, params) / std::max(1e-3f, std::abs(s.current.speed));
    if (s.next.state >= 0) {
        // Interruption: the running blend becomes a frozen source pose.
        Layered snap;
        sampleLayer(li, params, snap);
        s.snapshot = snap.pose; s.snapshotReference = snap.reference; s.snapshotCoverage = snap.coverage;
        s.frozen = true;
    }
    if (seconds <= 0) { s.current = to; s.next = {}; s.frozen = false; s.fade = s.fadeDuration = 0; return; }
    s.next = to; s.fade = 0; s.fadeDuration = seconds;
}

void AnimationControllerPlayer::update(float dt, AnimationParameters& params) {
    if (!controller_ || !std::isfinite(dt) || dt < 0) return;
    dt = std::min(dt, 1.f);
    for (const auto& in : controller_->inputs())
        params.set(in.target, std::clamp(value(params, in.from, 0) * in.scale + in.offset, in.min, in.max));
    const auto& trig = controller_->triggers();
    for (size_t i = 0; i < trig.size(); ++i) {
        const float v = value(params, trig[i], 0);
        if (v != lastTrigger_[i] && v != 0) latched_[i] = 1;
        lastTrigger_[i] = v;
    }
    using I = AnimationController::Interruption;
    for (size_t li = 0; li < layers_.size(); ++li) {
        const auto& L = controller_->layers()[li];
        auto& s = layers_[li];
        const double prevCurrent = s.current.time, prevNext = s.next.time;
        if (!s.frozen) advance(s.current, L, dt, params);
        if (s.next.state >= 0) {
            advance(s.next, L, dt, params);
            s.fade += dt;
            if (s.fade >= s.fadeDuration) { s.current = s.next; s.next = {}; s.frozen = false; s.fade = s.fadeDuration = 0; continue; }
            // Interruptions by the running transition's source and/or destination state.
            const AnimationController::Transition* running = nullptr;
            for (const auto& t : L.states[size_t(s.current.state)].transitions) if ((t.exit ? L.defaultState : t.to) == s.next.state) { running = &t; break; }
            for (const auto& t : L.anyState) if (!running && t.to == s.next.state) running = &t;
            const I mode = running ? running->interruption : I::None;
            auto tryList = [&](const std::vector<AnimationController::Transition>& list, const Play& p, double prev) {
                for (const auto& t : list) if (&t != running && fires(t, p, prev, params)) { start(li, t, params); return true; }
                return false;
            };
            bool done = false;
            if (!s.frozen && mode != I::None) {
                const auto& src = L.states[size_t(s.current.state)].transitions;
                const auto& dst = L.states[size_t(s.next.state)].transitions;
                if (mode == I::Source) done = tryList(src, s.current, prevCurrent);
                else if (mode == I::Destination) done = tryList(dst, s.next, prevNext);
                else if (mode == I::SourceThenDestination) done = tryList(src, s.current, prevCurrent) || tryList(dst, s.next, prevNext);
                else done = tryList(dst, s.next, prevNext) || tryList(src, s.current, prevCurrent);
            }
            (void)done;
            continue;
        }
        bool fired = false;
        for (const auto& t : L.anyState) {
            if (!t.canTransitionToSelf && !t.exit && t.to == s.current.state) continue;
            if (fires(t, s.current, prevCurrent, params)) { start(li, t, params); fired = true; break; }
        }
        if (!fired)
            for (const auto& t : L.states[size_t(s.current.state)].transitions)
                if (fires(t, s.current, prevCurrent, params)) { start(li, t, params); break; }
    }
}

void AnimationControllerPlayer::sampleMotion(const AnimationController::Motion& m, double normalized, float cycleOffset, const AnimationParameters& params, Pose& out, bool reference, size_t depth) const {
    const Model& model = *controller_->model();
    if (m.type == AnimationController::Motion::Type::Clip) {
        const auto& clip = model.clips[size_t(m.clip)];
        double n = normalized + cycleOffset;
        n = m.loop ? n - std::floor(n) : std::clamp(n, 0.0, 1.0);
        sampleClip(clip, reference ? 0.f : float(n * clip.duration), out, 1);
        return;
    }
    float w[AnimationController::kMaxChildren];
    computeWeights(m, params, w);
    Pose& acc = stack_[2 * depth];
    Pose& child = stack_[2 * depth + 1];
    float cum = 0;
    for (size_t i = 0; i < m.children.size(); ++i) {
        if (w[i] <= 1e-5f) continue;
        const auto& c = m.children[i];
        child = bind_;
        if (c.motion) sampleMotion(*c.motion, normalized, cycleOffset + c.cycleOffset, params, child, reference, depth + 1);
        cum += w[i];
        if (cum == w[i]) acc = child;
        else blendPose(acc, child, w[i] / cum);
    }
    if (cum > 0) out = acc;
}

bool AnimationControllerPlayer::samplePlay(const AnimationController::Layer& L, const Play& p, const AnimationParameters& params, Pose& out, bool reference) const {
    out = bind_;
    if (p.state < 0) return false;
    const auto& s = L.states[size_t(p.state)];
    if (!s.motion) return false;
    const float offset = s.cycleOffset + (s.cycleOffsetParameter.empty() ? 0.f : value(params, s.cycleOffsetParameter, 0));
    sampleMotion(*s.motion, p.time, offset, params, out, reference, 0);
    return true;
}

void AnimationControllerPlayer::sampleLayer(size_t li, const AnimationParameters& params, Layered& out) const {
    const auto& L = controller_->layers()[li];
    const auto& s = layers_[li];
    float srcCov;
    if (s.frozen) { out.pose = s.snapshot; out.reference = s.snapshotReference; srcCov = s.snapshotCoverage; }
    else {
        srcCov = samplePlay(L, s.current, params, out.pose, false) ? 1.f : 0.f;
        if (L.additive) samplePlay(L, s.current, params, out.reference, true);
    }
    out.coverage = srcCov;
    if (s.next.state < 0) return;
    const float t = s.fadeDuration > 0 ? std::clamp(s.fade / s.fadeDuration, 0.f, 1.f) : 1.f;
    Layered& d = transitionPose_;
    const float dstCov = samplePlay(L, s.next, params, d.pose, false) ? 1.f : 0.f;
    if (L.additive) samplePlay(L, s.next, params, d.reference, true);
    const float cs = srcCov * (1 - t), cd = dstCov * t, cov = cs + cd;
    out.coverage = cov;
    if (cov <= 0) return;
    blendPose(out.pose, d.pose, cd / cov);
    if (L.additive) blendPose(out.reference, d.reference, cd / cov);
}

void AnimationControllerPlayer::evaluate(Pose& pose, const AnimationParameters& params) const {
    if (!controller_) { pose.clear(); return; }
    pose = bind_;
    for (size_t li = 0; li < layers_.size(); ++li) {
        const auto& L = controller_->layers()[li];
        const float weight = li == 0 ? 1.f : std::clamp(value(params, L.weightParameter, L.weight), 0.f, 1.f);
        if (weight <= 0) continue;
        Layered& lp = layered_;
        sampleLayer(li, params, lp);
        const float w = weight * lp.coverage;
        if (w <= 0) continue;
        for (size_t k = 0; k < pose.size(); ++k) {
            const float wk = L.mask.empty() ? w : w * L.mask[k];
            if (wk <= 0) continue;
            auto& x = pose[k];
            const auto& y = lp.pose[k];
            if (!L.additive) {
                x.t = x.t + (y.t - x.t) * wk; x.s = x.s + (y.s - x.s) * wk; x.r = nlerp(x.r, y.r, wk);
                for (int c = 0; c < kMaxMorphTargets; ++c) x.w[c] += (y.w[c] - x.w[c]) * wk;
            } else {
                const auto& r = lp.reference[k];
                x.t += (y.t - r.t) * wk;
                const quat inv{-r.r.x, -r.r.y, -r.r.z, r.r.w};
                x.r = qnormalize(x.r * nlerp(quat{}, inv * y.r, wk));
                x.s += (y.s - r.s) * wk;
                for (int c = 0; c < kMaxMorphTargets; ++c) x.w[c] += (y.w[c] - r.w[c]) * wk;
            }
        }
    }
}
} // namespace aiverse::render
