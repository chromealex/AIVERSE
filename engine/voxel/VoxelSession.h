#pragma once
// Authoritative network session of the voxel game over any net::Conn (UDP, WebSocket, loopback). The server runs
// the only simulation; clients get a snapshot of players and mobs every snapshot tick and the block world through
// voxel-replication (chunks around them, then edits). The client predicts its own movement by replaying unacked
// inputs on its mirror world with the same motor as the server. Single player is the same path over a loopback.
#include "VoxelGame.h"
#include "engine/net/Conn.h"
#include <aiverselib/net/voxel/VoxelReplication.h>
#include <deque>
namespace aiverse::voxelgame {
constexpr uint32_t kProtocol=1;
uint64_t contentId(const Json& rules,const Json& map);
struct RemotePlayer{uint8_t slot=0;Vec3 pos{};Fixed yaw{},pitch{};int32_t health=0;BlockId holding=0;};
struct RemoteMob{uint32_t id=0;Vec3 pos{};Fixed yaw{};int32_t health=0;uint32_t hurtAt=0;};
struct Snapshot{uint32_t tick=0,ack=0;uint64_t time=0;Player own;std::vector<RemotePlayer> players;std::vector<RemoteMob> mobs;};
net::Bytes encodeSnapshot(const Snapshot&);
bool decodeSnapshot(const net::Bytes&,Snapshot&);
class Server{
public:
    Server(const Json& rules,const Json& map,uint64_t seed=1);
    bool attach(uint8_t slot,std::unique_ptr<net::Conn> conn);
    bool occupied(uint8_t slot)const;
    void poll(uint64_t now);
    bool step();
    Simulation& simulation(){return sim_;}
    VoxelHost& module(){return host(sim_);}
    size_t peers()const{return peers_.size();}
    uint64_t sentBytes()const{return sent_;}
private:
    struct Peer{uint8_t slot=0;std::unique_ptr<net::Conn> conn;std::deque<Input> inputs;bool ready=false;lib::net::VoxelPeer voxel;};
    void send(Peer&,const net::Bytes&,bool latest=false);
    Simulation sim_;uint64_t content_=0,sent_=0;std::vector<Peer> peers_;
};
class Client{
public:
    Client(const Json& rules,const Json& map,std::unique_ptr<net::Conn> conn);
    void poll(uint64_t now);
    // Queue one input for the next server tick and advance the local prediction by one tick.
    bool submit(Input input);
    bool ready()const{return ready_;}
    bool connected()const{return conn_->state()!=net::ConnState::Closed;}
    uint8_t slot()const{return slot_;}
    const Snapshot& snapshot()const{return latest_;}
    const Player& predicted()const{return predicted_;}
    const Content& content()const{return content_;}
    VoxelWorld& world(){return mirror_;}
    const VoxelWorld& world()const{return mirror_;}
    uint64_t receivedChunks()const{return chunks_;}
private:
    void rebase();
    Content content_;VoxelWorld mirror_;std::unique_ptr<net::Conn> conn_;
    Snapshot latest_;Player predicted_;std::deque<Input> pending_;
    uint32_t sequence_=0;uint8_t slot_=0;bool ready_=false,hello_=false;uint64_t content_id_=0,chunks_=0;
};
}
