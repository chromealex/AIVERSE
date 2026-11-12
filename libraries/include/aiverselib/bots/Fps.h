#pragma once
#include "Config.h"
#include "Navigation.h"
#include "NavMesh.h"
#include "modules/fps/FpsModule.h"
#include "engine/core/FixedMath.h"
#include <limits>
namespace aiverse::lib::bots {
enum class FpsDecision : uint8_t {Idle,Patrol,Investigate,Engage,Reload,Retreat,Zone,Blocked};
struct FpsConfig {
    uint32_t reactionTicks=12,memoryTicks=180,sensorTicks=6,navigationBudget=2048;
    Fixed sightRange=80_fx,halfFov=Fixed::fromRatio(1,5),turnPerTick=Fixed::fromRatio(1,60);
    Fixed aimTolerance=Fixed::fromRatio(1,200),aimError=Fixed::fromRatio(1,2000);
    Fixed preferredRange=15_fx,arrivalRadius=1_fx;
    int32_t retreatHealth=25,strafe=250;
};
inline bool valid(const FpsConfig& c){
    return c.navigationBudget>=16&&c.navigationBudget<=8192&&c.reactionTicks<=3600&&c.memoryTicks<=36000&&c.sensorTicks>=1&&c.sensorTicks<=3600&&
        c.sightRange>=1_fx&&c.sightRange<=10000_fx&&c.halfFov>=Fixed::fromRatio(1,360)&&c.halfFov<=Fixed::fromRatio(1,2)&&
        c.turnPerTick>Fixed{}&&c.turnPerTick<=Fixed::fromRatio(1,2)&&c.aimTolerance>=Fixed{}&&c.aimTolerance<=Fixed::fromRatio(1,8)&&
        c.aimError>=Fixed{}&&c.aimError<=Fixed::fromRatio(1,8)&&c.preferredRange>=1_fx&&c.preferredRange<=10000_fx&&
        c.arrivalRadius>=Fixed::fromRatio(1,10)&&c.arrivalRadius<=10_fx&&c.retreatHealth>=0&&c.retreatHealth<=100&&c.strafe>=0&&c.strafe<=1000;
}
inline FpsConfig fpsConfig(const Json& j) {
    keys(j,{"preset","navigationBudget","reactionTicks","memoryTicks","sensorTicks","sightRange","halfFov","turnPerTick","aimTolerance","aimError","preferredRange","arrivalRadius","retreatHealth","strafe"});
    FpsConfig c;auto p=preset(j);
    if(p=="easy"){c.reactionTicks=30;c.turnPerTick=Fixed::fromRatio(1,120);c.aimError=Fixed::fromRatio(1,500);c.strafe=0;}
    else if(p=="hard"){c.reactionTicks=6;c.sensorTicks=3;c.turnPerTick=Fixed::fromRatio(1,40);c.aimError={};c.strafe=400;}
    else if(p=="guard"){c.preferredRange=30_fx;c.strafe=0;c.retreatHealth=40;}
    else if(p=="rusher"){c.preferredRange=5_fx;c.strafe=150;c.retreatHealth=10;}
    else if(p=="survivor"){c.preferredRange=25_fx;c.retreatHealth=60;}
    else if(p!="normal")throw JsonError("bots.preset: unknown FPS preset '"+p+"'");
    c.navigationBudget=uint32_t(integer(j,"navigationBudget",c.navigationBudget,16,8192));
    c.reactionTicks=uint32_t(integer(j,"reactionTicks",c.reactionTicks,0,3600));
    c.memoryTicks=uint32_t(integer(j,"memoryTicks",c.memoryTicks,0,36000));c.sensorTicks=uint32_t(integer(j,"sensorTicks",c.sensorTicks,1,3600));
    c.sightRange=number(j,"sightRange",c.sightRange,1_fx,10000_fx);c.halfFov=number(j,"halfFov",c.halfFov,Fixed::fromRatio(1,360),Fixed::fromRatio(1,2));
    c.turnPerTick=number(j,"turnPerTick",c.turnPerTick,Fixed::fromRaw(1),Fixed::fromRatio(1,2));
    c.aimTolerance=number(j,"aimTolerance",c.aimTolerance,Fixed{},Fixed::fromRatio(1,8));
    c.aimError=number(j,"aimError",c.aimError,Fixed{},Fixed::fromRatio(1,8));
    c.preferredRange=number(j,"preferredRange",c.preferredRange,1_fx,10000_fx);c.arrivalRadius=number(j,"arrivalRadius",c.arrivalRadius,Fixed::fromRatio(1,10),10_fx);
    c.retreatHealth=int32_t(integer(j,"retreatHealth",c.retreatHealth,0,100));c.strafe=int32_t(integer(j,"strafe",c.strafe,0,1000));return c;
}
struct FpsState {
    uint8_t slot=0,target=0;uint32_t life=0,targetLife=0,lastTick=0,lastSensor=0,acquired=0,lastSeen=0,sequence=0;
    bool initialized=false;Vec3 lastKnown{};uint32_t patrol=0;FpsDecision decision=FpsDecision::Idle;
};
inline bool valid(const FpsState& s){
    return uint8_t(s.decision)<=uint8_t(FpsDecision::Blocked)&&s.patrol<512&&
        s.lastKnown.x>= -100000_fx&&s.lastKnown.x<=100000_fx&&s.lastKnown.y>= -100000_fx&&s.lastKnown.y<=100000_fx&&s.lastKnown.z>= -100000_fx&&s.lastKnown.z<=100000_fx&&
        (!s.initialized||(s.slot>0&&s.life>0&&s.lastSensor<=s.lastTick&&s.acquired<=s.lastTick&&s.lastSeen<=s.lastTick))&&
        ((s.target==0)==(s.targetLife==0));
}
inline Vec3 flat(Vec3 v){v.y={};return v;}
inline Fixed wrapTurn(Fixed angle){angle.raw%=Fixed::kOne;if(angle<Fixed{})angle+=1_fx;return angle;}
inline Fixed turnDelta(Fixed to,Fixed from){auto d=wrapTurn(to-from);return d>Fixed::fromRatio(1,2)?d-1_fx:d;}
inline Vec3 eye(const fps::Actor& a,const fps::MotorConfig& c){return a.pos+Vec3{{},fps::stanceHeight(a,c)-Fixed::fromRatio(1,10),{}};}
inline Vec3 center(const fps::Actor& a,const fps::MotorConfig& c){return a.pos+Vec3{{},fps::stanceHeight(a,c)/2,{}};}
inline bool visible(const fps::Actor& self,const fps::Actor& other,const fps::Module& module,const FpsConfig& c){
    if(other.slot==self.slot||other.team==self.team||other.health<=0)return false;
    auto delta=center(other,module.config().motor)-eye(self,module.config().motor);auto distance=delta.length();
    if(distance>c.sightRange)return false;
    if(distance>Fixed{}&&fps::direction(self.yaw,self.pitch).dot(delta)/distance<fxCosTurns(c.halfFov))return false;
    return !module.collision().cast(eye(self,module.config().motor),delta).hit;
}
inline bool friendlyInLine(const fps::Actor& self,const fps::State& world,const fps::Module& module,Fixed yaw,Fixed pitch,Fixed distance){
    auto start=eye(self,module.config().motor),delta=fps::direction(yaw,pitch)*distance;
    for(const auto& a:world.actors)if(a.slot!=self.slot&&a.team==self.team&&a.health>0){
        Fixed fraction;Vec3 normal;if(world::intersect(start,delta,fps::actorBounds(a,module.config().motor),fraction,normal))return true;
    }
    return false;
}
// Run once before the authoritative simulation step; return one ordinary input Command.
// State is explicit: save it alongside Simulation, or through a game-owned NativeState record.
inline std::optional<Command> fpsCommand(Simulation& sim,const fps::Module& module,uint8_t slot,const FpsConfig& c,const Waypoints& nodes,FpsState& state,std::optional<Vec3> objective={},const NavMesh* navigation=nullptr) {
    const auto& world=module.state(sim);if(!slot||slot>world.actors.size()||!valid(c)||!valid(state)||!valid(nodes)||sim.result()!=0)return {};
    if(objective&&(objective->x< -100000_fx||objective->x>100000_fx||objective->y< -100000_fx||objective->y>100000_fx||objective->z< -100000_fx||objective->z>100000_fx))return {};
    const auto& self=world.actors[slot-1];const auto tick=sim.tick();
    if(state.initialized&&(state.slot!=slot||tick<=state.lastTick))return {};
    auto s=state;
    if(!s.initialized||s.life!=self.life){s={};s.slot=slot;s.life=self.life;s.initialized=true;}
    s.lastTick=tick;s.decision=FpsDecision::Idle;
    fps::Input input;input.yaw=self.yaw;input.pitch=self.pitch;input.viewTick=tick;
    auto seq=std::max(s.sequence,self.lastSequence);if(seq==UINT32_MAX)return {};s.sequence=seq+1;input.sequence=s.sequence;
    const fps::Actor* target=nullptr;
    // Recheck current target each tick for LOS; new sightings are sampled at the configured cadence.
    for(const auto& a:world.actors)if(a.slot==s.target&&a.life==s.targetLife&&visible(self,a,module,c))target=&a;
    if(self.blindUntil>tick)target=nullptr;
    if(self.blindUntil<=tick&&!target&&(!state.initialized||tick-s.lastSensor>=c.sensorTicks)){
        s.lastSensor=tick;Fixed best=c.sightRange*c.sightRange;
        for(const auto& a:world.actors)if(visible(self,a,module,c)){
            auto d=(a.pos-self.pos).lengthSq();if(!target||d<best||(d==best&&a.slot<target->slot)){best=d;target=&a;}
        }
    }
    if(target){
        if(s.target!=target->slot||s.targetLife!=target->life){s.acquired=tick;s.target=target->slot;s.targetLife=target->life;}
        s.lastKnown=target->pos;s.lastSeen=tick;
    }else if(s.target&&tick-s.lastSeen>=c.memoryTicks){s.target=0;s.targetLife=0;}
    std::optional<Vec3> goal=objective;std::optional<Vec3> look;
    if(goal)s.decision=FpsDecision::Patrol;
    if(!goal&&nodes.empty()&&navigation&&!navigation->polygons.empty()){
        auto current=navPolygonAt(*navigation,self.pos);
        if(current){
            const auto component=navigation->polygons[*current].component;
            if(component<navigation->components.size()&&!navigation->components[component].empty()){
                const auto& choices=navigation->components[component];auto index=choices[(size_t(slot)*997+size_t(s.patrol)*67)%choices.size()];
                if((navigation->polygons[index].center-self.pos).xz().length()<=c.arrivalRadius){s.patrol=(s.patrol+1)%512;index=choices[(size_t(slot)*997+size_t(s.patrol)*67)%choices.size()];}
                goal=navigation->polygons[index].center;s.decision=FpsDecision::Patrol;
            }
        }
    }
    if(!goal&&!nodes.empty()){
        s.patrol%=uint32_t(nodes.size());if((nodes[s.patrol].point-self.pos).xz().length()<=c.arrivalRadius)s.patrol=(s.patrol+1)%uint32_t(nodes.size());
        goal=nodes[s.patrol].point;s.decision=FpsDecision::Patrol;
    }
    if(s.target){goal=s.lastKnown;s.decision=FpsDecision::Investigate;}
    if(target){
        look=center(*target,module.config().motor);s.decision=FpsDecision::Engage;
        if((target->pos-self.pos).xz().length()<=c.preferredRange)goal.reset();
    }
    const bool reload=self.ammo==0&&self.reserve>0;
    const bool retreat=target&&(self.health<=c.retreatHealth||reload||self.reloadUntil>tick);
    if(retreat){
        s.decision=FpsDecision::Retreat;std::optional<Vec3> cover;Fixed best{};
        for(const auto& node:nodes)if(node.cover){
            auto at=node.point+Vec3{{},module.config().motor.height/2,{}};
            if(!module.collision().cast(eye(*target,module.config().motor),at-eye(*target,module.config().motor)).hit)continue;
            auto d=(node.point-self.pos).lengthSq();
            if(!cover||d<best){cover=node.point;best=d;}
        }
        goal=cover?cover:std::optional<Vec3>(self.pos+fxNormalize(flat(self.pos-target->pos))*5_fx);
    }
    // Safe-zone movement has priority over chasing and stationary shooting.
    const auto& cfg=module.config();
    const auto& weapon=self.weaponIndex<cfg.weapons.size()?cfg.weapons[self.weaponIndex]:cfg.weapon;
    if(cfg.mode=="battleRoyale"){
        auto elapsed=tick>cfg.zoneStart?std::min(tick-cfg.zoneStart,cfg.zoneDuration):0;
        auto radius=cfg.zoneRadius+(cfg.zoneEndRadius-cfg.zoneRadius)*Fixed::fromRatio(elapsed,cfg.zoneDuration);
        if((self.pos-cfg.zoneCenter).xz().length()>radius*Fixed::fromRatio(9,10)){goal=cfg.zoneCenter;s.decision=FpsDecision::Zone;}
    }
    if(reload){input.buttons|=fps::Reload;if(s.decision!=FpsDecision::Zone&&s.decision!=FpsDecision::Retreat)s.decision=FpsDecision::Reload;}
    if(goal){
        auto next=navigation?navStep(*navigation,module.collision(),self.pos,*goal,c.navigationBudget):waypointStep(module.collision(),nodes,self.pos,*goal,cfg.motor.radius,cfg.motor.height,c.navigationBudget);
        if(!next){s.decision=FpsDecision::Blocked;goal.reset();}
        else goal=next;
    }
    if(!look&&goal)look=*goal+Vec3{{},cfg.motor.height-Fixed::fromRatio(1,10),{}};
    if(look){
        auto delta=*look-eye(self,cfg.motor);auto desired=wrapTurn(fxAtan2Turns(delta.x,delta.z));auto pitch=fxAtan2Turns(delta.y,delta.xz().length());
        if(target){auto error=slot%2?c.aimError:-c.aimError;desired=wrapTurn(desired+error);pitch+=error;}
        input.yaw=wrapTurn(self.yaw+fxClamp(turnDelta(desired,self.yaw),-c.turnPerTick,c.turnPerTick));
        input.pitch=fxClamp(self.pitch+fxClamp(pitch-self.pitch,-c.turnPerTick,c.turnPerTick),Fixed::fromRatio(-24,100),Fixed::fromRatio(24,100));
        if(target&&!retreat&&!reload&&self.reloadUntil==0&&self.ammo>0&&tick>=self.switchUntil&&
           (weapon.automatic||(!self.fireHeld&&tick>=self.nextFire))&&tick-s.acquired>=c.reactionTicks&&
           fxAbs(turnDelta(desired,input.yaw))<=c.aimTolerance&&fxAbs(pitch-input.pitch)<=c.aimTolerance&&
           delta.length()<=weapon.range&&!friendlyInLine(self,world,module,input.yaw,input.pitch,delta.length()+cfg.motor.radius))input.buttons|=fps::Fire;
    }
    const auto arrival=navigation?fxMin(navigation->config.cellSize/8,Fixed::fromRatio(1,10)):c.arrivalRadius;
    if(goal&&(self.pos-*goal).xz().length()>arrival){
        auto wish=fxNormalize(flat(*goal-self.pos));
        input.forward=int32_t((wish.dot(fps::direction(input.yaw,{}))*1000).floorInt());
        input.right=int32_t((wish.dot(fps::direction(input.yaw-Fixed::fromRatio(1,4),{}))*1000).floorInt());
    }else if(target&&s.decision==FpsDecision::Engage){
        auto strafe=((tick/60+slot)%2)?c.strafe:-c.strafe;
        auto offset=fps::direction(input.yaw-Fixed::fromRatio(1,4),{})*Fixed::fromRatio(strafe,1000);
        if(clearPath(module.collision(),self.pos,self.pos+offset,cfg.motor.radius,cfg.motor.height))input.right=strafe;
    }
    input.forward=std::clamp(input.forward,-1000,1000);input.right=std::clamp(input.right,-1000,1000);
    if(self.health<=0){input.forward=input.right=0;input.buttons=0;s.target=0;s.targetLife=0;s.decision=FpsDecision::Idle;}
    state=s;return fps::command(slot,input);
}
}
