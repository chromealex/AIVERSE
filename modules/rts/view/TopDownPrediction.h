// Render-only hero presentation for top-down games (docs/topdown.md § Network): visual prediction of the local hero
// beyond the lockstep input delay, and blending of rollback corrections. Floats only; nothing here feeds the sim.
//
// Prediction. Input reaches the simulation `lead` seconds after the player moved the stick (the session's input delay
// plus the render interpolation lag). In those seconds the sim will execute exactly the stick states the player
// already sent, so the hero's position `lead` seconds from now is about sim position + integral of the stick's
// velocity over the last `lead` seconds. The local hero is drawn there: it starts, stops and turns the frame the
// stick does. The offset is clamped by the map (a `passable(x, z)` callback: never drawn inside a wall), limited
// to `maxOffset` and eased with a short half-life so a changed lead or a hit wall never pops. Other heroes are drawn
// where the sim has them (their input is predicted by the rollback session already).
//
// Corrections. RollbackSession::setRollbackHook calls the host right before a rollback and right after the
// re-simulation, at the same tick: record positions in both (before / after) and the difference goes to
// net::CorrectionSmoother, whose offset decays with a half-life (large jumps such as respawns or escapes snap).
#pragma once
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <utility>
#include "../../../engine/net/Smoothing.h"

namespace aiverse::game {

struct TopDownPredictionConfig {
    bool enabled = true;           // game.json topDown.prediction.enabled
    float maxLead = 0.3f;          // seconds of input in flight that are predicted at most
    float maxOffset = 3.0f;        // world units
    float easeHalfLifeMs = 35.0f;  // the drawn offset follows the predicted one with this half-life
    float correctionHalfLifeMs = 50.0f;   // rollback correction blending (net::CorrectionSmoother)
    float snapDistance = 4.0f;
};

class TopDownPrediction {
public:
    using Positions = std::map<uint64_t, std::pair<float, float>>;   // id -> (x, z)
    explicit TopDownPrediction(TopDownPredictionConfig c = {}) { configure(c); }
    void configure(const TopDownPredictionConfig& c) {
        cfg_ = c;
        smoother_ = net::CorrectionSmoother({c.correctionHalfLifeMs, c.snapDistance});
        reset();
    }
    const TopDownPredictionConfig& config() const { return cfg_; }
    void reset() { history_.clear(); ox_ = oz_ = 0; smoother_.clear(); before_.clear(); time_ = 0; }

    // Rollback hook halves (positions of every hero / squad member, read from the sim at the same tick).
    void beforeRollback(Positions pos) { before_ = std::move(pos); }
    void afterRollback(const Positions& pos) {
        for (auto& [id, p0] : before_) {
            auto it = pos.find(id);
            if (it == pos.end()) continue;
            const float dx = p0.first - it->second.first, dz = p0.second - it->second.second;
            if (dx * dx + dz * dz > 1e-8f) smoother_.correct(id, dx, dz);
        }
        before_.clear();
    }

    // Once per rendered frame. wishX / wishZ: world velocity (units / s) the newest local stick asks of the hero
    // (0 when it cannot move: stunned, rooted, dead); lead: seconds until input sent now reaches the sim;
    // heroX / heroZ: the local hero's drawn (interpolated) sim position; passable: the map test for clamping.
    void frame(float dt, float wishX, float wishZ, float lead, bool haveHero, float heroX, float heroZ,
               const std::function<bool(float, float)>& passable) {
        time_ += double(dt);
        smoother_.advance(dt * 1000.0f);
        float tx = 0, tz = 0;
        if (cfg_.enabled && haveHero) {
            history_.push_back({time_ - double(dt), time_, wishX, wishZ});   // the stick during this frame
            const double window = double(std::fmax(0.0f, std::fmin(lead, cfg_.maxLead)));
            while (!history_.empty() && history_.front().t1 <= time_ - window) history_.pop_front();
            for (auto& h : history_) {   // integral of the stick velocity over [now - window, now]
                const double span = h.t1 - std::fmax(h.t0, time_ - window);
                if (span <= 0) continue;
                tx += float(double(h.vx) * span);
                tz += float(double(h.vz) * span);
            }
            const float len = std::sqrt(tx * tx + tz * tz);
            if (len > cfg_.maxOffset && len > 0) { tx *= cfg_.maxOffset / len; tz *= cfg_.maxOffset / len; }
            // walls: shorten the offset until the drawn point is on passable ground (and the way there is)
            if (passable) {
                float k = 1;
                for (int i = 0; i < 6 && k > 0; ++i) {
                    bool ok = true;
                    for (int s = 1; s <= 4 && ok; ++s) ok = passable(heroX + tx * k * float(s) / 4, heroZ + tz * k * float(s) / 4);
                    if (ok) break;
                    k = i == 5 ? 0 : k * 0.5f;
                }
                tx *= k; tz *= k;
            }
        } else history_.clear();
        const float e = cfg_.easeHalfLifeMs > 0 ? 1.0f - std::exp2(-dt * 1000.0f / cfg_.easeHalfLifeMs) : 1.0f;
        ox_ += (tx - ox_) * e; oz_ += (tz - oz_) * e;
    }

    // Draw offset of an object: rollback smoothing for everyone (by id), plus the prediction for the local hero.
    void offset(uint64_t id, bool localHero, float& dx, float& dz) const {
        smoother_.offset(id, dx, dz);
        if (localHero) { dx += ox_; dz += oz_; }
    }
    void predicted(float& dx, float& dz) const { dx = ox_; dz = oz_; }
    size_t corrections() const { return smoother_.active(); }

private:
    struct Sample { double t0, t1; float vx, vz; };
    TopDownPredictionConfig cfg_;
    net::CorrectionSmoother smoother_;
    std::deque<Sample> history_;
    Positions before_;
    double time_ = 0;
    float ox_ = 0, oz_ = 0;
};

} // namespace aiverse::game
