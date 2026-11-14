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
constexpr uint32_t kProtocol = 4;
// What both sides must agree on: the unit types (their order is the wire's type id) and the map's players and
// entities (their order is the entity ids at the start). Other rules may differ: the client does not simulate.
uint64_t contentId(const Json& rules, const Json& map);
// The project as the city runs it, the same on the server and on every client: `seats` heroes (the map's own hero
// first, the others at crossroads / doors spread over the city) and property that belongs to accounts.
// Call it on the rules and the map the simulation is about to be set up from. Throws when there is no room.
void prepare(Json& rules, Json& map, int seats);
uint64_t accountId(const std::string& player);
std::string toHex(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> fromHex(const std::string& text);   // empty when malformed   // the commerce account of a player id (never 0)

enum class Replicate : uint8_t { All, Involved };

struct Options {
    int radius = 40;              // cells around the hero a client is told about
    int snapshotRate = 10;        // snapshots per second
    int restMs = 3000;            // an entity that did not change this long counts as standing: told once, not repeated
    int keepaliveMs = 1000;       // a moving entity that did not change is repeated this often (a client forgets it after 2.5x)
    int maxEntities = 250;        // per snapshot (14 bytes each)
    Replicate replicate = Replicate::All;
    int saveSeconds = 60;         // bound accounts are stored this often (and when the player leaves)
    // The streets fill with the players: the traffic limits of the rules (meant for one player) are multiplied by
    // 1 + trafficPerPlayer * (players - 1), at most trafficMaxScale times.
    double trafficPerPlayer = 0.1; double trafficMaxScale = 6;
    bool emptySeats = true;       // a seat nobody plays has no hero in the city; he comes back where he left
    std::vector<uint8_t> key;     // 32 bytes: admit() lets in only players with a ticket() sealed by this key (empty: anybody, as a guest)
    std::function<int64_t()> clock;   // unix seconds for ticket expiry (default: the system clock)
};
// A ticket the login service gives a player: his id and an expiry time sealed with the server's key.
std::vector<uint8_t> ticket(const std::vector<uint8_t>& key, const std::string& player, int64_t expiresUnix);

struct EntityState {              // 14 bytes on the wire
    uint32_t id = 0;              // Entity::packed()
    uint16_t type = 0;
    uint8_t owner = 0;
    uint16_t x = 0, z = 0;        // 1/64 cell
    uint8_t yaw = 0;              // the unit's facing, 1/256 turn
    uint8_t hp = 0;               // 0..255 of the type's max
    uint8_t flags = 0;            // 1: a player is it or drives it, 2: this hero is inside a vehicle
    bool operator==(const EntityState&) const = default;
};
struct Shot { uint32_t id = 0, source = UINT32_MAX; uint16_t x = 0, z = 0; uint8_t y = 0; };   // a projectile in flight; y: 1/8 cell
struct Event { uint32_t tick = 0, id = 0; uint8_t kind = 0; uint16_t x = 0, z = 0; uint32_t subject = 0; };
struct RemoteEvent {              // a SimEvent of the server's simulation that happened in view (or is about this player)
    uint16_t type = 0; uint8_t player = 0; uint32_t a = UINT32_MAX, b = UINT32_MAX; int64_t value = 0; uint16_t x = 0, z = 0;
};
struct PremiseState {             // one of the map's premises as this player may know it
    uint8_t owner = 0, tenant = 0;    // 0 nobody (the city), 1 this player, 2 somebody else
    int16_t business = -1;
    uint8_t staff = 0, flags = 0;     // flags 1: a manager, 2: visited today
    uint8_t attendance = 255;
    int64_t lastNet = 0, total = 0, served = 0, stashCash = 0;   // stashCash: only of his own home
    bool operator==(const PremiseState&) const = default;
};
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
    uint64_t snapshots = 0, entities = 0, snapshotBytes = 0, reliableBytes = 0, commands = 0, rejected = 0, events = 0, broadcastNs = 0, fixedTold = 0, simEvents = 0, premisesTold = 0;
};

class Server {
public:
    Server(const Json& rules, const Json& map, uint64_t seed = 1, Options options = {}, AccountStore* accounts = nullptr);
    ~Server();
    std::vector<uint8_t> seats() const;          // slots that have a hero
    bool occupied(uint8_t slot) const;
    // false: no such seat, it is taken, or that player is already in.
    bool attach(uint8_t slot, std::unique_ptr<Conn> connection, const std::string& player = {});
    // A connection nobody vouched for: it gets a free seat when its hello carries a valid ticket (Options::key),
    // or, without a key, as the guest it names itself.
    void admit(std::unique_ptr<Conn> connection);
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
        std::vector<RemoteEvent> events;                // of the ticks since the last snapshot
        std::vector<PremiseState> premises;             // as he was told
        std::unordered_map<uint32_t, uint32_t> fixed;   // things that never moved, told once (reliably): id -> the tick they were last in view
    };
    void leave(Peer& p);
    bool seat(Peer& p, uint8_t slot, const std::string& player);
    bool hello(Peer& p, const Bytes& packet, bool seated);
    void welcome(Peer& p);
    void collectEvents();
    void tellPremises(Peer& p);
    std::vector<uint8_t> seats_;                        // slots that had a hero on the map
    std::vector<Peer> waiting_;                         // admitted, no hello yet
    uint64_t guests_ = 0;
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
    Client(std::unique_ptr<Conn> connection, uint64_t content, std::string player = {}, std::vector<uint8_t> ticket = {});
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
    const std::vector<Shot>& shots() const { return shots_; }   // every projectile in view as of the last snapshot
    std::vector<Event> takeEvents() { return std::move(events_); }
    std::vector<RemoteEvent> takeSimEvents() { return std::move(simEvents_); }
    const std::vector<PremiseState>& premises() const { return premises_; }   // by the map's premises index (as far as told)
    uint32_t premisesVersion() const { return premisesVersion_; }
    uint32_t rttMs() const { return conn_->rttMs(); }
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
    std::vector<Shot> shots_;
    std::vector<RemoteEvent> simEvents_;
    std::vector<PremiseState> premises_;
    uint32_t premisesVersion_ = 0;
    std::string player_;
    std::vector<uint8_t> ticket_;
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
