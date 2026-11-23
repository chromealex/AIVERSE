#include "ActorModule.h"
#include "engine/gameplay/Inventory.h"
#include "engine/mechanics/EcsHitscanResolutionSystems.h"
#include "engine/mechanics/EcsOrderedImpactSystems.h"
#include "engine/mechanics/EcsProjectileSystems.h"
#include "engine/mechanics/EcsDamageSystems.h"
#include <algorithm>
#include <optional>
namespace aiverse::actor {
namespace {
Vec3 position(const Json& j) {
    if(!j.isArray()||j.size()!=3)throw JsonError("actor object: expected position [x,y,z]");
    Vec3 v;Fixed* coordinates[]={&v.x,&v.y,&v.z};for(size_t i=0;i<3;++i){if(!j[i].isNumber()||fxAbs(j[i].fx())>100000_fx)throw JsonError("actor object: invalid coordinate");*coordinates[i]=j[i].fx();}return v;
}
bool near(Vec3 a,Vec3 b,Fixed radius){return (a-b).lengthSq()<=radius*radius;}
bool inside(Vec3 p,const world::Bounds& b){return p.x>=b.min.x&&p.x<=b.max.x&&p.y>=b.min.y&&p.y<=b.max.y&&p.z>=b.min.z&&p.z<=b.max.z;}
std::optional<lib::gameplay::ItemType> item(int64_t id){if(id<1||id>9)return {};return lib::gameplay::ItemType{id,20,1,0};}
bool bag(std::vector<Item>& bag,int32_t type,int32_t count,size_t slots=12) {
    std::vector<lib::gameplay::ItemStack> items;for(auto i:bag)items.push_back({i.type,i.count});
    auto result=count>0?lib::gameplay::addItem(items,{slots,100},{type,count},item):lib::gameplay::removeItem(items,{type,-count});
    if(!result)return false;
    bag.clear();
    for(auto i:items)bag.push_back({int32_t(i.type),int32_t(i.count)});
    return true;
}
bool has(const std::vector<Item>& bag,int32_t type){for(auto i:bag)if(i.type==type&&i.count>0)return true;return false;}
uint32_t ticks(const Simulation& sim){return sim.tick();}
}
void ActorHost::setupSystems(Simulation& sim,const Json& map) {
    ladders_.clear();sites_.clear();penetrable_.clear();initialLoot_.clear();initialVehicles_.clear();
    if(!config_.systems.enabled)return;
    auto& s=state(sim);
    forEachCharacter(sim,[&](CharacterRef actor){actor.armor=config_.systems.startArmor;actor.money=config_.systems.startMoney;});
    auto boxes=[&](const char* key,std::vector<world::Bounds>& output){
        if(!map.has(key))return;
        if(!map[key].isArray()||map[key].size()>128)throw JsonError(std::string("actor.")+key+": expected at most 128 boxes");
        for(const auto& box:map[key].items()){world::Bounds b{position(box["min"]),position(box["max"])};if(b.min.x>=b.max.x||b.min.y>=b.max.y||b.min.z>=b.max.z)throw JsonError("actor region: invalid bounds");output.push_back(b);}
    };
    boxes("ladders",ladders_);boxes("bombSites",sites_);
    if(config_.mode=="bomb"){
        bool attack=false,defense=false;
        forEachCharacter(sim,[&](CharacterRef actor){if(actor.team>2)throw JsonError("actor bomb: teams must be 1 or 2");attack|=actor.team==1;defense|=actor.team==2;});
        if(!attack||!defense||sites_.empty())throw JsonError("actor bomb: both teams and a bomb site are required");
        for(size_t slot=1;slot<=characterCount(sim);++slot)if(character(sim,uint8_t(slot)).team==1){s.match.carrier=uint8_t(slot);break;}
    }
    for(const auto& box:map["colliders"].items())if(box.has("penetration")) {
        if(!box["penetration"].isNumber()||box["penetration"].fx()<=Fixed{}||box["penetration"].fx()>10_fx)throw JsonError("actor collider.penetration: expected (0,10]");
        penetrable_[uint32_t(box.getInt("id"))]=box["penetration"].fx();
    }
    auto array=[&](const char* key,size_t limit){if(map.has(key)&&(!map[key].isArray()||map[key].size()>limit))throw JsonError(std::string("actor.")+key+": invalid array size");};
    array("loot",kMaxLoot);array("vehicles",kMaxVehicles);
    for(const auto& j:map["loot"].items()) {
        auto type=j.getInt("type"),count=j.getInt("count",1);
        if(!((type>=1&&type<=9)||(type>=100&&type<100+int64_t(config_.weapons.size()))||(type>=200&&config_.gear.contains(type-200)))||count<1||count>20)throw JsonError("actor loot: invalid type or count");
        Loot l{s.nextObject++,position(j["pos"]),int32_t(type),int32_t(count)};
        if(type>=200){l.count=1;l.ammo=int32_t(config_.gear.at(type-200).durability);}
        else if(type>=100){l.count=1;l.ammo=config_.weapons[size_t(type-100)].magazine;l.reserve=config_.weapons[size_t(type-100)].reserve;}
        spawnLoot(sim,l);
    }
    for(const auto& j:map["vehicles"].items()) {
        auto speed=j.getFx("speed",15_fx);if(speed<1_fx||speed>50_fx)throw JsonError("actor vehicle.speed: expected 1..50");
        Vehicle v;v.id=s.nextObject++;v.pos=position(j["pos"]);v.speed=speed;
        if(collision_.blocked({v.pos-Vec3{1_fx,{},2_fx},v.pos+Vec3{1_fx,2_fx,2_fx}}))throw JsonError("actor vehicle spawn intersects collision");
        spawnVehicle(sim,v);
    }
    s.match.start=ticks(sim);initialLoot_=loot(sim);initialVehicles_=vehicles(sim);
}
void ActorHost::damage(Simulation& sim,uint8_t source,CharacterRef target,int32_t amount,bool head) {
    auto& s=state(sim);World& w=sim.world();
    std::optional<CharacterRef> attacker;
    if(source&&source<=characterCount(sim))attacker.emplace(character(sim,source));
    // Side-effect order: gear / armor -> hits -> knockdown -> kill / loot -> event.
    if(target.health<=0||amount<=0)return;
    if(attacker&&source!=target.slot&&!config_.friendlyFire&&attacker->team==target.team)return;
    if(!config_.gear.empty()){   // gear-tiers: helmets cover zone 1 (head shots), everything else hits zone 2
        if(auto hit=lib::systems::absorbHit(config_.gear,*gear(sim,target.slot),head?1u:2u,amount)){
            amount=int32_t(hit->damage);for(auto slot:hit->broken)sim.emit({9,target.slot,{},{},int64_t(slot),target.pos});
        }
    }else if(config_.systems.enabled&&!head){auto absorbed=std::min(target.armor,amount/2);target.armor-=absorbed;amount-=absorbed;}
    target.lastDamage=sim.tick();target.activeAction=Action::None;target.actionUntil=0;
    if(s.royale.enabled&&attacker&&source!=target.slot){s.royale.lastAttacker[target.slot-1]=source;s.royale.lastWeapon[target.slot-1]=uint8_t(attacker->weaponIndex+1);}
    if(attacker)++attacker->hits;
    auto releaseVehicles=[&](uint8_t slot){for(const Entity e:vehicleEntities(sim)){auto& v=*w.tryGet<Vehicle>(e);if(v.driver==slot){w.notifyComponentChange<Vehicle>(e);v.driver=0;}}};
    if(config_.systems.enabled&&config_.systems.knockdown&&!target.downed&&amount>=target.health) {
        bool teammate=false;
        for(size_t k=1;k<=characterCount(sim);++k){const auto a=character(sim,uint8_t(k));if(a.slot!=target.slot&&a.team==target.team&&a.health>0&&!a.downed)teammate=true;}
        if(teammate){releaseVehicles(target.slot);w.notifyComponentChange<CharacterBody>(target.entity);target.vehicle=0;target.downed=true;target.health=30;target.bleedUntil=sim.tick()+config_.systems.bleedTicks;return;}
    }
    // The same immediate integer HP commit as the generic ECS DamageResolutionSystem.
    lib::mechanics::applyIntegerHitPointDamage(target.health,amount);
    if(target.health)return;
    target.downed=false;target.respawnAt=sim.tick()+config_.respawnTicks;
    if(attacker&&source!=target.slot&&attacker->team!=target.team){++attacker->kills;attacker->money=std::min(16000,attacker->money+config_.systems.killReward);}
    if(config_.systems.enabled){
        if(s.match.carrier==target.slot){s.match.carrier=0;s.match.bombPos=target.pos;}
        if(w.countExisting<Loot>()<kMaxLoot&&(target.owned&(1u<<target.weaponIndex)))
            spawnLoot(sim,{s.nextObject++,target.pos,100+target.weaponIndex,1,target.ammo,target.reserve,target.attachments[target.weaponIndex]});
        if(auto* loadout=gear(sim,target.slot)){
            for(const auto& [slot,piece]:loadout->slots)if(w.countExisting<Loot>()<kMaxLoot)spawnLoot(sim,{s.nextObject++,target.pos,int32_t(200+piece.def),1,int32_t(piece.durability)});
            *gear(sim,target.slot)={};
        }
        releaseVehicles(target.slot);
        w.notifyComponentChange<CharacterBody>(target.entity);
        target.vehicle=0;
    }
    sim.emit({2,source,{}, {},target.slot,target.pos});
}
// Hit on a vehicle: a destroyed vehicle kills the driver of an untyped vehicle (typed ones eject via VehicleDrive seats).
void ActorHost::damageVehicle(Simulation& sim,uint8_t source,uint32_t vehicleId,int32_t amount) {
    auto* v=sim.world().tryGet<Vehicle>(vehicleEntity(sim,vehicleId));if(!v)return;
    v->health=std::max(0,v->health-amount);
    if(!v->health&&v->driver){auto driver=character(sim,v->driver);sim.world().notifyComponentChange<Vehicle>(vehicleEntity(sim,vehicleId));sim.world().notifyComponentChange<CharacterBody>(driver.entity);v->driver=0;driver.vehicle=0;damage(sim,source,driver,1000);}
}
bool ActorHost::traceShot(Simulation& sim,uint8_t source,Vec3 eye,Vec3 delta,int32_t amount,const WeaponConfig& weapon,const std::vector<HitPose>* rewind,Projectile* projectile) {
    Fixed best=1_fx;uint8_t victim=0;std::optional<HitPose> pose;
    // Rewind pose selection comes from the tick history; the nearest-hit policy is the shared ECS one.
    // Resolved inline to keep the per-shooter damage / event order and the rewind.
    std::vector<HitPose> live;
    if(!rewind)live=livePoses(sim);
    const auto& targets=rewind?*rewind:live;
    const World& view=sim.world();
    lib::mechanics::HitscanIntentComponent shotIntent;
    shotIntent.origin=eye;
    shotIntent.delta=delta;
    shotIntent.maximumFraction=best;
    shotIntent.requested=true;
    const auto actorHit=lib::mechanics::resolveHitscanIntent(
        shotIntent,characterEntity(sim,source),targets,
        [&](const HitPose& target) {
            if(target.slot==source||target.health<=0)return false;
            const auto* id=view.tryGetConst<CharacterId>(characterEntity(sim,target.slot));
            return id&&target.life==id->life;
        },
        [&](const HitPose& target) {return actorBounds(target,config_.motor);},
        [&](const HitPose& target) {return characterEntity(sim,target.slot);});
    if(actorHit) {
        best=actorHit.event.fraction;
        victim=actorHit.pose->slot;
        pose=*actorHit.pose;
    }
    // Vehicles after characters: an exact equal-distance hit keeps the character; nearer vehicles occlude.
    uint32_t vehicleId=0;
    const auto cars=vehicles(sim);
    const auto vehicleHit=lib::mechanics::selectClosestHitscanCandidate(
        cars,eye,delta,best,
        [&](const Vehicle& v) {return v.health>0 && v.driver!=source;},
        [&](const Vehicle& v) {
            return world::Bounds{
                v.pos-Vec3{1_fx,{},2_fx},v.pos+Vec3{1_fx,2_fx,2_fx}};
        });
    if(vehicleHit) {
        best=vehicleHit.fraction;
        victim=0;
        vehicleId=vehicleHit.target->id;
    }
    auto penetration=projectile?projectile->penetration:weapon.penetration;bool blocked=false;
    Vec3 end=eye+delta*best;
    world::Bounds bounds{{fxMin(eye.x,end.x),fxMin(eye.y,end.y),fxMin(eye.z,end.z)},{fxMax(eye.x,end.x),fxMax(eye.y,end.y),fxMax(eye.z,end.z)}};
    for(auto id:collision_.query(bounds)) {
        auto hit=collision_.castCollider(id,eye,delta);const auto entry=hit.fraction;
        if(!hit.hit||entry>best)continue;
        bool passed=false;if(projectile)for(uint8_t i=0;i<projectile->piercedCount;++i)passed|=projectile->pierced[i]==id;
        if(passed)continue;
        auto material=penetrable_.find(id);
        if(config_.systems.enabled&&material!=penetrable_.end()&&penetration>=material->second&&(!projectile||projectile->piercedCount<8)){penetration-=material->second;amount=amount*3/4;if(projectile)projectile->pierced[projectile->piercedCount++]=id;}
        else {best=entry;victim=0;vehicleId=0;blocked=true;}
    }
    if(projectile){projectile->penetration=penetration;projectile->damage=amount;}
    auto point=eye+delta*best;sim.emit({1,source,{}, {},victim,point});
    if(vehicleId){damageVehicle(sim,source,vehicleId,amount);return true;}
    if(!victim)return blocked;
    bool head=false;
    if(config_.systems.enabled&&pose){auto height=stanceHeight(*pose,config_.motor);auto relative=point.y-pose->pos.y;head=relative>height*Fixed::fromRatio(4,5);if(head)amount=int32_t((Fixed::fromInt(amount)*weapon.headMultiplier).floorInt());else if(relative<height*Fixed::fromRatio(2,5))amount=int32_t((Fixed::fromInt(amount)*weapon.limbMultiplier).floorInt());}
    // Accepted direct hits go through the shared ordered ECS impact policy; head / limb modifiers, armor,
    // knockdown, kill and replay side effects stay in damage(). A ray has no splash or deferred on-hit stage.
    const Entity victimEntity=characterEntity(sim,victim);
    lib::mechanics::resolveOrderedProjectileImpact(
        {kNullEntity,characterEntity(sim,source),victimEntity,point,
         Fixed::fromInt(amount),Fixed{}},
        [&](Entity) { damage(sim,source,character(sim,victim),amount,head); },
        [](Entity) {},
        [](Vec3,Fixed,auto&&) {},
        [](Entity) {return false;},
        [](Entity) {});
    return true;
}
void ActorHost::action(Simulation& sim,CharacterRef a,Action kind,uint32_t argument) {
    auto& s=state(sim);World& w=sim.world();const auto& c=config_.systems;auto now=sim.tick();
    if(a.health<=0||a.downed||a.activeAction!=Action::None)return;
    if(kind==Action::Buy) {
        if(config_.mode!="bomb"||s.match.end||now-s.match.start>c.buyTicks||s.match.planted||argument>=config_.weapons.size()||!near(a.pos,spawns_[a.slot-1],6_fx))return;
        const auto& wpn=config_.weapons[argument];if(a.money<wpn.price)return;
        a.money-=wpn.price;a.owned|=uint8_t(1u<<argument);a.storedAmmo[argument]=wpn.magazine;a.storedReserve[argument]=wpn.reserve;
        if(a.weaponIndex==argument){a.ammo=wpn.magazine;a.reserve=wpn.reserve;}return;
    }
    if(a.vehicle&&kind!=Action::Interact)return;
    if(kind==Action::Frag||kind==Action::Smoke||kind==Action::Flash) {
        if(w.countExisting<Projectile>()>=kMaxProjectiles||!bag(a.bag,kind==Action::Frag?3:kind==Action::Smoke?4:5,-1))return;
        uint8_t grenade=kind==Action::Frag?1:kind==Action::Smoke?2:3;
        auto eye=a.pos+Vec3{{},stanceHeight(a,config_.motor)-Fixed::fromRatio(1,10),{}};
        spawnProjectile(sim,Projectile{s.nextObject++,now+c.grenadeFuse,a.slot,
            grenade,eye,direction(a.yaw,a.pitch)*15_fx+Vec3{{},3_fx,{}},
            100,{},0});
        return;
    }
    if(kind==Action::Drop) {
        if(w.countExisting<Loot>()>=kMaxLoot||!(a.owned&(1u<<a.weaponIndex)))return;
        spawnLoot(sim,{s.nextObject++,a.pos,100+a.weaponIndex,1,a.ammo,a.reserve,a.attachments[a.weaponIndex]});a.owned&=uint8_t(~(1u<<a.weaponIndex));a.ammo=a.reserve=0;a.attachments[a.weaponIndex]=0;a.reloadUntil=0;return;
    }
    if(kind==Action::Attach){if(argument<6||argument>8||!(a.owned&(1u<<a.weaponIndex)))return;auto bit=uint8_t(1u<<(argument-6));if(!(a.attachments[a.weaponIndex]&bit)&&bag(a.bag,int32_t(argument),-1))a.attachments[a.weaponIndex]|=bit;return;}
    if(kind==Action::Vault) {
        if(!a.grounded||a.prone)return;
        auto dir=direction(a.yaw,{})*2_fx;auto wall=collision_.cast(a.pos+Vec3{{},Fixed::fromRatio(1,2),{}},dir);
        if(!wall.hit)return;
        auto top=collision_.colliders().at(wall.id).bounds.max.y;
        auto up=top-a.pos.y+Fixed::fromRatio(1,10);
        if(up<=Fixed{}||up>Fixed::fromRatio(15,10))return;
        auto height=stanceHeight(a,config_.motor);Vec3 center=a.pos+Vec3{{},height/2,{}},half{config_.motor.radius,height/2,config_.motor.radius};
        if(collision_.cast(center,{{},up,{}},half).hit||collision_.cast(center+Vec3{{},up,{}},dir,half).hit)return;
        a.pos+=Vec3{{},up,{}}+dir;a.velocity={};
        if(!reportRegionMoveIfCrossed(sim,a.entity))
            throw std::logic_error("regionOwnership: vault boundary dirty report failed");
        return;
    }
    if(a.activeAction!=Action::None)return;
    auto begin=[&](Action action,uint32_t duration,uint32_t target){a.activeAction=action;a.actionUntil=now+duration;a.actionTarget=target;a.actionOrigin=a.pos;};
    if(kind==Action::Heal){if(a.health<100&&has(a.bag,1))begin(kind,c.healTicks,0);return;}
    if(kind==Action::Plant) {
        if(config_.mode!="bomb"||a.team!=1||s.match.carrier!=a.slot||s.match.planted||now<s.match.start+c.freezeTicks)return;
        for(auto site:sites_){
            if(inside(a.pos,site)){
                begin(kind,c.plantTicks,0);
                return;
            }
        }
        return;
    }
    if(kind!=Action::Interact)return;
    if(a.vehicle&&vehicleInteract(sim,a))return;
    if(a.vehicle){
        if(auto* v=w.tryGet<Vehicle>(vehicleEntity(sim,a.vehicle))){
            HitPose exit=HitPose::of(a);exit.pos=v->pos+Vec3{2_fx,Fixed::fromRatio(1,10),{}};
            auto box=actorBounds(exit,config_.motor);box.min.y+=Fixed::fromRatio(1,100);if(collision_.blocked(box))return;
            w.notifyComponentChange<CharacterBody>(a.entity);
            w.notifyComponentChange<Vehicle>(vehicleEntity(sim,a.vehicle));
            a.pos=exit.pos;v->driver=0;a.vehicle=0;
            if(!reportRegionMoveIfCrossed(sim,a.entity))
                throw std::logic_error("regionOwnership: arcade exit dirty report failed");
        }
        return;
    }
    if(a.climbing){a.climbing=false;return;}
    auto accessible=[&](Vec3 p){return near(a.pos,p,c.interactRange)&&!collision_.cast(a.pos+Vec3{{},1_fx,{}},p-a.pos).hit;};
    if(config_.mode=="bomb") {
        if(s.match.planted&&a.team==2&&accessible(s.match.bombPos)){begin(Action::Interact,c.defuseTicks,0);return;}
        if(!s.match.planted&&!s.match.carrier&&a.team==1&&accessible(s.match.bombPos)){s.match.carrier=a.slot;return;}
    }
    for(size_t k=1;k<=characterCount(sim);++k){const auto target=character(sim,uint8_t(k));if(target.downed&&target.team==a.team&&accessible(target.pos)){begin(Action::Interact,c.reviveTicks,target.slot);return;}}
    for(const Entity e:lootEntities(sim)){
        auto& l=*w.tryGet<Loot>(e);
        if(!accessible(l.pos))continue;
        if(l.type>=100&&l.type<200){auto index=uint32_t(l.type-100);if(a.owned&(1u<<index))continue;a.owned|=uint8_t(1u<<index);a.storedAmmo[index]=l.ammo;a.storedReserve[index]=l.reserve;a.attachments[index]=l.attachments;if(a.weaponIndex==index){a.ammo=l.ammo;a.reserve=l.reserve;}}
        else if(l.type==2){a.armor=100;}
        else if(l.type==9){a.reserve=std::min(100000,a.reserve+30*l.count);}
        else if(l.type>=200){
            auto* loadout=gear(sim,a.slot);if(!loadout)continue;
            lib::systems::GearPiece piece{l.type-200,l.ammo};
            auto previous=lib::systems::equipGear(config_.gear,*loadout,piece);if(!previous)continue;
            if(previous->def){l.type=int32_t(200+previous->def);l.ammo=int32_t(previous->durability);l.count=1;return;}   // swap in place
        }
        else{
            const auto* loadout=gear(sim,a.slot);
            if(!bag(a.bag,l.type,l.count,size_t(std::min<int64_t>(24,12+(loadout?lib::systems::gearCapacity(config_.gear,*loadout):0)))))continue;
        }
        if(!reportRegionChange(sim,e,world::RegionDirtyKind::Retire))
            throw std::logic_error("regionOwnership: dirty loot retire rejected");
        w.destroy(e);return;
    }
    if(vehicleInteract(sim,a))return;
    for(const Entity e:vehicleEntities(sim)){
        auto& v=*w.tryGet<Vehicle>(e);
        if(v.health>0&&!v.driver&&!w.has<vehicles::VehicleDrive>(e)&&accessible(v.pos)){
            w.notifyComponentChange<CharacterBody>(a.entity);
            w.notifyComponentChange<Vehicle>(e);
            v.driver=a.slot;a.vehicle=v.id;a.pos=v.pos;
            if(!reportRegionMoveIfCrossed(sim,a.entity))
                throw std::logic_error("regionOwnership: arcade enter dirty report failed");
            return;
        }
    }
    for(auto ladder:ladders_)if(inside(a.pos,ladder)){a.climbing=true;return;}
}
void ActorHost::actorSystems(Simulation& sim,CharacterRef a,Input& input) {
    auto& s=state(sim);World& w=sim.world();auto now=sim.tick();const auto& c=config_.systems;
    a.recoil=fxMax(Fixed{},a.recoil-Fixed::fromRatio(1,config_.tickRate*5));a.aiming=(input.buttons&Aim)!=0;
    if(a.downed&&now>=a.bleedUntil){damage(sim,0,a,1000);input={};return;}
    bool frozen=config_.mode=="bomb"&&(s.match.end||now<s.match.start+c.freezeTicks);
    a.frozen=frozen;
    if(input.action!=Action::None&&(!frozen||input.action==Action::Buy))action(sim,a,input.action,input.argument);
    a.input.action=Action::None;
    if(a.activeAction!=Action::None) {
        bool valid=near(a.pos,a.actionOrigin,Fixed::fromRatio(1,2))&&!(input.buttons&Fire)&&!input.forward&&!input.right;
        if(a.activeAction==Action::Heal)valid&=has(a.bag,1)&&a.health<100;
        if(a.activeAction==Action::Plant)valid&=!s.match.planted&&s.match.carrier==a.slot;
        if(a.activeAction==Action::Interact&&a.actionTarget){const auto target=character(sim,uint8_t(a.actionTarget));valid&=target.downed&&target.health>0&&near(a.pos,target.pos,c.interactRange)&&!collision_.cast(a.pos+Vec3{{},1_fx,{}},target.pos-a.pos).hit;}
        if(a.activeAction==Action::Interact&&!a.actionTarget)valid&=s.match.planted&&near(a.pos,s.match.bombPos,c.interactRange);
        if(!valid){a.activeAction=Action::None;a.actionUntil=0;}
        else if(now>=a.actionUntil) {
            if(a.activeAction==Action::Heal&&bag(a.bag,1,-1))a.health=std::min(100,a.health+60);
            if(a.activeAction==Action::Plant){s.match.planted=true;s.match.bombPos=a.pos;s.match.carrier=0;s.match.bombExplodes=now+c.bombTicks;}
            if(a.activeAction==Action::Interact){if(a.actionTarget){auto target=character(sim,uint8_t(a.actionTarget));target.downed=false;target.health=40;target.bleedUntil=0;}else finishRound(sim,2);}
            a.activeAction=Action::None;a.actionUntil=0;
        }
    }
    if(a.climbing){bool in=false;for(auto ladder:ladders_)in|=inside(a.pos,ladder);if(!in)a.climbing=false;}
    if(a.vehicle&&!vehicleDrive(sim,a.vehicle))if(auto* vp=w.tryGet<Vehicle>(vehicleEntity(sim,a.vehicle))) {
        auto& v=*vp;
        if(!frozen){v.yaw+=Fixed::fromRatio(input.right,1000)*sim.dt()/4;auto delta=direction(v.yaw,{})*v.speed*Fixed::fromRatio(input.forward,1000)*sim.dt();auto hit=collision_.cast(v.pos+Vec3{{},1_fx,{}},delta,{1_fx,1_fx,2_fx});v.pos+=delta*(hit.hit?fxMax(Fixed{},hit.fraction-Fixed::fromRatio(1,100)):1_fx);v.pos.x=fxClamp(v.pos.x,-config_.worldLimit,config_.worldLimit);v.pos.z=fxClamp(v.pos.z,-config_.worldLimit,config_.worldLimit);v.velocity=delta/sim.dt();}
        a.pos=v.pos+Vec3{{},1_fx,{}};a.velocity=v.velocity;
        if(!reportRegionMoveIfCrossed(sim,a.entity) ||
           !reportRegionMoveIfCrossed(sim,vehicleEntity(sim,a.vehicle)))
            throw std::logic_error("regionOwnership: arcade driver/vehicle dirty report failed");
    }
    if(frozen||a.downed||a.vehicle){if(frozen||a.downed||!canFireFromVehicle(sim,a))input.buttons&=uint32_t(Prone);else input.buttons&=uint32_t(Fire|Aim|Reload|WeaponMask);if(frozen||a.vehicle)input.forward=input.right=0;}   // passengers / gunners may shoot
}
void ActorHost::finishRound(Simulation& sim,int32_t team) {
    auto& s=state(sim);if(s.match.end)return;s.match.winner=team;s.match.end=sim.tick()+config_.tickRate*3;
    if(team==1)++s.match.scoreA;else if(team==2)++s.match.scoreB;
    forEachCharacter(sim,[&](CharacterRef a){a.money=std::min(16000,a.money+(a.team==team?config_.systems.winReward:config_.systems.lossReward));a.activeAction=Action::None;a.actionUntil=0;});
    if(s.match.scoreA>=int(config_.systems.roundsToWin)||s.match.scoreB>=int(config_.systems.roundsToWin))s.winner=team;
}
// Non-programmed flight / impact of one projectile (bullet, grenade, smoke cloud). Returns true to remove it.
bool ActorHost::stepOrdinaryProjectile(Simulation& sim,Projectile& p) {
    const auto now=sim.tick();
    const auto& c=config_.systems;
    bool remove=false;
    if(p.kind==0) {
        auto delta=p.velocity*sim.dt();
        auto length=delta.length();
        if(length>p.remaining&&length>Fixed{})
            delta=delta*(p.remaining/length);
        remove=traceShot(sim,p.owner,p.pos,delta,p.damage,
                         config_.weapons[p.weapon],nullptr,&p);
        p.pos+=delta;
        p.remaining-=length;
        p.velocity.y-=config_.weapons[p.weapon].bulletGravity*sim.dt();
        remove|=p.remaining<=Fixed{}||now>=p.expires;
    } else if(p.kind==4) {
        remove=now>=p.expires;
    } else if(now<p.expires) {
        // Physical bounce is a shared policy; fuse, blast, smoke and flash belong to the effect stages below.
        lib::mechanics::stepBouncingProjectile(p.pos,p.velocity,
                                            collision_,sim.dt());
    } else {
        if(p.kind==2) {
            p.kind=4;
            p.expires=now+c.smokeTicks;
            p.velocity={};
        } else {
            for(size_t k=1;k<=characterCount(sim);++k){
                auto target=character(sim,uint8_t(k));
                if(target.health<=0)continue;
                auto eye=target.pos+Vec3{{},stanceHeight(target,config_.motor)
                                                  -Fixed::fromRatio(1,10),{}};
                auto delta=eye-p.pos;
                auto distance=delta.length();
                auto radius=p.kind==1?c.blastRadius:c.flashRadius;
                if(distance>radius||collision_.cast(p.pos,delta).hit)continue;
                if(p.kind==1)
                    damage(sim,p.owner,target,
                           int32_t((100_fx*(1_fx-distance/radius)).floorInt()));
                else {
                    auto facing=direction(target.yaw,target.pitch).dot(Vec3{}-delta);
                    auto duration=facing>Fixed{}?c.flashTicks:c.flashTicks/4;
                    target.blindUntil=std::max(target.blindUntil,now+duration);
                }
            }
            remove=true;
        }
    }
    remove|=fxAbs(p.pos.x)>config_.worldLimit||
            fxAbs(p.pos.y)>config_.worldLimit||
            fxAbs(p.pos.z)>config_.worldLimit;
    return remove;
}
void ActorHost::tickOrdinaryProjectiles(Simulation& sim) {
    // Ascending id (creation order). Nothing in this stage reads other projectiles, so removals are applied
    // after the pass, in the same order.
    World& w=sim.world();std::vector<Entity> removed;
    for(const Entity e:projectileEntities(sim)){
        auto* p=w.tryGet<Projectile>(e);
        if(!p||p->program.enabled)continue;
        if(stepOrdinaryProjectile(sim,*p))removed.push_back(e);
    }
    for(const Entity e:removed)w.destroy(e);
}
void ActorHost::tickSystems(Simulation& sim) {
    auto& s=state(sim);auto now=sim.tick();const auto& c=config_.systems;
    if(config_.mode!="bomb")return;
    if(s.match.end) {
        if(now<s.match.end)return;
        int scoreA=s.match.scoreA,scoreB=s.match.scoreB;s.match={};s.match.start=now;s.match.scoreA=scoreA;s.match.scoreB=scoreB;s.match.carrier=0;++s.round;
        forEachCharacter(sim,[&](CharacterRef a){
            CharacterSnapshot fresh;fresh.slot=a.slot;fresh.team=a.team;fresh.life=a.life+1;fresh.money=a.money;fresh.kills=a.kills;fresh.lastSequence=a.lastSequence;
            fresh.pos=spawns_[a.slot-1];equipInitial(fresh);fresh.armor=c.startArmor;
            assign(a,fresh);
            if(!reportRegionMoveIfCrossed(sim,a.entity))
                throw std::logic_error("regionOwnership: bomb respawn dirty report failed");
            if(fresh.team==1&&!s.match.carrier)s.match.carrier=fresh.slot;
        });
        clearProjectiles(sim);resetLootAndVehicles(sim);s.history.clear();return;
    }
    if(now<s.match.start+c.freezeTicks)return;
    bool attack=false,defense=false;
    forEachCharacter(sim,[&](CharacterRef a){if(a.health>0&&!a.downed){attack|=a.team==1;defense|=a.team==2;}});
    if(s.match.planted&&now>=s.match.bombExplodes)finishRound(sim,1);
    else if(!defense)finishRound(sim,1);
    else if(!attack&&!s.match.planted)finishRound(sim,2);
    else if(!s.match.planted&&now-s.match.start>=config_.roundTicks)finishRound(sim,2);
}
}
