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

enum class CommerceOp : uint8_t { None, Rent, Leave, Buy, Sell, Open, Close, Borrow, Repay, Hire, Fire, Manager, Purchase, Stash, Unstash, StashWeapons, TakeWeapons };
// value: business index for Open, whole currency units for Borrow / Repay, offer index for Purchase, currency units for Stash / Unstash
inline int64_t commercePack(CommerceOp op, int premise, int64_t value = 0) { return int64_t(op) | int64_t(premise & 0xFFFF) << 8 | value << 24; }

struct BusinessType {
    std::string id, name;
    std::vector<std::string> kinds;      // premises kinds it fits in (empty: any)
    Fixed setup, incomeDay, costDay;     // income is multiplied by the premises' traffic
    int staff = 0;                       // employees it needs to run at full income (0: the owner alone)
    Fixed wageWeek;                      // per employee
    Fixed ticket;                        // what one walk-in customer pays (default: a twentieth of incomeDay)
};

// Something sold over the counter of a kind of premises: a weapon with ammo (rules "arsenal") or a vehicle.
struct CommerceOffer {
    std::string id, name;
    std::vector<std::string> kinds;
    Fixed price;
    std::string weapon;                  // arsenal weapon id ("" none)
    int ammo = 0;
    int vehicle = -1;                    // unit type index (-1 none)
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
    Fixed managerWageWeek = Fixed::fromInt(400);
    Fixed unmanagedShare = Fixed::fromRatio(1, 2);   // income share of a business with no manager on a day its owner did not come by
    bool externalAccounts = false;       // "accounts": "external": slots start unbound, a server binds them (Commerce::bind)
    int absentDays = 0;                  // an account away for this many game days is sold out to the city (0: never)
    std::vector<std::string> storage{"flat"};        // kinds of premises with a private stash
    std::vector<BusinessType> businesses;
    std::vector<CommerceOffer> offers;
};

struct Premise {
    std::string id, kind;
    Vec3 at;                             // the door, cells
    Fixed price, rentWeek, traffic = Fixed::fromInt(1);
    uint16_t owner = 0, tenant = 0;      // account records (CommerceStore::accounts); 0: the city owns it / nobody rents it
    int16_t business = -1;               // index into rules.businesses, run by the tenant, else by the owner
    Fixed lastNet, total;                // what the business brought yesterday / since it opened
    bool isPublic = false;               // run by the city (a gun shop, a car dealer): not for rent or sale
    uint8_t staff = 0;                   // employees hired
    Fixed attendance = Fixed::fromInt(1);// share of today's working hours the staff was really there (the game sets it; 1 by default)
    bool manager = false;
    bool visited = false;                // the one who runs it came by today
    Fixed served;                        // paid by walk-in customers today
    // The stash of a home (kinds in commerce.storage): only the one who lives there reaches it, death does not touch it.
    Fixed stashCash;
    std::vector<int32_t> stashAmmo;      // per arsenal slot
    bool stashEmpty() const { for (int32_t n : stashAmmo) if (n > 0) return false; return stashCash.raw <= 0; }
    uint16_t runBy() const { return tenant ? tenant : owner; }
};

// An account is whoever owns things: it outlives the player's seat. While its player is in the match it is bound
// to his slot and the money is his resource stock; while he is away the account keeps its property, its
// businesses go on and the money sits in `wallet`.
struct CommerceAccount {
    uint64_t id = 0;                     // outside identity (a meta account); by default the slot number
    uint8_t slot = 0;                    // the slot it is bound to, 0: its player is away
    Fixed wallet;                        // the money while away
    uint32_t lastSeenDay = 0;
    Fixed debt;
    uint8_t missed = 0;                  // weekly loan payments missed in a row
    Fixed dayNet, weekNet, lastWeekNet;  // business results, for the UI
    Fixed lastRent, lastLoanPayment, lastWages;   // what the last weekly settlement charged
    std::vector<Entity> cars;            // vehicles bought at a dealer
};

struct CommerceStore {
    std::vector<Premise> premises;       // map order
    std::vector<CommerceAccount> accounts;   // records; [0] is the city. By default record i belongs to slot i
    std::vector<uint16_t> slotRec;           // slot -> record (0: nobody is bound to the slot)
};

class Commerce {
public:
    bool active() const { return rules_.enabled; }
    const CommerceRules& rules() const { return rules_; }
    static CommerceRules parse(const Json& j, const std::vector<std::string>& resourceIds, uint32_t tickRate,
                               const std::vector<std::string>& typeIds = {});   // throws readable errors
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map);
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdCommerce handled
    void tick(Simulation& sim);
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    CommerceStore* store(Simulation& sim) const;

    // The same checks the command runs. nullptr: allowed; otherwise a short reason ("too far", "not enough money"...).
    const char* refusal(Simulation& sim, uint8_t player, CommerceOp op, int premise, int64_t value) const;
    bool act(Simulation& sim, uint8_t player, CommerceOp op, int premise, int64_t value);   // simulation code only
    // A fine (caught by the police, a hospital bill): taken from the pocket, what is missing becomes bank debt.
    void fine(Simulation& sim, uint8_t player, Fixed amount);   // simulation code only
    Fixed cash(Simulation& sim, uint8_t player) const;
    // Accounts and seats. bind: the account `id` (created when new) takes the slot: the money in its wallet becomes
    // the slot's stock. false: the id is 0, the slot does not exist, or that account sits in another slot. unbind:
    // the player leaves, the stock goes to the wallet, the property stays his. sellOut: everything the account owns
    // or rents goes back to the city, the proceeds and the stash money to its wallet / stock, the debt is paid from
    // them as far as they reach. Simulation code only (a server, a script).
    bool bind(Simulation& sim, uint8_t slot, uint64_t id);
    void unbind(Simulation& sim, uint8_t slot);
    bool sellOut(Simulation& sim, uint64_t id);
    const CommerceAccount* account(Simulation& sim, uint8_t player) const;     // the record bound to the slot, or null
    const CommerceAccount* accountById(Simulation& sim, uint64_t id) const;
    uint8_t slotOf(Simulation& sim, uint16_t record) const;                    // 0: the city, or its player is away
    Fixed creditLimit(Simulation& sim, uint8_t player) const;   // the most the bank lets the debt grow to
    int findPremise(Simulation& sim, const std::string& id) const;
    int findBusiness(const std::string& id) const;
    int findOffer(const std::string& id) const;
    // A walk-in customer pays the ticket of the business in these premises (the crowd near the player). What real
    // customers paid during a day is taken off the day's statistical income, so both add up to the same total;
    // a busy day can exceed it by half. false: no business there, or the day's cap is reached.
    bool visit(Simulation& sim, int premise);
    Fixed expectedToday(const Premise& p) const;                // income before costs with today's staff and supervision
    uint32_t day(const Simulation& sim) const;                  // days since the start, 0-based

private:
    void pay(Simulation& sim, uint16_t rec, Fixed amount);      // negative: charge; what he lacks becomes debt
    Fixed cashOf(Simulation& sim, uint16_t rec) const;
    Fixed limitOf(const CommerceStore& st, uint16_t rec) const;
    uint16_t recOf(const CommerceStore& st, uint8_t slot) const { return slot < st.slotRec.size() ? st.slotRec[slot] : uint16_t(0); }
    void liquidate(Simulation& sim, CommerceStore& st, uint16_t rec);
    void closeBusiness(Premise& p);
    void vacate(Simulation& sim, Premise& p, uint16_t who);       // thrown out: stashed money goes back to the pocket, the rest is lost
    void endDay(Simulation& sim, CommerceStore& st);
    void endWeek(Simulation& sim, CommerceStore& st);

    RtsModule* m_ = nullptr;
    CommerceRules rules_;
};

} // namespace aiverse::game
