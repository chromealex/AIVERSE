#pragma once
// card-games: deterministic card/board game rules (definitions + rule variants as data, zones, shuffled decks,
// draw/mulligan, phases, legal action generation, a LIFO effect stack with priority passing, fizzling,
// per-seat hidden information with a non-leaking viewFor projection and redacted events).
// Composes card-runtime's zone vocabulary (strategy/Cards.h: Deck, Hand, Board, Discard, Exile) and adds Stack.
// Randomness only through the passed-in engine Random (shuffles at setup/mulligan). Commands carry strictly
// increasing sequence numbers; an invalid or repeated command returns nullopt and leaves the state untouched.
#include "../strategy/Cards.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
enum class CardGameZone:uint8_t{Deck,Hand,Board,Discard,Exile,Stack};
static_assert(uint8_t(CardGameZone::Exile)==uint8_t(strategy::CardZone::Exile)&&uint8_t(CardGameZone::Hand)==uint8_t(strategy::CardZone::Hand));
// Zone in card-runtime terms (Stack has no card-runtime equivalent and maps to nullopt).
inline std::optional<strategy::CardZone> cardRuntimeZone(CardGameZone z){if(z==CardGameZone::Stack)return std::nullopt;return strategy::CardZone(uint8_t(z));}

inline constexpr int32_t kCardMaxSeats=4,kCardValueLimit=1000000,kCardNoWinner=-1,kCardDraw=-2;
inline constexpr std::size_t kCardMaxDefs=1024,kCardMaxEffects=8,kCardMaxDeck=200,kCardMaxStack=32,kCardMaxHand=20,kCardMaxBoard=12,kCardMaxActions=4096;
inline constexpr uint32_t kCardMaxMulliganEnumerated=10; // partial mulligan masks are enumerated up to this hand size
inline constexpr uint8_t kCardPublic=0xFF; // visibility mask: every seat and spectators

enum class CardKind:uint8_t{Unit,Spell,Instant};
enum class CardTarget:uint8_t{None,Player,Unit,Character,StackItem};
enum class CardSide:uint8_t{Any,Enemy,Friendly};
enum class CardEffectOp:uint8_t{Damage,Heal,Draw,Energy,Buff,Counter,Reveal,Mill};
enum class CardEffectTo:uint8_t{Target,Controller,Opponents,EnemyUnits,FriendlyUnits};
struct CardEffectDef{CardEffectOp op=CardEffectOp::Damage;int32_t amount=0;CardEffectTo to=CardEffectTo::Target;bool operator==(const CardEffectDef&)const=default;};
struct CardDef{
    int64_t id=0;std::string name;            // name: localization key
    CardKind kind=CardKind::Spell;int32_t cost=0,power=0,health=0;
    CardTarget target=CardTarget::None;CardSide side=CardSide::Any;
    bool guard=false,rush=false;              // guard: attackers of this seat must hit guard units first; rush: attacks the turn it enters
    std::vector<CardEffectDef> effects;
    bool operator==(const CardDef&)const=default;
};
enum class CardMulligan:uint8_t{None,Partial,Full};
// Rule variants as data. Turn numbers count player turns from 1.
struct CardRules{
    int32_t seats=2,startingHand=4,maxHand=10,startingHealth=20,energyStart=1,energyGain=1,energyMax=10,drawPerTurn=1,boardLimit=6,turnLimit=60,mulliganPenalty=0;
    int32_t deckMin=1,deckMax=60;
    bool firstPlayerSkipsDraw=true,fatigue=true,separateCombat=false,unitsAttackImmediately=false;
    CardMulligan mulligan=CardMulligan::Partial;
    bool operator==(const CardRules&)const=default;
};
struct CardLibrary{CardRules rules;std::map<int64_t,CardDef> cards;bool operator==(const CardLibrary&)const=default;};

namespace detail_cardgame {
inline bool targetsWith(const CardDef& d){for(const auto& e:d.effects)if(e.to==CardEffectTo::Target)return true;return false;}
inline bool unitTarget(CardTarget t){return t==CardTarget::Unit||t==CardTarget::Character;}
inline bool playerTarget(CardTarget t){return t==CardTarget::Player||t==CardTarget::Character;}
}
// Empty when the library is usable; messages are "cards.<path>: ..." like the JSON loader.
inline std::vector<std::string> cardLibraryErrors(const CardLibrary& lib){
    std::vector<std::string> out;const auto& r=lib.rules;
    auto range=[&](int32_t v,int32_t lo,int32_t hi,const char* name){if(v<lo||v>hi)out.push_back(std::string("cards.rules.")+name+": must be in "+std::to_string(lo)+".."+std::to_string(hi));};
    range(r.seats,2,kCardMaxSeats,"seats");range(r.maxHand,1,int32_t(kCardMaxHand),"maxHand");range(r.startingHand,0,r.maxHand,"startingHand");
    range(r.startingHealth,1,kCardValueLimit,"startingHealth");range(r.energyMax,0,1000,"energyMax");range(r.energyStart,0,r.energyMax,"energyStart");range(r.energyGain,0,1000,"energyGain");
    range(r.drawPerTurn,0,10,"drawPerTurn");range(r.boardLimit,1,int32_t(kCardMaxBoard),"boardLimit");range(r.turnLimit,0,100000,"turnLimit");range(r.mulliganPenalty,0,r.startingHand,"mulliganPenalty");
    range(r.deckMin,0,int32_t(kCardMaxDeck),"deckMin");range(r.deckMax,r.deckMin,int32_t(kCardMaxDeck),"deckMax");
    if(uint8_t(r.mulligan)>uint8_t(CardMulligan::Full))out.push_back("cards.rules.mulligan: unknown variant");
    if(lib.cards.empty()||lib.cards.size()>kCardMaxDefs)out.push_back("cards.cards: expected 1.."+std::to_string(kCardMaxDefs)+" definitions");
    for(const auto& [id,d]:lib.cards){
        auto p="cards.card "+std::to_string(id);
        if(id<=0||d.id!=id){out.push_back(p+".id: must be positive and match its key");continue;}
        if(uint8_t(d.kind)>uint8_t(CardKind::Instant)||uint8_t(d.target)>uint8_t(CardTarget::StackItem)||uint8_t(d.side)>uint8_t(CardSide::Friendly))out.push_back(p+": unknown kind/target/side");
        if(d.cost<0||d.cost>1000)out.push_back(p+".cost: must be in 0..1000");
        if(d.kind==CardKind::Unit){
            if(d.power<0||d.power>kCardValueLimit||d.health<1||d.health>kCardValueLimit)out.push_back(p+": units need power 0.. and health 1..1000000");
            if(d.target!=CardTarget::None)out.push_back(p+".target: units enter play without a target");
        }else if(d.power||d.health||d.guard||d.rush)out.push_back(p+": power/health/guard/rush are only for units");
        if(d.effects.size()>kCardMaxEffects)out.push_back(p+".effects: at most "+std::to_string(kCardMaxEffects));
        if(d.target!=CardTarget::None&&!detail_cardgame::targetsWith(d))out.push_back(p+".target: no effect uses the chosen target");
        for(std::size_t i=0;i<d.effects.size();++i){
            const auto& e=d.effects[i];auto ep=p+".effects["+std::to_string(i)+"]";
            if(uint8_t(e.op)>uint8_t(CardEffectOp::Mill)||uint8_t(e.to)>uint8_t(CardEffectTo::FriendlyUnits)){out.push_back(ep+": unknown op/to");continue;}
            if(e.amount<0||e.amount>1000)out.push_back(ep+".amount: must be in 0..1000");
            bool unitsTo=e.to==CardEffectTo::EnemyUnits||e.to==CardEffectTo::FriendlyUnits,target=e.to==CardEffectTo::Target;
            if(target&&d.target==CardTarget::None)out.push_back(ep+".to: card has no target");
            switch(e.op){
            case CardEffectOp::Counter:if(!target||d.target!=CardTarget::StackItem)out.push_back(ep+": counter needs a stackItem target");break;
            case CardEffectOp::Buff:if(!(unitsTo||(target&&d.target==CardTarget::Unit)))out.push_back(ep+": buff needs units");break;
            case CardEffectOp::Damage:case CardEffectOp::Heal:if(target&&d.target==CardTarget::StackItem)out.push_back(ep+": cannot damage/heal a stack item");break;
            case CardEffectOp::Draw:case CardEffectOp::Energy:case CardEffectOp::Reveal:case CardEffectOp::Mill:
                if(unitsTo||(target&&d.target!=CardTarget::Player))out.push_back(ep+": player effect needs controller, opponents or a player target");
                break;
            }
            if(e.op!=CardEffectOp::Counter&&target&&d.target==CardTarget::StackItem)out.push_back(ep+": only counter uses a stackItem target");
        }
    }
    return out;
}
inline bool valid(const CardLibrary& lib){return cardLibraryErrors(lib).empty();}

// ---- State
enum class CardPhase:uint8_t{Mulligan,Main,Combat,GameOver};
enum class CardStackKind:uint8_t{Card,Attack};
enum class CardTargetType:uint8_t{None,Player,Unit,Stack};
struct CardTargetRef{CardTargetType type=CardTargetType::None;uint32_t value=0;bool operator==(const CardTargetRef&)const=default;auto operator<=>(const CardTargetRef&)const=default;}; // seat / card id / stack id
struct CardGameCard{
    uint32_t id=0;int64_t def=0;int32_t owner=0;CardGameZone zone=CardGameZone::Deck;
    uint8_t known=0;             // seats that may see this card's identity while it is in a hidden zone (deck/hand)
    int32_t damage=0,bonus=0;    // board units: damage taken, +power/+health buffs
    bool exhausted=false;
    bool operator==(const CardGameCard&)const=default;
};
struct CardGameSeat{
    int32_t health=0,energy=0,maxEnergy=0,fatigue=0,turnsTaken=0;bool eliminated=false,mulliganed=false;
    std::vector<uint32_t> deck,hand,board,discard,exile; // deck back = top
    bool operator==(const CardGameSeat&)const=default;
};
struct CardStackItem{uint32_t id=0;CardStackKind kind=CardStackKind::Card;uint32_t source=0;int32_t controller=0;CardTargetRef target;bool operator==(const CardStackItem&)const=default;};
struct CardGameState{
    std::vector<CardGameSeat> seats;std::map<uint32_t,CardGameCard> cards;std::vector<CardStackItem> stack;
    CardPhase phase=CardPhase::Mulligan;uint32_t turn=0,nextStackId=1;int32_t active=0,priority=0,passes=0,winner=kCardNoWinner;
    uint64_t lastCommand=0;
    bool operator==(const CardGameState&)const=default;
};
enum class CardActionKind:uint8_t{Pass,Mulligan,Play,Attack};
struct CardAction{CardActionKind kind=CardActionKind::Pass;uint32_t card=0;CardTargetRef target;uint32_t replaceMask=0;bool operator==(const CardAction&)const=default;auto operator<=>(const CardAction&)const=default;};
struct CardCommand{uint64_t seq=0;int32_t player=0;CardAction action;};
enum class CardEventKind:uint8_t{Shuffled,Drew,Burned,Fatigue,Mulligan,TurnStarted,PhaseChanged,PriorityPassed,Played,Attacked,Resolved,Fizzled,Countered,
    Damaged,Healed,Buffed,EnergyGained,Revealed,Milled,UnitDied,PlayerEliminated,GameOver};
// card/def are visible only to seats in `visible` (kCardPublic = everybody); use cardEventsFor before sending.
struct CardEvent{CardEventKind kind=CardEventKind::Shuffled;int32_t player=-1;uint32_t card=0;int64_t def=0;CardTargetRef target;int32_t value=0;uint8_t visible=kCardPublic;bool operator==(const CardEvent&)const=default;};
struct CardStepReport{std::vector<CardEvent> events;};

namespace detail_cardgame {
inline uint8_t bit(int32_t seat){return uint8_t(1u<<uint32_t(seat));}
inline bool canSee(uint8_t mask,int32_t viewer){return mask==kCardPublic||(viewer>=0&&(mask>>uint32_t(viewer))&1u);}
inline std::vector<uint32_t>& zoneList(CardGameSeat& s,CardGameZone z){
    switch(z){case CardGameZone::Deck:return s.deck;case CardGameZone::Hand:return s.hand;case CardGameZone::Board:return s.board;case CardGameZone::Discard:return s.discard;default:return s.exile;}
}
inline const std::vector<uint32_t>* zoneList(const CardGameSeat& s,CardGameZone z){
    switch(z){case CardGameZone::Deck:return &s.deck;case CardGameZone::Hand:return &s.hand;case CardGameZone::Board:return &s.board;case CardGameZone::Discard:return &s.discard;case CardGameZone::Exile:return &s.exile;default:return nullptr;}
}
inline bool hiddenZone(CardGameZone z){return z==CardGameZone::Deck||z==CardGameZone::Hand;}
}
// Structural validation (limits, every card in exactly one place, stack references, phase/seat ranges).
inline bool valid(const CardGameState& s){
    using namespace detail_cardgame;
    auto n=int32_t(s.seats.size());
    if(n<2||n>kCardMaxSeats||s.cards.size()>std::size_t(n)*kCardMaxDeck||s.stack.size()>kCardMaxStack||uint8_t(s.phase)>uint8_t(CardPhase::GameOver))return false;
    if(s.active<0||s.active>=n||s.priority<0||s.priority>=n||s.passes<0||s.passes>=n||s.winner<kCardDraw||s.winner>=n||!s.nextStackId)return false;
    if((s.phase==CardPhase::GameOver)!=(s.winner!=kCardNoWinner))return false;
    std::map<uint32_t,int> placed;
    for(int32_t i=0;i<n;++i){
        const auto& p=s.seats[std::size_t(i)];
        if(p.health<-kCardValueLimit||p.health>kCardValueLimit||p.energy<0||p.energy>kCardValueLimit||p.maxEnergy<0||p.maxEnergy>1000||p.fatigue<0||p.fatigue>kCardValueLimit||p.turnsTaken<0)return false;
        if(p.hand.size()>kCardMaxHand||p.board.size()>kCardMaxBoard)return false;
        for(auto z:{CardGameZone::Deck,CardGameZone::Hand,CardGameZone::Board,CardGameZone::Discard,CardGameZone::Exile})
            for(auto id:*zoneList(p,z)){auto it=s.cards.find(id);if(it==s.cards.end()||it->second.owner!=i||it->second.zone!=z||placed[id]++)return false;}
    }
    uint32_t prior=0;
    for(const auto& f:s.stack){
        if(f.id<=prior||f.id>=s.nextStackId||f.controller<0||f.controller>=n||uint8_t(f.kind)>uint8_t(CardStackKind::Attack)||uint8_t(f.target.type)>uint8_t(CardTargetType::Stack))return false;
        prior=f.id;auto it=s.cards.find(f.source);if(it==s.cards.end()||it->second.owner!=f.controller)return false;
        if(f.kind==CardStackKind::Card){if(it->second.zone!=CardGameZone::Stack||placed[f.source]++)return false;}
        else if(it->second.zone!=CardGameZone::Board)return false;
    }
    if(placed.size()!=s.cards.size())return false;
    for(const auto& [id,c]:s.cards){
        if(!id||c.id!=id||c.def<=0||c.owner<0||c.owner>=n||uint8_t(c.zone)>uint8_t(CardGameZone::Stack)||placed[id]!=1)return false;
        if(c.damage<0||c.damage>kCardValueLimit||c.bonus<0||c.bonus>kCardValueLimit)return false;
    }
    return true;
}
inline bool cardStateCompatible(const CardLibrary& lib,const CardGameState& s){
    if(!valid(lib)||!valid(s)||int32_t(s.seats.size())!=lib.rules.seats)return false;
    for(const auto& [id,c]:s.cards){(void)id;if(!lib.cards.contains(c.def))return false;}
    return true;
}

// ---- Views: what one seat (or a spectator, viewer -1) may know. Hidden cards appear only as counts.
struct CardViewCard{uint32_t id=0;int64_t def=0;int32_t owner=0,power=0,health=0;bool exhausted=false;bool operator==(const CardViewCard&)const=default;};
struct CardSeatView{
    int32_t health=0,energy=0,maxEnergy=0,fatigue=0;bool eliminated=false,mulliganed=false;uint32_t deckCount=0,handCount=0;
    std::vector<CardViewCard> hand,board,discard,exile; // hand: only cards this viewer may see
    bool operator==(const CardSeatView&)const=default;
};
struct CardStackView{uint32_t id=0;CardStackKind kind=CardStackKind::Card;CardViewCard source;int32_t controller=0;CardTargetRef target;bool operator==(const CardStackView&)const=default;};
struct CardGameView{
    int32_t viewer=-1;CardPhase phase=CardPhase::Mulligan;uint32_t turn=0;int32_t active=0,priority=0,passes=0,winner=kCardNoWinner;
    std::vector<CardSeatView> seats;std::vector<CardStackView> stack;
    bool operator==(const CardGameView&)const=default;
};
inline int32_t cardPower(const CardLibrary& lib,const CardGameCard& c){return lib.cards.at(c.def).power+c.bonus;}
inline int32_t cardHealth(const CardLibrary& lib,const CardGameCard& c){return lib.cards.at(c.def).health+c.bonus-c.damage;}
inline std::optional<CardGameView> cardViewFor(const CardLibrary& lib,const CardGameState& s,int32_t viewer){
    using namespace detail_cardgame;
    if(!cardStateCompatible(lib,s)||viewer< -1||viewer>=int32_t(s.seats.size()))return std::nullopt;
    CardGameView v;v.viewer=viewer;v.phase=s.phase;v.turn=s.turn;v.active=s.active;v.priority=s.priority;v.passes=s.passes;v.winner=s.winner;
    auto card=[&](uint32_t id){const auto& c=s.cards.at(id);bool unit=lib.cards.at(c.def).kind==CardKind::Unit&&c.zone==CardGameZone::Board;
        return CardViewCard{c.id,c.def,c.owner,unit?cardPower(lib,c):0,unit?cardHealth(lib,c):0,c.exhausted};};
    for(int32_t i=0;i<int32_t(s.seats.size());++i){
        const auto& p=s.seats[std::size_t(i)];CardSeatView sv;
        sv.health=p.health;sv.energy=p.energy;sv.maxEnergy=p.maxEnergy;sv.fatigue=p.fatigue;sv.eliminated=p.eliminated;sv.mulliganed=p.mulliganed;
        sv.deckCount=uint32_t(p.deck.size());sv.handCount=uint32_t(p.hand.size());
        for(auto id:p.hand)if(i==viewer||canSee(s.cards.at(id).known,viewer))sv.hand.push_back(card(id));
        for(auto id:p.board)sv.board.push_back(card(id));
        for(auto id:p.discard)sv.discard.push_back(card(id));
        for(auto id:p.exile)sv.exile.push_back(card(id));
        v.seats.push_back(std::move(sv));
    }
    for(const auto& f:s.stack)v.stack.push_back({f.id,f.kind,card(f.source),f.controller,f.target});
    return v;
}
// Events with card identities removed for seats that may not see them.
inline std::vector<CardEvent> cardEventsFor(const std::vector<CardEvent>& events,int32_t viewer){
    std::vector<CardEvent> out;out.reserve(events.size());
    for(auto e:events){if(!detail_cardgame::canSee(e.visible,viewer)){e.card=0;e.def=0;if(e.target.type==CardTargetType::Unit)e.target={};}out.push_back(e);}
    return out;
}

// ---- Legal actions, computed from a seat's own view only (so they can never depend on hidden cards).
namespace detail_cardgame {
inline bool sideOk(CardSide side,int32_t controller,int32_t seat){return side==CardSide::Any||(side==CardSide::Enemy)==(seat!=controller);}
inline void cardTargets(const CardDef& d,const CardGameView& v,int32_t me,std::vector<CardTargetRef>& out){
    for(int32_t i=0;i<int32_t(v.seats.size());++i){
        const auto& sv=v.seats[std::size_t(i)];if(sv.eliminated||!sideOk(d.side,me,i))continue;
        if(playerTarget(d.target))out.push_back({CardTargetType::Player,uint32_t(i)});
        if(unitTarget(d.target))for(const auto& u:sv.board)out.push_back({CardTargetType::Unit,u.id});
    }
    if(d.target==CardTarget::StackItem)for(const auto& f:v.stack)if(sideOk(d.side,me,f.controller))out.push_back({CardTargetType::Stack,f.id});
}
inline bool hasGuard(const CardLibrary& lib,const CardSeatView& sv){for(const auto& u:sv.board)if(lib.cards.at(u.def).guard)return true;return false;}
}
inline std::vector<CardAction> cardLegalActions(const CardLibrary& lib,const CardGameView& v){
    using namespace detail_cardgame;
    std::vector<CardAction> out;const auto me=v.viewer;
    if(me<0||me>=int32_t(v.seats.size())||v.phase==CardPhase::GameOver||v.priority!=me)return out;
    const auto& self=v.seats[std::size_t(me)];if(self.eliminated)return out;
    if(v.phase==CardPhase::Mulligan){
        if(self.mulliganed)return out;
        auto n=uint32_t(self.hand.size());
        if(lib.rules.mulligan==CardMulligan::Full){out.push_back({CardActionKind::Mulligan,0,{},0});if(n)out.push_back({CardActionKind::Mulligan,0,{},(1u<<n)-1u});}
        else if(n<=kCardMaxMulliganEnumerated)for(uint32_t m=0;m<(1u<<n);++m)out.push_back({CardActionKind::Mulligan,0,{},m});
        else{out.push_back({CardActionKind::Mulligan,0,{},0});out.push_back({CardActionKind::Mulligan,0,{},(1u<<n)-1u});} // keep or replace all
        return out;
    }
    out.push_back({});
    const bool mine=v.active==me,empty=v.stack.empty();
    const bool sorcery=mine&&empty&&v.phase==CardPhase::Main;
    for(const auto& c:self.hand){
        const auto& d=lib.cards.at(c.def);
        if(d.cost>self.energy||v.stack.size()>=kCardMaxStack)continue;
        if(d.kind!=CardKind::Instant&&!sorcery)continue;
        if(d.kind==CardKind::Unit&&int32_t(self.board.size())>=lib.rules.boardLimit)continue;
        if(d.target==CardTarget::None){out.push_back({CardActionKind::Play,c.id,{},0});continue;}
        std::vector<CardTargetRef> targets;cardTargets(d,v,me,targets);
        for(auto t:targets)out.push_back({CardActionKind::Play,c.id,t,0});
    }
    const bool combat=mine&&empty&&(lib.rules.separateCombat?v.phase==CardPhase::Combat:v.phase==CardPhase::Main);
    if(combat&&v.stack.size()<kCardMaxStack)for(const auto& u:self.board){
        if(u.exhausted||u.power<=0)continue;
        for(int32_t i=0;i<int32_t(v.seats.size());++i){
            const auto& sv=v.seats[std::size_t(i)];if(i==me||sv.eliminated)continue;
            bool guard=hasGuard(lib,sv);
            if(!guard)out.push_back({CardActionKind::Attack,u.id,{CardTargetType::Player,uint32_t(i)},0});
            for(const auto& t:sv.board)if(!guard||lib.cards.at(t.def).guard)out.push_back({CardActionKind::Attack,u.id,{CardTargetType::Unit,t.id},0});
        }
    }
    std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());
    if(out.size()>kCardMaxActions)out.resize(kCardMaxActions);
    return out;
}
inline std::vector<CardAction> cardLegalActions(const CardLibrary& lib,const CardGameState& s,int32_t player){
    auto v=cardViewFor(lib,s,player);if(!v)return {};return cardLegalActions(lib,*v);
}
}
