// Bunker Wars computer player: debuts (DebutPlayer: a recorded opening replayed while the bot's points look like
// the recording) and map bot triggers (Database/AI TriggerInfo / TriggerItem: timers, values, conditions on
// captures and groups, actions that steer the bot).
#include "RtsBwAIInternal.h"

namespace aiverse::game {

namespace {
constexpr uint8_t kNoOwner = 254;
BwMode modeNow(BwAI::Ctx& c) {
    if (!c.cfg.modeAuto) return c.cfg.mode;
    for (auto& p : c.world().pts) { if (p.dom) return BwMode::Domination; if (p.koth) return BwMode::KingOfTheHill; }
    return BwMode::Standard;
}
} // namespace

void BwImpl::initDebut(Ctx& c) {
    BwAI& ai = c.ai;
    ai.debut_ = -1; ai.debutPlaying_ = false; ai.debutCursor_ = 0;
    const BwMode mode = modeNow(c);
    for (size_t i = 0; i < c.cfg.debuts.size(); ++i) {
        const BwDebut& d = c.cfg.debuts[i];
        if (d.complexity && d.complexity != c.prof.complexity) continue;
        if (d.slot && d.slot != c.me()) continue;
        if (!d.anyMode && d.mode != mode) continue;
        if (d.actions.empty()) continue;
        ai.debut_ = int(i);
        ai.debutPlaying_ = true;
        // a debut belongs to the start of the match: skip what is already behind us (a bot created mid-match)
        while (ai.debutCursor_ < d.actions.size() && d.actions[ai.debutCursor_].tick < c.sim.tick()) ++ai.debutCursor_;
        break;
    }
}

bool BwImpl::debut(Ctx& c) {
    BwAI& ai = c.ai;
    if (ai.debut_ < 0 || !ai.debutPlaying_ || size_t(ai.debut_) >= c.cfg.debuts.size()) return false;
    const BwDebut& d = c.cfg.debuts[size_t(ai.debut_)];
    BwWorld& W = c.world();
    const uint32_t now = c.sim.tick();
    // ValidateDebut: an own point that changed type, or a point taken by someone else, ends the opening
    for (auto& st : d.states) {
        if (st.tick != now) continue;
        if (st.point >= int(W.pts.size())) { ai.debutPlaying_ = false; return false; }
        const BwPoint& p = W.pts[size_t(st.point)];
        if ((!c.neutral(p.owner) && p.owner != c.me()) || (p.owner == c.me() && p.type != st.type)) { ai.debutPlaying_ = false; return false; }
    }
    if (ai.debutCursor_ >= d.actions.size()) { ai.debutPlaying_ = false; return true; }   // finished (this tick still belongs to it)
    for (; ai.debutCursor_ < d.actions.size() && d.actions[ai.debutCursor_].tick <= now; ++ai.debutCursor_) {
        const BwDebut::Act& a = d.actions[ai.debutCursor_];
        const int n = int(W.pts.size());
        if (a.kind == 0 && a.from < n && a.to < n && W.pts[size_t(a.from)].owner == c.me()) {
            Command cmd; cmd.type = kCmdSend; cmd.units = {W.pts[size_t(a.from)].e}; cmd.target = W.pts[size_t(a.to)].e; cmd.param = -int64_t(std::max(1, a.count));
            const uint32_t at = c.submit(cmd);
            ai.pending_.push_back({W.pts[size_t(a.from)].e, a.count, at, false});
        } else if (a.kind == 1 && a.from < n && W.pts[size_t(a.from)].owner == c.me()) {
            c.upgrade(a.from, a.type);
        } else if (a.kind == 2) {
            c.cast(a.ability, a.point);
        }
    }
    return true;
}

void BwImpl::triggers(Ctx& c) {
    BwAI& ai = c.ai;
    const auto& groups = c.cfg.triggers.groups;
    if (groups.empty()) return;
    BwWorld& W = c.world();
    const size_t n = W.pts.size();
    // capture bookkeeping (PointInfo.GetOldOwner)
    if (ai.lastOwners_.size() != n) { ai.lastOwners_.assign(n, 0); for (size_t i = 0; i < n; ++i) ai.lastOwners_[i] = W.pts[i].owner; ai.prevOwners_.assign(n, kNoOwner); }
    for (size_t i = 0; i < n; ++i)
        if (W.pts[i].owner != ai.lastOwners_[i]) { ai.prevOwners_[i] = ai.lastOwners_[i]; ai.lastOwners_[i] = W.pts[i].owner; }
    for (auto& [name, t] : ai.trigTimers_)
        if (t.second == 1) { t.first -= c.dt; if (t.first.raw <= 0) t.second = 2; }
    if (ai.trigGroupOn_.size() != groups.size()) { ai.trigGroupOn_.clear(); for (auto& g : groups) ai.trigGroupOn_.push_back(g.enabled ? 1 : 0); }
    size_t total = 0;
    for (auto& g : groups) total += g.elements.size();
    if (ai.trigArrived_.size() != total) ai.trigArrived_.assign(total, 0);
    auto who = [&](uint8_t filter, uint8_t owner) {
        switch (filter) {
            case 1: return owner == c.me();
            case 2: return c.ally(owner, c.me());
            case 3: return c.enemy(owner, c.me());
            default: return true;
        }
    };
    auto cond = [&](const BwTrigger::Cond& k) {
        switch (k.kind) {
            case 0: return true;
            case 1: { auto it = ai.trigTimers_.find(k.name); return it != ai.trigTimers_.end() && it->second.second == 2; }
            case 2: { auto it = ai.trigValues_.find(k.name); return bwCompare(k.cmp, it != ai.trigValues_.end() ? it->second : Fixed{}, k.value); }
            case 3: {
                if (k.point < 0 || size_t(k.point) >= n || ai.prevOwners_[size_t(k.point)] == kNoOwner) return false;
                const uint8_t was = ai.prevOwners_[size_t(k.point)], now = W.pts[size_t(k.point)].owner;
                return was != now && !c.ally(was, now);
            }
            case 4:
                for (auto& g : W.groups) {
                    if (g.need() < k.minCount || g.need() - g.pending < k.point) continue;
                    if (k.from >= 0 && g.from >= 0 && g.from != k.from) continue;
                    if (k.to >= 0 && g.to != k.to) continue;
                    if (!who(k.who, g.owner)) continue;
                    if (k.progress.raw >= 0) {
                        if (g.routeLen.raw <= 0 || Fixed::fromInt(1) - g.minDist / g.routeLen < k.progress) continue;
                    }
                    return true;
                }
                return false;
            case 5: return bwCompare(k.cmp, W.time, k.value);
            default: return false;
        }
    };
    size_t idx = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const auto& g = groups[gi];
        for (size_t ei = 0; ei < g.elements.size(); ++ei, ++idx) {
            if (!ai.trigGroupOn_[gi] || ai.trigArrived_[idx]) continue;
            const auto& el = g.elements[ei];
            bool ok = el.all;
            for (auto& k : el.conds) {
                const bool r = cond(k);
                if (el.all && !r) { ok = false; break; }
                if (!el.all && r) { ok = true; break; }
            }
            if (!ok) continue;
            ai.trigArrived_[idx] = 1;
            for (auto& a : el.actions) {
                switch (a.kind) {
                    case 0: ai.trigTimers_[a.name] = {a.value, uint8_t(a.value.raw > 0 ? 1 : 2)}; break;
                    case 1: ai.trigValues_[a.name] = a.value; break;
                    case 2:
                        if (a.mask & 1) { ai.objSources_ = a.a; ai.hasObjSources_ = !a.a.empty(); }
                        if (a.mask & 2) { ai.objTargets_ = a.b; ai.hasObjTargets_ = !a.b.empty(); }
                        if (a.mask & 4) ai.objCount_ = a.count;
                        ai.attackTesting_ = a.flag2;
                        ai.useAllUnitsForAttack_ = a.flag;
                        break;
                    case 3: ai.ignore_ = a.a; ai.hasIgnore_ = !a.a.empty(); break;
                    case 4: ai.disabled_ = a.flag; break;
                    case 5: resetBot(c); break;
                    case 6:
                        if (a.from < int(n) && a.to < int(n) && W.pts[size_t(a.from)].owner == c.me()) c.sendPercent(a.from, a.to, a.value / 100);
                        break;
                    case 7: {
                        size_t start = 0;
                        while (start < a.name.size()) {
                            const size_t end = a.name.find('\n', start);
                            const std::string nm = a.name.substr(start, end - start);
                            for (size_t k = 0; k < groups.size(); ++k) if (groups[k].name == nm) ai.trigGroupOn_[k] = a.flag ? 1 : 0;
                            start = end == std::string::npos ? a.name.size() : end + 1;
                        }
                        break;
                    }
                    case 8: {
                        bool* flags[4] = {&ai.enableAttack_, &ai.enableRelocation_, &ai.enableDefense_, &ai.enableUpgrade_};
                        for (int k = 0; k < 4; ++k)
                            if (a.mask & (1 << k)) *flags[k] = std::find(a.a.begin(), a.a.end(), k) != a.a.end();
                        break;
                    }
                    default: break;
                }
            }
        }
    }
}

} // namespace aiverse::game
