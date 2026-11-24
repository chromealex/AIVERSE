#include "Session.h"
#include "engine/game/GameCore.h"
#include "engine/mechanics/VehiclePersistence.h"
#include <algorithm>
#include <utility>

namespace aiverse::net::authority {
namespace {
constexpr uint32_t magic=0x46505331;
enum class Message:uint8_t { Hello=1,Input=2,Snapshot=3,SnapshotDelta=4 };
Bytes bytes(const std::string& s){return Bytes(s.begin(),s.end());}
std::string string(const Bytes& b){return std::string(b.begin(),b.end());}
void header(Archive& a,Message& type) {uint32_t m=magic,v=kProtocol;a(m,v,type);if(m!=magic||v!=kProtocol)a.fail();}
Bytes inputPacket(actor::Input input){std::string s;Archive a(&s);Message type=Message::Input;header(a,type);a(input);return bytes(s);}
bool validCharacter(const actor::CharacterSnapshot& a) {
    if(a.weaponIndex>=4||a.bag.size()>24||a.armor<0||a.armor>100||a.money<0||a.money>16000||a.owned>15)return false;
    for(int i=0;i<4;++i)if(a.storedAmmo[i]<0||a.storedAmmo[i]>1500||a.storedReserve[i]<0||a.storedReserve[i]>100000)return false;
    for(auto item:a.bag)if(item.type<1||item.type>9||item.count<1||item.count>20)return false;
    return a.slot>0&&a.slot<=200&&a.team>0&&a.health>=0&&a.health<=100&&a.ammo>=0&&a.ammo<=1500&&a.reserve>=0&&a.reserve<=100000&&
        fxAbs(a.pos.x)<=100000_fx&&fxAbs(a.pos.y)<=100000_fx&&fxAbs(a.pos.z)<=100000_fx;
}
}
uint64_t contentId(const Json& rules,const Json& map){Hasher h;h.str(rules.dump(-1));h.str(map.dump(-1));if(map["world"].getBool("async"))h.str("world-stream-v1");return h.value();}
Bytes encodeSnapshot(const Snapshot& snapshot) {
    std::string s;Archive a(&s);Message type=Message::Snapshot;header(a,type);
    auto copy=snapshot;uint32_t count=uint32_t(copy.characters.size());a(copy.tick,copy.ack,copy.localSlot,copy.winner,count);
    for(auto& character:copy.characters)a(character);
    a(copy.projectiles,copy.loot,copy.vehicles,copy.match,copy.round,copy.royale,copy.gear,copy.race);
    return bytes(s);
}
bool decodeSnapshot(const Bytes& b,Snapshot& out) {
    if(b.size()>131072)return false;
    auto s=string(b);Archive a(s);Message type{};header(a,type);Snapshot v;uint32_t count=0;
    a(v.tick,v.ack,v.localSlot,v.winner,count);
    if(!a.ok()||type!=Message::Snapshot||count==0||count>200||v.localSlot==0||v.localSlot>200)return false;
    bool own=false;uint8_t previous=0;
    for(uint32_t i=0;i<count;++i) {
        actor::CharacterSnapshot character;a(character);if(!a.ok()||!validCharacter(character)||character.slot<=previous)return false;
        previous=character.slot;own|=character.slot==v.localSlot;v.characters.push_back(character);
    }
    a(v.projectiles,v.loot,v.vehicles,v.match,v.round,v.royale,v.gear,v.race);
    if(!a.ok()||v.race.order.size()>32||v.projectiles.size()>128||v.loot.size()>128||v.vehicles.size()>32||v.royale.crates.size()>8||v.royale.feed.size()>6||v.royale.divePhase>3||v.gear.slots.size()>lib::systems::kGearMaxSlots)return false;
    auto position=[](Vec3 p){return fxAbs(p.x)<=100000_fx&&fxAbs(p.y)<=100000_fx&&fxAbs(p.z)<=100000_fx;};
    for(const auto& p:v.projectiles)if(p.kind>4||p.weapon>=4||p.owner>200||p.piercedCount>8||!position(p.pos))return false;
    for(const auto& l:v.loot)if(!position(l.pos)||l.count<1||l.count>20)return false;
    for(const auto& vehicle:v.vehicles)if(!position(vehicle.pos)||vehicle.driver>200||vehicle.health<0||vehicle.health>500)return false;
    if(!own||a.remaining()!=0)return false;
    out=std::move(v);
    return true;
}
namespace {
// kLocomotion: the receiver's own locomotion bytes, present only with rules `locomotion` (no bytes otherwise).
constexpr uint32_t kGlobals=0,kActor=1u<<28,kProjectile=2u<<28,kLoot=3u<<28,kVehicle=4u<<28,kLocomotion=5u<<28,kIdMask=(1u<<28)-1;
template<class T> std::string record(T value){std::string out;Archive a(&out);a(value);return out;}
}
lib::delta::RecordSet snapshotRecords(const Snapshot& snapshot) {
    lib::delta::RecordSet r;auto c=snapshot;
    {std::string g;Archive a(&g);a(c.tick,c.ack,c.localSlot,c.winner,c.match,c.round,c.royale,c.gear,c.race);r[kGlobals]=std::move(g);}
    for(const auto& character:c.characters)r[kActor|character.slot]=record(character);
    for(const auto& p:c.projectiles)r[kProjectile|(p.id&kIdMask)]=record(p);
    for(const auto& l:c.loot)r[kLoot|(l.id&kIdMask)]=record(l);
    for(const auto& v:c.vehicles)r[kVehicle|(v.id&kIdMask)]=record(v);
    if(!c.locomotion.empty())r[kLocomotion]=c.locomotion;
    return r;
}
bool recordsSnapshot(const lib::delta::RecordSet& records,Snapshot& out) {
    // Rebuild the classic packet and reuse its validation so both paths accept exactly the same snapshots.
    Snapshot v;std::string locomotion;auto g=records.find(kGlobals);if(g==records.end())return false;
    {Archive a(g->second);a(v.tick,v.ack,v.localSlot,v.winner,v.match,v.round,v.royale,v.gear,v.race);if(!a.ok()||a.remaining())return false;}
    for(const auto& [id,bytes]:records){
        if(id==kGlobals)continue;
        if(id==kLocomotion){actor::LocomotionBody l;Archive check(bytes);actor::io(check,l);if(!check.ok()||check.remaining()||!actor::valid(l))return false;locomotion=bytes;continue;}
        Archive a(bytes);auto kind=id&~kIdMask;
        if(kind==kActor){actor::CharacterSnapshot x;a(x);v.characters.push_back(x);}
        else if(kind==kProjectile){actor::Projectile x;a(x);v.projectiles.push_back(x);}
        else if(kind==kLoot){actor::Loot x;a(x);v.loot.push_back(x);}
        else if(kind==kVehicle){actor::Vehicle x;a(x);v.vehicles.push_back(x);}
        else return false;
        if(!a.ok()||a.remaining())return false;
    }
    if(!decodeSnapshot(encodeSnapshot(v),out))return false;
    out.locomotion=std::move(locomotion);return true;
}
Server::Server(const Json& rules,const Json& map,uint64_t seed,world::AssetReader reader):
    simulation_({seed,actor::readConfig(rules).tickRate,0,false},std::make_unique<game::GameCore>()),content_(contentId(rules,map)) {simulation_.setup(rules,map);if(map["world"].getBool("async")){if(!reader)throw JsonError("world: server content reader required");module().setContentReader(std::move(reader));}}
bool Server::occupied(uint8_t slot) const {for(const auto& p:peers_)if(p.slot==slot&&p.conn->state()!=ConnState::Closed)return true;return false;}
bool Server::attach(uint8_t slot,std::unique_ptr<Conn> conn) {
    if(!conn||slot==0||slot>module().characterCount(simulation_)||occupied(slot))return false;
    Peer peer;peer.slot=slot;peer.conn=std::move(conn);peer.lastReceived=module().character(std::as_const(simulation_),slot).lastSequence;peers_.push_back(std::move(peer));return true;
}
void Server::poll(uint64_t now) {
    for(auto& p:peers_) {
        if(!p.born)p.born=now+1;
        p.conn->update(now);
        if(!p.ready&&now+1-p.born>10000)p.conn->close();
        Bytes packet;int count=0;
        while(count++<64&&p.conn->recv(packet)) {
            if(packet.size()>256){++rejected_;p.conn->close();break;}
            auto data=string(packet);Archive a(data);Message type{};header(a,type);
            if(type==Message::Hello&&!p.ready) {
                uint64_t id=0;a(id);
                if(!a.ok()||a.remaining()||id!=content_){++rejected_;p.conn->close();break;}
                p.ready=true;continue;
            }
            actor::Input input;a(input);
            if(p.ready&&type==Message::Input&&a.ok()&&!a.remaining()&&input.viewTick<=simulation_.tick())p.delta.acknowledge(input.viewTick);
            actor::Input validated;
            if(!p.ready||type!=Message::Input||!a.ok()||a.remaining()||!actor::decodeInput(actor::command(p.slot,input),validated)||
               input.sequence<=p.lastReceived||uint64_t(input.sequence)>uint64_t(p.lastReceived)+256||
               input.viewTick>simulation_.tick()||p.inputs.size()>=8) {++rejected_;continue;}
            p.lastReceived=input.sequence;p.inputs.push_back(input);
        }
    }
    for(auto it=peers_.begin();it!=peers_.end();)if(it->conn->state()==ConnState::Closed) {
        auto actor=module().character(std::as_const(simulation_),it->slot);
        // Release through the same authoritative input path, never leave a disconnected trigger held.
        actor::Input release;release.sequence=actor.lastSequence+1;release.yaw=actor.yaw;release.pitch=actor.pitch;
        auto c=actor::command(it->slot,release);c.tick=simulation_.tick();simulation_.inject(c);
        it=peers_.erase(it);
    }else ++it;
}
bool Server::step() {
    if(!module().readyToStep(simulation_))return false;
    for(auto& p:peers_)if(p.ready&&!p.inputs.empty()) {
        auto c=actor::command(p.slot,p.inputs.front());p.inputs.pop_front();c.tick=simulation_.tick();simulation_.inject(c);
    }
    simulation_.step();
    auto& cfg=module().config();
    if(uint64_t(simulation_.tick())*cfg.snapshotRate/cfg.tickRate!=uint64_t(simulation_.tick()-1)*cfg.snapshotRate/cfg.tickRate)broadcast();
    return true;
}
void Server::broadcast() {
    const auto actors=module().snapshots(simulation_);
    world::SpatialWorld index(64);
    const auto projectiles=module().projectiles(simulation_);const auto loot=module().loot(simulation_);const auto vehicles=module().vehicles(simulation_);
    for(const auto& a:actors)index.add({a.slot,actor::actorBounds(a,module().config().motor)});
    auto radius=module().config().interestRadius;Vec3 extent{radius,radius,radius};
    for(auto& p:peers_)if(p.ready) {
        const auto& own=actors[p.slot-1];Snapshot s;s.tick=simulation_.tick();s.ack=own.lastSequence;s.localSlot=p.slot;s.winner=simulation_.result();
        auto ids=index.query({own.pos-extent,own.pos+extent});
        for(auto id:ids) {
            auto actor=actors[id-1];
            if(id!=p.slot&&(actor.pos-own.pos).lengthSq()>radius*radius)continue;
            // Remote control intent is never replicated; spectators need their own authorization policy.
            if(id!=p.slot){actor.input={};actor.reserve=0;actor.ammo=0;for(int i=0;i<4;++i){actor.storedAmmo[i]=0;actor.storedReserve[i]=0;}actor.hits=0;actor.bag.clear();actor.money=0;actor.activeAction=actor::Action::None;actor.actionTarget=0;actor.actionUntil=0;}
            s.characters.push_back(actor);
        }
        const auto& state=module().state(simulation_);
        if(const auto* l=module().locomotion(simulation_,p.slot)){auto copy=*l;Archive w(&s.locomotion);actor::io(w,copy);}
        s.round=state.round;s.match=state.match;s.royale=module().royaleView(simulation_,p.slot);s.race=module().raceView(simulation_,p.slot);if(auto* gear=module().gear(std::as_const(simulation_),p.slot))s.gear=*gear;
        auto nearby=[&](const auto& objects,auto& out,size_t limit){for(const auto& object:objects)if(out.size()<limit&&(object.pos-own.pos).lengthSq()<=radius*radius)out.push_back(object);};
        nearby(projectiles,s.projectiles,128);nearby(loot,s.loot,128);nearby(vehicles,s.vehicles,32);
        auto records=snapshotRecords(s);uint64_t full=0;for(const auto& [id,bytes]:records)full+=bytes.size()+5;
        std::string packet;Archive a(&packet);Message type=Message::SnapshotDelta;header(a,type);
        auto delta=p.delta.encode(s.tick,std::move(records));if(delta.empty())continue;packet+=delta;
        stats_.packets++;stats_.bytes+=packet.size();stats_.fullBytes+=full;stats_.fullPackets+=p.delta.lastWasFull();
        p.conn->sendLatest(bytes(packet));
    }
}
Client::Client(const Json& rules,const Json& map,std::unique_ptr<Conn> conn,world::AssetReader reader):config_(actor::readConfig(rules)),conn_(std::move(conn)),content_(contentId(rules,map)) {
    Simulation local({1,config_.tickRate,0},std::make_unique<game::GameCore>());local.setup(rules,map);
    collision_=actor::host(local).collision();
    if(map["world"].getBool("async")){if(!reader)throw JsonError("world: client content reader required");streamedCollision_=std::make_unique<world::CollisionStream>(map["world"],std::move(reader),collision_);predicted_.pos=world::worldVector(map["spawns"][0],100000_fx);}
}
void Client::updateStreaming() {
    if(!streamedCollision_)return;
    auto radius=fxMax(32_fx,config_.motor.sprintSpeed*Fixed::fromRatio(256,config_.tickRate)+16_fx);
    Vec3 extent{radius,radius,radius};world::Bounds regions[2]={{predicted_.pos-extent,predicted_.pos+extent},{predicted_.pos-extent,predicted_.pos+extent}};
    for(const auto& actor:latest_.characters)if(actor.slot==latest_.localSlot)regions[1]={actor.pos-extent,actor.pos+extent};
    streamedCollision_->prepareRegions(regions);
}
void Client::predict(const actor::Input& input) {
    // Never predict into a missing cell; keep authoritative pose until collision is available.
    const auto radius=config_.motor.sprintSpeed/Fixed::fromInt(config_.tickRate)+8_fx;
    Vec3 extent{radius,radius,radius};
    if(streamedCollision_&&!streamedCollision_->covers({predicted_.pos-extent,predicted_.pos+extent}))return;
    if(latest_.royale.enabled&&latest_.royale.divePhase!=3)return;   // skydive is server-driven: keep the authoritative pose
    if(predicted_.vehicle)return;   // a seated actor rides with its vehicle: the server drives the pose
    if(config_.locomotion)actor::moveActorModes(predicted_,predictedLocomotion_,input,config_.motor,*config_.locomotion,collision_,Fixed::fromRatio(1,config_.tickRate),config_.worldLimit);
    else actor::moveActor(predicted_,input,config_.motor,collision_,Fixed::fromRatio(1,config_.tickRate),config_.worldLimit);
}
void Client::poll(uint64_t now) {
    conn_->update(now);
    if(!sentHello_&&conn_->state()==ConnState::Connected) {
        std::string s;Archive a(&s);Message type=Message::Hello;header(a,type);a(content_);conn_->send(bytes(s));sentHello_=true;
    }
    Bytes packet;
    for(int count=0;count<64&&conn_->recv(packet);++count) {
        Snapshot s;
        auto data=string(packet);bool ok=false;
        {Archive a(data);Message type{};header(a,type);
         if(a.ok()&&type==Message::SnapshotDelta&&data.size()>=9&&data.size()<=131072){auto records=delta_.decode(std::string_view(data).substr(9));ok=records&&recordsSnapshot(*records,s);}
         else ok=decodeSnapshot(packet,s);}
        if(!ok|| (ready_&&(s.tick<=latest_.tick||s.localSlot!=latest_.localSlot||s.ack<latest_.ack)))continue;
        if(ready_&&s.ack>sequence_)continue;
        latest_=std::move(s);
        auto own=std::find_if(latest_.characters.begin(),latest_.characters.end(),[&](const auto& p){return p.slot==latest_.localSlot;});
        predicted_=*own;
        if(config_.locomotion){   // reconcile the mode machine too: the server's state, then the unacknowledged inputs
            actor::LocomotionBody l;Archive a(latest_.locomotion);actor::io(a,l);
            predictedLocomotion_=(!latest_.locomotion.empty()&&a.ok()&&!a.remaining()&&actor::valid(l))?l:actor::LocomotionBody{};
            if(latest_.locomotion.empty())predictedLocomotion_.life=predicted_.life;
        }
        if(!ready_)sequence_=latest_.ack;
        while(!pending_.empty()&&pending_.front().sequence<=latest_.ack)pending_.pop_front();
        for(const auto& input:pending_)predict(input);
        ready_=true;
    }
}
bool Client::submit(actor::Input input) {
    if(!ready_||!connected()||pending_.size()>=256||sequence_==UINT32_MAX)return false;
    input.sequence=sequence_+1;input.viewTick=latest_.tick;
    actor::Input validated;if(!actor::decodeInput(actor::command(latest_.localSlot,input),validated))return false;
    ++sequence_;pending_.push_back(input);conn_->send(inputPacket(input));
    predict(input);return true;
}
}
