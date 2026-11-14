// Careers (agent-gta2): paths a player follows side by side, each with its own ranks. rules "careers" (docs/careers.md).
//
// A career is a ladder of ranks; a rank is reached at a number of points and brings perks (plain names that
// scripts and other systems ask about: "hotwire", "siren", "bulk_discount"). A player may be on several careers at
// once; points are counted per career. Who earns points and for what is the game's business (scripts call
// career.add): the engine keeps the ladder, the membership and the perks. Open careers are joined by the player's
// own command (kCmdCareer), the others only by a script (a gang takes you in, you do not sign up).
//
// State lives in the CareerStore world resource: hashed, saved, copied by rollback snapshots. Games without the
// block keep their checksums and save layout.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;

struct CareerRank {
    std::string name;
    Fixed points;                        // reached at this many
    std::vector<std::string> perks;      // gained here, kept on higher ranks
};

struct CareerType {
    std::string id, name;
    bool open = false;                   // the player may join and leave by command
    std::string office;                  // kind of premises (rules "commerce") where one signs up; "" anywhere
    std::vector<CareerRank> ranks;       // ascending points, the first at 0
};

struct CareerRules {
    bool enabled = false;
    std::vector<CareerType> careers;     // sorted by id
};

struct CareerProgress {
    bool joined = false;
    Fixed points;
};

struct CareerStore {
    std::vector<std::vector<CareerProgress>> players;   // [player slot][career]
};

enum class CareerOp : uint8_t { None, Join, Leave };
inline int64_t careerPack(CareerOp op, int career) { return int64_t(op) | int64_t(career) << 8; }

class Career {
public:
    bool active() const { return rules_.enabled; }
    const CareerRules& rules() const { return rules_; }
    static CareerRules parse(const Json& j);   // throws readable errors
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson);
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdCareer handled
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    CareerStore* store(Simulation& sim) const;

    int find(const std::string& id) const;
    const CareerProgress* progress(Simulation& sim, uint8_t player, int career) const;
    int rank(Simulation& sim, uint8_t player, int career) const;          // rank index, -1: not on this career
    bool hasPerk(Simulation& sim, uint8_t player, const std::string& perk) const;
    // simulation code only
    bool join(Simulation& sim, uint8_t player, int career);               // false: already in, or unknown
    bool leave(Simulation& sim, uint8_t player, int career);              // the points are lost
    int add(Simulation& sim, uint8_t player, int career, Fixed points);   // ranks gained (negative: lost); 0 when not joined
    // A player's careers as data that outlives the match (a server keeps it with his account): {"police": raw points}.
    // load replaces what the slot has (unknown careers are skipped); clear empties the slot for the next player.
    Json save(Simulation& sim, uint8_t player) const;
    void load(Simulation& sim, uint8_t player, const Json& data);
    void clear(Simulation& sim, uint8_t player);

private:
    int rankOf(const CareerType& c, Fixed points) const;

    RtsModule* m_ = nullptr;
    CareerRules rules_;
};

} // namespace aiverse::game
