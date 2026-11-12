#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json jobs(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id", "task", "seconds"});
    auto task = id(a["task"]);
    auto jobs = current["tasks"];
    if (!jobs.isObject())
        jobs = Json::object();
    if (action == "start") {
        need(!jobs.has(task), Code::Conflict, "task id");
        std::vector<Time> available(size_t(c.def.getInt("slots")), c.now);
        size_t count = 0;
        for (auto &[k, j] : jobs.members()) {
            (void)k;
            if (j.getStr("status") == "pending") {
                ++count;
                auto slot = size_t(j.getInt("slot"));
                need(slot < available.size(), Code::Conflict, "removed queue slot");
                available[slot] = std::max(available[slot], j.getInt("finish"));
            }
        }
        need(count < size_t(c.def.getInt("maxQueued")), Code::Limit, "job queue");
        auto slot = size_t(std::min_element(available.begin(), available.end()) - available.begin());
        c.pay(bundle(c.def["cost"]), {});
        jobs.set(task, row({{"slot", number(int64_t(slot))},
                            {"start", number(available[slot])},
                            {"finish", number(add(available[slot], c.def.getInt("duration")))},
                            {"cost", c.def["cost"]},
                            {"reward", c.def["reward"]},
                            {"refund", c.def["refundPermille"]},
                            {"status", str("pending")}}));
    } else {
        need(jobs.has(task), Code::Missing, "job");
        auto job = jobs[task];
        need(job.getStr("status") == "pending", Code::Unavailable, "job complete");
        if (action == "claim") {
            need(c.now >= job.getInt("finish"), Code::Unavailable, "job not ready");
            c.pay({}, bundle(job["reward"]));
            job.set("status", str("claimed"));
        } else if (action == "cancel") {
            need(c.now < job.getInt("finish"), Code::Unavailable, "completed job cannot cancel");
            auto refund = bundle(job["cost"]);
            for (auto &[asset, n] : refund) {
                (void)asset;
                n = (n / 1000) * job.getInt("refund") + (n % 1000) * job.getInt("refund") / 1000;
            }
            c.pay({}, refund);
            job.set("status", str("cancelled"));
        } else if (action == "speed") {
            need(c.now >= job.getInt("start") && c.now < job.getInt("finish"), Code::Unavailable,
                 "only running jobs can accelerate");
            auto seconds = integer(a["seconds"], 1, job.getInt("finish") - c.now);
            c.pay({{c.def.getStr("speedAsset"), mul(seconds, c.def.getInt("speedCost"))}}, {});
            job.set("finish", number(job.getInt("finish") - seconds));
        } else
            throw Stop(Code::Unsupported, action);
        jobs.set(task, job);
    }
    current.set("tasks", jobs);
    c.self.set(c.id, current);
    return jobs[task];
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::jobs(Modules &m) {
    m.features.insert("jobs");
    m.live["jobs"] = live::jobs;
}
} // namespace aiverse::meta
