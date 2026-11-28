#pragma once
#include "StudioHierarchyGroups.h"
#include "StudioEntityNesting.h"
#include "apps/editor/core/WorldInspect.h"
#include "engine/world/WorldPersistenceDiagnostics.h"
#include "engine/game/Script.h"
#include <map>
#include <sstream>
#include <charconv>
#include <stdexcept>

namespace aiverse::studio {
// UI projection of the hosted world's real entities; never an editable mirror.
class StudioRuntimeInspect {
    editor::WorldModel model_;
    std::vector<uint64_t> selection_;
    std::string selectedEpoch_;
    struct Filter {
        std::string shared;
        std::vector<std::string> stable,cell,handle;
        explicit Filter(const std::string& query){
            std::istringstream tokens(query);std::string token;
            while(tokens>>token){
                if(token.starts_with("sid:"))stable.push_back(token.substr(4));
                else if(token.starts_with("cell:"))cell.push_back(token.substr(5));
                else if(token.starts_with("ref:"))handle.push_back(token.substr(4));
                else {if(!shared.empty())shared+=' ';shared+=token;}
            }
        }
        bool matches(const World& w,const world::RegionEntityLedger* ledger,Entity e,uint64_t ref)const{
            for(const auto& value:handle)if(value!=std::to_string(ref))return false;
            if(stable.empty()&&cell.empty())return true;
            const auto* tag=w.tryGetConst<world::RegionStableIdentity>(e);
            if(!tag||!tag->id)return false;
            for(const auto& value:stable)if(value!=std::to_string(tag->id))return false;
            if(cell.empty())return true;
            const auto* record=ledger?ledger->find(tag->id):nullptr;
            if(!record||!record->live)return false;
            const auto& c=record->owner;
            const auto key=std::to_string(c.x)+","+std::to_string(c.y)+","+std::to_string(c.z);
            for(const auto& value:cell)if(value!=key)return false;
            return true;
        }
    };
    // Read only existing component/resource pools; no rebuild or identity allocation.
    static Json ownership(const World& w,const world::RegionEntityLedger* ledger,Entity e){
        Json out=Json::object();
        const auto* tag=w.tryGetConst<world::RegionStableIdentity>(e);
        const bool dormant=w.tryGetConst<world::RegionDormant>(e)!=nullptr;
        out.set("dormantMarker",Json::fromBool(dormant));
        if(!tag||!tag->id){
            out.set("state",Json::string(tag?"invalid-tag":"untracked"));return out;
        }
        out.set("stableId",world::worldCounter(tag->id));
        const auto* record=ledger?ledger->find(tag->id):nullptr;
        if(!ledger){out.set("state",Json::string("ledger-unavailable"));return out;}
        if(!record){
            out.set("state",Json::string(ledger->isRetired(tag->id)?"retired":"missing-record"));return out;
        }
        out.set("state",Json::string(record->live?"live-record":"retired"));
        out.set("cell",world::worldCell(record->owner));
        out.set("revision",world::worldCounter(record->revision));
        return out;
    }
    static uint64_t checkedRef(Simulation* sim,const std::string& currentEpoch,const std::string& epoch,const std::string& ref) {
        if(epoch!=currentEpoch)throw std::runtime_error("This entity belongs to a previous game session");
        if(ref.empty()||ref.size()>20)throw std::runtime_error("Invalid runtime entity reference");
        uint64_t packed=0;auto parsed=std::from_chars(ref.data(),ref.data()+ref.size(),packed);
        if(parsed.ec!=std::errc{} || parsed.ptr!=ref.data()+ref.size() || packed==0)
            throw std::runtime_error("Invalid runtime entity reference");
        auto* mod=sim?dynamic_cast<game::GameCore*>(&sim->module()):nullptr;
        if(!mod)throw std::runtime_error("No inspectable game is running");
        const Entity entity=editor::worldEntity(packed);
        if(!sim->world().alive(entity))throw std::runtime_error("Runtime entity no longer exists");
        return packed;
    }
public:
    void select(Simulation* sim,const std::string& currentEpoch,const std::string& epoch,const std::string& ref) {
        selectMany(sim,currentEpoch,epoch,{ref});
    }
    size_t selectMany(Simulation* sim,const std::string& currentEpoch,const std::string& epoch,const std::vector<std::string>& refs) {
        if(epoch!=currentEpoch)throw std::runtime_error("Runtime session changed");
        std::vector<uint64_t> selected;
        std::unordered_set<uint64_t> seen;
        for(const auto& ref:refs){
            const auto id=checkedRef(sim,currentEpoch,epoch,ref);
            if(seen.insert(id).second)selected.push_back(id);
        }
        selection_=std::move(selected);
        selectedEpoch_=epoch;
        return selection_.size();
    }
    std::string selectedRef(Simulation* sim,const std::string& epoch) const {
        const auto refs=selectedRefs(sim,epoch);
        return refs.empty()?std::string{}:refs.front();
    }
    std::vector<std::string> selectedRefs(Simulation* sim,const std::string& epoch) const {
        std::vector<std::string> refs;
        if(sim && epoch==selectedEpoch_)
            for(auto id:selection_)if(sim->world().alive(editor::worldEntity(id)))refs.push_back(std::to_string(id));
        return refs;
    }
    struct GroupQuery {
        editor::WorldConfig config;
        std::vector<std::string> keys, path;
        std::unordered_set<uint64_t> collapsed;
    };
    Json entities(Simulation* sim,const std::string& epoch,int offset,int limit,const std::string& query,
                  const GroupQuery& group={},const std::vector<uint64_t>& setupRefs={}) {
        if(offset<0 || limit<1 || limit>100 || query.size()>256)
            throw std::runtime_error("Invalid runtime query");
        Json out=Json::object(),rows=Json::array();int total=0;
        auto* mod=sim?dynamic_cast<game::GameCore*>(&sim->module()):nullptr;
        out.set("active",Json::fromBool(mod!=nullptr));out.set("epoch",Json::string(epoch));
        out.set("tick",Json::number(int64_t(sim?sim->tick():0)));
        if(mod) {
            std::vector<uint64_t> links;
            links.reserve(mod->mapPlaced().size());
            for(const Entity e:mod->mapPlaced())links.push_back(e.valid()?editor::worldRef(e):0);
            editor::readWorld(*sim,*mod,group.config,links,setupRefs,nullptr,model_,{false});
            const bool scriptData=!group.keys.empty() && (!group.config.labelKey.empty() ||
                std::any_of(group.keys.begin(),group.keys.end(),[](const auto& key){return key.starts_with("data:");}));
            if(scriptData && mod->scripts()){
                std::string blob;
                for(auto& row:model_.rows)
                    if(mod->scripts()->peekData(*sim,row.e,blob))editor::setWorldRowData(group.config,blob,row);
            }
            std::map<std::string,int> children;
            const bool nesting=group.keys==std::vector<std::string>{"parent"};
            const bool hasChildren=!nesting && group.path.size()<group.keys.size();
            const World& w=sim->world();
            const auto* ledger=w.resourceConst<world::RegionEntityLedger>();
            const Filter filter(query);
            auto matches=[&](const editor::WorldRow& row){
                return filter.matches(w,ledger,row.e,row.ref) && editor::worldRowMatches(model_,mod->rules(),nullptr,row,filter.shared);
            };
            std::vector<NestedEntityRow> nested;
            if(nesting){
                std::vector<const editor::WorldRow*> matched;
                for(const auto& row:model_.rows)if(matches(row))matched.push_back(&row);
                nested=nestedEntities(matched,w,group.collapsed);
            }
            for(size_t index=0;index<(nesting?nested.size():model_.rows.size());++index){
                const auto& row=nesting?*nested[index].entity:model_.rows[index];
                if(!nesting && !matches(row))continue;
                bool matchesPath=true;
                for(size_t depth=0;depth<group.path.size();++depth){
                    bool inGroup=false;
                    visitHierarchyLabels(model_,mod->rules(),row,group.keys.at(depth),[&](const auto& label){
                        if(group.path[depth]==label)inGroup=true;
                    });
                    if(!inGroup){
                        matchesPath=false;
                        break;
                    }
                }
                if(!matchesPath)continue;
                const int i=total++;
                if(hasChildren){
                    visitHierarchyLabels(model_,mod->rules(),row,group.keys[group.path.size()],
                                         [&](const auto& label){ ++children[label]; });
                    continue;
                }
                if(i<offset || i>=offset+limit)continue;
                Json item=Json::object();item.set("ref",Json::string(std::to_string(row.ref)));
                item.set("name",Json::string(editor::worldRowName(model_,mod->rules(),nullptr,row)));
                item.set("kind",Json::string(editor::worldKindId(row.kind)));
                item.set("doc",Json::number(int64_t(row.doc)));
                item.set("origin",Json::string(row.origin==editor::WorldOrigin::Map?"map":row.origin==editor::WorldOrigin::Setup?"setup":"runtime"));
                item.set("owner",Json::number(int64_t(row.owner)));
                item.set("parent",Json::string(row.parent?std::to_string(row.parent):""));
                if(nesting){
                    item.set("depth",Json::number(int64_t(nested[index].depth)));
                    item.set("children",Json::number(int64_t(nested[index].children)));
                }
                item.set("health",Json::number(double(row.hp)));
                item.set("ownership",ownership(w,ledger,row.e));
                Json components=Json::array();row.eachComp([&](int c){components.push(Json::string(editor::worldComponents()[size_t(c)].id));});
                item.set("components",std::move(components));rows.push(std::move(item));
            }
            if(hasChildren){
                Json groups=Json::array();
                int index=0;
                for(const auto& [name,count]:children){
                    if(index++<offset)continue;
                    if(groups.size()>=size_t(limit))break;
                    Json entry=Json::object();
                    entry.set("name",Json::string(name));
                    entry.set("count",Json::number(int64_t(count)));
                    groups.push(std::move(entry));
                }
                out.set("groups",std::move(groups));
                out.set("groupCount",Json::number(int64_t(children.size())));
            }
        }else model_={};
        out.set("rows",std::move(rows));out.set("total",Json::number(int64_t(total)));return out;
    }
    Json inspect(Simulation* sim,const std::string& currentEpoch,const std::string& epoch,const std::string& ref) {
        const auto packed=checkedRef(sim,currentEpoch,epoch,ref);
        auto* mod=dynamic_cast<game::GameCore*>(&sim->module());
        const Entity entity=editor::worldEntity(packed);
        Json out=editor::inspectEntity(*sim,*mod,entity,Json::object());
        const World& w=sim->world();
        out.set("ownership",ownership(w,w.resourceConst<world::RegionEntityLedger>(),entity));
        out.set("ref",Json::string(std::to_string(packed)));out.set("epoch",Json::string(epoch));
        out.set("tick",Json::number(int64_t(sim->tick())));return out;
    }
};
}
