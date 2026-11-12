// Commerce (agent-gta2): the peaceful economy of a city game. rules "commerce" + map "premises" (docs/commerce.md).
//
// Money is an ordinary player resource (commerce.currency). The map lists premises (a shop, an office, a garage: a
// door position, a price, a weekly rent, how busy the spot is). A player rents or buys them, opens a business of a
// type from the rules inside, borrows from the bank against what he owns. The clock is the simulation tick: a game
// day is commerce.dayMinutes of real time; every day each business books its income and running costs, every week
// rent and the loan instalment are charged. Who cannot pay rent is evicted; who misses loan payments for longer
// than the grace period loses property to the bank.
//
// Everything is driven by kCmdCommerce (param = commercePack(op, premise, value)) and validated here, so the UI,
// scripts, bots and the network use the same door. State lives in the CommerceStore world resource: hashed, saved,
// copied by rollback snapshots. Games without the block keep their checksums and save layout.
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

enum class CommerceOp : uint8_t { None, Rent, Leave, Buy, Sell, Open, Close, Borrow, Repay };
// value: business index for Open, whole currency units for Borrow / Repay
inline int64_t commercePack(CommerceOp op, int premise, int64_t value = 0) { return int64_t(op) | int64_t(premise & 0xFFFF) << 8 | value << 24; }

struct BusinessType {
    std::string id, name;
    std::vector<std::string> kinds;      // premises kinds it fits in (empty: any)
    Fixed setup, incomeDay, costDay;     // income is multiplied by the premises' traffic
};

struct CommerceRules {
    bool enabled = false;
    int currency = -1;                   // resource index
    uint32_t dayTicks = 0;
    int weekDays = 7;
    Fixed dayMinutes = Fixed::fromInt(24);
    Fixed range = Fixed::fromInt(3);     // cells between the hero and the door for a deal (0: from anywhere)
    Fixed sellShare = Fixed::fromRatio(8, 10);
    Fixed rateWeek = Fixed::fromRatio(2, 100), unsecured = Fixed::fromInt(2000), collateralShare = Fixed::fromRatio(6, 10);
    int termWeeks = 20, graceWeeks = 2;
    std::vector<BusinessType> businesses;
};

struct Premise {
    std::string id, kind;
    Vec3 at;                             // the door, cells
    Fixed price, rentWeek, traffic = Fixed::fromInt(1);
    uint8_t owner = 0, tenant = 0;       // player slots; 0: the city owns it / nobody rents it
    int16_t business = -1;               // index into rules.businesses, run by the tenant, else by the owner
    Fixed lastNet, total;                // what the business brought yesterday / since it opened
    uint8_t runBy() const { return tenant ? tenant : owner; }
};

struct CommerceAccount {
    Fixed debt;
    uint8_t missed = 0;                  // weekly loan payments missed in a row
    Fixed dayNet, weekNet, lastWeekNet;  // business results, for the UI
    Fixed lastRent, lastLoanPayment;     // what the last weekly settlement charged
};

struct CommerceStore {
    std::vector<Premise> premises;       // map order
    std::vector<CommerceAccount> accounts;   // by player slot
};

class Commerce {
public:
    bool active() const { return rules_.enabled; }
    const CommerceRules& rules() const { return rules_; }
    static CommerceRules parse(const Json& j, const std::vector<std::string>& resourceIds, uint32_t tickRate);   // throws readable errors
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map);
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdCommerce handled
    void tick(Simulation& sim);
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    CommerceStore* store(Simulation& sim) const;

    // The same checks the command runs. nullptr: allowed; otherwise a short reason ("too far", "not enough money"...).
    const char* refusal(Simulation& sim, uint8_t player, CommerceOp op, int premise, int64_t value) const;
    bool act(Simulation& sim, uint8_t player, CommerceOp op, int premise, int64_t value);   // simulation code only
    Fixed cash(Simulation& sim, uint8_t player) const;
    Fixed creditLimit(Simulation& sim, uint8_t player) const;   // the most the bank lets the debt grow to
    int findPremise(Simulation& sim, const std::string& id) const;
    int findBusiness(const std::string& id) const;
    uint32_t day(const Simulation& sim) const;                  // days since the start, 0-based

private:
    void pay(Simulation& sim, uint8_t player, Fixed amount);    // negative: charge; what he lacks becomes debt
    void closeBusiness(Premise& p);
    void endDay(Simulation& sim, CommerceStore& st);
    void endWeek(Simulation& sim, CommerceStore& st);

    RtsModule* m_ = nullptr;
    CommerceRules rules_;
};

} // namespace aiverse::game
