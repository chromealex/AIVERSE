#pragma once
// Card evaluator bot for card-games. It sees only a seat's CardGameView (cardViewFor): legal actions come from that
// view, and lookahead runs on a state rebuilt from the view (cardStateFromView: unknown hands and decks are absent),
// so hidden cards can never influence a choice. Each candidate is simulated with the real rules (other seats are
// assumed to pass until the stack is empty) and scored on health, board and hand; a bounded second ply tries
// follow-up actions. Deterministic: no RNG is consumed (simulations use a private fixed-seed Random), ties keep the
// earlier action in legal order (Pass first). Returns the same CardAction a player submits.
#include "../genre/CardGameRunner.h"
#include "../gameplay/Configuration.h"
#include <optional>
namespace aiverse::lib::bots {
struct CardBotConfig{
    uint32_t nodeBudget=256,depth=2;                     // simulated actions per decision; plies (1..4)
    int32_t healthWeight=10,powerWeight=6,toughnessWeight=4,handWeight=3;
    int32_t mulliganCostAbove=3;                          // mulligan away cards costing more than this
    bool operator==(const CardBotConfig&)const=default;
};
inline bool valid(const CardBotConfig& c){return c.nodeBudget>=1&&c.nodeBudget<=100000&&c.depth>=1&&c.depth<=4&&c.healthWeight>=0&&c.powerWeight>=0&&c.toughnessWeight>=0&&c.handWeight>=0&&c.mulliganCostAbove>=0;}
inline CardBotConfig cardBotConfig(const gameplay::GameplaySettings& s){CardBotConfig c;c.nodeBudget=uint32_t(s.cardGame.botNodeBudget);c.depth=uint32_t(s.cardGame.botDepth);return c;}
struct CardBotChoice{genre::CardAction action;int64_t score=0;uint32_t nodes=0;};

// A rules-valid state holding exactly what the view shows (its own hand, revealed cards, public zones, the stack).
inline std::optional<genre::CardGameState> cardStateFromView(const genre::CardLibrary& lib,const genre::CardGameView& v){
    using namespace genre;
    if(v.seats.size()<2||v.seats.size()>std::size_t(kCardMaxSeats))return std::nullopt;
    CardGameState s;s.seats.resize(v.seats.size());s.phase=v.phase;s.turn=v.turn;s.active=v.active;s.priority=v.priority;s.passes=v.passes;s.winner=v.winner;
    auto add=[&](const CardViewCard& c,CardGameZone z)->bool{
        auto def=lib.cards.find(c.def);if(!c.id||def==lib.cards.end()||c.owner<0||c.owner>=int32_t(s.seats.size())||s.cards.contains(c.id))return false;
        CardGameCard card{c.id,c.def,c.owner,z,kCardPublic,0,0,c.exhausted};
        if(z==CardGameZone::Board&&def->second.kind==CardKind::Unit){card.bonus=std::max(0,c.power-def->second.power);card.damage=std::max(0,def->second.health+card.bonus-c.health);}
        s.cards[c.id]=card;if(z!=CardGameZone::Stack){auto& seat=s.seats[std::size_t(c.owner)];
            (z==CardGameZone::Hand?seat.hand:z==CardGameZone::Board?seat.board:z==CardGameZone::Discard?seat.discard:seat.exile).push_back(c.id);}
        return true;
    };
    for(std::size_t i=0;i<v.seats.size();++i){
        const auto& sv=v.seats[i];auto& p=s.seats[i];
        p.health=sv.health;p.energy=sv.energy;p.maxEnergy=sv.maxEnergy;p.fatigue=sv.fatigue;p.eliminated=sv.eliminated;p.mulliganed=sv.mulliganed;
        for(const auto& c:sv.hand)if(!add(c,CardGameZone::Hand))return std::nullopt;
        for(const auto& c:sv.board)if(!add(c,CardGameZone::Board))return std::nullopt;
        for(const auto& c:sv.discard)if(!add(c,CardGameZone::Discard))return std::nullopt;
        for(const auto& c:sv.exile)if(!add(c,CardGameZone::Exile))return std::nullopt;
    }
    for(const auto& f:v.stack){
        if(f.kind==CardStackKind::Card&&!add(f.source,CardGameZone::Stack))return std::nullopt;
        s.stack.push_back({f.id,f.kind,f.source.id,f.controller,f.target});s.nextStackId=std::max(s.nextStackId,f.id+1);
    }
    if(!cardStateCompatible(lib,s))return std::nullopt;
    return s;
}
inline int64_t cardEvaluate(const genre::CardLibrary& lib,const genre::CardGameState& s,int32_t seat,const CardBotConfig& c){
    using namespace genre;
    if(s.winner==seat)return int64_t(1)<<40;
    if(s.winner>=0)return -(int64_t(1)<<40);
    if(s.winner==kCardDraw)return 0;
    int64_t score=0;
    for(int32_t i=0;i<int32_t(s.seats.size());++i){
        const auto& p=s.seats[std::size_t(i)];int64_t v=int64_t(std::max(0,p.health))*c.healthWeight+int64_t(p.hand.size())*c.handWeight;
        for(auto id:p.board){const auto& u=s.cards.at(id);v+=int64_t(cardPower(lib,u))*c.powerWeight+int64_t(std::max(0,cardHealth(lib,u)))*c.toughnessWeight;}
        score+=i==seat?v:-v;
    }
    return score;
}
namespace detail_cardbot {
// Applies `a` for `seat`, then lets every holder of priority pass until the stack is empty.
inline std::optional<genre::CardGameState> simulate(const genre::CardLibrary& sim,const genre::CardGameState& from,int32_t seat,const genre::CardAction& a){
    using namespace genre;Random rng(0x5eed);auto s=from;uint64_t seq=s.lastCommand;
    if(!stepCardGame(sim,s,{++seq,seat,a},rng))return std::nullopt;
    for(std::size_t guard=0;guard<4*kCardMaxStack*std::size_t(kCardMaxSeats)&&!s.stack.empty()&&s.phase!=CardPhase::GameOver;++guard)
        if(!stepCardGame(sim,s,{++seq,s.priority,{}},rng))return std::nullopt;
    return s;
}
inline int64_t followUp(const genre::CardLibrary& sim,const genre::CardGameState& s,int32_t seat,uint32_t depth,uint32_t& budget,uint32_t& nodes,const CardBotConfig& c){
    using namespace genre;auto best=cardEvaluate(sim,s,seat,c);
    if(depth==0||s.phase==CardPhase::GameOver||s.priority!=seat||s.active!=seat||!s.stack.empty())return best;
    for(const auto& a:cardLegalActions(sim,s,seat)){
        if(a.kind==CardActionKind::Pass)continue;
        if(!budget)break;
        --budget;++nodes;
        if(auto next=simulate(sim,s,seat,a))best=std::max(best,followUp(sim,*next,seat,depth-1,budget,nodes,c));
    }
    return best;
}
}
inline std::optional<CardBotChoice> cardBotChoose(const genre::CardLibrary& lib,const genre::CardGameView& view,const CardBotConfig& cfg=CardBotConfig{}){
    using namespace genre;
    if(!valid(cfg))return std::nullopt;
    auto legal=cardLegalActions(lib,view);if(legal.empty())return std::nullopt;
    if(view.phase==CardPhase::Mulligan){
        const auto& hand=view.seats[std::size_t(view.viewer)].hand;uint32_t mask=0,count=0;
        for(std::size_t i=0;i<hand.size();++i)if(lib.cards.at(hand[i].def).cost>cfg.mulliganCostAbove){mask|=1u<<i;++count;}
        if(lib.rules.mulligan==CardMulligan::Full)mask=count*2>hand.size()?(1u<<hand.size())-1u:0;
        CardAction want{CardActionKind::Mulligan,0,{},mask};
        return CardBotChoice{std::binary_search(legal.begin(),legal.end(),want)?want:legal.front(),0,0};
    }
    auto root=cardStateFromView(lib,view);if(!root)return std::nullopt;
    auto sim=lib;sim.rules.fatigue=false; // unknown decks are empty in the rebuilt state: never punish simulated draws
    uint32_t budget=cfg.nodeBudget,nodes=0;
    std::vector<std::optional<CardGameState>> after(legal.size());std::vector<int64_t> score(legal.size(),INT64_MIN);
    for(std::size_t i=0;i<legal.size()&&budget;++i){--budget;++nodes;after[i]=detail_cardbot::simulate(sim,*root,view.viewer,legal[i]);if(after[i])score[i]=cardEvaluate(sim,*after[i],view.viewer,cfg);}
    if(cfg.depth>1)for(std::size_t i=0;i<legal.size()&&budget;++i){
        if(!after[i])continue;
        uint32_t share=std::max<uint32_t>(1,budget/uint32_t(legal.size()-i));uint32_t local=std::min(share,budget);auto before=local;
        score[i]=std::max(score[i],detail_cardbot::followUp(sim,*after[i],view.viewer,cfg.depth-1,local,nodes,cfg));budget-=before-local;
    }
    std::size_t best=0;for(std::size_t i=1;i<legal.size();++i)if(score[i]>score[best])best=i;
    if(score[best]==INT64_MIN)return CardBotChoice{legal.front(),0,nodes};
    return CardBotChoice{legal[best],score[best],nodes};
}
}
