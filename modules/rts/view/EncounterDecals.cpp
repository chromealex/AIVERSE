// Encounter ground marks (EncounterDecals.h): telegraphs from EncounterStore casts, status rings from Statuses.
#include "EncounterDecals.h"
#include "RtsView.h"
#include "../RtsModule.h"
#include "aiverselib/genre/TelegraphDecals.h"
#include <cmath>

namespace aiverse::game {

namespace {
ViewGroundVertex toGround(const lib::genre::DecalVertex& v) { return {v.x, v.y, v.z, 0.f, 0.f, v.rgba}; }

void ring(std::vector<ViewGroundVertex>& out, float cx, float y, float cz, float r0, float r1, uint32_t c) {
    const int n = 24;
    const float tau = 6.28318530718f;
    for (int i = 0; i < n; ++i) {
        float a0 = tau * float(i) / n, a1 = tau * float(i + 1) / n;
        ViewGroundVertex p00{cx + std::sin(a0) * r0, y, cz + std::cos(a0) * r0, 0, 0, c}, p01{cx + std::sin(a1) * r0, y, cz + std::cos(a1) * r0, 0, 0, c};
        ViewGroundVertex p10{cx + std::sin(a0) * r1, y, cz + std::cos(a0) * r1, 0, 0, c}, p11{cx + std::sin(a1) * r1, y, cz + std::cos(a1) * r1, 0, 0, c};
        out.insert(out.end(), {p00, p10, p11, p00, p11, p01});
    }
}
uint32_t statusColor(EncStatusKind k) {
    switch (k) {
    case EncStatusKind::Slow: return 0x4fb6ffc0u;
    case EncStatusKind::Root: return 0x6be36bc0u;
    case EncStatusKind::Stun: return 0xffe04ad0u;
    case EncStatusKind::Burn: return 0xff7a2ad0u;
    default: return 0;
    }
}
} // namespace

void appendEncounterDecals(Simulation& sim, RtsModule& m, const RtsView& view, uint8_t viewer, std::vector<ViewGroundBatch>& out) {
    Encounters* enc = m.encounters();
    EncounterStore* st = enc && enc->active() ? enc->store(sim) : nullptr;
    if (!st) return;
    World& w = sim.world();
    const MapGrid& grid = m.state(sim).grid;
    const uint8_t viewerTeam = viewer ? m.teamOf(sim, viewer) : 0;
    ViewGroundBatch b;
    b.layer = -1;   // under the view scripts' own marks
    const uint32_t tick = sim.tick();
    for (auto& c : st->casts) {
        const lib::genre::TelegraphView v = enc->view(c, tick);
        if (!v.live) continue;
        const Vec3 o{v.pose.origin.x, Fixed{}, v.pose.origin.y};
        if (viewer && !m.isVisible(sim, viewer, o)) continue;
        const bool own = viewer && c.owner && m.teamOf(sim, c.owner) == viewerTeam;
        const float x = float(o.x.toDouble()), z = float(o.z.toDouble());
        const float y = view.heightAt(grid, x, z) + 0.04f;
        for (auto& dv : lib::genre::telegraphDecal(v, y, own)) b.verts.push_back(toGround(dv));
    }
    w.each<Statuses, Unit, Transform>([&](Entity, Statuses& s, Unit& u, Transform& t) {
        if (s.list.empty() || u.hp.raw <= 0 || (u.flags & kUnitOffMap)) return;
        if (viewer && !m.isVisible(sim, viewer, t.pos)) return;
        float r = float(m.rules().types[u.type].radius.toDouble()) + 0.12f;
        const float x = float(t.pos.x.toDouble()), z = float(t.pos.z.toDouble());
        const float y = view.heightAt(grid, x, z) + 0.03f;
        uint32_t seen = 0;
        for (auto& in : s.list) {
            const EncStatusKind k = enc->statusKind(in.def);
            const uint32_t bit = 1u << uint32_t(k);
            if (k == EncStatusKind::Other || (seen & bit)) continue;
            seen |= bit;
            ring(b.verts, x, y, z, r, r + 0.1f, statusColor(k));
            r += 0.14f;
        }
    });
    if (!b.verts.empty()) out.push_back(std::move(b));
}

} // namespace aiverse::game
