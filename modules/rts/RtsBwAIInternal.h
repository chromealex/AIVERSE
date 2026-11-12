// Internals of the Bunker Wars computer player (RtsBwAI*.cpp): the per-update context and the functions each file
// implements. Not included outside these files.
#pragma once
#include "RtsBwAI.h"
#include <algorithm>

namespace aiverse::game {

constexpr Fixed kBwInf = Fixed::fromInt(int64_t(1) << 30);
constexpr int kBwRowUnits = 6;    // AI6Shared.maxUnitsInOneRow

struct BwAI::Ctx {
    Ctx(Simulation& s, RtsModule& mod, BwAI& bot, const BwAIConfig& c, const BwProfile& p, Fixed d)
        : sim(s), m(mod), R(mod.rules()), ai(bot), cfg(c), prof(p), dt(d) {}
    Simulation& sim;
    RtsModule& m;
    const Rules& R;
    BwAI& ai;
    const BwAIConfig& cfg;
    const BwProfile& prof;
    Fixed dt{};
    BwWorld* w = nullptr;            // built lazily by world()
    BwWorld store;
    bool built = false;
    BwWorld& world();
    uint8_t me() const { return ai.player_; }
    // ---- relations and routes (AIBase / AI6Shared)
    bool neutral(uint8_t p) const { return p == 0; }
    bool ally(uint8_t a, uint8_t b);                               // InAlliance
    bool enemy(uint8_t a, uint8_t b) { return !ally(a, b) && !neutral(a) && !neutral(b); }
    Fixed dist(int i, int j);                                      // route AI length, < 0: no route
    bool route(int i, int j) { return i != j && dist(i, j).raw >= 0; }
    Fixed px(int pixels) const { return Fixed::fromInt(pixels) / cfg.pxPerCell; }
    int towerKillsOnRoute(int from, int to, int count, uint8_t owner, std::vector<std::pair<int, int>>* byTower = nullptr);
    int roadsInTowerRange(int tower);
    Fixed rowTime(uint8_t player) { (void)player; return Fixed::fromInt(kBwRowUnits) / world().releaseRate; }
    // reducers over points (AI6Shared.Get{Min,Max,Avg}DistanceTo{Own,Enemy}Points)
    Fixed minDistOwn(int p, uint8_t player), maxDistOwn(int p, uint8_t player), avgDistOwn(int p, uint8_t player);
    Fixed minDistEnemy(int p, uint8_t player), maxDistEnemy(int p, uint8_t player), avgDistEnemy(int p, uint8_t player);
    Fixed minDistEnemyFromOwn(uint8_t player);
    int countRole(int player, BwRole r);                           // player < 0: everyone
    int objectiveCount(bool dom, int player);                      // player: -1 any, else owner
    bool anyUpgrading(uint8_t player);
    Fixed kothScore(uint8_t player);
    Fixed kothMax();
    bool blizzard();
    // commands
    void send(int from, int to, int units);                        // AI6Shared.InvokeSend (exact count)
    void sendPercent(int from, int to, Fixed percent);             // PointInfo.Go(target, percent)
    void upgrade(int point, int type);                             // kCmdUpgrade into a type
    bool cast(int ability, Vec3 point, Entity target = kNullEntity);
    uint32_t submit(Command c);
};

BwRole bwRoleOf(const Rules& R, const BwAIConfig& cfg, uint16_t type);
bool bwCompare(BwCmp c, Fixed a, Fixed b);

struct BwImpl {
    using Ctx = BwAI::Ctx;
    // RtsBwAIWorld.cpp
    static void buildWorld(Ctx& c, BwWorld& w);
    static void refreshRoutes(Ctx& c);
    // RtsBwAI.cpp (AIBase, AI6MapState, AI6 ranker / worker / modes / custom)
    static void updateMapState(Ctx& c);
    static void ai6(Ctx& c);
    static void ai6Worker(Ctx& c);                                 // deferred sends (every tick)
    static void checkDefeat(Ctx& c);
    static void actionDetector(Ctx& c);
    static Fixed calcTimeout(Ctx& c);
    // RtsBwAIBots.cpp
    static void ashley(Ctx& c);
    static void bishop(Ctx& c);
    static int needCount(Ctx& c, int source, int target);          // AIBase.GetNeedCount
    // RtsBwAISkills.cpp
    static void initCasters(Ctx& c);
    static void skills(Ctx& c);
    static void prepareToSend(Ctx& c, int from, int to, int count);
    // RtsBwAIScript.cpp
    static void initDebut(Ctx& c);
    static bool debut(Ctx& c);                                     // true: the debut played this tick
    static void triggers(Ctx& c);
    static void resetBot(Ctx& c);                                  // AIBase.Reset
};

} // namespace aiverse::game
