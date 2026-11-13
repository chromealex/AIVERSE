#include "MotionModule.h"
#include "engine/core/FixedMath.h"
#include <algorithm>

namespace aiverse::genres {
namespace {
Fixed scalar(const Json& j,const char* key,Fixed fallback,Fixed lo,Fixed hi) {
    if(j.has(key)&&!j[key].isNumber())throw JsonError(std::string("motion.")+key+": expected number");
    auto value=j.getFx(key,fallback);
    if(value<lo||value>hi)throw JsonError(std::string("motion.")+key+": outside supported range");
    return value;
}
int integer(const Json& j,const char* key,int fallback,int lo,int hi) {
    auto value=scalar(j,key,Fixed::fromInt(fallback),Fixed::fromInt(lo),Fixed::fromInt(hi));
    if(value.raw%Fixed::kOne)throw JsonError(std::string("motion.")+key+": expected integer");
    return int(value.floorInt());
}
Vec3 point(const Json& j) {
    if(!j.isArray()||j.size()!=3)throw JsonError("motion: expected xyz coordinates");
    Vec3 out;Fixed* coords[]={&out.x,&out.y,&out.z};
    for(size_t i=0;i<3;++i) {
        if(!j[i].isNumber()||fxAbs(j[i].fx())>10000_fx)throw JsonError("motion: invalid coordinate");
        *coords[i]=j[i].fx();
    }
    return out;
}
world::Bounds bounds(const Json& j) {
    world::Bounds b{point(j["min"]),point(j["max"])};
    if(b.min.x>=b.max.x||b.min.y>=b.max.y||b.min.z>=b.max.z)throw JsonError("motion: invalid bounds");
    return b;
}
}
void io(Archive& a,MotionActor& p){a(p.body,p.speed,p.axis,p.steer,p.checkpoint,p.lap,p.deaths,p.buttons,p.lastInput,p.jumpHeld,p.respawn);}
void io(Archive& a,MotionState& s){a(s.actors,s.winner);}
void MotionModule::setup(Simulation& sim,const Json& rules,const Json& map) {
    const auto& j=rules["motion"];
    if(!j.isObject())throw JsonError("rules.motion: expected object");
    auto kind=j.getStr("kind");if(kind!="platformer"&&kind!="racing")throw JsonError("motion.kind: expected platformer or racing");
    kind_=kind=="racing"?MotionKind::Racing:MotionKind::Platformer;
    if(sim.tickRate()<20||sim.tickRate()>120)throw JsonError("motion: tick rate must be 20..120");
    limit_=scalar(j,"worldLimit",1000_fx,10_fx,10000_fx);
    motor_={};motor_.speed=scalar(j,"speed",5_fx,1_fx,50_fx);
    motor_.jumpSpeed=scalar(j,"jumpSpeed",8_fx,1_fx,30_fx);
    motor_.gravity=scalar(j,"gravity",20_fx,1_fx,100_fx);motor_.stepHeight={};
    maxSpeed_=scalar(j,"maxSpeed",20_fx,1_fx,100_fx);
    acceleration_=scalar(j,"acceleration",10_fx,1_fx,100_fx);
    braking_=scalar(j,"braking",20_fx,1_fx,100_fx);
    drag_=scalar(j,"drag",2_fx,Fixed{},20_fx);
    turnSpeed_=scalar(j,"turnSpeed",Fixed::fromRatio(1,4),Fixed::fromRatio(1,100),1_fx);
    laps_=integer(j,"laps",3,1,100);maxDeaths_=integer(j,"lives",10,1,1000);timeout_=integer(j,"timeoutTicks",12000,1,10000000);
    collision_.clear();checkpoints_.clear();hazards_.clear();bots_.clear();
    if(!map["colliders"].isArray()||map["colliders"].size()>10000)throw JsonError("motion.colliders: expected at most 10000 boxes");
    uint32_t id=1;for(const auto& b:map["colliders"].items())if(!collision_.add({id++,bounds(b)}))throw JsonError("motion: invalid collider");
    if(!map["checkpoints"].isArray()||map["checkpoints"].size()==0||map["checkpoints"].size()>1000)throw JsonError("motion.checkpoints: expected 1..1000 checkpoints");
    for(const auto& c:map["checkpoints"].items()) {
        auto b=bounds(c);for(const auto& previous:checkpoints_)if(world::overlaps(b,previous))throw JsonError("motion.checkpoints: overlapping checkpoints");
        checkpoints_.push_back(b);
    }
    if(map.has("hazards")) {
        if(!map["hazards"].isArray()||map["hazards"].size()>10000)throw JsonError("motion.hazards: expected at most 10000 boxes");
        for(const auto& h:map["hazards"].items())hazards_.push_back(bounds(h));
    }
    const auto& spawns=map["spawns"];
    if(sim.players().empty()||sim.players().size()>8||!spawns.isArray()||spawns.size()!=sim.players().size())throw JsonError("motion.spawns: one spawn per player, 1..8 players");
    MotionState s;
    for(size_t i=0;i<spawns.size();++i) {
        MotionActor a;a.body.slot=uint8_t(i+1);a.body.team=sim.players()[i].team;
        if(a.body.team!=i+1)throw JsonError("motion.players: teams must match slots");
        a.body.pos=point(spawns[i]);a.respawn=a.body.pos;
        if(fxAbs(a.body.pos.x)>limit_||fxAbs(a.body.pos.y)>limit_||fxAbs(a.body.pos.z)>limit_)throw JsonError("motion.spawn: outside world limit");
        if(!collision_.query(fps::actorBounds(a.body,motor_)).empty())throw JsonError("motion.spawn: intersects collider");
        s.actors.push_back(a);bots_.push_back(map["players"][i].getBool("bot",false));
    }
    Hasher h;h.str(rules.dump(-1));h.str(map.dump(-1));contentHash_=h.value();
    sim.world().setResource<MotionState>(std::move(s));sim.addSystem("motion",[this](Simulation& world){tick(world);});
}
void MotionModule::applyCommand(Simulation& sim,const Command& c) {
    auto& s=state(sim);
    if(s.winner||c.player==0||c.player>s.actors.size()||c.type!=kMotionInput||c.args.size()!=3||!c.units.empty()||c.target.valid())return;
    if(c.args[0]<-1000||c.args[0]>1000||c.args[1]<-1000||c.args[1]>1000||c.args[2]<0||c.args[2]>1)return;
    auto& a=s.actors[c.player-1];a.axis=int32_t(c.args[0]);a.steer=int32_t(c.args[1]);a.buttons=uint32_t(c.args[2]);a.lastInput=sim.tick();
}
void MotionModule::race(MotionActor& a,Fixed dt) {
    auto throttle=Fixed::fromRatio(a.axis,1000);
    if(throttle>=Fixed{})a.speed+=throttle*acceleration_*dt;
    else a.speed=fxMax(Fixed{},a.speed+throttle*braking_*dt);
    if(!a.axis)a.speed=fxMax(Fixed{},a.speed-drag_*dt);
    a.speed=fxClamp(a.speed,Fixed{},maxSpeed_);
    a.body.yaw+=Fixed::fromRatio(a.steer,1000)*turnSpeed_*dt*fxMin(1_fx,a.speed/5);
    a.body.yaw.raw=((a.body.yaw.raw%Fixed::kOne)+Fixed::kOne)%Fixed::kOne;
    Vec3 delta=fps::direction(a.body.yaw,{})*(a.speed*dt),half{motor_.radius,motor_.height/2,motor_.radius};
    auto hit=collision_.cast(a.body.pos+Vec3{{},half.y,{}},delta,half);
    if(hit.hit) {
        auto length=delta.length();auto fraction=fxMax(Fixed{},hit.fraction-(length.raw?Fixed::fromRatio(1,1000)/length:Fixed{}));
        a.body.pos+=delta*fraction;a.speed={};
    }else a.body.pos+=delta;
    a.body.pos.x=fxClamp(a.body.pos.x,-limit_,limit_);a.body.pos.z=fxClamp(a.body.pos.z,-limit_,limit_);
}
void MotionModule::tick(Simulation& sim) {
    auto& s=state(sim);if(s.winner)return;
    if(sim.tick()>=uint32_t(timeout_)){s.winner=-1;return;}
    for(auto& a:s.actors) {
        if(a.deaths>=maxDeaths_)continue;
        if(bots_[a.body.slot-1]) {
            auto b=checkpoints_[size_t(a.checkpoint)];auto target=(b.min+b.max)/2_fx;
            if(kind_==MotionKind::Platformer){a.axis=target.x>a.body.pos.x?1000:-1000;a.buttons=sim.tick()%20==0?1:0;}
            else {
                auto delta=target-a.body.pos;auto angle=fxAtan2Turns(delta.x,delta.z)-a.body.yaw;
                while(angle>Fixed::fromRatio(1,2))angle-=1_fx;
                while(angle<-Fixed::fromRatio(1,2))angle+=1_fx;
                a.steer=angle>Fixed::fromRatio(1,100)?1000:angle<-Fixed::fromRatio(1,100)?-1000:0;
                a.axis=fxAbs(angle)>Fixed::fromRatio(1,8)&&a.speed>3_fx?-1000:1000;
            }
        }else if(sim.tick()-a.lastInput>sim.tickRate()/2){a.axis=0;a.steer=0;a.buttons=0;}
        auto before=a.body.pos;
        if(kind_==MotionKind::Platformer) {
            fps::Input input;input.yaw=Fixed::fromRatio(1,4);input.forward=a.axis;
            if(a.buttons&&!a.jumpHeld)input.buttons=fps::Jump;
            fps::moveActor(a.body,input,motor_,collision_,sim.dt(),limit_);a.jumpHeld=a.buttons!=0;
        }else race(a,sim.dt());
        bool hazard=a.body.pos.y<-20_fx;
        Fixed fraction;Vec3 normal;
        for(const auto& b:hazards_)if(world::intersect(before,a.body.pos-before,b,fraction,normal))hazard=true;
        if(hazard) {
            ++a.deaths;a.body.pos=a.respawn;a.body.velocity={};a.body.grounded=false;a.speed={};
            sim.emit({2,a.body.slot,{},{},a.deaths,a.body.pos});continue;
        }
        // A checkpoint counts on entry, never once per tick while parked inside it.
        Fixed insideFraction;Vec3 insideNormal;
        const auto& next=checkpoints_[size_t(a.checkpoint)];
        if(!world::intersect(before,{},next,insideFraction,insideNormal)&&
           world::intersect(before,a.body.pos-before,next,fraction,normal)) {
            ++a.checkpoint;
            if(kind_==MotionKind::Platformer)a.respawn=a.body.pos;
            if(a.checkpoint==int(checkpoints_.size())) {
                a.checkpoint=0;++a.lap;
                if(kind_==MotionKind::Platformer||a.lap>=laps_){s.winner=a.body.slot;return;}
            }
            sim.emit({3,a.body.slot,{},{},a.checkpoint,a.body.pos});
        }
    }
    if(std::all_of(s.actors.begin(),s.actors.end(),[this](const auto& a){return a.deaths>=maxDeaths_;}))s.winner=-1;
}
void MotionModule::hashState(Simulation& sim,Hasher& h) {
    std::string bytes;Archive a(&bytes);a(state(sim));h.u64(contentHash_);h.str(bytes);
}
bool MotionModule::serializeState(Simulation& sim,Archive& a) {
    uint32_t version=1;a(version);if(version!=1){a.fail();return false;}
    if(a.writing()){a(state(sim));return a.ok();}
    MotionState loaded;a(loaded);if(!a.ok())return false;
    bool valid=loaded.actors.size()==bots_.size()&&loaded.winner>=-1&&loaded.winner<=int(bots_.size());
    for(size_t i=0;i<loaded.actors.size();++i) {
        const auto& p=loaded.actors[i];
        valid=valid&&p.body.slot==i+1&&p.body.team==i+1&&p.axis>=-1000&&p.axis<=1000&&p.steer>=-1000&&p.steer<=1000&&p.buttons<=1;
        valid=valid&&p.checkpoint>=0&&size_t(p.checkpoint)<checkpoints_.size()&&p.lap>=0&&p.lap<=laps_&&p.deaths>=0&&p.deaths<=maxDeaths_;
        valid=valid&&p.speed>=Fixed{}&&p.speed<=maxSpeed_;
        for(auto v:{p.body.pos.x,p.body.pos.y,p.body.pos.z,p.respawn.x,p.respawn.y,p.respawn.z})valid=valid&&v>=-limit_&&v<=limit_;
    }
    if(!valid){a.fail();return false;}
    state(sim)=std::move(loaded);return true;
}
}
