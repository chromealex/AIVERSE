// Generic server-authoritative replication (docs/replica.md).
//
// The third network model next to the lockstep relay (engine/net/server: every client runs the same
// deterministic simulation) and the two genre-bound authoritative hosts (engine/net/authority: FPS actors,
// engine/net/city: RTS city mirror). Here the server owns an arbitrary set of objects whose fields are
// described by a Schema; clients never simulate the world, they receive:
//   - delta snapshots against the newest snapshot they acknowledged (Quake 3 model), over Conn::sendLatest,
//   - only objects relevant to them (interest grid around their viewers + always-relevant objects),
//   - within a per-client byte budget, highest accumulated priority first (starved objects catch up),
//   - reliable fields (names, colours, strings) and remote messages over Conn::send.
// The server may use float state (Box3D physics, Lua): nothing here needs determinism.
//
// Prediction is the game's: the client sends opaque input frames with a sequence number; every snapshot
// tells it the last input the server applied for it, and the state of the object it owns, so it can rewind
// and replay its pending inputs (Predictor<State> below).
//
// Identity is the connection: the server assigns the slot; a client never names a player or an object it
// does not own. Every decode treats the peer as hostile (bounds, sizes, unknown ids are dropped silently).
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "engine/net/Conn.h"

namespace aiverse::net::replica {

constexpr uint32_t kProtocol = 2;     // 2: object generations, delta-coded numbers
// 0 = none; ids are reused only after every client acked the destroy. Each reuse bumps a generation that travels
// with creates and reliable fields, so a client never mixes the reliable fields of two objects behind one id.
using ObjectId = uint32_t;
using ClassId = uint16_t;
constexpr ObjectId kNoObject = 0;

// ---------------------------------------------------------------- schema
enum class FieldKind : uint8_t {
    Bool, Int,          // Int: [min, max] integer range, bits derived from it
    Float,              // quantized to `precision` inside [min, max]
    Vec3,               // position-like: each axis quantized to `precision` inside [min, max]
    Quat,               // rotation, smallest-three, `bits` per component (default 12)
    String, Bytes,      // always reliable (sent on change through Conn::send), at most 4 KiB
};

struct FieldDef {
    std::string name;
    FieldKind kind = FieldKind::Float;
    double min = 0, max = 0, precision = 0.01;
    uint8_t bits = 0;               // Quat bits per component; 0 = default
    bool reliable = false;          // force the reliable path (rarely changing values)
    bool interpolate = false;       // Float / Vec3 / Quat: client blends between snapshots
};

struct ClassDef {
    std::string name;
    std::vector<FieldDef> fields;
    float priority = 1.0f;          // accumulated per snapshot it was not sent; higher goes first
    float radius = 0;               // relevance radius around viewers; 0 = the server's default
    bool alwaysRelevant = false;    // game state, teams, round clock ...
    int positionField = -1;         // index of the Vec3 field used for interest (-1: always relevant)
};

class Schema {
public:
    ClassId add(ClassDef def);      // throws std::runtime_error on a malformed definition
    const ClassDef& cls(ClassId id) const { return classes_.at(id); }
    size_t size() const { return classes_.size(); }
    int field(ClassId cls, const std::string& name) const;   // -1 when unknown
    uint64_t hash() const;          // both sides must agree (checked in the hello)
private:
    std::vector<ClassDef> classes_;
};

// One field value. Which members are meaningful depends on FieldKind.
struct Value {
    int64_t i = 0;                  // Bool, Int
    std::array<float, 4> f{};       // Float: f[0]; Vec3: f[0..2]; Quat: x y z w
    std::string s;                  // String, Bytes
    bool operator==(const Value&) const = default;
};

// Immutable stack-backed batch for one authoritative replicated object.
// The caller retains ownership; setFields never stores these references.
struct FieldUpdate {
    int field = -1;
    Value value{};
};

// ---------------------------------------------------------------- server
struct ServerOptions {
    int tickRate = 60;              // Server::tick() calls per second (snapshot tick numbering)
    int snapshotRate = 20;          // snapshots per second per client
    int bytesPerSecond = 48000;     // per client budget for latest snapshots (reliable traffic not counted)
    float defaultRadius = 64;       // world units
    float cellSize = 32;            // interest grid
    int maxSnapshotBytes = 16000;   // hard cap of one snapshot (LatestChannel limit is 128 KiB)
    int history = 32;               // snapshots kept per client for delta baselines
    int maxInputsPerTick = 4;       // input frames accepted per client per tick (the rest wait, then are dropped)
    int inputGapTicks = 3;          // a missing input frame is skipped after this many ticks of waiting
    size_t maxInputBytes = 256;     // a packet with a larger frame is rejected whole
    size_t maxMessageBytes = 8192;
    int maxMessagesPerSecond = 30;  // remote messages per client (burst: twice this); the rest are dropped
    uint32_t helloTimeoutMs = 5000;
    // Interest grid on x/y/z (towers, multi-floor buildings, space): only cells near a viewer are visited. Off:
    // x/z columns (every object above or below a viewer is distance-checked). The relevance test is the same
    // 3D sphere of the class radius either way; this only changes what it costs.
    bool interest3d = true;
};

struct ServerStats {
    uint64_t snapshots = 0, snapshotBytes = 0, reliableBytes = 0, inputs = 0, rejected = 0, starved = 0;
    uint64_t entries = 0;           // objects written into snapshots (creates + deltas)
    uint64_t creates = 0;           // of which full creates
    uint64_t deltaFields = 0;       // numeric fields written as a small difference to a confirmed value
    uint64_t evaluated = 0;         // relevant objects compared field by field (changed or not yet confirmed)
    uint64_t relevant = 0;          // relevant objects summed over snapshots (evaluated / relevant: work share)
    uint64_t interestChecks = 0;    // distance tests (grid passes + re-tests of changed objects)
};

class Server {
public:
    Server(const Schema& schema, ServerOptions options = {});
    ~Server();

    // ---- objects (authoritative state)
    ObjectId create(ClassId cls);
    void destroy(ObjectId id);
    bool exists(ObjectId id) const;
    // Stores the value as given (get() returns it); snapshots carry its quantized form, so a change below
    // the field's precision costs nothing. Unknown ids / fields are ignored.
    void set(ObjectId id, int field, const Value& v);
    // Same per-field quantization, change-log and reliable notifications as
    // set(), but one object/layout lookup for a group of contiguous updates.
    // Duplicate fields are processed in order; no heap allocation required.
    void setFields(ObjectId id, std::span<const FieldUpdate> updates);
    const Value& get(ObjectId id, int field) const;
    void setOwner(ObjectId id, uint8_t slot);               // 0 = nobody; the owner gets ownedState for prediction
    void setVisibleTo(ObjectId id, uint8_t slot, bool on);  // per-client override (hidden info); default: by interest
    void resetVisibility(ObjectId id, uint8_t slot = 0);    // back to interest (slot 0: every override)
    uint8_t ownerOf(ObjectId id) const;

    // ---- clients
    // The host authenticates `connection` before handing it over. Returns the slot (1..255), 0 when full.
    uint8_t attach(std::unique_ptr<Conn> connection);
    void kick(uint8_t slot);
    std::vector<uint8_t> clients() const;
    // Where the client looks from (its character, camera); several viewers per client are allowed.
    void setViewers(uint8_t slot, const std::vector<std::array<float, 3>>& points);
    // Single-viewer fast path for character-driven cameras: same finite
    // filter and replacement semantics, without a temporary vector.
    void setViewer(uint8_t slot, std::array<float, 3> point);
    uint32_t rttMs(uint8_t slot) const;

    std::function<void(uint8_t slot)> onJoin, onLeave;
    // Input frames in sequence order, at most maxInputsPerTick per tick; `seq` is the client's counter.
    std::function<void(uint8_t slot, uint32_t seq, const Bytes& input)> onInput;
    // Remote events from clients (channel = game-defined id). Rate-limited, size-limited.
    std::function<void(uint8_t slot, uint16_t channel, const Bytes& payload)> onMessage;
    void send(uint8_t slot, uint16_t channel, const Bytes& payload);   // reliable; slot 0 = everybody

    // ---- loop: poll() -> (game reads inputs via onInput, steps its world, set()s fields) -> tick() -> broadcast()
    void poll(uint64_t nowMs);
    void tick();                    // advances the server tick (stamped on snapshots)
    void broadcast(uint64_t nowMs); // sends due snapshots
    uint32_t serverTick() const;
    const ServerStats& stats() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------- client
struct ClientOptions {
    uint32_t interpolationDelayMs = 100;   // render remote objects this far in the past
    // When snapshots stop (loss burst) and render time passes the newest one, interpolated Float / Vec3 fields
    // of objects in that newest snapshot continue along their last velocity for at most this long, then hold.
    // Objects missing from a snapshot that covers render time hold (they did not change, or were starved).
    uint32_t extrapolateMs = 250;
    // Each input packet repeats the last N frames: covers loss, and the latest channel keeping only the newest
    // packet when two arrive between server polls. 5 survives the SimNet "bad" preset's 5-packet bursts mostly.
    int inputRedundancy = 5;
    // Render clock: render time follows (estimated newest server time - delay) by changing its rate by at most
    // this fraction (0.05: 5 % fast or slow), so it never jumps or runs backwards; a target more than
    // `clockSnapMs` away (first snapshot, server restart, long freeze) is taken at once.
    float maxClockAdjust = 0.05f;
    uint32_t clockSnapMs = 1000;
    // Adaptive delay: the delay follows the measured snapshot jitter and loss (1.5 snapshot intervals + 4 x jitter
    // + up to one more interval at 4 % snapshot loss), clamped to [minDelayMs (0: one interval), maxDelayMs];
    // interpolationDelayMs is then only the start value.
    bool adaptiveDelay = false;
    uint32_t minDelayMs = 0, maxDelayMs = 300;
};

struct ClientStats {
    uint64_t snapshots = 0, bytesIn = 0, bytesOut = 0, dropped = 0;
    uint64_t underruns = 0;         // times render time passed the newest snapshot (extrapolate / hold began)
    uint64_t underrunMs = 0;        // time spent so
    uint64_t clockSnaps = 0;        // times the render clock was set instead of slewed
};

class Client {
public:
    Client(const Schema& schema, std::unique_ptr<Conn> connection, ClientOptions options = {});
    ~Client();
    void poll(uint64_t nowMs);
    bool ready() const;             // welcomed, first snapshot applied
    bool connected() const;
    uint8_t slot() const;

    // Input frames for the owned object(s); returns the sequence number assigned.
    uint32_t sendInput(const Bytes& input);
    uint32_t lastAppliedInput() const;     // as of the newest snapshot
    void send(uint16_t channel, const Bytes& payload);
    std::function<void(uint16_t channel, const Bytes& payload)> onMessage;

    // ---- what the server told
    std::vector<ObjectId> objects() const;
    bool exists(ObjectId id) const;
    ClassId classOf(ObjectId id) const;
    uint8_t ownerOf(ObjectId id) const;
    // Value at render time (interpolated for `interpolate` fields, else the newest at or before it).
    Value sample(ObjectId id, int field) const;
    // Newest authoritative value (for the owner's reconciliation).
    const Value& latest(ObjectId id, int field) const;
    uint32_t latestTick() const;           // server tick of the newest snapshot
    // ---- clock (ms on the server's tick clock: tick * 1000 / tickRate)
    double serverTimeMs() const;           // estimated server time now (newest arrival path + RTT / 2)
    double renderTimeMs() const;           // what sample() shows: newest server time - delay, rate-slewed, monotonic
    double renderTick() const;             // renderTimeMs in server ticks (fractional)
    double jitterMs() const;               // snapshot arrival jitter (RFC 3550 style mean deviation)
    double snapshotLoss() const;           // fraction of snapshots that did not arrive (recent, smoothed)
    double interpolationDelayMs() const;   // current delay (fixed, or adaptive)
    uint32_t rttMs() const;                // connection RTT, else measured from input frames to lastAppliedInput
    std::function<void(ObjectId)> onCreate, onDestroy;
    std::function<void(ObjectId, int field)> onReliableChange;   // String / Bytes / reliable fields
    const ClientStats& stats() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------- prediction helper (header only)
// The owner predicts its own object: apply(state, input) is the game's deterministic-enough step (the same
// code the server runs). On each new authoritative state: drop acknowledged inputs, rewind, replay pending.
template <class State, class Input>
class Predictor {
public:
    using Step = std::function<void(State&, const Input&)>;
    explicit Predictor(Step step) : step_(std::move(step)) {}
    void local(uint32_t seq, const Input& in) { pending_.push_back({seq, in}); step_(state_, in); }
    // returns the correction (predicted before - after) magnitude the caller may smooth (net/Smoothing.h)
    template <class Diff>
    auto reconcile(const State& authoritative, uint32_t lastApplied, Diff diff) {
        while (!pending_.empty() && int32_t(pending_.front().seq - lastApplied) <= 0) pending_.pop_front();
        State before = state_;
        state_ = authoritative;
        for (auto& p : pending_) step_(state_, p.in);
        return diff(before, state_);
    }
    const State& state() const { return state_; }
    void reset(const State& s) { state_ = s; pending_.clear(); }
    size_t pending() const { return pending_.size(); }
private:
    struct Frame { uint32_t seq; Input in; };
    Step step_;
    State state_{};
    std::deque<Frame> pending_;
};

} // namespace aiverse::net::replica
