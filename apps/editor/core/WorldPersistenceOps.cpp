#include "Workspace.h"
#include "WorldOperatorJobs.h"
#include <chrono>
#include <thread>
#include <map>
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
using Control=WorldOperatorJobs::Control;
struct SavedProject {std::string root;actor::Project project;};
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
fs::path directory(const SavedProject& ws,const std::string& name,bool create){
    uuid(name);
    if(ws.root.empty())throw std::runtime_error("open a project first");
    const auto root=fs::path(ws.root)/"world-saves";
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
    // Match the server CLI: project rules and map are read from saved files;
    // only the external asset reader may resolve content.rpak.
    return actor::readProject([&](const std::string& name,std::string& out){
        if(!wd::safeAssetPath(name))return false;
        const auto root=fs::canonical(ws.project.dir);
        const auto path=fs::weakly_canonical(root/name);
        auto relative=path.lexically_relative(root);
        if(relative.empty()||*relative.begin()==".."||relative.is_absolute())return false;
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input)return false;
        const auto size=input.tellg();
        if(size<0||uint64_t(size)>32u*1024u*1024u)return false;
        out.resize(size_t(size));input.seekg(0);
        return bool(input.read(out.data(),std::streamsize(out.size())));
    });
}
SavedProject snapshot(const Workspace& ws,bool listOnly=false){
    if(ws.project.dir.empty())throw std::runtime_error("open a project first");
    SavedProject saved;saved.root=ws.project.dir;
    if(!listOnly)saved.project=readProject(ws);
    return saved;
}
void check(Control* job){if(job)job->check();}
void irreversible(Control* job,const char* phase){if(job)job->irreversible(phase);}
struct Offline {
    std::unique_ptr<Simulation> sim;
    std::unique_ptr<actor::ActorPersistentSession> session;
    Offline(const SavedProject& ws,const Json& p,bool create=false,Control* job=nullptr){
        check(job);
        const auto id=p.getStr("world");const auto world=uuid(id);
        const auto& project=ws.project;
        if(!project.rules.has("regionOwnership"))throw std::runtime_error("project has no region ownership capability");
        const auto reader=aiverse::platform::makeContentReader(ws.root);
        std::string identity="aiverse.actor.portable.v1:"+std::to_string(net::authority::contentId(project.rules,project.map));
        if(project.map["world"].getBool("async")){
            auto assets=wd::WorldAssets::parse(project.map["world"]);std::string error;
            size_t files=0;
            if(job)job->progress("asset-audit");
            auto observed=[&](const std::string& file,std::string& out){
                if(job&&job->cancelled())return false;
                if(!reader(file,out))return false;
                if(job){Json p=Json::object();p.set("assetsRead",wd::worldCounter(++files));
                    p.set("assetsTotal",wd::worldCounter(assets.hashes.size()));job->progress("asset-audit",std::move(p));}
                return true;
            };
            if(!assets.verifyAll(observed,&error)){check(job);throw std::runtime_error(error);}
            identity=wd::WorldAssets::persistenceIdentity(project.rules,project.map);
        }
        check(job);
        if(job)job->progress("setup");
        sim=std::make_unique<Simulation>(SimConfig{u64(p,"seed",1),actor::readConfig(project.rules).tickRate,0,false},
            std::make_unique<game::GameCore>());
        sim->setup(project.rules,project.map);
        auto* host=actor::tryHost(*sim);
        if(!host)throw std::runtime_error("no supported portable persistence adapter");
        session=std::make_unique<actor::ActorPersistentSession>(*sim,*host,identity,budget);
        check(job);
        if(create)irreversible(job,"creating");
        const auto dir=directory(ws,id,create);
        if(job&&!create){
            using R=actor::ActorPersistentSession::RecoveryResult;
            if(session->beginRecovery(dir,world)!=R::Loading)throw std::runtime_error("world busy or recovery refused");
            auto result=R::Loading;
            while(result==R::Loading){
                if(job->cancelled())session->cancelRecovery();
                const auto progress=session->recoveryProgress();
                Json p=Json::object();
                p.set("checkpointBytes",wd::worldCounter(progress.checkpointBytes));
                p.set("checkpointTotal",wd::worldCounter(progress.checkpointTotal));
                p.set("walBytes",wd::worldCounter(progress.walBytes));
                p.set("frames",wd::worldCounter(progress.frames));
                p.set("sequence",wd::worldCounter(progress.sequence));
                using Phase=wd::UnifiedWalSession::RecoveryPhase;
                const char* phase=progress.phase==Phase::Checkpoint?"checkpoint-read":
                    progress.phase==Phase::Replay?"wal-replay":progress.phase==Phase::OpenWriter?"open-writer":"recovery-validation";
                job->progress(phase,std::move(p));
                result=session->pollRecovery();
                if(result==R::Loading)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            check(job);
            if(result!=R::Ready)throw std::runtime_error("world recovery failed");
        }else if(!session->start(dir,world,!create,false))throw std::runtime_error("world busy, incompatible, incomplete or recovery required");
        if(job&&!create)job->progress("maintenance");
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
    using Operation=std::function<Json(const SavedProject&,const Json&,Control*)>;
    auto jobs=std::make_shared<WorldOperatorJobs>();
    std::map<std::string,Operation> operations;
    auto add=[&](const char* name,const char* desc,const char* schema,Operation fn){
        operations.emplace(name,fn);
        ws.registerOp({name,desc,Json::parse(schema),[&ws,jobs,fn,name=std::string(name)](const Json& p){
            if(jobs->occupied())throw std::runtime_error("world job slot occupied; poll/release it first");
            return fn(snapshot(ws,name=="world_save_list"),p,nullptr);
        }});
    };
    add("world_save_list","List private persistent world UUIDs under the saved project. No recovery claim.",
        R"({"type":"object","properties":{},"additionalProperties":false})",[](const SavedProject& ws,const Json&,Control* job){
        check(job);
        Json out=Json::object(),rows=Json::array();
        if(ws.root.empty())throw std::runtime_error("open a project first");
        auto root=fs::path(ws.root)/"world-saves";auto st=fs::symlink_status(root);
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
    add("world_save_create","Create a new private world from the saved project. Never overwrite existing saves.",basic,[](const SavedProject& ws,const Json& p,Control* job){Offline op(ws,p,true,job);return op.status();});
    add("world_save_status","Recover offline under exclusive lock, report finalized tick, ledger and region counts.",basic,[](const SavedProject& ws,const Json& p,Control* job){Offline op(ws,p,false,job);return op.status();});
    add("world_save_page","Read bounded stable ID bootstrap or revision delta; resetRequired means replace projection.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"after":{"type":"string"},"limit":{"type":"string"},"bootstrap":{"type":"boolean"},"expectedRevision":{"type":"string"}},"additionalProperties":false})",[](const SavedProject& ws,const Json& p,Control* job){
        Offline op(ws,p,false,job);const auto* state=op.session->durableState();
        return wd::ledgerPage(state->ledger,u64(p,"after"),size_t(std::min<uint64_t>(1000,u64(p,"limit",100))),p.getBool("bootstrap"),u64(p,"expectedRevision"));
    });
    add("world_save_checkpoint","Rotate checkpoint of an offline verified world; optional retained revision window.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"retainRevisions":{"type":"string"}},"additionalProperties":false})",[](const SavedProject& ws,const Json& p,Control* job){
        Offline op(ws,p,false,job);irreversible(job,"checkpoint-write");if(!op.session->checkpoint(u64(p,"retainRevisions",65536)))throw std::runtime_error("checkpoint refused; recovery required");return op.status();
    });
    add("world_save_backups","List backup metadata or explicitly verify full checkpoint/WAL recovery.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"verify":{"type":"boolean"}},"additionalProperties":false})",[](const SavedProject& ws,const Json& p,Control* job){
        Offline op(ws,p,false,job);if(job)job->progress("backup-scan");auto catalog=op.session->backups(p.getBool("verify"));Json j=Json::object(),rows=Json::array();
        for(const auto& entry:catalog.entries){Json row=Json::object();row.set("generation",wd::worldCounter(entry.generation));row.set("active",Json::fromBool(entry.active));row.set("status",Json::string(backupStatus(entry.status)));
            if(entry.status==wd::UnifiedBackupStatus::Recoverable){row.set("sequence",wd::worldCounter(entry.lastSequence));row.set("revision",wd::worldCounter(entry.lastRevision));}rows.push(std::move(row));}
        j.set("complete",Json::fromBool(catalog.complete));j.set("backups",std::move(rows));return j;
    });
    add("world_save_retention_preview","Plan deletion of recoverable old generations without deleting files.",
        R"({"type":"object","required":["world"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"keepPrevious":{"type":"string"},"maxBytes":{"type":"string"}},"additionalProperties":false})",[](const SavedProject& ws,const Json& p,Control* job){Offline op(ws,p,false,job);if(job)job->progress("retention-preview");return planJson(op.session->retentionPlan(policy(p)));});
    add("world_save_retention_apply","Delete only the exact confirmed generation list from a verified retention preview; revalidate under lock.",
        R"({"type":"object","required":["world","expectedGenerations"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"keepPrevious":{"type":"string"},"maxBytes":{"type":"string"},"expectedGenerations":{"type":"array","items":{"type":"string"}}},"additionalProperties":false})",[](const SavedProject& ws,const Json& p,Control* job){
        if(!p["expectedGenerations"].isArray()||p["expectedGenerations"].size()>10000)throw std::runtime_error("explicit expectedGenerations required");
        std::vector<uint64_t> expected;for(const auto& value:p["expectedGenerations"].items()){Json arg=Json::object();arg.set("generation",value);expected.push_back(u64(arg,"generation"));}
        Offline op(ws,p,false,job);const auto retentionPolicy=policy(p);
        irreversible(job,"retention-apply");auto result=op.session->cleanupRetention(retentionPolicy,expected);auto j=planJson(result.plan);
        j.set("ok",Json::fromBool(result.ok));j.set("removed",ids(result.removed));j.set("failedGeneration",wd::worldCounter(result.failedGeneration));return j;
    });
    add("world_save_fork","Restore a verified generation into an independent empty project world UUID. Source unchanged.",
        R"({"type":"object","required":["world","generation","newWorld"],"properties":{"world":{"type":"string"},"seed":{"type":"string"},"generation":{"type":"string"},"newWorld":{"type":"string"}},"additionalProperties":false})",[](const SavedProject& ws,const Json& p,Control* job){
        const auto dest=p.getStr("newWorld");const auto id=uuid(dest);if(dest==p.getStr("world"))throw std::runtime_error("new world UUID must differ");
        Offline op(ws,p,false,job);const auto generation=u64(p,"generation");if(!generation)throw std::runtime_error("generation required");
        irreversible(job,"fork-write");auto path=directory(ws,dest,true);
        if(op.session->forkGeneration(generation,path,id)!=wd::RegionWalWrite::Ok)throw std::runtime_error("fork refused");
        Json j=Json::object();j.set("ok",Json::fromBool(true));j.set("world",Json::string(dest));return j;
    });
    ws.registerOp({"world_job_start","Start one bounded background world operation using the saved project snapshot. Poll status then release terminal result.",
        Json::parse(R"({"type":"object","required":["operation","params"],"properties":{"operation":{"type":"string"},"params":{"type":"object"}},"additionalProperties":false})"),
        [&ws,jobs,operations](const Json& p){
            if(jobs->occupied())throw std::runtime_error("world job slot occupied; release terminal result first");
            const auto name=p.getStr("operation");auto it=operations.find(name);
            if(it==operations.end()||!p["params"].isObject())throw std::runtime_error("unsupported world job operation");
            auto saved=snapshot(ws,name=="world_save_list");
            auto root=saved.root;auto args=p["params"];auto fn=it->second;
            const auto id=jobs->start(root,name,[saved=std::move(saved),args=std::move(args),fn=std::move(fn)](Control& control){
                return fn(saved,args,&control);
            });
            Json out=Json::object();out.set("id",wd::worldCounter(id));return out;
        }});
    const auto jobSchema=Json::parse(R"({"type":"object","required":["id"],"properties":{"id":{"type":"string"}},"additionalProperties":false})");
    ws.registerOp({"world_job_status","Poll actual background phase, recovery counters and terminal result. Omit id to discover the occupied slot.",
        Json::parse(R"({"type":"object","properties":{"id":{"type":"string"}},"additionalProperties":false})"),
        [jobs](const Json& p){return jobs->status(u64(p,"id"));}});
    ws.registerOp({"world_job_cancel","Request cancellation only before the irreversible barrier.",jobSchema,[jobs](const Json& p){
        Json out=Json::object();out.set("accepted",Json::fromBool(jobs->cancel(u64(p,"id"))));return out;
    }});
    ws.registerOp({"world_job_release","Release a terminal job result and free the single job slot.",jobSchema,[jobs](const Json& p){
        Json out=Json::object();out.set("released",Json::fromBool(jobs->release(u64(p,"id"))));return out;
    }});
}
}
