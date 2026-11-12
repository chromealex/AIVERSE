#pragma once
// card-games: setup (seeded shuffles, deal) and the single authoritative step for every command:
// mulligan, play (cards go on the stack and priority passes), attack (on the stack too), pass (all living seats
// passing in a row resolves the top item; passing with an empty stack advances the phase/turn).
#include "CardGame.h"
namespace aiverse::lib::genre {
struct CardSetupResult{CardGameState state;std::vector<CardEvent> events;};
namespace detail_cardgame {
struct Ctx{const CardLibrary& lib;CardGameState& s;std::vector<CardEvent>& ev;};
inline int32_t living(const CardGameState& s){int32_t n=0;for(const auto& p:s.seats)if(!p.eliminated)++n;return n;}
inline int32_t nextLiving(const CardGameState& s,int32_t from){
    auto n=int32_t(s.seats.size());for(int32_t k=1;k<=n;++k){auto i=(from+k)%n;if(!s.seats[std::size_t(i)].eliminated)return i;}return from;
}
inline void shuffleIds(std::vector<uint32_t>& v,Random& rng){for(std::size_t i=v.size();i>1;--i)std::swap(v[i-1],v[rng.below(uint32_t(i))]);}
// Moves a card between zones of its owner, keeping visibility: deck = nobody, hand = owner (+ seats that already
// knew it), public zones and the stack = everybody. Leaving the board clears damage, buffs and exhaustion.
inline void moveCard(Ctx& c,uint32_t id,CardGameZone to){
    auto& card=c.s.cards.at(id);auto& seat=c.s.seats[std::size_t(card.owner)];
    if(card.zone!=CardGameZone::Stack){auto& from=zoneList(seat,card.zone);from.erase(std::find(from.begin(),from.end(),id));}
    if(card.zone==CardGameZone::Board){card.damage=0;card.bonus=0;card.exhausted=false;}
    if(to==CardGameZone::Deck)card.known=0;
    else if(to==CardGameZone::Hand)card.known=hiddenZone(card.zone)?uint8_t(card.known|bit(card.owner)):kCardPublic;
    else card.known=kCardPublic;
    card.zone=to;if(to!=CardGameZone::Stack)zoneList(seat,to).push_back(id);
}
inline void emit(Ctx& c,CardEventKind k,int32_t player,uint32_t card=0,CardTargetRef target={},int32_t value=0,uint8_t visible=kCardPublic){
    c.ev.push_back({k,player,card,card?c.s.cards.at(card).def:0,target,value,visible});
}
inline void hurtPlayer(Ctx& c,int32_t seat,int32_t amount,uint32_t source){
    auto& p=c.s.seats[std::size_t(seat)];p.health=std::max(-kCardValueLimit,p.health-amount);
    emit(c,CardEventKind::Damaged,seat,source,{CardTargetType::Player,uint32_t(seat)},amount);
}
inline void drawCard(Ctx& c,int32_t seat){
    auto& p=c.s.seats[std::size_t(seat)];
    if(p.deck.empty()){if(c.lib.rules.fatigue){p.fatigue=std::min(kCardValueLimit,p.fatigue+1);emit(c,CardEventKind::Fatigue,seat,0,{},p.fatigue);hurtPlayer(c,seat,p.fatigue,0);}return;}
    auto id=p.deck.back();
    if(int32_t(p.hand.size())>=c.lib.rules.maxHand){moveCard(c,id,CardGameZone::Discard);emit(c,CardEventKind::Burned,seat,id);return;}
    moveCard(c,id,CardGameZone::Hand);emit(c,CardEventKind::Drew,seat,id,{},0,c.s.cards.at(id).known);
}
inline void finish(Ctx& c,int32_t winner){
    for(auto it=c.s.stack.rbegin();it!=c.s.stack.rend();++it)if(it->kind==CardStackKind::Card)moveCard(c,it->source,CardGameZone::Discard);
    c.s.stack.clear();c.s.phase=CardPhase::GameOver;c.s.winner=winner;c.s.passes=0;emit(c,CardEventKind::GameOver,winner);
}
inline void finishByHealth(Ctx& c){
    int32_t best=kCardDraw,hp=0;bool tie=false;
    for(int32_t i=0;i<int32_t(c.s.seats.size());++i){const auto& p=c.s.seats[std::size_t(i)];if(p.eliminated)continue;
        if(best==kCardDraw||p.health>hp){best=i;hp=p.health;tie=false;}else if(p.health==hp)tie=true;}
    finish(c,tie?kCardDraw:best);
}
// State-based checks after every change: dead units leave the board, players at 0 health are eliminated.
inline bool cleanup(Ctx& c){
    for(std::size_t seat=0;seat<c.s.seats.size();++seat){
        auto board=c.s.seats[seat].board;
        for(auto id:board){const auto& u=c.s.cards.at(id);if(cardHealth(c.lib,u)<=0){moveCard(c,id,CardGameZone::Discard);emit(c,CardEventKind::UnitDied,int32_t(seat),id);}}
    }
    for(int32_t i=0;i<int32_t(c.s.seats.size());++i){auto& p=c.s.seats[std::size_t(i)];if(!p.eliminated&&p.health<=0){p.eliminated=true;emit(c,CardEventKind::PlayerEliminated,i);}}
    auto alive=living(c.s);
    if(alive<=1){int32_t w=kCardDraw;for(int32_t i=0;i<int32_t(c.s.seats.size());++i)if(!c.s.seats[std::size_t(i)].eliminated)w=i;finish(c,w);return false;}
    if(c.s.seats[std::size_t(c.s.priority)].eliminated)c.s.priority=nextLiving(c.s,c.s.priority);
    return true;
}
inline void startTurn(Ctx& c,int32_t seat){
    for(int32_t guard=0;guard<=kCardMaxSeats;++guard){
        const auto& r=c.lib.rules;
        if(r.turnLimit>0&&int64_t(c.s.turn)+1>r.turnLimit){finishByHealth(c);return;}
        ++c.s.turn;c.s.active=c.s.priority=seat;c.s.passes=0;c.s.phase=CardPhase::Main;
        auto& p=c.s.seats[std::size_t(seat)];
        p.maxEnergy=int32_t(std::min<int64_t>(r.energyMax,int64_t(r.energyStart)+int64_t(r.energyGain)*p.turnsTaken));
        p.energy=p.maxEnergy;if(p.turnsTaken<kCardValueLimit)++p.turnsTaken;
        for(auto id:p.board)c.s.cards.at(id).exhausted=false;
        emit(c,CardEventKind::TurnStarted,seat,0,{},int32_t(std::min<uint32_t>(c.s.turn,uint32_t(kCardValueLimit))));
        if(!(c.s.turn==1&&r.firstPlayerSkipsDraw))for(int32_t k=0;k<r.drawPerTurn;++k)drawCard(c,seat);
        if(!cleanup(c))return;
        if(!c.s.seats[std::size_t(seat)].eliminated)return;
        seat=nextLiving(c.s,seat); // fatigue eliminated the new active seat in a multiplayer game
    }
}
inline void endTurn(Ctx& c){emit(c,CardEventKind::PhaseChanged,c.s.active,0,{},-1);startTurn(c,nextLiving(c.s,c.s.active));}
inline bool targetValid(const CardGameState& s,CardTargetRef t){
    switch(t.type){
    case CardTargetType::None:return true;
    case CardTargetType::Player:return t.value<s.seats.size()&&!s.seats[t.value].eliminated;
    case CardTargetType::Unit:{auto it=s.cards.find(t.value);return it!=s.cards.end()&&it->second.zone==CardGameZone::Board;}
    case CardTargetType::Stack:for(const auto& f:s.stack)if(f.id==t.value)return true;return false;
    }
    return false;
}
inline std::vector<uint32_t> unitsOf(const CardGameState& s,int32_t controller,bool enemies){
    std::vector<uint32_t> out;
    for(int32_t i=0;i<int32_t(s.seats.size());++i){if(s.seats[std::size_t(i)].eliminated||(i!=controller)!=enemies)continue;for(auto id:s.seats[std::size_t(i)].board)out.push_back(id);}
    return out;
}
inline void applyEffect(Ctx& c,const CardEffectDef& e,const CardStackItem& f){
    std::vector<int32_t> players;std::vector<uint32_t> units;
    switch(e.to){
    case CardEffectTo::Target:if(f.target.type==CardTargetType::Player)players.push_back(int32_t(f.target.value));else if(f.target.type==CardTargetType::Unit)units.push_back(f.target.value);break;
    case CardEffectTo::Controller:players.push_back(f.controller);break;
    case CardEffectTo::Opponents:for(int32_t i=0;i<int32_t(c.s.seats.size());++i)if(i!=f.controller&&!c.s.seats[std::size_t(i)].eliminated)players.push_back(i);break;
    case CardEffectTo::EnemyUnits:units=unitsOf(c.s,f.controller,true);break;
    case CardEffectTo::FriendlyUnits:units=unitsOf(c.s,f.controller,false);break;
    }
    for(auto id:units){
        auto& u=c.s.cards.at(id);if(u.zone!=CardGameZone::Board)continue;CardTargetRef t{CardTargetType::Unit,id};
        if(e.op==CardEffectOp::Damage){u.damage=std::min(kCardValueLimit,u.damage+e.amount);emit(c,CardEventKind::Damaged,u.owner,f.source,t,e.amount);}
        else if(e.op==CardEffectOp::Heal){auto healed=std::min(u.damage,e.amount);u.damage-=healed;emit(c,CardEventKind::Healed,u.owner,f.source,t,healed);}
        else if(e.op==CardEffectOp::Buff){u.bonus=std::min(kCardValueLimit,u.bonus+e.amount);emit(c,CardEventKind::Buffed,u.owner,f.source,t,e.amount);}
    }
    for(auto seat:players){
        auto& p=c.s.seats[std::size_t(seat)];CardTargetRef t{CardTargetType::Player,uint32_t(seat)};
        switch(e.op){
        case CardEffectOp::Damage:hurtPlayer(c,seat,e.amount,f.source);break;
        case CardEffectOp::Heal:{auto before=p.health;p.health=std::max(p.health,std::min(c.lib.rules.startingHealth,p.health+e.amount));emit(c,CardEventKind::Healed,seat,f.source,t,p.health-before);break;}
        case CardEffectOp::Draw:for(int32_t k=0;k<e.amount;++k)drawCard(c,seat);break;
        case CardEffectOp::Energy:p.energy=std::min(kCardValueLimit,p.energy+e.amount);emit(c,CardEventKind::EnergyGained,seat,f.source,t,e.amount);break;
        case CardEffectOp::Reveal:for(auto id:p.hand){auto& card=c.s.cards.at(id);card.known=uint8_t(card.known|bit(f.controller));
            emit(c,CardEventKind::Revealed,seat,id,{CardTargetType::Player,uint32_t(f.controller)},0,uint8_t(bit(f.controller)|bit(seat)));}break;
        case CardEffectOp::Mill:for(int32_t k=0;k<e.amount&&!p.deck.empty();++k){auto id=p.deck.back();moveCard(c,id,CardGameZone::Discard);emit(c,CardEventKind::Milled,seat,id);}break;
        default:break;
        }
    }
    if(e.op==CardEffectOp::Counter&&f.target.type==CardTargetType::Stack){
        auto it=std::find_if(c.s.stack.begin(),c.s.stack.end(),[&](const CardStackItem& x){return x.id==f.target.value;});
        if(it!=c.s.stack.end()){auto item=*it;c.s.stack.erase(it);if(item.kind==CardStackKind::Card)moveCard(c,item.source,CardGameZone::Discard);
            emit(c,CardEventKind::Countered,item.controller,item.source,{CardTargetType::Stack,item.id});}
    }
}
inline void resolveTop(Ctx& c){
    auto f=c.s.stack.back();c.s.stack.pop_back();c.s.passes=0;
    if(f.kind==CardStackKind::Attack){
        auto& a=c.s.cards.at(f.source);
        if(a.zone!=CardGameZone::Board||!targetValid(c.s,f.target)){emit(c,CardEventKind::Fizzled,f.controller,f.source,f.target);}
        else{
            auto power=cardPower(c.lib,a);emit(c,CardEventKind::Resolved,f.controller,f.source,f.target);
            if(f.target.type==CardTargetType::Player)hurtPlayer(c,int32_t(f.target.value),power,f.source);
            else{auto& d=c.s.cards.at(f.target.value);auto back=cardPower(c.lib,d);
                d.damage=std::min(kCardValueLimit,d.damage+power);emit(c,CardEventKind::Damaged,d.owner,f.source,f.target,power);
                a.damage=std::min(kCardValueLimit,a.damage+back);emit(c,CardEventKind::Damaged,a.owner,d.id,{CardTargetType::Unit,a.id},back);}
        }
    }else{
        const auto& d=c.lib.cards.at(c.s.cards.at(f.source).def);
        if(!targetValid(c.s,f.target)){emit(c,CardEventKind::Fizzled,f.controller,f.source,f.target);moveCard(c,f.source,CardGameZone::Discard);}
        else{
            emit(c,CardEventKind::Resolved,f.controller,f.source,f.target);
            for(const auto& e:d.effects)applyEffect(c,e,f);
            if(d.kind==CardKind::Unit){moveCard(c,f.source,CardGameZone::Board);c.s.cards.at(f.source).exhausted=!(d.rush||c.lib.rules.unitsAttackImmediately);}
            else moveCard(c,f.source,CardGameZone::Discard);
        }
    }
    if(!cleanup(c))return;
    if(c.s.seats[std::size_t(c.s.active)].eliminated){if(c.s.stack.empty()){startTurn(c,nextLiving(c.s,c.s.active));return;}c.s.priority=nextLiving(c.s,c.s.active);}
    else c.s.priority=c.s.active;
}
inline void mulligan(Ctx& c,Random& rng,int32_t seat,uint32_t mask){
    auto& p=c.s.seats[std::size_t(seat)];std::vector<uint32_t> back;
    for(std::size_t i=0;i<p.hand.size();++i)if((mask>>i)&1u)back.push_back(p.hand[i]);
    auto count=int32_t(back.size());
    if(count){
        for(auto id:back)moveCard(c,id,CardGameZone::Deck);
        shuffleIds(c.s.seats[std::size_t(seat)].deck,rng);emit(c,CardEventKind::Shuffled,seat);
        auto draws=c.lib.rules.mulligan==CardMulligan::Full?std::max(0,c.lib.rules.startingHand-c.lib.rules.mulliganPenalty):count;
        for(int32_t k=0;k<draws;++k)drawCard(c,seat);
    }
    c.s.seats[std::size_t(seat)].mulliganed=true;emit(c,CardEventKind::Mulligan,seat,0,{},count);
    for(int32_t k=1;k<=int32_t(c.s.seats.size());++k){auto i=(seat+k)%int32_t(c.s.seats.size());if(!c.s.seats[std::size_t(i)].mulliganed){c.s.priority=i;return;}}
    startTurn(c,c.s.active);
}
}
// Shuffles every deck with `rng` (seat order), assigns instance ids in shuffled order, deals the starting hands and
// opens the mulligan (priority: firstPlayer, then seat order) or the first turn.
inline std::optional<CardSetupResult> cardGameSetup(const CardLibrary& lib,const std::vector<std::vector<int64_t>>& decks,int32_t firstPlayer,Random& rng){
    using namespace detail_cardgame;
    const auto& r=lib.rules;
    if(!valid(lib)||int32_t(decks.size())!=r.seats||firstPlayer<0||firstPlayer>=r.seats)return std::nullopt;
    for(const auto& d:decks){if(int32_t(d.size())<r.deckMin||int32_t(d.size())>r.deckMax)return std::nullopt;for(auto def:d)if(!lib.cards.contains(def))return std::nullopt;}
    CardSetupResult out;auto& s=out.state;s.seats.resize(decks.size());s.active=s.priority=firstPlayer;
    Ctx c{lib,s,out.events};uint32_t next=1;
    for(std::size_t seat=0;seat<decks.size();++seat){
        auto list=decks[seat];for(std::size_t i=list.size();i>1;--i)std::swap(list[i-1],list[rng.below(uint32_t(i))]);
        auto& p=s.seats[seat];p.health=r.startingHealth;
        for(auto def:list){s.cards[next]={next,def,int32_t(seat),CardGameZone::Deck,0,0,0,false};p.deck.push_back(next++);}
        emit(c,CardEventKind::Shuffled,int32_t(seat));
    }
    for(std::size_t seat=0;seat<decks.size();++seat)for(int32_t k=0;k<r.startingHand;++k)drawCard(c,int32_t(seat));
    if(r.mulligan==CardMulligan::None){for(auto& p:s.seats)p.mulliganed=true;startTurn(c,firstPlayer);}
    if(!valid(s))return std::nullopt;
    return out;
}
// The authoritative step. The command must come from the seat holding priority, with a sequence number above the
// last accepted one, and be one of cardLegalActions for that seat. RNG is consumed only by mulligan shuffles.
inline std::optional<CardStepReport> stepCardGame(const CardLibrary& lib,CardGameState& state,const CardCommand& cmd,Random& rng){
    using namespace detail_cardgame;
    if(cmd.seq<=state.lastCommand)return std::nullopt;
    auto legal=cardLegalActions(lib,state,cmd.player);
    if(!std::binary_search(legal.begin(),legal.end(),cmd.action))return std::nullopt;
    auto s=state;CardStepReport report;Ctx c{lib,s,report.events};s.lastCommand=cmd.seq;
    const auto& a=cmd.action;auto me=cmd.player;
    switch(a.kind){
    case CardActionKind::Mulligan:mulligan(c,rng,me,a.replaceMask);break;
    case CardActionKind::Play:{
        const auto& d=lib.cards.at(s.cards.at(a.card).def);s.seats[std::size_t(me)].energy-=d.cost;
        moveCard(c,a.card,CardGameZone::Stack);s.stack.push_back({s.nextStackId++,CardStackKind::Card,a.card,me,a.target});
        emit(c,CardEventKind::Played,me,a.card,a.target,d.cost);s.passes=0;s.priority=nextLiving(s,me);break;
    }
    case CardActionKind::Attack:
        s.cards.at(a.card).exhausted=true;s.stack.push_back({s.nextStackId++,CardStackKind::Attack,a.card,me,a.target});
        emit(c,CardEventKind::Attacked,me,a.card,a.target);s.passes=0;s.priority=nextLiving(s,me);break;
    case CardActionKind::Pass:
        if(!s.stack.empty()){
            emit(c,CardEventKind::PriorityPassed,me);
            if(++s.passes>=living(s))resolveTop(c);else s.priority=nextLiving(s,me);
        }else if(s.phase==CardPhase::Main&&lib.rules.separateCombat){s.phase=CardPhase::Combat;emit(c,CardEventKind::PhaseChanged,me,0,{},int32_t(CardPhase::Combat));}
        else endTurn(c);
        break;
    }
    if(s.nextStackId==UINT32_MAX||!valid(s))return std::nullopt;
    state=std::move(s);return report;
}
}
