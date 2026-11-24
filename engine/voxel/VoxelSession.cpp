#include "../game/GameCore.h"
#include "VoxelSession.h"
#include "engine/core/Hash.h"
#include "engine/core/FixedMath.h"
#include <aiverselib/systems/voxellight/VoxelLight.h>
namespace aiverse::voxelgame {
using namespace lib::systems;
namespace {
enum Tag:uint8_t{Hello='H',Welcome='W',InputMsg='I',SnapshotMsg='S',VoxelMsg='V'};
net::Bytes bytesOf(const std::string& s){return net::Bytes(s.begin(),s.end());}
std::string stringOf(const net::Bytes& b){return std::string(b.begin(),b.end());}
void ioRemote(Archive& a,RemotePlayer& p){a(p.slot,p.pos,p.yaw,p.pitch,p.health,p.holding);}
void ioRemote(Archive& a,RemoteMob& m){a(m.id,m.pos,m.yaw,m.health,m.hurtAt);}
}
uint64_t contentId(const Json& rules,const Json& map){Hasher h;h.str(rules.dump(-1));h.str(map.dump(-1));h.u32(kProtocol);return h.value();}
net::Bytes encodeSnapshot(const Snapshot& snapshot){
    auto s=snapshot;std::string out;Archive a(&out);uint8_t tag=SnapshotMsg;a(tag,s.tick,s.ack,s.time);io(a,s.own);
    uint32_t n=uint32_t(s.players.size());a(n);for(auto& p:s.players)ioRemote(a,p);
    n=uint32_t(s.mobs.size());a(n);for(auto& m:s.mobs)ioRemote(a,m);return bytesOf(out);
}
bool decodeSnapshot(const net::Bytes& bytes,Snapshot& out){
    auto text=stringOf(bytes);Archive a(text);uint8_t tag=0;Snapshot s;a(tag,s.tick,s.ack,s.time);if(tag!=SnapshotMsg)return false;io(a,s.own);
    uint32_t n=a.count(0,10);if(n>256)return false;s.players.resize(n);for(auto& p:s.players)ioRemote(a,p);
    n=a.count(0,10);if(n>1024)return false;s.mobs.resize(n);for(auto& m:s.mobs)ioRemote(a,m);
    if(!a.ok()||a.remaining())return false;out=std::move(s);return true;
}
Server::Server(const Json& rules,const Json& map,uint64_t seed):sim_({seed,0,0},std::make_unique<game::GameCore>()){
    sim_.setup(rules,map);content_=contentId(rules,map);
}
bool Server::occupied(uint8_t slot)const{for(const auto& p:peers_)if(p.slot==slot&&p.conn->state()!=net::ConnState::Closed)return true;return false;}
bool Server::attach(uint8_t slot,std::unique_ptr<net::Conn> conn){
    if(!conn||slot<1||slot>module().state(sim_).players.size()||occupied(slot))return false;
    peers_.erase(std::remove_if(peers_.begin(),peers_.end(),[&](const Peer& p){return p.slot==slot;}),peers_.end());
    Peer p;p.slot=slot;p.conn=std::move(conn);peers_.push_back(std::move(p));return true;
}
void Server::send(Peer& p,const net::Bytes& b,bool latest){sent_+=b.size();if(latest)p.conn->sendLatest(b);else p.conn->send(b);}
void Server::poll(uint64_t now){
    for(auto& p:peers_){p.conn->update(now);net::Bytes msg;
        while(p.conn->recv(msg)){
            if(msg.empty())continue;
            if(msg[0]==Hello&&!p.ready){ByteReader r(msg.data()+1,msg.size()-1);auto protocol=r.u32();auto content=r.u64();
                if(!r.ok()||protocol!=kProtocol||content!=content_){p.conn->close();break;}
                p.ready=true;ByteWriter w;w.u8(Welcome);w.u8(p.slot);w.u32(sim_.tick());send(p,w.buf);}
            else if(msg[0]==InputMsg&&p.ready){
                ByteReader r(msg.data()+1,msg.size()-1);Input in;in.forward=int32_t(r.u32());in.right=int32_t(r.u32());in.yaw=Fixed::fromRaw(r.i64());in.pitch=Fixed::fromRaw(r.i64());
                in.buttons=r.u32();in.slot=r.u8();in.sequence=r.u32();if(!r.ok()||!r.atEnd())continue;
                p.inputs.push_back(in);while(p.inputs.size()>8)p.inputs.pop_front();}
        }
    }
    peers_.erase(std::remove_if(peers_.begin(),peers_.end(),[](const Peer& p){return p.conn->state()==net::ConnState::Closed;}),peers_.end());
}
bool Server::step(){
    for(auto& p:peers_)if(p.ready&&!p.inputs.empty()){auto c=command(p.slot,p.inputs.front());p.inputs.pop_front();c.tick=sim_.tick();sim_.inject(c);}
    sim_.step();
    auto& s=module().state(sim_);const auto& cfg=module().config();
    bool snap=uint64_t(sim_.tick())*cfg.snapshotRate/cfg.tickRate!=uint64_t(sim_.tick()-1)*cfg.snapshotRate/cfg.tickRate;
    for(auto& p:peers_){if(!p.ready)continue;const auto& own=s.players[p.slot-1];
        lib::net::VoxelView view;view.center=chunkOf({int32_t(own.pos.x.floorInt()),int32_t(own.pos.y.floorInt()),int32_t(own.pos.z.floorInt())});
        view.radius=cfg.viewRadius;view.below=s.world.bounds().maxChunkY-s.world.bounds().minChunkY;view.above=view.below; // whole columns
        lib::net::VoxelPumpConfig pump;pump.maxChunks=24;pump.maxBytes=512*1024;
        for(auto& m:lib::net::pumpVoxelPeer(pump,s.world,view,p.voxel,s.world.edits)){net::Bytes b;b.reserve(m.size()+1);b.push_back(VoxelMsg);b.insert(b.end(),m.begin(),m.end());send(p,b);}
        if(!snap)continue;
        Snapshot shot;shot.tick=sim_.tick();shot.ack=own.lastSequence;shot.time=s.time;shot.own=own;
        auto range=Fixed::fromInt(int64_t(cfg.viewRadius)*16);
        for(const auto& o:s.players)if(o.slot!=p.slot&&o.active&&(o.pos-own.pos).lengthSq()<range*range)
            shot.players.push_back({o.slot,o.pos,o.yaw,o.pitch,o.health,o.hotbar[o.selected].block});
        for(const auto& m:s.mobs)if((m.pos-own.pos).lengthSq()<range*range)shot.mobs.push_back(RemoteMob{m.id,m.pos,m.yaw,m.health,m.hurtAt});
        send(p,encodeSnapshot(shot),true);
    }
    s.world.edits.clear();s.world.editOverflow=false; // transport queue: every peer has been pumped
    return true;
}
Client::Client(const Json& rules,const Json& map,std::unique_ptr<net::Conn> conn):content_(readContent(rules)),mirror_(content_.bounds),conn_(std::move(conn)){
    content_id_=contentId(rules,map);
}
void Client::poll(uint64_t now){
    conn_->update(now);
    if(!hello_&&conn_->state()==net::ConnState::Connected){ByteWriter w;w.u8(Hello);w.u32(kProtocol);w.u64(content_id_);conn_->send(w.buf);hello_=true;}
    net::Bytes msg;bool rebaseNeeded=false;std::vector<BlockId> old;
    while(conn_->recv(msg)){
        if(msg.empty())continue;
        if(msg[0]==Welcome){ByteReader r(msg.data()+1,msg.size()-1);slot_=r.u8();ready_=r.ok()&&slot_>0;}
        else if(msg[0]==SnapshotMsg){Snapshot s;if(decodeSnapshot(msg,s)&&s.tick>=latest_.tick){latest_=std::move(s);rebaseNeeded=true;}}
        else if(msg[0]==VoxelMsg){net::Bytes body(msg.begin()+1,msg.end());old.clear();
            auto res=lib::net::applyVoxelMessage(mirror_,content_.blocks,body,&old);if(!res.ok)continue;chunks_+=res.inserted.size();
            for(size_t i=0;i<res.applied.size();++i)relightBlock(mirror_,content_.blocks,res.applied[i].pos,old[i]);}
    }
    // Light arriving chunks top-down (whole columns arrive nearest-first).
    if(!mirror_.lightDirty.empty()){std::vector<ChunkPos> pending(mirror_.lightDirty.begin(),mirror_.lightDirty.end());
        std::sort(pending.begin(),pending.end(),[](ChunkPos a,ChunkPos b){return a.x!=b.x?a.x<b.x:a.z!=b.z?a.z<b.z:a.y>b.y;});
        for(auto c:pending)lightChunk(mirror_,content_.blocks,c);}
    if(rebaseNeeded)rebase();
}
void Client::rebase(){
    while(!pending_.empty()&&pending_.front().sequence<=latest_.ack)pending_.pop_front();
    predicted_=latest_.own;auto dt=Fixed::fromRatio(1,content_.config.tickRate);
    for(const auto& in:pending_)movePlayer(predicted_,in,content_.config,mirror_,content_.blocks,dt);
}
bool Client::submit(Input in){
    if(!ready_)return false;in.sequence=++sequence_;
    ByteWriter w;w.u8(InputMsg);w.u32(uint32_t(in.forward));w.u32(uint32_t(in.right));w.i64(in.yaw.raw);w.i64(in.pitch.raw);w.u32(in.buttons);w.u8(in.slot);w.u32(in.sequence);
    conn_->send(w.buf);pending_.push_back(in);while(pending_.size()>120)pending_.pop_front();
    if(predicted_.slot&&predicted_.health>0)movePlayer(predicted_,in,content_.config,mirror_,content_.blocks,Fixed::fromRatio(1,content_.config.tickRate));
    return true;
}
}
