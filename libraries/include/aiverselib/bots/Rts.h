#pragma once
#include "Config.h"
#include "modules/rts/RtsAI.h"
namespace aiverse::lib::bots {
inline game::AIConfig rtsConfig(const Json& j) {
    keys(j,{"preset","thinkTicks","workersPerBase","firstAttackArmy","armyGrowth","maxAttackArmy","aggression","towers","scouts"});
    game::AIConfig c;auto p=preset(j);
    if(p=="easy"){c.thinkTicks=30;c.workersPerBase=10;c.firstAttackArmy=12;c.aggression=Fixed::fromRatio(3,4);}
    else if(p=="hard"){c.thinkTicks=5;c.workersPerBase=18;c.firstAttackArmy=6;c.maxAttackArmy=24;}
    else if(p=="rush"){c.thinkTicks=6;c.firstAttackArmy=4;c.armyGrowth=2;c.aggression=Fixed::fromRatio(3,2);}
    else if(p=="defender"){c.firstAttackArmy=14;c.maxAttackArmy=24;c.towers=3;c.aggression=Fixed::fromRatio(3,4);}
    else if(p=="economist"){c.workersPerBase=22;c.firstAttackArmy=16;c.maxAttackArmy=28;}
    else if(p!="normal")throw JsonError("bots.preset: unknown RTS preset '"+p+"'");
    c.thinkTicks=uint32_t(integer(j,"thinkTicks",c.thinkTicks,1,3600));
    c.workersPerBase=int(integer(j,"workersPerBase",c.workersPerBase,1,100));
    c.firstAttackArmy=int(integer(j,"firstAttackArmy",c.firstAttackArmy,1,1000));
    c.armyGrowth=int(integer(j,"armyGrowth",c.armyGrowth,0,100));
    c.maxAttackArmy=int(integer(j,"maxAttackArmy",c.maxAttackArmy,c.firstAttackArmy,1000));
    c.aggression=number(j,"aggression",c.aggression,Fixed::fromRatio(1,10),4_fx);
    c.towers=int(integer(j,"towers",c.towers,0,100));c.scouts=int(integer(j,"scouts",c.scouts,0,100));return c;
}
// Host/controller object. Never put this mutable object on a NativeModule instance.
// Updates submit existing commands; the original RTS AI retains its existing knowledge policy.
class RtsBot {
public:
    RtsBot(uint8_t player,uint64_t seed,const Json& settings):player_(player),seed_(seed),settings_(settings.dump(-1)),config_(rtsConfig(settings)),ai_(player,seed,config_) {
        if(!player)throw JsonError("bots: player must be positive");
    }
    void update(Simulation& sim){ai_.update(sim);}
    std::string save() const {
        std::string bytes;Archive a(&bytes);uint32_t version=1;auto player=player_;auto seed=seed_;auto settings=settings_;auto body=ai_.saveState();a(version,player,seed,settings,body);return bytes;
    }
    bool load(const std::string& bytes) {
        if(bytes.size()>16*1024*1024)return false;
        Archive a(bytes);uint32_t version=0;uint8_t player=0;uint64_t seed=0;std::string settings,body;a(version,player,seed,settings,body);
        if(!a.ok()||a.remaining()||version!=1||player!=player_||seed!=seed_||settings!=settings_)return false;
        game::RtsAI candidate(player_,seed_,config_);if(!candidate.loadState(body)||candidate.player()!=player_)return false;
        ai_=std::move(candidate);return true;
    }
private:
    uint8_t player_;uint64_t seed_;std::string settings_;game::AIConfig config_;game::RtsAI ai_;
};
}
