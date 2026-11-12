#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace aiverse::lib::strategy {
enum class CardZone:uint8_t {Deck,Hand,Board,Discard,Exile};
enum class CardEffectKind:uint8_t {Damage,Guard,Heal,Energy,Draw};
struct CardEffect {CardEffectKind kind=CardEffectKind::Damage;int32_t amount=0;bool targetOpponent=true;bool operator==(const CardEffect&)const=default;};
struct CardRule {int32_t id=0,cost=0;bool instant=false,permanent=false;std::vector<CardEffect> effects;};
using CardRules=std::map<int32_t,CardRule>;
struct CardInstance {uint64_t id=0;int32_t definition=0,owner=0;bool operator==(const CardInstance&)const=default;};
struct CardSeat {
    int32_t health=30,guard=0,energy=3;
    std::array<std::vector<uint64_t>,5> zones;
    bool operator==(const CardSeat&)const=default;
};
struct CardFrame {uint64_t card=0;int32_t caster=0,target=0;bool operator==(const CardFrame&)const=default;};
struct CardState {
    std::vector<CardSeat> seats;
    std::map<uint64_t,CardInstance> cards;
    std::vector<CardFrame> stack;
    int32_t active=0,priority=0,passes=0;
    uint64_t turn=1,lastCommand=0;
    bool operator==(const CardState&)const=default;
};
inline bool valid(const CardRules& rules) {
    if(rules.empty()||rules.size()>4096)return false;
    for(const auto& [id,c]:rules){
        if(id<=0||c.id!=id||c.cost<0||c.cost>1000000||c.effects.size()>64)return false;
        for(const auto& e:c.effects)if(uint8_t(e.kind)>uint8_t(CardEffectKind::Draw)||e.amount<0||e.amount>1000000||(e.kind==CardEffectKind::Draw&&e.amount>4096))return false;
    }
    return true;
}
inline bool valid(const CardState& s) {
    const auto n=int32_t(s.seats.size());
    if(n<2||n>8||s.cards.size()>4096||s.stack.size()>64||s.active<0||s.active>=n||s.priority<0||s.priority>=n||s.passes<0||s.passes>=n||!s.turn)return false;
    std::set<uint64_t> placed;
    for(int32_t i=0;i<n;++i){
        const auto& p=s.seats[size_t(i)];
        if(p.health<0||p.health>1000000||p.guard<0||p.guard>1000000||p.energy<0||p.energy>1000000)return false;
        for(const auto& zone:p.zones)for(auto id:zone){auto it=s.cards.find(id);if(it==s.cards.end()||it->second.owner!=i||!placed.insert(id).second)return false;}
    }
    for(const auto& frame:s.stack){auto it=s.cards.find(frame.card);if(it==s.cards.end()||it->second.owner!=frame.caster||frame.target<0||frame.target>=n||frame.target==frame.caster||!placed.insert(frame.card).second)return false;}
    if(placed.size()!=s.cards.size())return false;
    for(const auto& [id,c]:s.cards)if(!id||id!=c.id||c.definition<=0||c.owner<0||c.owner>=n)return false;
    return true;
}
inline bool compatible(const CardRules& rules,const CardState& state){if(!valid(rules)||!valid(state))return false;for(const auto& [id,c]:state.cards){(void)id;if(!rules.contains(c.definition))return false;}return true;}
inline std::optional<CardState> makeCards(const CardRules& rules,const std::vector<std::vector<int32_t>>& decks,int32_t hand=5) {
    if(!valid(rules)||decks.size()<2||decks.size()>8||hand<0||hand>4096)return {};
    CardState s;s.seats.resize(decks.size());uint64_t id=1;
    for(size_t p=0;p<decks.size();++p){
        for(auto def:decks[p]){if(!rules.contains(def)||id>4096)return {};s.cards[id]={id,def,int32_t(p)};s.seats[p].zones[0].push_back(id++);}
        auto& deck=s.seats[p].zones[0];auto& held=s.seats[p].zones[1];
        for(int32_t k=0;k<hand&&!deck.empty();++k){held.push_back(deck.back());deck.pop_back();}
    }
    return s;
}
// Shuffle before distributing hands, using a simulation RNG callback returning [0,bound).
// Invalid callback values reject the state update; the caller owns RNG transactional semantics.
template<class Draw> bool shuffleDeck(CardState& s,int32_t player,Draw draw) {
    if(!valid(s)||player<0||size_t(player)>=s.seats.size()||!s.stack.empty())return false;
    auto deck=s.seats[size_t(player)].zones[0];
    for(size_t i=deck.size();i>1;--i){auto n=draw(uint32_t(i));if(uint64_t(n)>=i)return false;std::swap(deck[i-1],deck[size_t(n)]);}
    s.seats[size_t(player)].zones[0]=std::move(deck);return true;
}
inline bool playCard(const CardRules& rules,CardState& s,int32_t player,uint64_t card,int32_t target,uint64_t command) {
    if(!compatible(rules,s)||command<=s.lastCommand||player!=s.priority||target<0||size_t(target)>=s.seats.size()||target==player||s.stack.size()==64)return false;
    auto it=s.cards.find(card);if(it==s.cards.end()||it->second.owner!=player)return false;
    const auto& def=rules.at(it->second.definition);auto& p=s.seats[size_t(player)];auto& hand=p.zones[1];auto h=std::find(hand.begin(),hand.end(),card);
    if(!p.health||!s.seats[size_t(target)].health||h==hand.end()||p.energy<def.cost||(!def.instant&&(s.active!=player||!s.stack.empty())))return false;
    p.energy-=def.cost;hand.erase(h);s.stack.push_back({card,player,target});s.passes=0;s.lastCommand=command;s.priority=(player+1)%int32_t(s.seats.size());return true;
}
// All players passing resolves exactly one top frame. Effects are ordered and atomic.
inline bool passCards(const CardRules& rules,CardState& s,int32_t player,uint64_t command) {
    if(!compatible(rules,s)||command<=s.lastCommand||player!=s.priority)return false;
    auto out=s;out.lastCommand=command;
    if(++out.passes<int32_t(out.seats.size())){out.priority=(player+1)%int32_t(out.seats.size());s=std::move(out);return true;}
    out.passes=0;out.priority=out.active;
    if(!out.stack.empty()){
        auto frame=out.stack.back();out.stack.pop_back();const auto& def=rules.at(out.cards.at(frame.card).definition);
        for(const auto& e:def.effects){
            auto& p=out.seats[size_t(e.targetOpponent?frame.target:frame.caster)];
            switch(e.kind){
                case CardEffectKind::Damage:{auto blocked=std::min(p.guard,e.amount);p.guard-=blocked;p.health=std::max(0,p.health-(e.amount-blocked));break;}
                case CardEffectKind::Guard:if(p.guard>1000000-e.amount)return false;p.guard+=e.amount;break;
                case CardEffectKind::Heal:if(p.health>1000000-e.amount)return false;p.health+=e.amount;break;
                case CardEffectKind::Energy:if(p.energy>1000000-e.amount)return false;p.energy+=e.amount;break;
                case CardEffectKind::Draw:for(int32_t i=0;i<e.amount&&!p.zones[0].empty();++i){p.zones[1].push_back(p.zones[0].back());p.zones[0].pop_back();}break;
            }
        }
        out.seats[size_t(frame.caster)].zones[def.permanent?2:3].push_back(frame.card);
    }
    s=std::move(out);return true;
}
inline bool nextCardTurn(CardState& s,int32_t player,uint64_t command,int32_t energy=3) {
    if(!valid(s)||player!=s.active||s.priority!=player||s.passes||!s.stack.empty()||command<=s.lastCommand||s.turn==UINT64_MAX||energy<0||energy>1000000)return false;
    auto out=s;out.active=(out.active+1)%int32_t(out.seats.size());out.priority=out.active;++out.turn;out.lastCommand=command;
    auto& p=out.seats[size_t(out.active)];p.energy=energy;p.guard=0;
    if(!p.zones[0].empty()){p.zones[1].push_back(p.zones[0].back());p.zones[0].pop_back();}
    s=std::move(out);return true;
}
struct CardSeatView {int32_t health=0,guard=0,energy=0;size_t deckCount=0,handCount=0;std::vector<CardInstance> hand,board,discard,exile;};
struct CardView {std::vector<CardSeatView> seats;std::vector<CardInstance> stack;int32_t active=0,priority=0;};
// No secret IDs, definitions or deck order in the opponent/spectator projection.
inline std::optional<CardView> cardView(const CardState& s,int32_t viewer=-1) {
    if(!valid(s)||viewer< -1||viewer>=int32_t(s.seats.size()))return {};
    CardView out;out.active=s.active;out.priority=s.priority;
    for(size_t i=0;i<s.seats.size();++i){const auto& p=s.seats[i];CardSeatView v;v.health=p.health;v.guard=p.guard;v.energy=p.energy;v.deckCount=p.zones[0].size();v.handCount=p.zones[1].size();
        for(size_t z=1;z<5;++z){if(z==1&&int32_t(i)!=viewer)continue;auto* dest=z==1?&v.hand:z==2?&v.board:z==3?&v.discard:&v.exile;for(auto id:p.zones[z])dest->push_back(s.cards.at(id));}out.seats.push_back(std::move(v));}
    for(const auto& f:s.stack)out.stack.push_back(s.cards.at(f.card));
    return out;
}
}
