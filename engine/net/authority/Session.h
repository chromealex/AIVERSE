#pragma once
#include "engine/net/Conn.h"
#include "engine/actor/ActorModule.h"
#include "engine/net/SnapshotDelta.h"
#include <deque>

namespace aiverse::net::authority {
constexpr uint32_t kProtocol=6;   // 6: RaceView in snapshots
uint64_t contentId(const Json& rules,const Json& map);
struct Snapshot {
    uint32_t tick=0,ack=0;uint8_t localSlot=0;int32_t winner=0;
    std::vector<actor::CharacterSnapshot> characters;
    std::vector<actor::Projectile> projectiles;std::vector<actor::Loot> loot;std::vector<actor::Vehicle> vehicles;actor::RoundState match;uint32_t round=1;
    actor::RoyaleView royale;
    lib::systems::GearLoadout gear;   // own gear (fps.gear)
    lib::mechanics::RaceView race;        // own race standing (map "race")
    std::string locomotion;               // own actor::LocomotionBody bytes (rules `locomotion`), empty otherwise
};
Bytes encodeSnapshot(const Snapshot& snapshot);
bool decodeSnapshot(const Bytes&,Snapshot& out);
// Snapshot <-> stable records (0 = globals, kind << 28 | id: characters by slot, projectiles / loot / vehicles by id)
// for snapshot-delta. Servers send deltas against the newest snapshot the client acknowledged (its input viewTick).
lib::delta::RecordSet snapshotRecords(const Snapshot& snapshot);
bool recordsSnapshot(const lib::delta::RecordSet& records,Snapshot& out);
struct SnapshotStats{uint64_t packets=0,bytes=0,fullBytes=0,fullPackets=0;};
// Conn binding is the identity boundary: a client never sends a player slot or position.
// Production hosts must authenticate/admit the connection before attach().
class Server {
public:
    Server(const Json& rules,const Json& map,uint64_t seed=1,world::AssetReader reader={});
    bool attach(uint8_t slot,std::unique_ptr<Conn> connection);
    void poll(uint64_t now);
    bool step();
    void broadcast();
    Simulation& simulation(){return simulation_;}
    actor::ActorHost& module(){return actor::host(simulation_);}
    bool occupied(uint8_t slot) const;
    uint64_t rejected()const{return rejected_;}
    const SnapshotStats& snapshotStats()const{return stats_;}
private:
    struct Peer { uint8_t slot=0;std::unique_ptr<Conn> conn;std::deque<actor::Input> inputs;uint32_t lastReceived=0;bool ready=false;uint64_t born=0;lib::delta::DeltaSender delta; };
    Simulation simulation_;
    uint64_t content_=0,rejected_=0;
    SnapshotStats stats_;
    std::vector<Peer> peers_;
};
class Client {
public:
    Client(const Json& rules,const Json& map,std::unique_ptr<Conn> connection,world::AssetReader reader={});
    void updateStreaming();
    const world::AsyncChunkStream::Status* streamingStatus()const{return streamedCollision_?&streamedCollision_->status():nullptr;}
    void poll(uint64_t now);
    bool submit(actor::Input input);
    const Snapshot& snapshot()const{return latest_;}
    const actor::CharacterSnapshot& predicted()const{return predicted_;}
    // Predicted locomotion mode state (rules `locomotion`); default-constructed without it.
    const actor::LocomotionBody& predictedLocomotion()const{return predictedLocomotion_;}
    bool ready()const{return ready_;}
    bool connected()const{return conn_->state()!=ConnState::Closed;}
    uint32_t nextSequence()const{return sequence_+1;}
    size_t pending()const{return pending_.size();}
private:
    actor::Config config_;
    world::SpatialWorld collision_;
    std::unique_ptr<Conn> conn_;
    Snapshot latest_;
    actor::CharacterSnapshot predicted_;
    actor::LocomotionBody predictedLocomotion_;
    std::deque<actor::Input> pending_;
    uint32_t sequence_=0;
    bool ready_=false,sentHello_=false;
    uint64_t content_=0;
    lib::delta::DeltaReceiver delta_;
    std::unique_ptr<world::CollisionStream> streamedCollision_;
    void predict(const actor::Input&);
};
}
