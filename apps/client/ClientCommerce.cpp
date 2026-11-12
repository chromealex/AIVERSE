// The city economy in the client (rules "commerce", modules/rts/RtsCommerce.h): at a door the top bar names the
// place, E opens the list of deals, the number keys make them (kCmdCommerce). Walking away closes the list.
#include "GameClient.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace aiverse::client {

using platform::Key;
using game::CommerceOp;

namespace {
std::string fmt(const char* f, auto... args) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, args...);
    return buf;
}
long long whole(Fixed v) { return (long long)v.toInt(); }
bool has(const std::vector<std::string>& v, const std::string& s) { return v.empty() || std::find(v.begin(), v.end(), s) != v.end(); }
}

// The premises whose door the hero stands at (the nearest in range), -1: none.
int GameClient::commerceDoor() {
    const game::Commerce* c = sim_ ? mod().commerce() : nullptr;
    if (!c || !c->active() || spectator_ || !mod().topDown() || !mod().topDown()->active()) return -1;
    const Entity hero = mod().topDown()->heroOf(*sim_, human_);
    if (!hero.valid() || !sim_->world().alive(hero)) return -1;
    const Vec3 hp = sim_->world().get<Transform>(hero).pos;
    const Fixed range = c->rules().range.raw > 0 ? c->rules().range : Fixed::fromInt(3);
    const auto& ps = c->store(*sim_)->premises;
    int best = -1;
    Fixed bestD = range;
    for (size_t i = 0; i < ps.size(); ++i) {
        Vec3 d = hp - ps[i].at;
        d.y = Fixed{};
        const Fixed dist = d.length();
        if (dist <= bestD) { bestD = dist; best = int(i); }
    }
    return best;
}

std::vector<GameClient::CommerceChoice> GameClient::commerceChoices(int premise, std::string& title) {
    std::vector<CommerceChoice> out;
    game::Commerce* c = mod().commerce();
    const auto& st = *c->store(*sim_);
    const game::Premise& p = st.premises[size_t(premise)];
    const auto& R = c->rules();
    const game::CommerceAccount& acc = st.accounts[human_];
    const bool mine = p.runBy() == human_;
    title = p.kind;
    if (!title.empty()) title[0] = char(std::toupper(static_cast<unsigned char>(title[0])));
    if (game::Career* cr = mod().career(); cr && cr->active())   // the place where one signs up for a service
        for (size_t k = 0; k < cr->rules().careers.size(); ++k) {
            const game::CareerType& ct = cr->rules().careers[k];
            if (!ct.open || ct.office.empty() || ct.office != p.kind) continue;
            const int r = cr->rank(*sim_, human_, int(k));
            title = ct.name + (r >= 0 ? ": " + ct.ranks[size_t(r)].name : std::string());
            out.push_back({r >= 0 ? "Quit the service" : "Join: " + ct.name, CommerceOp::None, premise, r >= 0 ? 2 : 1, int(k)});
        }
    if (p.isPublic) {
        if (p.kind == "bank") {
            title = fmt("Bank: debt %lld of %lld", whole(acc.debt), whole(c->creditLimit(*sim_, human_)));
            out.push_back({"Borrow 500", CommerceOp::Borrow, premise, 500});
            out.push_back({"Borrow 2000", CommerceOp::Borrow, premise, 2000});
            if (acc.debt.raw > 0) {
                out.push_back({"Repay 500", CommerceOp::Repay, premise, 500});
                out.push_back({fmt("Repay all (%lld)", whole(acc.debt) + 1), CommerceOp::Repay, premise, whole(acc.debt) + 1});
            }
        }
        for (size_t i = 0; i < R.offers.size(); ++i)
            if (has(R.offers[i].kinds, p.kind) && !R.offers[i].kinds.empty())
                out.push_back({fmt("%s: %lld", R.offers[i].name.c_str(), whole(R.offers[i].price)), CommerceOp::Purchase, premise, int64_t(i)});
        return out;
    }
    if (!mine) {
        if (p.owner == 0 && p.tenant == 0) {
            title += fmt(": rent %lld a week, price %lld", whole(p.rentWeek), whole(p.price));
            out.push_back({fmt("Rent (%lld a week)", whole(p.rentWeek)), CommerceOp::Rent, premise, 0});
            out.push_back({fmt("Buy (%lld)", whole(p.price)), CommerceOp::Buy, premise, 0});
        } else title += ": taken";
        return out;
    }
    const bool home = std::find(R.storage.begin(), R.storage.end(), p.kind) != R.storage.end();
    if (home) {
        title = fmt("Home: %lld in the stash", whole(p.stashCash));
        out.push_back({"Put 500 in the stash", CommerceOp::Stash, premise, 500});
        if (p.stashCash.raw > 0) out.push_back({"Take 500 from the stash", CommerceOp::Unstash, premise, 500});
        if (mod().arsenal() && mod().arsenal()->active()) {
            out.push_back({"Put weapons away", CommerceOp::StashWeapons, premise, 0});
            out.push_back({"Take weapons", CommerceOp::TakeWeapons, premise, 0});
        }
    } else if (p.business < 0) {
        title += ": yours, empty";
        for (size_t i = 0; i < R.businesses.size(); ++i)
            if (has(R.businesses[i].kinds, p.kind))
                out.push_back({fmt("Open %s (%lld)", R.businesses[i].name.c_str(), whole(R.businesses[i].setup)), CommerceOp::Open, premise, int64_t(i)});
    } else {
        const game::BusinessType& b = R.businesses[size_t(p.business)];
        title = fmt("%s: %+lld yesterday, staff %d of %d%s", b.name.c_str(), whole(p.lastNet), int(p.staff), b.staff, p.manager ? ", manager" : "");
        if (p.staff < b.staff) out.push_back({fmt("Hire (%lld a week)", whole(b.wageWeek)), CommerceOp::Hire, premise, 0});
        if (p.staff > 0) out.push_back({"Fire one", CommerceOp::Fire, premise, 0});
        out.push_back({p.manager ? std::string("Dismiss the manager") : fmt("Hire a manager (%lld a week)", whole(R.managerWageWeek)), CommerceOp::Manager, premise, 0});
        out.push_back({"Close the business", CommerceOp::Close, premise, 0});
    }
    if (p.tenant == human_) {
        out.push_back({fmt("Buy the place (%lld)", whole(p.price)), CommerceOp::Buy, premise, 0});
        out.push_back({"End the lease", CommerceOp::Leave, premise, 0});
    } else out.push_back({fmt("Sell (%lld)", whole(p.price * R.sellShare)), CommerceOp::Sell, premise, 0});
    return out;
}

// E opens / closes the list at a door; with the list open 1..9 make the deal. true: the key is used up.
bool GameClient::commerceKey(Key key) {
    const int door = commerceDoor();
    if (door < 0) { comMenu_ = false; return false; }
    if (key == Key::E) { comMenu_ = !comMenu_; return true; }
    if (!comMenu_ || int(key) < int(Key::Num1) || int(key) > int(Key::Num9)) return false;
    std::string title;
    const auto choices = commerceChoices(door, title);
    const size_t pick = size_t(int(key) - int(Key::Num1));
    if (pick >= choices.size()) return true;
    const CommerceChoice& ch = choices[pick];
    if (ch.career >= 0) {   // signing up / quitting (rules "careers")
        Command c;
        c.type = game::kCmdCareer;
        c.param = game::careerPack(game::CareerOp(ch.value), ch.career);
        issue(c);
        return true;
    }
    if (const char* why = mod().commerce()->refusal(*sim_, human_, ch.op, ch.premise, ch.value)) { notice(why); return true; }
    Command c;
    c.type = game::kCmdCommerce;
    c.param = game::commercePack(ch.op, ch.premise, ch.value);
    issue(c);
    return true;
}

// Top bar: the debt, the day, and what the door in front of the hero offers.
void GameClient::commerceTop(std::vector<UiItem>& top) {
    game::Commerce* c = sim_ ? mod().commerce() : nullptr;
    if (!c || !c->active() || spectator_) return;
    const auto& st = *c->store(*sim_);
    if (human_ >= st.accounts.size()) return;
    const game::CommerceAccount& acc = st.accounts[human_];
    top.push_back({fmt("Day %u", unsigned(c->day(*sim_) + 1)), "", "", false});
    if (acc.debt.raw > 0) top.push_back({fmt("Debt: %lld", whole(acc.debt)), "", "", acc.missed > 0});
    if (game::Career* cr = mod().career(); cr && cr->active())   // the ranks on the paths the player follows
        for (size_t k = 0; k < cr->rules().careers.size(); ++k)
            if (const int r = cr->rank(*sim_, human_, int(k)); r >= 0)
                top.push_back({cr->rules().careers[k].name + ": " + cr->rules().careers[k].ranks[size_t(r)].name, "", "", false});
    const int door = commerceDoor();
    if (door < 0) { comMenu_ = false; return; }
    std::string title;
    const auto choices = commerceChoices(door, title);
    if (choices.empty()) { top.push_back({title, "", "", false}); return; }
    top.push_back({title + (comMenu_ ? "" : "  [E]"), "", "", false});
    if (!comMenu_) return;
    for (size_t i = 0; i < choices.size() && i < 9; ++i)
        top.push_back({fmt("%d %s", int(i + 1), choices[i].label.c_str()), "", "",
                       choices[i].career < 0 && c->refusal(*sim_, human_, choices[i].op, choices[i].premise, choices[i].value) != nullptr});
}

} // namespace aiverse::client
