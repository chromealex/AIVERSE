// HeroDuel: a minimal direct-control top-down shooter used to test and load-test battle
// networking (rollback with sticky hero input, desync checks, reconnect, hosted battles).
// It is a stand-in shooter, not a game. It speaks the same hero commands as the RTS module's top-down heroes
// (engine/net/HeroInput.h: net::kCmdHeroInput = kCmdTopDown with topdown::Input, net::kCmdHeroAbility =
// kCmdTopDownAbility): heroes move with the stick, shoot while aiming with fire held, dash with ability 0;
// first team to `killsToWin` kills or the most kills after `ticks` wins. RTS-module battles (templates/
// topdown_arena, extraction) run through the same server with apps/battle/RtsBattle.h. Map JSON:
//   {"players":[{"team":1},{"team":2}], "heroDuel":{"size":30,"hp":100,"killsToWin":10,"ticks":2400}}
// Loadouts from the hosted start payload set per-slot hp / damage (2 bytes: hp, damage).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "engine/net/HeroInput.h"
#include "engine/sim/Simulation.h"

namespace aiverse::battle {

struct Hero {
    uint8_t slot = 0, team = 0;
    Vec2 pos{}, spawn{};
    int32_t hp = 100, maxHp = 100, damage = 12, cooldown = 0, respawn = 0, dashCooldown = 0;
    int32_t kills = 0, deaths = 0;
    topdown::Input input;
};
struct Bullet { uint8_t owner = 0, team = 0; Vec2 pos{}, vel{}; int32_t ttl = 0; };

class HeroDuelModule final : public GameModule {
public:
    explicit HeroDuelModule(std::map<uint8_t, std::pair<int32_t, int32_t>> loadouts = {}) : loadouts_(std::move(loadouts)) {}
    std::string name() const override { return "heroduel"; }

    void setup(Simulation& sim, const Json&, const Json& map) override {
        const Json& cfg = map["heroDuel"];
        size_ = Fixed::fromInt(cfg.getInt("size", 30));
        killsToWin_ = int32_t(cfg.getInt("killsToWin", 10));
        ticks_ = uint32_t(cfg.getInt("ticks", 2400));
        const int64_t hp = cfg.getInt("hp", 100);
        const auto& ps = sim.players();
        for (size_t i = 0; i < ps.size(); ++i) {
            Entity e = sim.world().create();
            Hero& h = sim.world().add<Hero>(e);
            h.slot = ps[i].slot; h.team = ps[i].team;
            const Fixed t = Fixed::fromRatio(int64_t(i), int64_t(ps.size()));
            h.spawn = {size_ / 2 + fxCosTurns(t) * (size_ / 3), size_ / 2 + fxSinTurns(t) * (size_ / 3)};
            h.pos = h.spawn;
            h.maxHp = h.hp = int32_t(hp);
            if (auto it = loadouts_.find(h.slot); it != loadouts_.end()) { h.maxHp = h.hp = it->second.first; h.damage = it->second.second; }
        }
        sim.addSystem("heroes", [this](Simulation& s) { step(s); });
    }

    void applyCommand(Simulation& sim, const Command& c) override {
        Hero* h = hero(sim, c.player);
        if (!h) return;
        if (c.type == net::kCmdHeroInput) {
            h->input = topdown::unpack(c.param);
            h->input.moveMag = topdown::quantizeMag(h->input.moveMag, 1, 50);   // Drone Fall: full speed or nothing
            h->input.moveAngle = h->input.moveMag ? topdown::quantizeAngle(h->input.moveAngle, 45) : 0;
        } else if (c.type == net::kCmdHeroAbility) {
            const topdown::Ability ab = topdown::unpackAbility(c.param);
            if (ab.index != 0 || (ab.phase != topdown::Phase::Tap && ab.phase != topdown::Phase::Release) || h->respawn || h->dashCooldown) return;
            const Vec2 d = ab.hasAngle ? topdown::directionXZ(ab.angle) : topdown::moveVector(h->input);
            h->pos = clamp(h->pos + Vec2{d.x * 3, d.y * 3});
            h->dashCooldown = int32_t(sim.tickRate());
        }
    }

    void hashState(Simulation& sim, Hasher& hs) override {
        sim.world().each<Hero>([&](Entity, Hero& h) {
            hs.u32(h.slot); hs.fx(h.pos.x); hs.fx(h.pos.y); hs.i32(h.hp); hs.i32(h.cooldown); hs.i32(h.respawn);
            hs.i32(h.dashCooldown); hs.i32(h.kills); hs.i32(h.deaths); hs.i64(topdown::pack(h.input));
        });
        sim.world().each<Bullet>([&](Entity, Bullet& b) { hs.u32(b.owner); hs.fx(b.pos.x); hs.fx(b.pos.y); hs.fx(b.vel.x); hs.fx(b.vel.y); hs.i32(b.ttl); });
    }

    int result(Simulation& sim) override {
        std::map<uint8_t, int32_t> kills;
        sim.world().each<Hero>([&](Entity, Hero& h) { kills[h.team] += h.kills; });
        int best = 0; int32_t top = -1; bool tie = false;
        for (auto& [team, k] : kills) { if (k > top) { top = k; best = team; tie = false; } else if (k == top) tie = true; }
        if (top >= killsToWin_ && !tie) return best;
        if (sim.tick() >= ticks_) return tie ? -1 : best;
        return 0;
    }

    // Deterministic per-player outcome for the hosted results report: slot, kills, deaths, hp.
    static std::vector<uint8_t> resultBlob(Simulation& sim) {
        std::vector<uint8_t> b;
        sim.world().each<Hero>([&](Entity, Hero& h) {
            b.push_back(h.slot); b.push_back(uint8_t(h.kills)); b.push_back(uint8_t(h.deaths)); b.push_back(uint8_t(h.hp));
        });
        return b;
    }
    static Hero* hero(Simulation& sim, uint8_t slot) {
        Hero* out = nullptr;
        sim.world().each<Hero>([&](Entity, Hero& h) { if (h.slot == slot) out = &h; });
        return out;
    }

private:
    Vec2 clamp(Vec2 p) const { return {fxClamp(p.x, Fixed{}, size_), fxClamp(p.y, Fixed{}, size_)}; }

    void step(Simulation& sim) {
        // Outcome state freezes once the match is decided, so every client's results report
        // (resultBlob) is identical however far past the result tick it has predicted.
        if (result(sim)) return;
        World& w = sim.world();
        const Fixed speed = Fixed::fromRatio(6, int64_t(sim.tickRate()));       // 6 units / s
        const Fixed bulletSpeed = Fixed::fromRatio(18, int64_t(sim.tickRate()));
        std::vector<Bullet> spawn;
        w.each<Hero>([&](Entity, Hero& h) {
            if (h.dashCooldown) --h.dashCooldown;
            if (h.cooldown) --h.cooldown;
            if (h.respawn) { if (--h.respawn == 0) { h.pos = h.spawn; h.hp = h.maxHp; } return; }
            const Vec2 m = topdown::moveVector(h.input);
            h.pos = clamp(h.pos + Vec2{m.x * speed, m.y * speed});
            if (h.input.fire && h.input.aim && !h.cooldown) {
                const Vec2 a = topdown::aimVector(h.input);
                spawn.push_back({h.slot, h.team, h.pos, {a.x * bulletSpeed, a.y * bulletSpeed}, int32_t(sim.tickRate())});
                h.cooldown = int32_t(sim.tickRate() / 5);
            }
        });
        std::vector<Entity> dead;
        const Fixed r2 = Fixed::fromRatio(1, 1);
        w.each<Bullet>([&](Entity e, Bullet& b) {
            b.pos = b.pos + b.vel;
            if (--b.ttl <= 0) { dead.push_back(e); return; }
            Hero* hit = nullptr;
            w.each<Hero>([&](Entity, Hero& h) {
                if (hit || h.team == b.team || h.respawn) return;
                const Vec2 d = h.pos - b.pos;
                if (d.x * d.x + d.y * d.y <= r2) hit = &h;
            });
            if (!hit) return;
            dead.push_back(e);
            Hero* shooter = hero(sim, b.owner);
            hit->hp -= shooter ? shooter->damage : 10;
            if (hit->hp <= 0) {
                ++hit->deaths; hit->hp = 0; hit->respawn = int32_t(sim.tickRate()) * 2;
                if (shooter) ++shooter->kills;
                sim.emit({1, hit->slot, kNullEntity, kNullEntity, b.owner, {}});
            }
        });
        for (Entity e : dead) w.destroyLater(e);
        for (auto& b : spawn) { Entity e = w.create(); w.add<Bullet>(e, b); }
    }

    std::map<uint8_t, std::pair<int32_t, int32_t>> loadouts_;
    Fixed size_ = Fixed::fromInt(30);
    int32_t killsToWin_ = 10;
    uint32_t ticks_ = 2400;
};

// Map JSON for n players in two teams.
inline std::string heroDuelMap(int players, int ticks = 2400, int killsToWin = 10) {
    std::string s = "{\"players\":[";
    for (int i = 0; i < players; ++i) s += std::string(i ? "," : "") + "{\"team\":" + std::to_string(i % 2 + 1) + "}";
    return s + "],\"heroDuel\":{\"size\":30,\"hp\":100,\"killsToWin\":" + std::to_string(killsToWin) + ",\"ticks\":" + std::to_string(ticks) + "}}";
}

} // namespace aiverse::battle
