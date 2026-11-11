#include "LiveOpsInternal.h"
namespace aiverse::meta::live {
Json activities(Call& c) {
    const auto& a=c.args();auto action=c.req.action;auto current=c.self[c.id];if(!current.isObject())current=Json::object();
    if(c.req.service=="jobs") {
        keys(a,{"id","task","seconds"});auto task=id(a["task"]);auto jobs=current["tasks"];if(!jobs.isObject())jobs=Json::object();
        if(action=="start") {
            need(!jobs.has(task),Code::Conflict,"task id");std::vector<Time> available(size_t(c.def.getInt("slots")),c.now);size_t count=0;
            for(auto& [k,j]:jobs.members()){(void)k;if(j.getStr("status")=="pending"){++count;auto slot=size_t(j.getInt("slot"));need(slot<available.size(),Code::Conflict,"removed queue slot");available[slot]=std::max(available[slot],j.getInt("finish"));}}
            need(count<size_t(c.def.getInt("maxQueued")),Code::Limit,"job queue");auto slot=size_t(std::min_element(available.begin(),available.end())-available.begin());
            c.pay(bundle(c.def["cost"]),{});jobs.set(task,row({{"slot",number(int64_t(slot))},{"start",number(available[slot])},{"finish",number(add(available[slot],c.def.getInt("duration")))},{"cost",c.def["cost"]},{"reward",c.def["reward"]},{"refund",c.def["refundPermille"]},{"status",str("pending")}}));
        } else {
            need(jobs.has(task),Code::Missing,"job");auto job=jobs[task];need(job.getStr("status")=="pending",Code::Unavailable,"job complete");
            if(action=="claim") {need(c.now>=job.getInt("finish"),Code::Unavailable,"job not ready");c.pay({},bundle(job["reward"]));job.set("status",str("claimed"));}
            else if(action=="cancel") {need(c.now<job.getInt("finish"),Code::Unavailable,"completed job cannot cancel");auto refund=bundle(job["cost"]);for(auto& [asset,n]:refund){(void)asset;n=(n/1000)*job.getInt("refund")+(n%1000)*job.getInt("refund")/1000;}c.pay({},refund);job.set("status",str("cancelled"));}
            else if(action=="speed") {need(c.now>=job.getInt("start")&&c.now<job.getInt("finish"),Code::Unavailable,"only running jobs can accelerate");auto seconds=integer(a["seconds"],1,job.getInt("finish")-c.now);c.pay({{c.def.getStr("speedAsset"),mul(seconds,c.def.getInt("speedCost"))}},{});job.set("finish",number(job.getInt("finish")-seconds));}
            else throw Stop(Code::Unsupported,action);
            jobs.set(task,job);
        }
        current.set("tasks",jobs);c.self.set(c.id,current);return jobs[task];
    }
    if(c.req.service=="quests") {
        keys(a,{"id","metric","amount"});auto period=c.def.getInt("period");auto bucket=period?c.now/period:0;
        if(!current.has("bucket")||current.getInt("bucket")!=bucket) {
            std::vector<uint32_t> weights(c.def["variants"].size(),1);auto variant=cohort(c.who.player,c.id+":"+std::to_string(bucket),weights);
            current=row({{"bucket",number(bucket)},{"variant",number(int64_t(variant))},{"step",number(0)},{"counts",Json::object()},{"rerolls",number(0)},{"active",Json::fromBool(false)}});
        }
        const auto& variant=c.def["variants"][size_t(current.getInt("variant"))];need(variant.isObject(),Code::Conflict,"removed quest variant");
        if(action=="activate") {
            for(auto& dep:c.def["requires"].items()){auto prior=c.self[dep.str()];auto pd=c.cfg.extensions["liveops"]["quests"][dep.str()].getInt("period");need(prior.getBool("claimed")&&prior.getInt("bucket")== (pd?c.now/pd:0),Code::Unavailable,"quest prerequisite");}
            if(c.def.has("group"))for(auto& [id,q]:c.self.members()){if(id==c.id)continue;const auto& d=c.cfg.extensions["liveops"]["quests"][id];auto qp=d.getInt("period");need(d.getStr("group")!=c.def.getStr("group")||!q.getBool("active")||q.getInt("bucket")!=(qp?c.now/qp:0),Code::Conflict,"quest branch already chosen");}
            current.set("active",Json::fromBool(true));
        } else {
            need(current.getBool("active")&&!current.getBool("claimed"),Code::Unavailable,"quest inactive or claimed");
            if(action=="reroll") {need(current.getInt("rerolls")<c.def.getInt("maxRerolls")&&c.def["variants"].size()>1,Code::Limit,"reroll limit");c.pay(bundle(c.def["rerollCost"]),{});current.set("rerolls",number(current.getInt("rerolls")+1));current.set("variant",number((current.getInt("variant")+1)%int64_t(c.def["variants"].size())));current.set("step",number(0));current.set("counts",Json::object());}
            else if(action=="event") {c.authority();auto metric=id(a["metric"]);auto delta=integer(a["amount"],1);auto index=size_t(current.getInt("step"));need(index<variant["steps"].size(),Code::Unavailable,"quest ready");auto step=variant["steps"][index];auto counts=current["counts"];bool relevant=false;for(auto& goal:step["goals"].items())if(goal.getStr("metric")==metric)relevant=true;
                if(relevant)counts.set(metric,number(add(counts.getInt(metric),delta)));bool all=true,any=false;for(auto& goal:step["goals"].items()){bool met=counts.getInt(goal.getStr("metric"))>=goal.getInt("target");all&=met;any|=met;}
                current.set("counts",counts);if(step.getStr("mode")=="all"?all:any){current.set("step",number(int64_t(index+1)));current.set("counts",Json::object());}
            } else if(action=="claim") {need(size_t(current.getInt("step"))==variant["steps"].size(),Code::Insufficient,"quest incomplete");c.pay({},bundle(variant["reward"]));current.set("claimed",Json::fromBool(true));}
            else throw Stop(Code::Unsupported,action);
        }
        c.self.set(c.id,current);return current;
    }
    if(c.req.service=="events") {
        keys(a,{"id","phase"});auto bucket=(c.now-c.def.getInt("start"))/c.def.getInt("period");auto base=c.def.getInt("start")+bucket*c.def.getInt("period");
        if(action=="join") {
            need(active(c.def,c.now),Code::Unavailable,"event unavailable");need(c.def.getBool("allowLate")||c.now==base,Code::Unavailable,"late enrollment disabled");
            if(!current.has("bucket")||current.getInt("bucket")!=bucket)current=row({{"bucket",number(bucket)},{"origin",number(c.def.getBool("personal")?c.now:base)},{"joined",number(c.now)},{"claimed",Json::object()}});
        } else {
            need(action=="claim"&&current.has("bucket"),Code::Unavailable,"join current event");auto phaseId=id(a["phase"]);Time phaseStart=current.getInt("origin");Json phase;
            for(auto& p:c.def["phases"].items()){if(p.getStr("id")==phaseId){phase=p;break;}phaseStart=add(phaseStart,p.getInt("duration"));}
            need(phase.isObject(),Code::Missing,"event phase");auto phaseEnd=add(phaseStart,phase.getInt("duration"));
            need(c.now>=phaseEnd&&c.now-phaseEnd<c.def.getInt("claimSeconds"),Code::Unavailable,"phase not complete or claim expired");need(c.def.getBool("catchUp")||current.getInt("joined")<=phaseStart,Code::Unavailable,"missed phase");
            need(!current["claimed"].has(phaseId),Code::Unavailable,"phase claimed");c.pay(bundle(phase["cost"]),bundle(phase["reward"]));object(current,"claimed").set(phaseId,Json::fromBool(true));
        }
        c.self.set(c.id,current);return current;
    }
    if(c.req.service=="notifications") {
        keys(a,{"id"});if(action=="schedule") {c.authority();current=row({{"due",number(add(c.now,c.def.getInt("delay")))},{"read",number(0)}});}
        else if(action=="ack") {need(current.has("due")&&c.now>=current.getInt("due"),Code::Unavailable,"notification not due");auto repeat=c.def.getInt("repeat");current.set("read",number(repeat?(c.now-current.getInt("due"))/repeat+1:1));}
        else if(action=="cancel") {c.authority();c.self.erase(c.id);return Json::object();}
        else throw Stop(Code::Unsupported,action);
        c.self.set(c.id,current);return current;
    }
    throw Stop(Code::Unsupported,c.req.service);
}
}
