#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json quests(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id", "metric", "amount"});
    auto period = c.def.getInt("period");
    auto bucket = period ? c.now / period : 0;
    if (!current.has("bucket") || current.getInt("bucket") != bucket) {
        std::vector<uint32_t> weights(c.def["variants"].size(), 1);
        auto variant = cohort(c.who.player, c.id + ":" + std::to_string(bucket), weights);
        current = row({{"bucket", number(bucket)},
                       {"variant", number(int64_t(variant))},
                       {"step", number(0)},
                       {"counts", Json::object()},
                       {"rerolls", number(0)},
                       {"active", Json::fromBool(false)}});
    }
    const auto &variant = c.def["variants"][size_t(current.getInt("variant"))];
    need(variant.isObject(), Code::Conflict, "removed quest variant");
    if (action == "activate") {
        for (auto &dep : c.def["requires"].items()) {
            auto prior = c.self[dep.str()];
            auto pd = c.cfg.extensions["liveops"]["quests"][dep.str()].getInt("period");
            need(prior.getBool("claimed") && prior.getInt("bucket") == (pd ? c.now / pd : 0), Code::Unavailable,
                 "quest prerequisite");
        }
        if (c.def.has("group"))
            for (auto &[id, q] : c.self.members()) {
                if (id == c.id)
                    continue;
                const auto &d = c.cfg.extensions["liveops"]["quests"][id];
                auto qp = d.getInt("period");
                need(d.getStr("group") != c.def.getStr("group") || !q.getBool("active") ||
                         q.getInt("bucket") != (qp ? c.now / qp : 0),
                     Code::Conflict, "quest branch already chosen");
            }
        current.set("active", Json::fromBool(true));
    } else {
        need(current.getBool("active") && !current.getBool("claimed"), Code::Unavailable, "quest inactive or claimed");
        if (action == "reroll") {
            need(current.getInt("rerolls") < c.def.getInt("maxRerolls") && c.def["variants"].size() > 1, Code::Limit,
                 "reroll limit");
            c.pay(bundle(c.def["rerollCost"]), {});
            current.set("rerolls", number(current.getInt("rerolls") + 1));
            current.set("variant", number((current.getInt("variant") + 1) % int64_t(c.def["variants"].size())));
            current.set("step", number(0));
            current.set("counts", Json::object());
        } else if (action == "event") {
            c.authority();
            auto metric = id(a["metric"]);
            auto delta = integer(a["amount"], 1);
            auto index = size_t(current.getInt("step"));
            need(index < variant["steps"].size(), Code::Unavailable, "quest ready");
            auto step = variant["steps"][index];
            auto counts = current["counts"];
            bool relevant = false;
            for (auto &goal : step["goals"].items())
                if (goal.getStr("metric") == metric)
                    relevant = true;
            if (relevant)
                counts.set(metric, number(add(counts.getInt(metric), delta)));
            bool all = true, any = false;
            for (auto &goal : step["goals"].items()) {
                bool met = counts.getInt(goal.getStr("metric")) >= goal.getInt("target");
                all &= met;
                any |= met;
            }
            current.set("counts", counts);
            if (step.getStr("mode") == "all" ? all : any) {
                current.set("step", number(int64_t(index + 1)));
                current.set("counts", Json::object());
            }
        } else if (action == "claim") {
            need(size_t(current.getInt("step")) == variant["steps"].size(), Code::Insufficient, "quest incomplete");
            c.pay({}, bundle(variant["reward"]));
            current.set("claimed", Json::fromBool(true));
        } else
            throw Stop(Code::Unsupported, action);
    }
    c.self.set(c.id, current);
    return current;
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::quests(Modules &m) {
    m.features.insert("quests");
    m.live["quests"] = live::quests;
}
} // namespace aiverse::meta
