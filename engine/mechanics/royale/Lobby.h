#pragma once
#include "engine/mechanics/Namespace.h"
// Pre-match lobby: players gather until `fillPlayers` joined or `maxWaitTicks` passed with at least
// `minPlayers`; then a countdown of `countdownTicks` runs and the match starts. If the count drops below
// minPlayers during the countdown it is cancelled. The owner adds bots to fill empty seats on Start when
// it wants full matches (botFill tells how many). Join / leave validation stays with the session owner.
#include "engine/mechanics/royale/Common.h"
namespace aiverse::lib::mechanics {
struct RoyaleLobbyDef{uint32_t minPlayers=2,fillPlayers=100,maxPlayers=100,maxWaitTicks=1800,countdownTicks=300;bool fillWithBots=true;};
enum class RoyaleLobbyStage:uint8_t{Gathering,Countdown,Started};
struct RoyaleLobbyState{RoyaleLobbyStage stage=RoyaleLobbyStage::Gathering;uint32_t opened=0,countdownFrom=0,lastTick=0;bool initialized=false;bool operator==(const RoyaleLobbyState&)const=default;};
enum class RoyaleLobbyEventKind:uint8_t{CountdownStarted,CountdownCancelled,Started};
struct RoyaleLobbyEvent{RoyaleLobbyEventKind kind{};uint32_t players=0,botFill=0;bool operator==(const RoyaleLobbyEvent&)const=default;};
using RoyaleLobbyEvents=std::vector<RoyaleLobbyEvent>;
inline bool valid(const RoyaleLobbyDef& d){return d.minPlayers>=1&&d.minPlayers<=d.fillPlayers&&d.fillPlayers<=d.maxPlayers&&d.maxPlayers<=kRoyaleMaxCompetitors&&d.maxWaitTicks<=kRoyaleMaxTicks&&d.countdownTicks<=kRoyaleMaxTicks;}
inline bool valid(const RoyaleLobbyState& s){return uint8_t(s.stage)<=uint8_t(RoyaleLobbyStage::Started)&&s.opened<=s.lastTick&&(s.initialized||!s.lastTick);}
inline uint32_t royaleLobbyTicksLeft(const RoyaleLobbyDef& d,const RoyaleLobbyState& s,uint32_t tick){auto end=s.countdownFrom+d.countdownTicks;return s.stage==RoyaleLobbyStage::Countdown&&end>tick?end-tick:0;}
inline std::optional<RoyaleLobbyEvents> stepRoyaleLobby(const RoyaleLobbyDef& d,RoyaleLobbyState& state,uint32_t players,uint32_t tick){
    using K=RoyaleLobbyEventKind;
    if(!valid(d)||!valid(state)||players>d.maxPlayers||!royaleDetail::nextTick(state.initialized,state.lastTick,tick))return {};
    auto s=state;if(!s.initialized){s.initialized=true;s.opened=tick;}s.lastTick=tick;RoyaleLobbyEvents events;
    if(s.stage==RoyaleLobbyStage::Gathering&&players>=d.minPlayers&&(players>=d.fillPlayers||tick>=s.opened+d.maxWaitTicks)){
        s.stage=RoyaleLobbyStage::Countdown;s.countdownFrom=tick;events.push_back({K::CountdownStarted,players,0});
    }else if(s.stage==RoyaleLobbyStage::Countdown&&players<d.minPlayers){s.stage=RoyaleLobbyStage::Gathering;events.push_back({K::CountdownCancelled,players,0});}
    if(s.stage==RoyaleLobbyStage::Countdown&&tick>=s.countdownFrom+d.countdownTicks){
        s.stage=RoyaleLobbyStage::Started;events.push_back({K::Started,players,d.fillWithBots&&players<d.fillPlayers?d.fillPlayers-players:0});
    }
    state=s;return events;
}
}
