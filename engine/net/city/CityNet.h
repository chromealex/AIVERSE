// A persistent city on an authoritative server (docs/city-server.md).
//
// The server runs the one simulation of the city (an RtsModule project with top-down heroes). A client never runs
// it: it sends its hero's commands and receives what is near its hero — the entities within `radius` cells, the
// incidents that happened in view, and its own account. Background life that nobody touched may be simulated by
// the client itself (Replicate::Involved sends only the players, what they drive and what an incident involves).
//
// Identity is the connection: the server stamps the slot on every command, a client never names a player.
// A production host authenticates the connection before attach() and passes the player's id.
#pragma once
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "engine/net/Conn.h"
#include "engine/sim/Simulation.h"

namespace aiverse::game { class RtsModule; }

namespace aiverse::net::city {
constexpr uint32_t kProtocol = 2;
// What both sides must agree on: the unit types (their order is the wire's type id) and the map's players and
// entities (their order is the entity ids at the start). Other rules may differ: the client does not simulate.
uint64_t contentId(const Json& rules, const Json& map);
// The project as the city runs it, the same on the server and on every client: `seats` heroes (the map's own hero
// first, the others at crossroads / doors spread over the city) and property that belongs to accounts.
// Call it on the rules and the map the simulation is about to be set up from. Throws when there is no room.
void prepare(Json& rules, Json& map, int seats);
uint64_t accountId(const std::string& player);   // the commerce account of a player id (never 0)

enum class Replicate : uint8_t { All, Involved };

struct Options {
    int radius = 40;              // cells around the hero a client is told about
    int snapshotRate = 10;        // snapshots per second
    int restMs = 3000;            // an entity that did not change this long counts as standing: told once, not repeated
    int keepaliveMs = 1000;       // a moving entity that did not change is repeated this often (a client forgets it after 2.5x)
    int maxEntities = 250;        // per snapshot (14 bytes each)
    Replicate replicate = Replicate::All;
    int saveSeconds = 60;         // bound accounts are stored this often (and when the player leaves)
};

struct EntityState {              // 14 bytes on the wire
    uint32_t id = 0;              // Entity::packed()
    uint16_t type = 0;
    uint8_t owner = 0;
    uint16_t x = 0, z = 0;        // 1/64 cell
    uint8_t yaw = 0;              // the unit's facing, 1/256 turn
    uint8_t hp = 0;               // 0..255 of the type's max
    uint8_t flags = 0;            // 1: a player drives / is it
    bool operator==(const EntityState&) const = default;
};
struct Event { uint32_t tick = 0, id = 0; uint8_t kind = 0; uint16_t x = 0, z = 0; uint32_t subject = 0; };
struct AccountState { int64_t cash = 0, debt = 0; uint32_t day = 0; bool operator==(const AccountState&) const = default; };

// Where accounts live between sessions (the meta service; a file in tests). Both run on the server's thread.
struct AccountStore {
    virtual ~AccountStore() = default;
    virtual Json enter(const std::string& player) = 0;                    // the stored account (null: a new player)
    virtual void save(const std::string& player, const Json& account, bool leaving) = 0;
    virtual std::vector<std::pair<std::string, Json>> known() { return {}; }   // every stored account (a starting server takes them all back)
};

struct Stats {
    std::unordered_map<uint16_t, uint64_t> byType;   // snapshot entries per unit type (what the traffic is made of)
    uint64_t snapshots = 0, entities = 0, snapshotBytes = 0, reliableBytes = 0, commands = 0, rejected = 0, events = 0, broadcastNs = 0, fixedTold = 0;
};

class Server {
public:
    Server(const Json& rules, const Json& map, uint64_t seed = 1, Options options = {}, AccountStore* accounts = nullptr);
    ~Server();
    std::vector<uint8_t> seats() const;          // slots that have a hero
    bool occupied(uint8_t slot) const;
    // false: no such seat, it is taken, or that player is already in.
    bool attach(uint8_t slot, std::unique_ptr<Conn> connection, const std::string& player = {});
    uint8_t freeSeat() const;                    // 0: full
    void poll(uint64_t now);                     // read the clients, drop the closed ones (their accounts are stored)
    void step();                                 // one tick; broadcasts when a snapshot is due
    void broadcast();
    void saveAll();
    // A player who has been away too long: everything he owns is sold to the city, the money waits in his account.
    // false: he is in the city now, or the server does not know him.
    bool sellOut(const std::string& player);
    Simulation& simulation() { return *sim_; }
    game::RtsModule& module();
    const Stats& stats() const { return stats_; }
    size_t clients() const { return peers_.size(); }
    uint64_t content() const { return content_; }

private:
    struct Sent { EntityState state; uint32_t tick = 0; };
    struct Peer {
        uint8_t slot = 0; std::unique_ptr<Conn> conn; std::string player; uint64_t account = 0;
        bool ready = false; uint64_t born = 0; std::deque<Command> commands;
        std::unordered_map<uint32_t, Sent> sent; AccountState told; bool toldAny = false;
        std::unordered_map<uint32_t, uint32_t> fixed;   // things that never moved, told once (reliably): id -> the tick they were last in view
    };
    void leave(Peer& p);
    void store(Peer& p, bool leaving);
    std::unique_ptr<Simulation> sim_;
    Options opt_;
    AccountStore* accounts_ = nullptr;
    uint64_t content_ = 0;
    uint32_t lastIncident_ = 0;
    struct Known { EntityState state; uint32_t seen = 0, changed = 0; bool moving = false; };
    std::unordered_map<uint32_t, Known> known_;   // every entity as it was last seen: one that has not changed for a while stands
    std::vector<Peer> peers_;
    Stats stats_;
};

class Client {
public:
    Client(std::unique_ptr<Conn> connection, uint64_t content);
    void poll(uint64_t now);
    bool submit(const Command& command);         // the server stamps the player and the tick
    bool ready() const { return slot_ != 0; }
    bool connected() const { return conn_->state() != ConnState::Closed; }
    void close() { conn_->close(); }
    uint8_t slot() const { return slot_; }
    uint32_t hero() const { return hero_; }
    uint32_t tick() const { return tick_; }
    uint32_t tickRate() const { return tickRate_; }
    struct Seen { EntityState state; uint32_t tick = 0; bool fixed = false; };   // fixed: stays until the server says it is gone
    const std::unordered_map<uint32_t, Seen>& entities() const { return entities_; }
    const AccountState& account() const { return account_; }
    std::vector<Event> takeEvents() { return std::move(events_); }
    uint64_t bytesIn() const { return bytesIn_; }
    uint64_t bytesOut() const { return bytesOut_; }
    uint64_t snapshots() const { return snapshots_; }

private:
    std::unique_ptr<Conn> conn_;
    uint64_t content_ = 0, bytesIn_ = 0, bytesOut_ = 0, snapshots_ = 0;
    uint8_t slot_ = 0;
    uint32_t hero_ = 0, tick_ = 0, tickRate_ = 20, expire_ = 50;
    bool sentHello_ = false;
    std::unordered_map<uint32_t, Seen> entities_;
    AccountState account_;
    std::vector<Event> events_;
};

// A file of accounts: {"<player>": {"account": {...}, "seen": unixSeconds, "online": bool}} (tests, a local server).
class FileAccounts final : public AccountStore {
public:
    explicit FileAccounts(std::string path, std::function<int64_t()> clock = {});
    Json enter(const std::string& player) override;
    void save(const std::string& player, const Json& account, bool leaving) override;
    std::vector<std::pair<std::string, Json>> known() override;
    std::vector<std::string> absent(int64_t seconds) const;   // offline longer than this
    const Json& data() const { return data_; }
private:
    void flush();
    std::string path_;
    std::function<int64_t()> clock_;
    Json data_ = Json::object();
};
}
