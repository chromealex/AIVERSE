#pragma once
#include "apps/editor/core/WorldInspect.h"
#include <charconv>
#include <stdexcept>

namespace aiverse::studio {
// UI projection of the hosted world's real entities; never an editable mirror.
class StudioRuntimeInspect {
    editor::WorldModel model_;
public:
    Json entities(Simulation* sim,const std::string& epoch,int offset,int limit,const std::string& query) {
        if(offset<0 || limit<1 || limit>100 || query.size()>256)
            throw std::runtime_error("Invalid runtime query");
        Json out=Json::object(),rows=Json::array();int total=0;
        auto* mod=sim?dynamic_cast<game::RtsModule*>(&sim->module()):nullptr;
        out.set("active",Json::fromBool(mod!=nullptr));out.set("epoch",Json::string(epoch));
        out.set("tick",Json::number(int64_t(sim?sim->tick():0)));
        if(mod) {
            editor::readWorld(*sim,*mod,{}, {},{},nullptr,model_,{false});
            for(const auto& row:model_.rows) {
                if(!editor::worldRowMatches(model_,mod->rules(),nullptr,row,query))continue;
                const int i=total++;
                if(i<offset || i>=offset+limit)continue;
                Json item=Json::object();item.set("ref",Json::string(std::to_string(row.ref)));
                item.set("name",Json::string(editor::worldRowName(model_,mod->rules(),nullptr,row)));
                item.set("kind",Json::string(editor::worldKindId(row.kind)));
                item.set("owner",Json::number(int64_t(row.owner)));
                item.set("parent",Json::string(row.parent?std::to_string(row.parent):""));
                item.set("health",Json::number(double(row.hp)));
                Json components=Json::array();row.eachComp([&](int c){components.push(Json::string(editor::worldComponents()[size_t(c)].id));});
                item.set("components",std::move(components));rows.push(std::move(item));
            }
        }else model_={};
        out.set("rows",std::move(rows));out.set("total",Json::number(int64_t(total)));return out;
    }
    Json inspect(Simulation* sim,const std::string& currentEpoch,const std::string& epoch,const std::string& ref) {
        if(epoch!=currentEpoch)throw std::runtime_error("This entity belongs to a previous game session");
        uint64_t packed=0;auto parsed=std::from_chars(ref.data(),ref.data()+ref.size(),packed);
        if(parsed.ec!=std::errc{} || parsed.ptr!=ref.data()+ref.size() || packed==0)
            throw std::runtime_error("Invalid runtime entity reference");
        auto* mod=sim?dynamic_cast<game::RtsModule*>(&sim->module()):nullptr;
        if(!mod)throw std::runtime_error("No inspectable game is running");
        const Entity entity=editor::worldEntity(packed);
        if(!sim->world().alive(entity))throw std::runtime_error("Runtime entity no longer exists");
        Json out=editor::inspectEntity(*sim,*mod,entity,Json::object());
        out.set("ref",Json::string(ref));out.set("epoch",Json::string(epoch));
        out.set("tick",Json::number(int64_t(sim->tick())));return out;
    }
};
}
