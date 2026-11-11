#pragma once
#include "engine/net/Conn.h"
#include "modules/fps/FpsModule.h"
#include <deque>

namespace aiverse::net::authority {
constexpr uint32_t kProtocol=3;
uint64_t contentId(const Json& rules,const Json& map);
struct Snapshot {
    uint32_t tick=0,ack=0;uint8_t localSlot=0;int32_t winner=0;
    std::vector<fps::Actor> actors;
    std::vector<fps::Projectile> projectiles;std::vector<fps::Loot> loot;std::vector<fps::Vehicle> vehicles;fps::RoundState match;uint32_t round=1;
};
Bytes encodeSnapshot(const Snapshot& snapshot);
bool decodeSnapshot(const Bytes&,Snapshot& out);
// Conn binding is the identity boundary: a client never sends a player slot or position.
// Production hosts must authenticate/admit the connection before attach().
class Server {
public:
    Server(const Json& rules,const Json& map,uint64_t seed=1);
    bool attach(uint8_t slot,std::unique_ptr<Conn> connection);
    void poll(uint64_t now);
    void step();
    void broadcast();
    Simulation& simulation(){return simulation_;}
    fps::Module& module(){return static_cast<fps::Module&>(simulation_.module());}
    bool occupied(uint8_t slot) const;
    uint64_t rejected()const{return rejected_;}
private:
    struct Peer { uint8_t slot=0;std::unique_ptr<Conn> conn;std::deque<fps::Input> inputs;uint32_t lastReceived=0;bool ready=false;uint64_t born=0; };
    Simulation simulation_;
    uint64_t content_=0,rejected_=0;
    std::vector<Peer> peers_;
};
class Client {
public:
    Client(const Json& rules,const Json& map,std::unique_ptr<Conn> connection);
    void poll(uint64_t now);
    bool submit(fps::Input input);
    const Snapshot& snapshot()const{return latest_;}
    const fps::Actor& predicted()const{return predicted_;}
    bool ready()const{return ready_;}
    bool connected()const{return conn_->state()!=ConnState::Closed;}
    uint32_t nextSequence()const{return sequence_+1;}
    size_t pending()const{return pending_.size();}
private:
    fps::Config config_;
    world::SpatialWorld collision_;
    std::unique_ptr<Conn> conn_;
    Snapshot latest_;
    fps::Actor predicted_;
    std::deque<fps::Input> pending_;
    uint32_t sequence_=0;
    bool ready_=false,sentHello_=false;
    uint64_t content_=0;
};
}
