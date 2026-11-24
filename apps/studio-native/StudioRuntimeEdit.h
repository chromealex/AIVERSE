#pragma once
#include "engine/game/UnitVitals.h"
#include "engine/world/RegionEcsBridge.h"
#include <charconv>
#include <stdexcept>
#include <string>

namespace aiverse::studio {
// Only trusted, paused local-host calls may invoke this adapter. Never expose
// it to player commands, network peers or arbitrary component memory writes.
class StudioRuntimeEdit {
    struct Values {Fixed hp,energy,maxHp,maxEnergy;};
    static bool values(Simulation& sim,Entity e,Values& result){
        auto* core=dynamic_cast<game::GameCore*>(&sim.module());
        const World& w=sim.world();
        const auto* unit=w.tryGetConst<game::Unit>(e);
        if(!core||!unit||!w.tryGetConst<Transform>(e)||
           w.tryGetConst<world::RegionDormant>(e)||unit->hp.raw<=0||
           (unit->flags&(game::kUnitOffMap|game::kUnitInTransit|game::kUnitSilent))||
           size_t(unit->type)>=core->rules().types.size())return false;
        result.hp=unit->hp;result.energy=unit->energy;
        result.maxEnergy=core->rules().types[unit->type].maxEnergy;
        result.maxHp=core->stat(sim,e,game::Stat::MaxHp);
        return true;
    }
    static Fixed decimal(const std::string& s){
        // Limit digits before Fixed::parse (which assumes bounded data).
        if(s.empty()||s.size()>17)throw std::runtime_error("Invalid decimal value");
        size_t integral=0,fraction=0;bool dot=false;
        for(char c:s){
            if(c=='.'&&!dot&&integral){dot=true;continue;}
            if(c<'0'||c>'9')throw std::runtime_error("Use a nonnegative decimal");
            if(dot)++fraction;else ++integral;
        }
        if(!integral||integral>9||fraction>7||(dot&&!fraction))
            throw std::runtime_error("Use up to 9 integer and 7 fractional digits");
        bool ok=false;const Fixed out=Fixed::parse(s,&ok);
        if(!ok)throw std::runtime_error("Invalid decimal value");
        return out;
    }
    static Json field(const char* id,const char* title,Fixed value,Fixed minimum,Fixed maximum){
        Json j=Json::object();j.set("id",Json::string(id));j.set("title",Json::string(title));
        j.set("raw",Json::string(std::to_string(value.raw)));
        j.set("value",Json::number(value.toDouble(),6));
        j.set("min",Json::number(minimum.toDouble(),6));j.set("max",Json::number(maximum.toDouble(),6));
        return j;
    }
public:
    static Json describe(Simulation& sim,Entity e){
        Json fields=Json::array();Values v{};
        if(!values(sim,e,v))return fields;
        if(v.maxHp.raw>0)fields.push(field("hp","Unit HP",v.hp,Fixed::fromRaw(1),v.maxHp));
        if(v.maxEnergy.raw>0)fields.push(field("energy","Unit Energy",v.energy,Fixed{},v.maxEnergy));
        return fields;
    }
    static Json apply(Simulation& sim,Entity e,uint32_t expectedTick,const std::string& id,
                      const std::string& expectedRaw,const std::string& value){
        if(sim.tick()!=expectedTick)throw std::runtime_error("Simulation tick changed; refresh before editing");
        Values v{};
        if(!values(sim,e,v))throw std::runtime_error("Entity is not eligible for runtime vital editing");
        if(id!="hp"&&id!="energy")throw std::runtime_error("Runtime field is not allowlisted");
        const bool hp=id=="hp";
        const Fixed before=hp?v.hp:v.energy,maximum=hp?v.maxHp:v.maxEnergy;
        if(expectedRaw!=std::to_string(before.raw))throw std::runtime_error("Field changed; refresh before editing");
        const Fixed next=decimal(value);
        if(maximum.raw<=0||next.raw<(hp?1:0)||next>maximum)
            throw std::runtime_error("Value outside the current domain bounds");
        auto& core=static_cast<game::GameCore&>(sim.module());
        if(!(hp?game::setUnitHp(core,sim,e,next):game::setUnitEnergy(core,sim,e,next)))
            throw std::runtime_error("Owning domain refused the value");
        const auto* after=sim.world().tryGetConst<game::Unit>(e);
        return field(id.c_str(),hp?"Unit HP":"Unit Energy",hp?after->hp:after->energy,
                     hp?Fixed::fromRaw(1):Fixed{},maximum);
    }
};
}
