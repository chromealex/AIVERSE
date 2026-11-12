#pragma once
// Shooter bot policy (genre-bots): engagement band, aim with bounded turn rate and seeded aim error, lead via
// genre::solveIntercept for projectile weapons, burst control from the recoil state, reload/overheat decisions
// from the weapon feed state (genre/WeaponMods.h) and cover preference among owner-supplied candidates.
// The output is ShooterInput: view angles, a locomotion wish and the FeedInput a player's trigger/reload keys
// produce. The owner applies them through stepFeed/stepRecoil and its own fire/command path.
#include "GenreCommon.h"
#include "../genre/Ballistics.h"
#include "../genre/WeaponMods.h"
namespace aiverse::lib::bots {
struct ShooterBotConfig {
    Fixed minRange=5_fx,preferredRange=15_fx,maxRange=40_fx; // retreat below min, strafe up to preferred, approach beyond; no fire past max
    Fixed projectileSpeed{},gravity{};          // projectile weapons lead with solveIntercept; speed 0 = hitscan (aim at the body)
    genre::AimConfig aim{};
    Fixed turnPerTick=Fixed::fromRatio(1,64);   // max view change per tick and axis (turns)
    Fixed fireTolerance=Fixed::fromRatio(1,200);// fire only when the view is this close to the (erroneous) solution
    Fixed recoilTolerance=Fixed::fromRatio(1,50);// hold fire while |recoil| exceeds this: bursts let recoil recover
    int64_t recoilCompensationMilli=500;        // share of the current recoil the bot pulls against
    int64_t reloadIdleMilli=500;                // with no target, top up when loaded < capacity * this / 1000
    int64_t heatStopMilli=900,heatResumeMilli=400; // stop at heat >= capacity*stop/1000, resume at <= capacity*resume/1000
    int64_t retreatHealthMilli=300;             // below this health fraction prefer cover while a target is seen
    int64_t coverDistanceWeight=100;            // cover score points lost per metre of travel
    uint32_t strafeTicks=40;                    // strafe side flips every strafeTicks (0 = hold)
    BotDifficulty difficulty{};
};
// protectionMilli: how well this spot hides the bot from its current threats (0..1000), computed by the owner
// from its LOS/cover data; reachable: the owner's navigation can reach it. Unreachable or 0-protection spots are skipped.
struct CoverCandidate {int64_t id=0;Vec3 position{};int64_t protectionMilli=1000;bool reachable=true;};
struct ShooterBotSelf {int64_t id=0,team=0;Vec3 position{},eye{};Fixed yaw{},pitch{};genre::RecoilState recoil{};genre::FeedState feed{};uint32_t reserve=0;int64_t healthMilli=1000;};
enum class ShooterDecision:uint8_t {Idle,Engage,Reload,Cool,Cover};
// Persisted bot memory (genre::save record kind 57).
struct ShooterBotState {
    int64_t target=0,cover=0;uint32_t noticedAt=0,lastDecision=0,lastTick=0;Fixed errorYaw{},errorPitch{};
    ShooterDecision decision=ShooterDecision::Idle;bool cooling=false,decided=false,initialized=false;
};
// Player-equivalent inputs: move -> locomotion wish (world X/Z), yaw/pitch -> view angles (recoil is added by the
// weapon owner), feed -> stepFeed input (fire is the held trigger level), cover -> chosen candidate id (diagnostic/nav goal).
struct ShooterInput {Vec2 move{};Fixed yaw{},pitch{};genre::FeedInput feed{};int64_t cover=0;ShooterDecision decision=ShooterDecision::Idle;};

inline bool valid(const ShooterBotConfig& c){
    auto milli=[](int64_t v){return v>=0&&v<=1000;};
    return c.minRange>=Fixed{}&&c.minRange<=c.preferredRange&&c.preferredRange<=c.maxRange&&c.maxRange<=kBotCoordLimit
        &&(c.projectileSpeed==Fixed{}||(c.projectileSpeed>=Fixed::fromRatio(1,16)&&c.projectileSpeed<=1000_fx))&&c.gravity>=Fixed{}&&c.gravity<=1000_fx&&genre::valid(c.aim)
        &&c.turnPerTick>Fixed{}&&c.turnPerTick<=Fixed::fromRatio(1,2)&&c.fireTolerance>=Fixed{}&&c.fireTolerance<=Fixed::fromRatio(1,8)
        &&c.recoilTolerance>=Fixed{}&&c.recoilTolerance<=Fixed::fromRatio(1,4)&&milli(c.recoilCompensationMilli)&&milli(c.reloadIdleMilli)
        &&milli(c.heatStopMilli)&&milli(c.heatResumeMilli)&&c.heatResumeMilli<=c.heatStopMilli&&milli(c.retreatHealthMilli)
        &&c.coverDistanceWeight>=0&&c.coverDistanceWeight<=1000000&&c.strafeTicks<=36000&&valid(c.difficulty);
}
inline bool valid(const ShooterBotState& s){
    auto small=[](Fixed v){return v>= -Fixed::fromRatio(1,8)&&v<=Fixed::fromRatio(1,8);};
    if(uint8_t(s.decision)>uint8_t(ShooterDecision::Cover)||s.target<0||s.cover<0||!small(s.errorYaw)||!small(s.errorPitch))return false;
    if(!s.initialized)return s.target==0&&s.cover==0&&s.noticedAt==0&&s.lastDecision==0&&s.lastTick==0&&s.errorYaw==Fixed{}&&s.errorPitch==Fixed{}
        &&s.decision==ShooterDecision::Idle&&!s.cooling&&!s.decided;
    return s.noticedAt<=s.lastTick&&s.lastDecision<=s.lastTick;
}
inline bool valid(const ShooterBotSelf& s){
    auto pitch=[](Fixed v){return v>= -Fixed::fromRatio(1,4)&&v<=Fixed::fromRatio(1,4);};
    return s.id>0&&s.team>=0&&botBounded(s.position,kBotCoordLimit)&&botBounded(s.eye,kBotCoordLimit)&&s.yaw>= -1_fx&&s.yaw<=1_fx&&pitch(s.pitch)
        &&pitch(s.recoil.yaw)&&pitch(s.recoil.pitch)&&s.healthMilli>=0&&s.healthMilli<=1000;
}
inline bool validCover(std::span<const CoverCandidate> cover){
    if(cover.size()>kBotMaxTargets)return false;
    std::vector<int64_t> ids;ids.reserve(cover.size());
    for(const auto& x:cover){if(x.id<=0||!botBounded(x.position,kBotCoordLimit)||x.protectionMilli<0||x.protectionMilli>1000)return false;ids.push_back(x.id);}
    std::sort(ids.begin(),ids.end());return std::adjacent_find(ids.begin(),ids.end())==ids.end();
}
// Highest protection minus travel cost wins; ties by smaller id. 0 when no usable candidate.
inline int64_t chooseCover(const ShooterBotConfig& c,Vec3 from,std::span<const CoverCandidate> cover){
    int64_t best=0,bestScore=0;
    for(const auto& x:cover){
        if(!x.reachable||x.protectionMilli<=0)continue;
        auto score=x.protectionMilli-((x.position-from).length()*c.coverDistanceWeight).toInt();
        if(!best||score>bestScore||(score==bestScore&&x.id<best)){best=x.id;bestScore=score;}
    }
    return best;
}

// Once per bot per tick. nullopt: invalid config/feed/self/targets/cover or repeated/backwards tick (state
// untouched). Consumes exactly two RNG draws per decision when aimError > 0 (none otherwise).
inline std::optional<ShooterInput> stepShooterBot(const ShooterBotConfig& c,const genre::FeedConfig& feed,ShooterBotState& state,uint32_t tick,
    const ShooterBotSelf& self,std::span<const PerceivedTarget> seen,std::span<const CoverCandidate> cover,Random& rng){
    using genre::FeedPhase;
    if(!valid(c)||!genre::compatible(feed,self.feed)||!valid(state)||!valid(self)||!validTargets(seen)||!validCover(cover)||(state.initialized&&tick<=state.lastTick))return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;
    ShooterInput out;out.yaw=self.yaw;out.pitch=self.pitch;
    const auto* t=pickTarget(self.position,self.id,self.team,seen,s.target);
    int64_t id=t?t->id:0;if(id!=s.target){s.target=id;s.noticedAt=tick;}
    bool reacted=t&&tick-s.noticedAt>=c.difficulty.reactionTicks;
    bool due=!s.decided||tick-s.lastDecision>=c.difficulty.decisionTicks;
    if(due){s.decided=true;s.lastDecision=tick;s.errorYaw=botJitter(rng,c.difficulty.aimError);s.errorPitch=botJitter(rng,c.difficulty.aimError);}

    // Heat hysteresis and ammunition, from the feed state the weapon owner reports.
    if(feed.heatPerShot){
        int64_t heat=self.feed.heat,cap=feed.heatCapacity;
        if(self.feed.phase==FeedPhase::Overheated||heat*1000>=cap*c.heatStopMilli)s.cooling=true;
        else if(heat*1000<=cap*c.heatResumeMilli)s.cooling=false;
    }else s.cooling=false;
    bool magazine=genre::usesMagazine(feed.kind);
    bool hasRound=magazine?self.feed.loaded>=feed.ammoPerShot:feed.kind==genre::FeedKind::Belt?self.reserve>=feed.ammoPerShot:true;
    bool reloading=self.feed.phase==FeedPhase::Reloading;
    bool wantReload=magazine&&self.feed.phase==FeedPhase::Ready&&self.feed.loaded<feed.capacity&&self.reserve>0
        &&(self.feed.loaded<feed.ammoPerShot||(!t&&int64_t(self.feed.loaded)*1000<int64_t(feed.capacity)*c.reloadIdleMilli));
    out.feed.reload=wantReload;

    // Cover while hurt, reloading or cooling under threat; re-chosen each decision, kept while still usable.
    bool needCover=t&&(self.healthMilli<c.retreatHealthMilli||reloading||wantReload||s.cooling);
    const CoverCandidate* spot=nullptr;
    if(!needCover)s.cover=0;
    else{
        for(const auto& x:cover)if(x.id==s.cover&&x.reachable&&x.protectionMilli>0)spot=&x;
        if(due||!spot){s.cover=chooseCover(c,self.position,cover);spot=nullptr;for(const auto& x:cover)if(x.id==s.cover)spot=&x;}
    }

    Fixed distance{};Vec2 toward{};
    if(t){Vec2 flat{t->position.x-self.position.x,t->position.z-self.position.z};distance=flat.length();toward=planarUnit(flat);}
    if(spot){Vec2 d{spot->position.x-self.position.x,spot->position.z-self.position.z};if(d.length()>Fixed::fromRatio(1,2))out.move=planarUnit(d);}
    else if(reacted&&toward!=Vec2{}){
        if(distance>c.preferredRange)out.move=toward;
        else if(distance<c.minRange)out.move={-toward.x,-toward.y};
        else if(c.strafeTicks)out.move=rightOf(toward)*(((tick/c.strafeTicks)%2)?-1_fx:1_fx);
    }

    bool fire=false;
    if(reacted){
        Fixed wantYaw=yawToward(self.eye,t->position,self.yaw),wantPitch=pitchToward(self.eye,t->position);bool solved=true;
        if(c.projectileSpeed>Fixed{}){
            auto lead=genre::solveIntercept(c.aim,self.eye,t->position,t->velocity,c.projectileSpeed,c.gravity);
            if(lead&&lead->found){auto v=lead->launch.velocity;if(v.x.raw||v.z.raw)wantYaw=fxAtan2Turns(v.x,v.z);wantPitch=lead->launch.elevation;}
            else solved=false;
        }
        wantYaw=wrapBotTurns(wantYaw+s.errorYaw-self.recoil.yaw*c.recoilCompensationMilli/1000);
        wantPitch=fxClamp(wantPitch+s.errorPitch-self.recoil.pitch*c.recoilCompensationMilli/1000,-Fixed::fromRatio(1,4),Fixed::fromRatio(1,4));
        auto dy=fxClamp(wrapBotTurns(wantYaw-self.yaw),-c.turnPerTick,c.turnPerTick),dp=fxClamp(wantPitch-self.pitch,-c.turnPerTick,c.turnPerTick);
        out.yaw=wrapBotTurns(self.yaw+dy);out.pitch=fxClamp(self.pitch+dp,-Fixed::fromRatio(1,4),Fixed::fromRatio(1,4));
        bool aligned=fxAbs(wrapBotTurns(wantYaw-out.yaw))<=c.fireTolerance&&fxAbs(wantPitch-out.pitch)<=c.fireTolerance;
        bool settled=fxAbs(self.recoil.yaw)<=c.recoilTolerance&&fxAbs(self.recoil.pitch)<=c.recoilTolerance;
        fire=solved&&aligned&&settled&&distance<=c.maxRange&&!s.cooling&&hasRound&&!wantReload&&self.feed.phase==FeedPhase::Ready;
    }
    out.feed.fire=fire;
    s.decision=spot?ShooterDecision::Cover:(wantReload||reloading)?ShooterDecision::Reload:s.cooling&&t?ShooterDecision::Cool:t?ShooterDecision::Engage:ShooterDecision::Idle;
    out.cover=s.cover;out.decision=s.decision;state=s;return out;
}
}
