#pragma once
#include "apps/editor/core/WorldInspect.h"
#include "engine/world/WorldPersistenceDiagnostics.h"
#include <sstream>
#include <charconv>
#include <stdexcept>

namespace aiverse::studio {
// UI projection of the hosted world's real entities; never an editable mirror.
class StudioRuntimeInspect {
    editor::WorldModel model_;
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
public:
    Json entities(Simulation* sim,const std::string& epoch,int offset,int limit,const std::string& query) {
        if(offset<0 || limit<1 || limit>100 || query.size()>256)
            throw std::runtime_error("Invalid runtime query");
        Json out=Json::object(),rows=Json::array();int total=0;
        auto* mod=sim?dynamic_cast<game::GameCore*>(&sim->module()):nullptr;
        out.set("active",Json::fromBool(mod!=nullptr));out.set("epoch",Json::string(epoch));
        out.set("tick",Json::number(int64_t(sim?sim->tick():0)));
        if(mod) {
            editor::readWorld(*sim,*mod,{}, {},{},nullptr,model_,{false});
            const World& w=sim->world();
            const auto* ledger=w.resourceConst<world::RegionEntityLedger>();
            const Filter filter(query);
            for(const auto& row:model_.rows) {
                if(!filter.matches(w,ledger,row.e,row.ref)||
                   !editor::worldRowMatches(model_,mod->rules(),nullptr,row,filter.shared))continue;
                const int i=total++;
                if(i<offset || i>=offset+limit)continue;
                Json item=Json::object();item.set("ref",Json::string(std::to_string(row.ref)));
                item.set("name",Json::string(editor::worldRowName(model_,mod->rules(),nullptr,row)));
                item.set("kind",Json::string(editor::worldKindId(row.kind)));
                item.set("owner",Json::number(int64_t(row.owner)));
                item.set("parent",Json::string(row.parent?std::to_string(row.parent):""));
                item.set("health",Json::number(double(row.hp)));
                item.set("ownership",ownership(w,ledger,row.e));
                Json components=Json::array();row.eachComp([&](int c){components.push(Json::string(editor::worldComponents()[size_t(c)].id));});
                item.set("components",std::move(components));rows.push(std::move(item));
            }
        }else model_={};
        out.set("rows",std::move(rows));out.set("total",Json::number(int64_t(total)));return out;
    }
    Json inspect(Simulation* sim,const std::string& currentEpoch,const std::string& epoch,const std::string& ref) {
        if(epoch!=currentEpoch)throw std::runtime_error("This entity belongs to a previous game session");
        if(ref.empty()||ref.size()>20)throw std::runtime_error("Invalid runtime entity reference");
        uint64_t packed=0;auto parsed=std::from_chars(ref.data(),ref.data()+ref.size(),packed);
        if(parsed.ec!=std::errc{} || parsed.ptr!=ref.data()+ref.size() || packed==0)
            throw std::runtime_error("Invalid runtime entity reference");
        auto* mod=sim?dynamic_cast<game::GameCore*>(&sim->module()):nullptr;
        if(!mod)throw std::runtime_error("No inspectable game is running");
        const Entity entity=editor::worldEntity(packed);
        if(!sim->world().alive(entity))throw std::runtime_error("Runtime entity no longer exists");
        Json out=editor::inspectEntity(*sim,*mod,entity,Json::object());
        const World& w=sim->world();
        out.set("ownership",ownership(w,w.resourceConst<world::RegionEntityLedger>(),entity));
        out.set("ref",Json::string(std::to_string(packed)));out.set("epoch",Json::string(epoch));
        out.set("tick",Json::number(int64_t(sim->tick())));return out;
    }
};
}
