// Player commands: the ONLY input to the simulation. In lockstep multiplayer only commands travel
// over the network; replays are just the initial setup + the command stream.
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "../core/ECS.h"
#include "../core/Fixed.h"

namespace aiverse {

enum CommandFlags : uint8_t {
    kCmdQueue = 1 << 0, // shift-queue: append instead of replacing current orders
    kCmdHasArgs = 1 << 1, // `args` present on the wire (set by writeCommand when args is not empty)
    kCmdAtPoint = 1 << 2, // train: warp the unit in at `point` (producer type warpIn) instead of queueing it
};

struct Command {
    uint32_t tick = 0;          // simulation tick on which the command executes
    uint8_t player = 0;         // issuing player slot (1..N; 0 = neutral/system)
    uint8_t flags = 0;
    uint16_t type = 0;          // module-defined command id
    uint32_t seq = 0;           // per-player sequence number (stable ordering inside a tick)
    std::vector<Entity> units;  // selection / source objects
    Entity target = kNullEntity;
    Vec3 point{};
    int64_t param = 0;          // module-defined (percent, ability index, ...)
    std::vector<int64_t> args;  // module-defined payload (script commands); serialized only when not empty

    bool operator==(const Command&) const = default;
};

enum class SeatController : uint8_t { Empty, Human, Computer };

inline SeatController defaultController(uint8_t slot) {
    return slot == 1 ? SeatController::Human : SeatController::Computer;
}

inline const char* controllerName(SeatController controller) {
    switch (controller) {
    case SeatController::Human: return "human";
    case SeatController::Computer: return "computer";
    default: return "empty";
    }
}

// A server-owned command uses player zero. The target seat is separate from its issuer.
inline constexpr uint16_t kSetSeatController = 0x7fff;
inline Command seatControllerCommand(uint8_t slot, SeatController controller, bool initial = false) {
    Command c;
    c.type = kSetSeatController;
    c.param = slot;
    c.args = {int64_t(controller), int64_t(initial)};
    return c;
}

// Binary little-endian (de)serialization shared by replays and network.
class ByteWriter {
public:
    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { for (int i = 0; i < 2; ++i) buf.push_back(uint8_t(v >> (8 * i))); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) buf.push_back(uint8_t(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) buf.push_back(uint8_t(v >> (8 * i))); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void str(const std::string& s) { u32(uint32_t(s.size())); buf.insert(buf.end(), s.begin(), s.end()); }
    std::vector<uint8_t> buf;
};

class ByteReader {
public:
    ByteReader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    bool ok() const { return ok_; }
    bool atEnd() const { return p_ >= n_; }
    uint8_t u8() { return need(1) ? d_[p_++] : 0; }
    uint16_t u16() { uint16_t v = 0; if (need(2)) { for (int i = 0; i < 2; ++i) v |= uint16_t(d_[p_++]) << (8 * i); } return v; }
    uint32_t u32() { uint32_t v = 0; if (need(4)) { for (int i = 0; i < 4; ++i) v |= uint32_t(d_[p_++]) << (8 * i); } return v; }
    uint64_t u64() { uint64_t v = 0; if (need(8)) { for (int i = 0; i < 8; ++i) v |= uint64_t(d_[p_++]) << (8 * i); } return v; }
    int64_t i64() { return int64_t(u64()); }
    std::string str() { uint32_t n = u32(); if (!need(n)) return {}; std::string s(reinterpret_cast<const char*>(d_ + p_), n); p_ += n; return s; }

private:
    bool need(size_t k) { if (p_ + k > n_) { ok_ = false; p_ = n_; return false; } return true; }
    const uint8_t* d_; size_t n_; size_t p_ = 0; bool ok_ = true;
};

void writeCommand(ByteWriter& w, const Command& c);
Command readCommand(ByteReader& r);

} // namespace aiverse
