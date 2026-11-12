#include "Session.h"
#include <algorithm>

namespace aiverse::net::authority {
namespace {
constexpr uint32_t magic=0x46505331;
enum class Message:uint8_t { Hello=1,Input=2,Snapshot=3 };
Bytes bytes(const std::string& s){return Bytes(s.begin(),s.end());}
std::string string(const Bytes& b){return std::string(b.begin(),b.end());}
void header(Archive& a,Message& type) {uint32_t m=magic,v=kProtocol;a(m,v,type);if(m!=magic||v!=kProtocol)a.fail();}
Bytes inputPacket(fps::Input input){std::string s;Archive a(&s);Message type=Message::Input;header(a,type);a(input);return bytes(s);}
bool validActor(const fps::Actor& a) {
    if(a.weaponIndex>=4||a.bag.size()>12||a.armor<0||a.armor>100||a.money<0||a.money>16000||a.owned>15)return false;
    for(int i=0;i<4;++i)if(a.storedAmmo[i]<0||a.storedAmmo[i]>1500||a.storedReserve[i]<0||a.storedReserve[i]>100000)return false;
    for(auto item:a.bag)if(item.type<1||item.type>9||item.count<1||item.count>20)return false;
    return a.slot>0&&a.slot<=200&&a.team>0&&a.health>=0&&a.health<=100&&a.ammo>=0&&a.ammo<=1500&&a.reserve>=0&&a.reserve<=100000&&
        fxAbs(a.pos.x)<=100000_fx&&fxAbs(a.pos.y)<=100000_fx&&fxAbs(a.pos.z)<=100000_fx;
}
}
uint64_t contentId(const Json& rules,const Json& map){Hasher h;h.str(rules.dump(-1));h.str(map.dump(-1));if(map["world"].getBool("async"))h.str("world-stream-v1");return h.value();}
Bytes encodeSnapshot(const Snapshot& snapshot) {
    std::string s;Archive a(&s);Message type=Message::Snapshot;header(a,type);
    auto copy=snapshot;uint32_t count=uint32_t(copy.actors.size());a(copy.tick,copy.ack,copy.localSlot,copy.winner,count);
    for(auto& actor:copy.actors)a(actor);
    a(copy.projectiles,copy.loot,copy.vehicles,copy.match,copy.round);
    return bytes(s);
}
bool decodeSnapshot(const Bytes& b,Snapshot& out) {
    if(b.size()>131072)return false;
    auto s=string(b);Archive a(s);Message type{};header(a,type);Snapshot v;uint32_t count=0;
    a(v.tick,v.ack,v.localSlot,v.winner,count);
    if(!a.ok()||type!=Message::Snapshot||count==0||count>200||v.localSlot==0||v.localSlot>200)return false;
    bool own=false;uint8_t previous=0;
    for(uint32_t i=0;i<count;++i) {
        fps::Actor actor;a(actor);if(!a.ok()||!validActor(actor)||actor.slot<=previous)return false;
        previous=actor.slot;own|=actor.slot==v.localSlot;v.actors.push_back(actor);
    }
    a(v.projectiles,v.loot,v.vehicles,v.match,v.round);
    if(!a.ok()||v.projectiles.size()>128||v.loot.size()>128||v.vehicles.size()>32)return false;
    auto position=[](Vec3 p){return fxAbs(p.x)<=100000_fx&&fxAbs(p.y)<=100000_fx&&fxAbs(p.z)<=100000_fx;};
    for(const auto& p:v.projectiles)if(p.kind>4||p.weapon>=4||p.owner>200||p.piercedCount>8||!position(p.pos))return false;
    for(const auto& l:v.loot)if(!position(l.pos)||l.count<1||l.count>20)return false;
    for(const auto& vehicle:v.vehicles)if(!position(vehicle.pos)||vehicle.driver>200||vehicle.health<0||vehicle.health>500)return false;
    if(!own||a.remaining()!=0)return false;
    out=std::move(v);
    return true;
}
Server::Server(const Json& rules,const Json& map,uint64_t seed,world::AssetReader reader):
    simulation_({seed,fps::readConfig(rules).tickRate,0,false},std::make_unique<fps::Module>()),content_(contentId(rules,map)) {simulation_.setup(rules,map);if(map["world"].getBool("async")){if(!reader)throw JsonError("world: server content reader required");module().setContentReader(std::move(reader));}}
bool Server::occupied(uint8_t slot) const {for(const auto& p:peers_)if(p.slot==slot&&p.conn->state()!=ConnState::Closed)return true;return false;}
bool Server::attach(uint8_t slot,std::unique_ptr<Conn> conn) {
    if(!conn||slot==0||slot>module().state(simulation_).actors.size()||occupied(slot))return false;
    peers_.push_back({slot,std::move(conn),{},module().state(simulation_).actors[slot-1].lastSequence,false,0});return true;
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
            fps::Input input;a(input);
            fps::Input validated;
            if(!p.ready||type!=Message::Input||!a.ok()||a.remaining()||!fps::decodeInput(fps::command(p.slot,input),validated)||
               input.sequence<=p.lastReceived||uint64_t(input.sequence)>uint64_t(p.lastReceived)+256||
               input.viewTick>simulation_.tick()||p.inputs.size()>=8) {++rejected_;continue;}
            p.lastReceived=input.sequence;p.inputs.push_back(input);
        }
    }
    for(auto it=peers_.begin();it!=peers_.end();)if(it->conn->state()==ConnState::Closed) {
        auto& actor=module().state(simulation_).actors[it->slot-1];
        // Release through the same authoritative input path, never leave a disconnected trigger held.
        fps::Input release;release.sequence=actor.lastSequence+1;release.yaw=actor.yaw;release.pitch=actor.pitch;
        auto c=fps::command(it->slot,release);c.tick=simulation_.tick();simulation_.inject(c);
        it=peers_.erase(it);
    }else ++it;
}
bool Server::step() {
    if(!module().readyToStep(simulation_))return false;
    for(auto& p:peers_)if(p.ready&&!p.inputs.empty()) {
        auto c=fps::command(p.slot,p.inputs.front());p.inputs.pop_front();c.tick=simulation_.tick();simulation_.inject(c);
    }
    simulation_.step();
    auto& cfg=module().config();
    if(uint64_t(simulation_.tick())*cfg.snapshotRate/cfg.tickRate!=uint64_t(simulation_.tick()-1)*cfg.snapshotRate/cfg.tickRate)broadcast();
    return true;
}
void Server::broadcast() {
    auto& actors=module().state(simulation_).actors;
    world::SpatialWorld index(64);
    for(const auto& a:actors)index.add({a.slot,fps::actorBounds(a,module().config().motor)});
    auto radius=module().config().interestRadius;Vec3 extent{radius,radius,radius};
    for(auto& p:peers_)if(p.ready) {
        const auto& own=actors[p.slot-1];Snapshot s;s.tick=simulation_.tick();s.ack=own.lastSequence;s.localSlot=p.slot;s.winner=simulation_.result();
        auto ids=index.query({own.pos-extent,own.pos+extent});
        for(auto id:ids) {
            auto actor=actors[id-1];
            if(id!=p.slot&&(actor.pos-own.pos).lengthSq()>radius*radius)continue;
            // Remote control intent is never replicated; spectators need their own authorization policy.
            if(id!=p.slot){actor.input={};actor.reserve=0;actor.ammo=0;for(int i=0;i<4;++i){actor.storedAmmo[i]=0;actor.storedReserve[i]=0;}actor.hits=0;actor.bag.clear();actor.money=0;actor.activeAction=fps::Action::None;actor.actionTarget=0;actor.actionUntil=0;}
            s.actors.push_back(actor);
        }
        const auto& state=module().state(simulation_);
        s.round=state.round;s.match=state.match;
        auto nearby=[&](const auto& objects,auto& out,size_t limit){for(const auto& object:objects)if(out.size()<limit&&(object.pos-own.pos).lengthSq()<=radius*radius)out.push_back(object);};
        nearby(state.projectiles,s.projectiles,128);nearby(state.loot,s.loot,128);nearby(state.vehicles,s.vehicles,32);
        p.conn->sendLatest(encodeSnapshot(s));
    }
}
Client::Client(const Json& rules,const Json& map,std::unique_ptr<Conn> conn,world::AssetReader reader):config_(fps::readConfig(rules)),conn_(std::move(conn)),content_(contentId(rules,map)) {
    Simulation local({1,config_.tickRate,0},std::make_unique<fps::Module>());local.setup(rules,map);
    collision_=static_cast<fps::Module&>(local.module()).collision();
    if(map["world"].getBool("async")){if(!reader)throw JsonError("world: client content reader required");streamedCollision_=std::make_unique<world::CollisionStream>(map["world"],std::move(reader),collision_);predicted_.pos=world::worldVector(map["spawns"][0],100000_fx);}
}
void Client::updateStreaming() {
    if(!streamedCollision_)return;
    auto radius=fxMax(32_fx,config_.motor.sprintSpeed*Fixed::fromRatio(256,config_.tickRate)+16_fx);
    Vec3 extent{radius,radius,radius};world::Bounds regions[2]={{predicted_.pos-extent,predicted_.pos+extent},{predicted_.pos-extent,predicted_.pos+extent}};
    for(const auto& actor:latest_.actors)if(actor.slot==latest_.localSlot)regions[1]={actor.pos-extent,actor.pos+extent};
    streamedCollision_->prepareRegions(regions);
}
void Client::predict(const fps::Input& input) {
    // Never predict into a missing cell; keep authoritative pose until collision is available.
    const auto radius=config_.motor.sprintSpeed/Fixed::fromInt(config_.tickRate)+8_fx;
    Vec3 extent{radius,radius,radius};
    if(streamedCollision_&&!streamedCollision_->covers({predicted_.pos-extent,predicted_.pos+extent}))return;
    fps::moveActor(predicted_,input,config_.motor,collision_,Fixed::fromRatio(1,config_.tickRate),config_.worldLimit);
}
void Client::poll(uint64_t now) {
    conn_->update(now);
    if(!sentHello_&&conn_->state()==ConnState::Connected) {
        std::string s;Archive a(&s);Message type=Message::Hello;header(a,type);a(content_);conn_->send(bytes(s));sentHello_=true;
    }
    Bytes packet;
    for(int count=0;count<64&&conn_->recv(packet);++count) {
        Snapshot s;if(!decodeSnapshot(packet,s)|| (ready_&&(s.tick<=latest_.tick||s.localSlot!=latest_.localSlot||s.ack<latest_.ack)))continue;
        if(ready_&&s.ack>sequence_)continue;
        latest_=std::move(s);
        auto own=std::find_if(latest_.actors.begin(),latest_.actors.end(),[&](const auto& p){return p.slot==latest_.localSlot;});
        predicted_=*own;
        if(!ready_)sequence_=latest_.ack;
        while(!pending_.empty()&&pending_.front().sequence<=latest_.ack)pending_.pop_front();
        for(const auto& input:pending_)predict(input);
        ready_=true;
    }
}
bool Client::submit(fps::Input input) {
    if(!ready_||!connected()||pending_.size()>=256||sequence_==UINT32_MAX)return false;
    input.sequence=sequence_+1;input.viewTick=latest_.tick;
    fps::Input validated;if(!fps::decodeInput(fps::command(latest_.localSlot,input),validated))return false;
    ++sequence_;pending_.push_back(input);conn_->send(inputPacket(input));
    predict(input);return true;
}
}
