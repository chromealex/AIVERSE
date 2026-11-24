#include "Workspace.h"
#include "engine/actor/ActorPersistentSession.h"
#include "engine/actor/Project.h"
#include "engine/game/GameCore.h"
#include "engine/net/authority/Session.h"
#include "engine/platform/ContentReader.h"
#include "engine/world/WorldPersistenceDiagnostics.h"
#include <filesystem>
#include <charconv>
#include <fstream>

namespace aiverse::editor {
namespace {
namespace fs=std::filesystem;
namespace wd=aiverse::world;
constexpr size_t budget=64u*1024u*1024u;
using Uuid=std::array<uint8_t,16>;
uint64_t u64(const Json& p,const char* key,uint64_t fallback=0){
    if(!p.has(key))return fallback;
    const auto& value=p[key];
    if(!value.isString())throw std::runtime_error(std::string(key)+" must be a decimal string");
    auto s=value.str();uint64_t n=0;
    auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(s.empty()||ec!=std::errc{}||end!=s.data()+s.size())throw std::runtime_error(std::string("invalid ")+key);
    return n;
}
Uuid uuid(const std::string& value){
    Uuid id{};
    if(value.size()!=32)throw std::runtime_error("world must be a 32-character lowercase hex UUID");
    for(size_t i=0;i<id.size();++i){
        auto hex=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
        int a=hex(value[i*2]),b=hex(value[i*2+1]);
        if(a<0||b<0)throw std::runtime_error("invalid lowercase world UUID");
        id[i]=uint8_t(a*16+b);
    }
    if(std::all_of(id.begin(),id.end(),[](auto n){return !n;}))throw std::runtime_error("zero world UUID");
    return id;
}
// All maintenance is confined to a project-owned private directory. Do not
// follow symlinks, including lock and manifest entries. This is a cooperating
// local operator API, not a sandbox against concurrent hostile filesystem edits.
fs::path directory(const Workspace& ws,const std::string& name,bool create){
    uuid(name);
    if(ws.project.dir.empty())throw std::runtime_error("open a project first");
    const auto root=fs::path(ws.project.dir)/"world-saves";
    const auto dir=root/name;
    auto check=[&](const fs::path& p){
        auto status=fs::symlink_status(p);
        if(status.type()==fs::file_type::not_found&&create){
            if(!fs::create_directory(p))throw std::runtime_error("cannot create private world directory");
            status=fs::symlink_status(p);
        }
        if(!fs::is_directory(status)||fs::is_symlink(status))throw std::runtime_error("world directory missing or unsafe");
    };
    check(root);check(dir);
    for(const auto& entry:fs::directory_iterator(dir)){
        if(!fs::is_regular_file(entry.symlink_status()))throw std::runtime_error("unsafe world directory entry");
    }
    return dir;
}
actor::Project readProject(const Workspace& ws){
    if(ws.project.dir.empty()||ws.project.rulesDirty||ws.map.dirty||ws.project.manifestDirty)
        throw std::runtime_error("save project documents before world maintenance");
    const auto reader=aiverse::platform::makeContentReader(ws.project.dir);
    return actor::readProject(reader);
}
struct Offline {
    std::unique_ptr<Simulation> sim;
    std::unique_ptr<actor::ActorPersistentSession> session;
    Offline(const Workspace& ws,const Json& p,bool create=false){
        const auto id=p.getStr("world");const auto world=uuid(id);
        auto project=readProject(ws);
        if(!project.rules.has("regionOwnership"))throw std::runtime_error("project has no region ownership capability");
        const auto reader=aiverse::platform::makeContentReader(ws.project.dir);
        std::string identity="aiverse.actor.portable.v1:"+std::to_string(net::authority::contentId(project.rules,project.map));
        if(project.map["world"].getBool("async")){
            auto assets=wd::WorldAssets::parse(project.map["world"]);std::string error;
            if(!assets.verifyAll(reader,&error))throw std::runtime_error(error);
            identity=wd::WorldAssets::persistenceIdentity(project.rules,project.map);
        }
        const auto dir=directory(ws,id,create);
        sim=std::make_unique<Simulation>(SimConfig{u64(p,"seed",1),actor::readConfig(project.rules).tickRate,0,false},
            std::make_unique<game::GameCore>());
        sim->setup(project.rules,project.map);
        auto* host=actor::tryHost(*sim);
        if(!host)throw std::runtime_error("no supported portable persistence adapter");
        session=std::make_unique<actor::ActorPersistentSession>(*sim,*host,identity,budget);
        if(!session->start(dir,world,!create,false))throw std::runtime_error("world busy, incompatible, incomplete or recovery required");
    }
    Json status()const{
        const auto* state=session->durableState();const auto* manifest=session->durableManifest();
        if(!state||!manifest)throw std::runtime_error("no finalized world");
        auto j=wd::ledgerStatus(state->ledger,state->world);
        j.set("mode",Json::string("offline"));j.set("tick",Json::number(int64_t(sim->tick())));
        j.set("sequence",wd::worldCounter(state->sequence));j.set("generation",wd::worldCounter(manifest->generation));
        j.set("checkpointBytes",wd::worldCounter(manifest->checkpointBytes));
        return j;
    }
};
Json ids(const std::vector<uint64_t>& values){Json j=Json::array();for(auto v:values)j.push(wd::worldCounter(v));return j;}
Json planJson(const wd::RegionRetentionPlan& p){
    Json j=Json::object();j.set("valid",Json::fromBool(p.valid));j.set("overBudget",Json::fromBool(p.overBudget));
    j.set("retainedBytes",wd::worldCounter(p.retainedBytes));j.set("removeGenerations",ids(p.removeGenerations));return j;
}
wd::RegionRetentionPolicy policy(const Json& p){
    auto keep=u64(p,"keepPrevious",2);if(keep>10000)throw std::runtime_error("keepPrevious exceeds tool limit");
    return {size_t(keep),u64(p,"maxBytes")};
}
const char* backupStatus(wd::UnifiedBackupStatus status){
    using S=wd::UnifiedBackupStatus;
    switch(status){
        case S::MetadataOnly:return "metadata-only";case S::Recoverable:return "recoverable";
        case S::WrongWorld:return "wrong-world";case S::MissingCheckpoint:return "missing-checkpoint";
        case S::RecoveryFailed:return "recovery-failed";default:return "invalid-metadata";
    }
}
}
void registerWorldPersistenceOps(Workspace& ws){
    auto add=[&](const char* name,const char* desc,const char* schema,Workspace::OpFn fn){
        ws.registerOp({name,desc,Json::parse(schema),std::move(fn)});
    };
    add("world_save_list","List private persistent world UUIDs under the saved project. No recovery claim.",
        R"({"type":"object","properties":{},"additionalProperties":false})",[&ws](const Json&){
        Json out=Json::object(),rows=Json::array();
        if(ws.project.dir.empty())throw std::runtime_error("open a project first");
        auto root=fs::path(ws.project.dir)/"world-saves";auto st=fs::symlink_status(root);
        if(st.type()!=fs::file_type::not_found){
            if(!fs::is_directory(st)||fs::is_symlink(st))throw std::runtime_error("unsafe world-saves directory");
            std::vector<std::string> names;
            for(const auto& e:fs::directory_iterator(root)){
                if(!fs::is_directory(e.symlink_status()))continue;
                auto name=e.path().filename().string();try{uuid(name);names.push_back(name);}catch(...){}
            }
            std::sort(names.begin(),names.end());
            for(const auto& name:names)rows.push(Json::string(name));
        }
        out.set("worlds",std::move(rows));return out;
    });
    const char* basic=R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"}},"additionalProperties":false})";
    add("world_save_create","Create a new private world from the saved project. Never overwrite existing saves.",basic,[&ws](const Json& p){Offline op(ws,p,true);return op.status();});
    add("world_save_status","Recover offline under exclusive lock, report finalized tick, ledger and region counts.",basic,[&ws](const Json& p){Offline op(ws,p);return op.status();});
    add("world_save_page","Read bounded stable ID bootstrap or revision delta; resetRequired means replace projection.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"after":{"type":"string"},"limit":{"type":"string"},"bootstrap":{"type":"boolean"},"expectedRevision":{"type":"string"}},"additionalProperties":false})",[&ws](const Json& p){
        Offline op(ws,p);const auto* state=op.session->durableState();
        return wd::ledgerPage(state->ledger,u64(p,"after"),size_t(std::min<uint64_t>(1000,u64(p,"limit",100))),p.getBool("bootstrap"),u64(p,"expectedRevision"));
    });
    add("world_save_checkpoint","Rotate checkpoint of an offline verified world; optional retained revision window.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"retainRevisions":{"type":"string"}},"additionalProperties":false})",[&ws](const Json& p){
        Offline op(ws,p);if(!op.session->checkpoint(u64(p,"retainRevisions",65536)))throw std::runtime_error("checkpoint refused; recovery required");return op.status();
    });
    add("world_save_backups","List backup metadata or explicitly verify full checkpoint/WAL recovery.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"verify":{"type":"boolean"}},"additionalProperties":false})",[&ws](const Json& p){
        Offline op(ws,p);auto catalog=op.session->backups(p.getBool("verify"));Json j=Json::object(),rows=Json::array();
        for(const auto& entry:catalog.entries){Json row=Json::object();row.set("generation",wd::worldCounter(entry.generation));row.set("active",Json::fromBool(entry.active));row.set("status",Json::string(backupStatus(entry.status)));
            if(entry.status==wd::UnifiedBackupStatus::Recoverable){row.set("sequence",wd::worldCounter(entry.lastSequence));row.set("revision",wd::worldCounter(entry.lastRevision));}rows.push(std::move(row));}
        j.set("complete",Json::fromBool(catalog.complete));j.set("backups",std::move(rows));return j;
    });
    add("world_save_retention_preview","Plan deletion of recoverable old generations without deleting files.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"keepPrevious":{"type":"string"},"maxBytes":{"type":"string"}},"additionalProperties":false})",[&ws](const Json& p){Offline op(ws,p);return planJson(op.session->retentionPlan(policy(p)));});
    add("world_save_retention_apply","Delete only the exact confirmed generation list from a verified retention preview; revalidate under lock.",
        R"({"type":"object","required":["world","expectedGenerations"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"keepPrevious":{"type":"string"},"maxBytes":{"type":"string"},"expectedGenerations":{"type":"array","items":{"type":"string"}}},"additionalProperties":false})",[&ws](const Json& p){
        if(!p["expectedGenerations"].isArray()||p["expectedGenerations"].size()>10000)throw std::runtime_error("explicit expectedGenerations required");
        std::vector<uint64_t> expected;for(const auto& value:p["expectedGenerations"].items()){Json arg=Json::object();arg.set("generation",value);expected.push_back(u64(arg,"generation"));}
        Offline op(ws,p);auto result=op.session->cleanupRetention(policy(p),expected);auto j=planJson(result.plan);
        j.set("ok",Json::fromBool(result.ok));j.set("removed",ids(result.removed));j.set("failedGeneration",wd::worldCounter(result.failedGeneration));return j;
    });
    add("world_save_fork","Restore a verified generation into an independent empty project world UUID. Source unchanged.",
        R"({"type":"object","required":["world","generation","newWorld"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"generation":{"type":"string"},"newWorld":{"type":"string"}},"additionalProperties":false})",[&ws](const Json& p){
        const auto dest=p.getStr("newWorld");const auto id=uuid(dest);if(dest==p.getStr("world"))throw std::runtime_error("new world UUID must differ");
        Offline op(ws,p);const auto generation=u64(p,"generation");if(!generation)throw std::runtime_error("generation required");
        auto path=directory(ws,dest,true);
        if(op.session->forkGeneration(generation,path,id)!=wd::RegionWalWrite::Ok)throw std::runtime_error("fork refused");
        Json j=Json::object();j.set("ok",Json::fromBool(true));j.set("world",Json::string(dest));return j;
    });
}
}
