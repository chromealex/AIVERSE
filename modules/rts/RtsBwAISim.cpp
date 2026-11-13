// Simulation-side support of the Bunker Wars computer player (agent-bw-ai): the per point counters the bots read
// (BW HouseModule.producedUnits, TowerModule.killCount; ModesState::pointProduced / pointKills) and sends out of
// a hypnotised enemy garrison (BW PointsHypnosis: the caster drags units out of the point; status flag
// "hypnotised", StatusDef::sendShare / captureSent).
#include "RtsInternal.h"
#include "RtsBwAI.h"

namespace aiverse::game {

void RtsModule::bwNoteProduced(Simulation& sim, Entity e, int n) {
    if (!rules_.bwAI) return;   // only games played by the BW bots keep (and hash) the counters
    ModesState& ms = state(sim).modes;
    if (ms.stats.empty()) return;
    if (ms.pointProduced.size() <= e.index) ms.pointProduced.resize(e.index + 1, 0);
    ms.pointProduced[e.index] += n;
}

// kCmdSend from a structure the sender does not own: allowed while it carries a "hypnotised" status the sender
// applied. The units stay the owner's (or become the sender's with captureSent); the status ends with the send
// (BW Modificator.OnUnitsMoved -> Deactivate). Returns true when the command was such a send.
bool RtsModule::hypnotisedFlagSend(Simulation& sim, const Command& cmd) {
    if (!rules_.advancedEffects || cmd.type != kCmdSend) return false;
    World& w = sim.world();
    bool handled = false;
    for (Entity e : cmd.units) {
        if (!w.alive(e) || !w.has<Garrison>(e) || !w.has<Owner>(e) || w.get<Owner>(e).player == cmd.player) continue;
        auto* st = w.tryGet<Statuses>(e);
        if (!st) continue;
        StatusInst* hyp = nullptr;
        for (auto& in : st->list)
            if ((rules_.statuses[in.def].flags & kStHypnotised) && in.owner == cmd.player && in.until > sim.tick()) { hyp = &in; break; }
        if (!hyp) continue;
        handled = true;
        const StatusDef& sd = rules_.statuses[hyp->def];
        if (!w.alive(cmd.target) || cmd.target == e || !w.has<Garrison>(cmd.target)) continue;
        const UnitType& t = rules_.types[w.get<Unit>(e).type];
        if (t.spawnUnit == kNoType) continue;
        if (env_ && env_->active() && !env_->sendAllowed(sim, e, cmd.target)) continue;
        if (!canSend(sim, e, cmd.target)) continue;
        Garrison& g = w.get<Garrison>(e);
        const int share = int((Fixed::fromInt(g.count) * fxClamp(sd.sendShare, Fixed{}, Fixed::fromInt(1))).floorInt());
        int n = cmd.param < 0 ? int(std::min<int64_t>(share, -cmd.param)) : int(int64_t(share) * std::clamp<int64_t>(cmd.param, 1, 100) / 100);
        if (n <= 0) continue;
        const uint8_t owner = w.get<Owner>(e).player;
        hyp->until = sim.tick();   // one group per hypnosis
        if (rules_.routes.groups) {
            sendAlongRoute(sim, e, cmd.target, owner, n);
            if (sd.captureSent)
                for (auto& sg : state(sim).routes.groups)
                    if (sg.from == e && sg.to == cmd.target && sg.owner == owner) { sg.owner = cmd.player; sg.payer = owner; }
            continue;
        }
        g.count -= n;
        g.pendingOut += n;
        g.pendingTarget = cmd.target;
        g.pendingGroup = state(sim).nextGroupId++;
    }
    return handled;
}

} // namespace aiverse::game
