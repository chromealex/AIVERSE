// Authoritative place server: a physics::Scene loaded from a scene JSON, one kinematic character per client,
// replicated with net::replica (docs/replica.md "First milestone"). One update() = one fixed tick.
#pragma once
#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <map>
#include <memory>
#include <vector>
#include "apps/place/PlaceProtocol.h"
#include "engine/physics/Scene.h"

namespace aiverse::place {

// Tuning read from the scene's optional "place" block (docs/replica.md § Place scene).
struct PlaceConfig {
    std::vector<b3Vec3> spawns{{0, 1, 0}};
    float relevance = 200;            // replica radius of bodies and characters (m)
    b3Vec3 boxSize{0.8f, 0.8f, 0.8f}; // spawn-box message
    float spawnPerSecond = 2, spawnBurst = 3;
    int maxSpawnedPerPlayer = 20, maxBodies = 4000;
    float pushSpeed = 6, pushRadius = 1.2f, pushReach = 1.1f, pushMaxMass = 400;
    int pushCooldownTicks = 18;
    float killY = -40;
    struct Mover { std::string body; b3Vec3 offset{0, 0, 0}; float period = 8; };
    std::vector<Mover> movers;
    static PlaceConfig read(const Json& scene);   // throws JsonError with the key in the message
};

struct PlaceServerStats {
    uint64_t inputUnderruns = 0, inputCatchUps = 0;   // input buffer ran empty / applied two in a tick
    uint64_t ticks = 0, inputs = 0, idleSteps = 0, pushes = 0, pushedBodies = 0, spawned = 0, spawnDenied = 0, badMessages = 0;
    uint64_t joined = 0, resumed = 0, replaced = 0, expired = 0, contested = 0;   // reconnect window (PlaceServer::attach)
    double tickMsSum = 0, tickMsWorst = 0;
    uint64_t ticksTimedFrom = 0;
    // Optional observability: wall-time phase sums for the SAME server tick,
    // not a second simulation clock or a modified client/network workload.
    // Useful in a real server/load harness; disabled by default.
    struct TickPhases {
        double pollMs = 0, playersMs = 0, physicsMs = 0;
        double syncMs = 0, broadcastMs = 0;
        uint64_t samples = 0;
    } phases;
    double tickMsAvg() const { return ticks > ticksTimedFrom ? tickMsSum / double(ticks - ticksTimedFrom) : 0; }
};

class PlaceServer {
public:
    explicit PlaceServer(const Json& scene, replica::ServerOptions options = {}, int workers = 1);
    ~PlaceServer();
    // The host admitted `connection` (SecureListener ticket, PlaceGate hello) as `player`. A player id already in the
    // place is taken over: the older connection gets kNoticeReplaced and is kicked half a second later (newest wins),
    // the new one keeps the character. An empty id is an anonymous player without reconnect (tests, loopback).
    // Returns 0 when the replica is full or the id is contested (see admits).
    uint8_t attach(std::unique_ptr<net::Conn> connection, const std::string& player = "");
    // False while `player` is contested: taken over 3 times within 60 s (one ticket on two devices that both keep
    // reconnecting); then refused for 60 s. Hosts also call it before a handshake (SecureServerConfig::admit).
    bool admits(const std::string& player) const;
    // A player whose connection dropped keeps the character this long (it stands; the same player id re-attaches to
    // it with its position and velocity). 0: the character leaves with the connection.
    void setReconnectSeconds(double s) { reconnectTicks_ = uint64_t(std::max(0.0, s) * scene_->tickRate()); }
    // Host log of joins, reconnects and expiries (one line each, no trailing newline).
    std::function<void(const std::string&)> onEvent;
    void update(uint64_t nowMs);      // poll, characters (input frames), movers, physics, fields, broadcast

    replica::Server& replica() { return server_; }
    const replica::Server& replica() const { return server_; }
    physics::Scene& scene() { return *scene_; }
    const Schema& schema() const { return schema_; }
    const PlaceConfig& config() const { return config_; }
    const PlaceServerStats& stats() const { return stats_; }
    // Timing scopes are opt-in and are independent of gameplay/replication.
    // Enabling them adds only a handful of steady_clock samples per tick.
    void setTickPhaseProfiling(bool enabled) { tickPhaseProfiling_ = enabled; }
    void resetTickTiming() {
        stats_.tickMsSum = 0; stats_.tickMsWorst = 0;
        stats_.ticksTimedFrom = stats_.ticks;
        stats_.phases = {};
    }
    // Replicated dynamic / kinematic bodies (scene + spawned), by replica id.
    std::vector<replica::ObjectId> bodies() const;
    physics::BodyHandle bodyHandle(replica::ObjectId id) const;
    replica::ObjectId characterOf(uint8_t slot) const;
    const physics::CharacterState* characterState(uint8_t slot) const;
    size_t players() const { return players_.size(); }   // connected
    size_t away() const { return away_.size(); }          // dropped, character kept for a reconnect
    const std::string& playerOf(uint8_t slot) const;
    replica::ObjectId characterOfPlayer(const std::string& player) const;   // connected or away
    const physics::CharacterState* characterStateOf(const std::string& player) const;

private:
    struct Rep { physics::BodyHandle handle = physics::kNoBody; replica::ObjectId id = 0; bool awake = true; b3Vec3 start{}; b3Quat startRot{}; uint8_t spawnedBy = 0; };
    struct Player {
        std::string id;
        uint8_t slot = 0;
        uint64_t awayUntilTick = 0;
        replica::ObjectId object = 0;
        physics::BodyHandle proxy = physics::kNoBody;
        physics::CharacterState state;
        uint64_t lastInputTick = 0, pushReadyTick = 0;
        // Input frames wait here and are applied one per tick (two while the queue is long), so the owner's
        // prediction knows the tick of each pending input (docs/replica.md § Prediction against bodies).
        std::deque<std::pair<uint32_t, Input>> inputs;
        bool buffering = true;
        uint32_t applied = 0;
        double spawnTokens = 0;
        std::deque<physics::BodyHandle> spawned;
    };
    struct Mover { physics::BodyHandle handle; b3Vec3 start, offset; b3Quat rot; float period; };
    void join(uint8_t slot);
    void leave(uint8_t slot);
    void expire();
    void keepStanding(Player& p);
    void destroyPlayer(Player& p);
    std::string describe(const Player& p, const std::string& what) const;
    void input(uint8_t slot, uint32_t seq, const net::Bytes& frame);
    void apply(Player& p, const Input& in);
    void message(uint8_t slot, uint16_t channel, const net::Bytes& payload);
    void step(Player& p, const physics::CharacterInput& in);
    void push(Player& p);
    void spawnBox(uint8_t slot, Player& p);
    replica::ObjectId track(physics::BodyHandle h, const BodyLook& look, uint8_t spawnedBy);
    void untrack(physics::BodyHandle h);
    void syncBodies();
    void syncCharacters();
    void respawn(Player& p, uint8_t slot);

    Schema schema_;
    PlaceConfig config_;
    physics::CharacterParams params_;
    std::unique_ptr<physics::Scene> scene_;
    replica::Server server_;
    net::Bytes staticScene_;
    std::vector<Rep> reps_;                                  // in creation order
    std::map<physics::BodyHandle, size_t> repOf_;
    std::map<uint8_t, Player> players_;
    std::map<std::string, Player> away_;                     // by player id
    std::map<uint8_t, std::string> attached_;                // slot -> player id until join
    std::map<uint8_t, uint64_t> kicking_;                    // replaced slots -> kick at tick
    std::map<std::string, std::deque<uint64_t>> takeovers_;  // player id -> ticks of recent takeovers
    uint64_t reconnectTicks_ = 0;
    std::vector<Mover> movers_;
    replica::ObjectId game_ = 0;
    uint64_t tick_ = 0;
    uint32_t colorSeed_ = 1;
    PlaceServerStats stats_;
    bool tickPhaseProfiling_ = false;
};
}
