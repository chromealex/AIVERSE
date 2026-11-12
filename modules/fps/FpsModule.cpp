#include "FpsModule.h"
#include <algorithm>
#include "engine/core/FixedMath.h"
#include <stdexcept>

namespace aiverse::fps {
namespace {
int64_t integer(const Json& j,const char* key,int64_t fallback,int64_t lo,int64_t hi) {
    if(j.has(key)&&(!j[key].isNumber()||j[key].fx().raw%Fixed::kOne))throw JsonError(std::string("fps.")+key+": expected integer");
    auto v=j.getInt(key,fallback);if(v<lo||v>hi)throw JsonError(std::string("fps.")+key+": outside supported range");return v;
}
Fixed number(const Json& j,const char* key,Fixed fallback,Fixed lo,Fixed hi) {
    if(j.has(key)&&!j[key].isNumber())throw JsonError(std::string("fps.")+key+": expected number");
    auto v=j.getFx(key,fallback);if(v<lo||v>hi)throw JsonError(std::string("fps.")+key+": outside supported range");return v;
}
Vec3 vector(const Json& j) {
    if(!j.isArray()||j.size()!=3)throw JsonError("fps: expected three coordinates");
    Vec3 v;Fixed* p[]={&v.x,&v.y,&v.z};
    for(int i=0;i<3;++i){if(!j[size_t(i)].isNumber())throw JsonError("fps: non-numeric coordinate");*p[i]=j[size_t(i)].fx();if(fxAbs(*p[i])>100000_fx)throw JsonError("fps: coordinate exceeds 100 km");}return v;
}
void stateIo(Archive& a,State& s) {
    a(s.round,s.winner,s.actors,s.nextObject,s.projectiles,s.loot,s.vehicles,s.match);
    uint32_t n=a.count(s.history.size());
    if(n>121){a.fail();return;}
    if(!a.writing())s.history.resize(n);
    for(auto& frame:s.history)a(frame.tick,frame.actors);
}
}
Config readConfig(const Json& root) {
    const auto& j=root["fps"];if(!j.isObject())throw JsonError("rules.fps: expected object");
    Config c;
    c.tickRate=uint32_t(integer(j,"tickRate",60,20,120));
    c.snapshotRate=uint32_t(integer(j,"snapshotRate",20,1,c.tickRate));
    c.maxPlayers=uint32_t(integer(j,"maxPlayers",100,1,200));
    c.rewindTicks=uint32_t(integer(j,"rewindTicks",12,0,120));
    c.inputTimeout=uint32_t(integer(j,"inputTimeout",15,1,120));
    c.interestRadius=number(j,"interestRadius",250_fx,10_fx,10000_fx);
    c.worldLimit=number(j,"worldLimit",100000_fx,10_fx,100000_fx);
    c.mode=j.getStr("mode","deathmatch");
    if(c.mode!="deathmatch"&&c.mode!="elimination"&&c.mode!="battleRoyale"&&c.mode!="bomb")throw JsonError("fps.mode: unknown mode");
    c.friendlyFire=j.getBool("friendlyFire",false);
    c.roundTicks=uint32_t(integer(j,"roundTicks",7200,1,10000000));
    c.respawnTicks=uint32_t(integer(j,"respawnTicks",180,1,100000));
    const auto& m=j["motor"];
    c.motor.radius=number(m,"radius",c.motor.radius,Fixed::fromRatio(1,10),2_fx);
    c.motor.height=number(m,"height",c.motor.height,c.motor.radius*2,4_fx);
    c.motor.crouchHeight=number(m,"crouchHeight",c.motor.crouchHeight,c.motor.radius*2,c.motor.height);
    c.motor.speed=number(m,"speed",5_fx,1_fx,30_fx);
    c.motor.sprintSpeed=number(m,"sprintSpeed",8_fx,c.motor.speed,50_fx);
    c.motor.gravity=number(m,"gravity",20_fx,1_fx,100_fx);
    c.motor.jumpSpeed=number(m,"jumpSpeed",6_fx,Fixed{},30_fx);
    c.motor.stepHeight=number(m,"stepHeight",c.motor.stepHeight,Fixed{},1_fx);
    auto weapon=[&](const Json& w) {
        if(!w.isNull()&&!w.isObject())throw JsonError("fps.weapon: expected object");
        WeaponConfig v;
        v.damage=int32_t(integer(w,"damage",25,1,1000));
        v.magazine=int32_t(integer(w,"magazine",30,1,1000));
        v.reserve=int32_t(integer(w,"reserve",120,0,100000));
        v.interval=uint32_t(integer(w,"intervalTicks",6,1,1000));
        v.reloadTicks=uint32_t(integer(w,"reloadTicks",120,1,100000));
        v.switchTicks=uint32_t(integer(w,"switchTicks",15,1,1000));
        v.range=number(w,"range",300_fx,1_fx,10000_fx);
        v.spread=number(w,"spread",Fixed{},Fixed{},Fixed::fromRatio(1,20));
        v.hipSpread=number(w,"hipSpread",Fixed{},Fixed{},Fixed::fromRatio(1,20));
        v.moveSpread=number(w,"moveSpread",Fixed{},Fixed{},Fixed::fromRatio(1,20));
        v.recoil=number(w,"recoil",Fixed{},Fixed{},Fixed::fromRatio(1,50));
        v.bulletSpeed=number(w,"bulletSpeed",Fixed{},Fixed{},2000_fx);
        v.bulletGravity=number(w,"bulletGravity",10_fx,Fixed{},100_fx);
        v.penetration=number(w,"penetration",Fixed{},Fixed{},10_fx);
        v.headMultiplier=number(w,"headMultiplier",2_fx,1_fx,5_fx);
        v.limbMultiplier=number(w,"limbMultiplier",Fixed::fromRatio(3,4),Fixed::fromRatio(1,10),1_fx);
        v.price=int32_t(integer(w,"price",600,0,16000));
        v.pellets=uint32_t(integer(w,"pellets",1,1,16));
        v.automatic=w.getBool("automatic",true);
        v.nameKey=w.getStr("nameKey","fps.weapon.rifle");
        return v;
    };
    if(j.has("weapons")) {
        if(!j["weapons"].isArray()||j["weapons"].size()<1||j["weapons"].size()>4)throw JsonError("fps.weapons: expected 1..4 weapons");
        for(const auto& w:j["weapons"].items())c.weapons.push_back(weapon(w));
    }else c.weapons.push_back(weapon(j["weapon"]));
    c.weapon=c.weapons.front(); // Legacy single-weapon rules remain supported.
    const auto& z=j["zone"];
    if(z.has("center"))c.zoneCenter=vector(z["center"]);
    c.zoneRadius=number(z,"radius",2000_fx,1_fx,100000_fx);
    c.zoneEndRadius=number(z,"endRadius",10_fx,Fixed{},c.zoneRadius);
    c.zoneStart=uint32_t(integer(z,"startTick",1800,0,10000000));
    c.zoneDuration=uint32_t(integer(z,"durationTicks",7200,1,10000000));
    c.zoneDamage=int32_t(integer(z,"damagePerSecond",5,1,100));
    const auto& systems=j["mechanics"];auto& mcfg=c.systems;
    mcfg.enabled=systems.getBool("enabled",false);mcfg.knockdown=systems.getBool("knockdown",false);
    mcfg.startArmor=int32_t(integer(systems,"startArmor",0,0,100));
    mcfg.startMoney=int32_t(integer(systems,"startMoney",800,0,16000));
    mcfg.killReward=int32_t(integer(systems,"killReward",300,0,16000));
    mcfg.winReward=int32_t(integer(systems,"winReward",3000,0,16000));
    mcfg.lossReward=int32_t(integer(systems,"lossReward",1500,0,16000));
    auto time=[&](const char* key,uint32_t seconds){return uint32_t(integer(systems,key,seconds*c.tickRate,1,100000));};
    mcfg.healTicks=time("healTicks",3);mcfg.reviveTicks=time("reviveTicks",4);mcfg.bleedTicks=time("bleedTicks",30);
    mcfg.freezeTicks=time("freezeTicks",3);mcfg.buyTicks=time("buyTicks",20);
    mcfg.plantTicks=time("plantTicks",3);mcfg.defuseTicks=time("defuseTicks",5);mcfg.bombTicks=time("bombTicks",40);
    mcfg.grenadeFuse=time("grenadeFuse",2);mcfg.smokeTicks=time("smokeTicks",15);mcfg.flashTicks=time("flashTicks",3);
    mcfg.roundsToWin=uint32_t(integer(systems,"roundsToWin",3,1,100));
    mcfg.interactRange=number(systems,"interactRange",3_fx,1_fx,5_fx);
    mcfg.blastRadius=number(systems,"blastRadius",8_fx,1_fx,30_fx);
    mcfg.smokeRadius=number(systems,"smokeRadius",6_fx,1_fx,20_fx);
    mcfg.flashRadius=number(systems,"flashRadius",20_fx,1_fx,50_fx);
    if(c.mode=="bomb"&&!mcfg.enabled)throw JsonError("fps bomb mode requires mechanics.enabled");
    return c;
}
void Module::equipInitial(Actor& a) const {
    a.weaponIndex=0;a.owned=config_.mode=="bomb"?1:uint8_t((1u<<config_.weapons.size())-1);
    for(size_t i=0;i<config_.weapons.size();++i){a.storedAmmo[i]=config_.weapons[i].magazine;a.storedReserve[i]=config_.weapons[i].reserve;}
    a.ammo=a.storedAmmo[0];a.reserve=a.storedReserve[0];
    if(config_.systems.enabled)a.bag={{1,2},{3,2},{4,2},{5,2}};
}
void Module::setup(Simulation& sim,const Json& rules,const Json& map) {
    config_=readConfig(rules);
    if(sim.tickRate()!=config_.tickRate)throw JsonError("fps.tickRate differs from simulation tickRate");
    if(sim.players().empty()||sim.players().size()>config_.maxPlayers)throw JsonError("fps: invalid player count");
    streamedCollision_.reset();streamManifest_=map["world"];
    if(streamManifest_.getBool("async"))world::WorldAssets::parse(streamManifest_);
    collision_.clear();spawns_.clear();bots_.clear();
    const auto& boxes=map["colliders"];
    if(!boxes.isArray()||boxes.size()>100000)throw JsonError("fps.colliders: expected at most 100000 boxes");
    for(const auto& b:boxes.items()) {
        auto id=integer(b,"id",0,1,UINT32_MAX);
        if(collision_.colliders().contains(uint32_t(id))||!collision_.add({uint32_t(id),{vector(b["min"]),vector(b["max"])}}))throw JsonError("fps.collider: invalid or duplicate box");
    }
    const auto& spawns=map["spawns"];
    if(!spawns.isArray()||spawns.size()<sim.players().size())throw JsonError("fps.spawns: one spawn per player required");
    State s;
    for(size_t i=0;i<sim.players().size();++i) {
        Actor a;a.slot=uint8_t(i+1);a.team=sim.players()[i].team;
        if(a.team==0)throw JsonError("fps.player.team: must be positive");
        a.pos=vector(spawns[i]);
        if(fxAbs(a.pos.x)>config_.worldLimit||fxAbs(a.pos.y)>config_.worldLimit||fxAbs(a.pos.z)>config_.worldLimit)throw JsonError("fps.spawn: outside world limit");
        equipInitial(a);
        auto b=actorBounds(a,config_.motor);b.min.y+=Fixed::fromRatio(1,1000);
        if(collision_.blocked(b))throw JsonError("fps.spawn: intersects collision");
        s.actors.push_back(a);spawns_.push_back(a.pos);bots_.push_back(map["players"][i].getBool("bot",false));
    }
    setupSystems(sim,map,s);
    Hasher hash;hash.str(rules.dump(-1));hash.str(map.dump(-1));if(streamManifest_.getBool("async"))hash.str("world-stream-v1");contentHash_=hash.value();
    sim.world().setResource<State>(std::move(s));
    sim.addSystem("fps",[this](Simulation& s){tick(s);});
}
void Module::applyCommand(Simulation& sim,const Command& c) {
    auto& s=state(sim);Input input;
    if(c.player==0||c.player>s.actors.size()||!decodeInput(c,input))return;
    auto& p=s.actors[c.player-1];
    if(input.sequence==0||input.sequence<=p.lastSequence||input.viewTick>sim.tick()||
       (p.lastSequence&&p.lastInputTick==sim.tick()))return;
    p.input=input;p.lastSequence=input.sequence;p.lastInputTick=sim.tick();
}
void Module::shoot(Simulation& sim,Actor& p) {
    auto& s=state(sim);
    uint32_t earliest=sim.tick()>config_.rewindTicks?sim.tick()-config_.rewindTicks:0;
    uint32_t requested=std::clamp(p.input.viewTick,earliest,sim.tick());
    const std::vector<Actor>* targets=&s.actors;
    for(auto it=s.history.rbegin();it!=s.history.rend();++it)if(it->tick<=requested){targets=&it->actors;break;}
    auto eye=p.pos+Vec3{{},stanceHeight(p,config_.motor)-Fixed::fromRatio(1,10),{}};
    const auto& weapon=config_.weapons[p.weaponIndex];
    --p.ammo;++p.shots;p.nextFire=sim.tick()+weapon.interval;
    for(uint32_t pellet=0;pellet<weapon.pellets;++pellet) {
        auto yaw=p.yaw,pitch=p.pitch;
        if(config_.systems.enabled) {
            Fixed spread=weapon.hipSpread*((p.aiming)?Fixed::fromRatio(1,4):1_fx);
            if(p.velocity.xz().lengthSq()>1_fx)spread+=weapon.moveSpread;
            if(p.attachments[p.weaponIndex]&1)spread=spread/2;
            if(weapon.pellets>1)spread+=weapon.spread;
            yaw+=(sim.rng().unit()*2-1_fx)*spread;pitch+=(sim.rng().unit()*2-1_fx)*spread+p.recoil;
        }else if(weapon.pellets>1) {yaw+=weapon.spread*int32_t(int(pellet%3)-1);pitch+=weapon.spread*int32_t(int(pellet/3)-1);}
        pitch=fxClamp(pitch,-Fixed::fromRatio(24,100),Fixed::fromRatio(24,100));
        if(config_.systems.enabled&&weapon.bulletSpeed>Fixed{}) {
            if(s.projectiles.size()<256)s.projectiles.push_back({s.nextObject++,sim.tick()+config_.tickRate*20,p.slot,0,eye,direction(yaw,pitch)*weapon.bulletSpeed,weapon.damage,weapon.range,p.weaponIndex,weapon.penetration});
        }else traceShot(sim,p.slot,eye,direction(yaw,pitch)*weapon.range,weapon.damage,weapon,*targets);
    }
    if(config_.systems.enabled)p.recoil=fxMin(Fixed::fromRatio(1,20),p.recoil+weapon.recoil*((p.attachments[p.weaponIndex]&2)?Fixed::fromRatio(3,4):1_fx));
}

void Module::tick(Simulation& sim) {
    auto& s=state(sim);if(s.winner)return;
    if(config_.systems.enabled)tickSystems(sim);
    if(s.winner)return;
    s.history.push_back({sim.tick(),s.actors});
    while(s.history.size()>size_t(config_.rewindTicks)+1)s.history.pop_front();
    for(auto& a:s.actors) {
        if(a.health<=0) {
            if(config_.mode=="deathmatch"&&sim.tick()>=a.respawnAt) {
                auto slot=a.slot,team=a.team;auto kills=a.kills;auto seq=a.lastSequence;auto life=a.life;
                a=Actor{};a.slot=slot;a.team=team;a.kills=kills;a.lastSequence=seq;
                a.armor=config_.systems.startArmor;a.money=config_.systems.startMoney;
                a.life=life+1;a.pos=spawns_[slot-1];equipInitial(a);
            } else continue;
        }
        if(bots_[a.slot-1]&&a.lastSequence==0) {
            const Actor* target=nullptr;Fixed nearest=config_.weapons[a.weaponIndex].range*config_.weapons[a.weaponIndex].range;
            for(const auto& candidate:s.actors)if(candidate.health>0&&!candidate.downed&&candidate.team!=a.team) {
                auto distance=(candidate.pos-a.pos).lengthSq();if(distance<nearest){
                    auto eye=a.pos+Vec3{{},1_fx,{}};auto delta=candidate.pos+Vec3{{},1_fx,{}}-eye;bool visible=!collision_.cast(eye,delta).hit;
                    if(config_.systems.enabled)for(const auto& cloud:s.projectiles)if(cloud.kind==4){auto radius=config_.systems.smokeRadius;Fixed fraction;Vec3 normal;if(world::intersect(eye,delta,{cloud.pos-Vec3{radius,radius,radius},cloud.pos+Vec3{radius,radius,radius}},fraction,normal))visible=false;}
                    if(visible){nearest=distance;target=&candidate;}
                }
            }
            a.input={};a.lastInputTick=sim.tick();a.input.viewTick=sim.tick();
            if(target) {
                auto delta=target->pos-a.pos;
                auto yaw=fxAtan2Turns(delta.x,delta.z);if(yaw<Fixed{})yaw+=1_fx;
                a.input.yaw=yaw;a.input.pitch=fxAtan2Turns(delta.y,delta.xz().length());
                a.input.forward=nearest>100_fx?700:0;a.input.right=((sim.tick()/90+a.slot)%2)?350:-350;
                a.input.buttons=a.ammo?((config_.weapons[a.weaponIndex].automatic||(!a.fireHeld&&sim.tick()>=a.nextFire))?Fire:0u):Reload;
            }
        }
        auto input=a.input;
        if(sim.tick()-a.lastInputTick>config_.inputTimeout){input.forward=input.right=0;input.buttons=0;input.action=Action::None;}
        if(config_.systems.enabled)actorSystems(sim,a,input);
        if(!a.vehicle)moveActor(a,input,config_.motor,collision_,sim.dt(),config_.worldLimit);
        // Jump is an edge, movement/fire are held; loss of input eventually releases them.
        a.input.buttons&=~uint32_t(Jump);
        auto selection=(input.buttons&WeaponMask)>>5;
        if(selection&&selection<=config_.weapons.size()&&selection-1!=a.weaponIndex&&(a.owned&(1u<<(selection-1)))) {
            a.storedAmmo[a.weaponIndex]=a.ammo;a.storedReserve[a.weaponIndex]=a.reserve;
            a.weaponIndex=uint8_t(selection-1);a.ammo=a.storedAmmo[a.weaponIndex];a.reserve=a.storedReserve[a.weaponIndex];
            a.reloadUntil=0;a.switchUntil=sim.tick()+config_.weapons[a.weaponIndex].switchTicks;
        }
        a.input.buttons&=~uint32_t(WeaponMask);
        const auto& weapon=config_.weapons[a.weaponIndex];
        int capacity=weapon.magazine+((a.attachments[a.weaponIndex]&4)?weapon.magazine/2:0);
        if(a.reloadUntil&&sim.tick()>=a.reloadUntil) {
            auto amount=std::min(capacity-a.ammo,a.reserve);a.ammo+=amount;a.reserve-=amount;a.reloadUntil=0;
        }
        if((input.buttons&Reload)&&!a.reloadUntil&&a.ammo<capacity&&a.reserve>0)a.reloadUntil=sim.tick()+weapon.reloadTicks;
        bool trigger=(input.buttons&Fire)!=0;
        if(trigger&&!a.downed&&!a.vehicle&&a.activeAction==Action::None&&(a.owned&(1u<<a.weaponIndex))&&(weapon.automatic||!a.fireHeld)&&!a.reloadUntil&&a.ammo>0&&sim.tick()>=a.nextFire&&sim.tick()>=a.switchUntil)shoot(sim,a);
        a.fireHeld=trigger;
        if(a.pos.y< -100_fx){a.health=0;a.respawnAt=sim.tick()+config_.respawnTicks;}
        if(config_.mode=="battleRoyale"&&sim.tick()>=config_.zoneStart&&sim.tick()%config_.tickRate==0) {
            auto t=Fixed::fromRatio(std::min(sim.tick()-config_.zoneStart,config_.zoneDuration),config_.zoneDuration);
            auto radius=config_.zoneRadius+(config_.zoneEndRadius-config_.zoneRadius)*t;
            if((a.pos-config_.zoneCenter).xz().lengthSq()>radius*radius)a.health=std::max(0,a.health-config_.zoneDamage);
        }
    }
    if(config_.mode=="bomb")return;
    if(config_.mode!="deathmatch") {
        std::set<uint8_t> teams;for(const auto& a:s.actors)if(a.health>0&&!a.downed)teams.insert(a.team);
        if(teams.size()<=1)s.winner=teams.empty()?-1:*teams.begin();
    }
    if(sim.tick()+1>=config_.roundTicks&&!s.winner) {
        if(config_.mode=="deathmatch") {
            std::map<uint8_t,int> scores;for(const auto& a:s.actors)scores[a.team]+=a.kills;
            int best=-1,winner=-1;for(auto [team,score]:scores)if(score>best){best=score;winner=team;}else if(score==best)winner=-1;
            s.winner=winner;
        } else s.winner=-1;
    }
}
void Module::hashState(Simulation& sim,Hasher& hash) {
    hash.u64(contentHash_);std::string bytes;Archive a(&bytes);stateIo(a,state(sim));hash.str(bytes);
}
bool Module::serializeState(Simulation& sim,Archive& archive) {
    uint32_t version=3;uint64_t content=contentHash_;archive(version,content);
    if(version!=3||content!=contentHash_)return false;
    State copy=state(sim);stateIo(archive,copy);
    if(!archive.ok()||copy.actors.size()!=spawns_.size())return false;
    auto valid=[&](const Actor& actor){
        if(actor.weaponIndex>=config_.weapons.size()||actor.slot==0||actor.slot>spawns_.size()||actor.bag.size()>12||actor.actionTarget>spawns_.size()||uint32_t(actor.activeAction)>uint32_t(Action::Vault)||actor.owned>15||actor.armor<0||actor.armor>100||actor.health<0||actor.health>100||actor.money<0||actor.money>16000)return false;
        for(auto item:actor.bag)if(item.type<1||item.type>9||item.count<1||item.count>20)return false;
        return true;
    };
    if(copy.projectiles.size()>256||copy.loot.size()>256||copy.vehicles.size()>32)return false;
    for(const auto& p:copy.projectiles)if(p.kind>4||!p.owner||p.owner>spawns_.size()||p.weapon>=config_.weapons.size()||p.piercedCount>8)return false;
    for(const auto& v:copy.vehicles)if(v.driver>spawns_.size()||v.health<0||v.health>500)return false;
    for(const auto& actor:copy.actors)if(!valid(actor))return false;
    for(const auto& frame:copy.history)for(const auto& actor:frame.actors)if(!valid(actor))return false;
    if(!archive.writing())state(sim)=std::move(copy);return true;
}
}
