#pragma once
// survival-ecology: body model (core temperature from owner-supplied environment, clothing and shelter;
// wetness; hunger/thirst/stamina interplay; condition) and weather modifiers. Integer math only: temperatures
// in milli-degrees C (core in micro-degrees), meters 0..100000, rates per tick. The owner supplies environment,
// clothing, exertion and food actually eaten (inventory stays with its owner); outputs are statuses and events.
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr int64_t kSurvivalMeterMax=100000;
inline constexpr int64_t kSurvivalTempLimit=200000;          // |milli-degrees| accepted from the owner
inline constexpr std::size_t kSurvivalMaxBodies=65536;

// ---- Weather: owner weather state -> modifiers shared by bodies, power producers, renewal and animals.
struct SurvivalWeather {
    int64_t temperatureMilli=15000;   // air temperature
    int64_t windMilli=0;              // wind speed, milli-metres per second
    int64_t precipitationMilli=0;     // rain/snow intensity 0..1000
    int64_t sunMilli=500;             // daylight/cloud cover 0..1000
};
struct SurvivalWeatherConfig {
    int64_t windChillMilli=600;       // felt milli-degrees per m/s of wind on exposed bodies
    int64_t wetPerTick=200;           // wetness gained per tick at full precipitation (exposed, no waterproofing)
    int64_t ratedWindMilli=12000;     // wind turbines reach nominal output here
    int64_t growMinMilli=2000,growBestMilli=20000,growMaxMilli=35000; // plant growth temperature band
    int64_t rainGrowthMilli=300;      // extra growth at full precipitation
    int64_t stormWindMilli=15000;     // animals shelter / activity drops above this
};
struct SurvivalWeatherEffects {
    int64_t ambientMilli=0,windChillMilli=0,wetPerTick=0;
    int64_t solarMilli=0,windPowerMilli=0; // PowerInput::outputMilli factors for solar / wind producers (0..1000)
    int64_t growthMilli=0;                 // renewal/ecology growth factor (0..1300)
    int64_t activityMilli=1000;            // animal activity factor (0..1000)
};
inline bool valid(const SurvivalWeather& w){return w.temperatureMilli>= -kSurvivalTempLimit&&w.temperatureMilli<=kSurvivalTempLimit&&w.windMilli>=0&&w.windMilli<=200000&&w.precipitationMilli>=0&&w.precipitationMilli<=1000&&w.sunMilli>=0&&w.sunMilli<=1000;}
inline bool valid(const SurvivalWeatherConfig& c){
    return c.windChillMilli>=0&&c.windChillMilli<=10000&&c.wetPerTick>=0&&c.wetPerTick<=kSurvivalMeterMax&&c.ratedWindMilli>=1&&c.ratedWindMilli<=200000
        &&c.growMinMilli<c.growBestMilli&&c.growBestMilli<c.growMaxMilli&&c.growMinMilli>= -kSurvivalTempLimit&&c.growMaxMilli<=kSurvivalTempLimit&&c.rainGrowthMilli>=0&&c.rainGrowthMilli<=1000&&c.stormWindMilli>=1;
}
inline SurvivalWeatherEffects survivalWeatherEffects(const SurvivalWeatherConfig& c,const SurvivalWeather& w){
    SurvivalWeatherEffects e;e.ambientMilli=w.temperatureMilli;e.windChillMilli=w.windMilli*c.windChillMilli/1000;e.wetPerTick=w.precipitationMilli*c.wetPerTick/1000;
    e.solarMilli=w.sunMilli*(1000-w.precipitationMilli/2)/1000;
    e.windPowerMilli=std::min<int64_t>(1000,w.windMilli*1000/c.ratedWindMilli);
    const auto t=w.temperatureMilli;int64_t g=0;
    if(t>c.growMinMilli&&t<c.growMaxMilli)g=t<=c.growBestMilli?(t-c.growMinMilli)*1000/(c.growBestMilli-c.growMinMilli):(c.growMaxMilli-t)*1000/(c.growMaxMilli-c.growBestMilli);
    e.growthMilli=g*(1000+w.precipitationMilli*c.rainGrowthMilli/1000)/1000;
    e.activityMilli=w.windMilli>=c.stormWindMilli?300:1000-w.precipitationMilli/2;
    return e;
}

// ---- Body model.
struct SurvivalConfig {
    int64_t coreNormalMilli=37000,comfortLowMilli=16000,comfortHighMilli=27000;
    int64_t coolingMicro=40,heatingMicro=30;   // core micro-degrees per tick per milli-degree outside the comfort band
    int64_t recoveryMicro=2000;                // core drift back to normal per tick inside the band (fed and watered)
    int64_t coldMilli=35500,severeColdMilli=33000,hotMilli=38500,severeHotMilli=40000;
    int64_t exertionHeatMicro=500;             // core gain per tick at full exertion
    int64_t hungerPerTick=8,thirstPerTick=12;  // base decay of the meters
    int64_t exertionHungerMilli=1000,shiverHungerMilli=1000,sweatThirstMilli=100; // extra decay: full exertion, shivering, per degree above comfort
    int64_t staminaDrain=600,staminaRegen=300; // per tick at full exertion / idle (resting doubles regen)
    int64_t lowAt=25000;                       // hungry/thirsty/exhausted threshold
    int64_t wetChillMilli=8000,dryPerTick=40,dryPerDegreeMicro=4000; // full wetness felt cooling; drying base and per degree above comfortLow
    int64_t starveDamage=20,thirstDamage=30,coldDamage=30,heatDamage=30,conditionRegen=10;
    SurvivalWeatherConfig weather;
};
inline std::string survivalConfigError(const SurvivalConfig& c){
    auto t=[](int64_t v){return v>= -kSurvivalTempLimit&&v<=kSurvivalTempLimit;};
    auto m=[](int64_t v){return v>=0&&v<=kSurvivalMeterMax;};
    if(!t(c.coreNormalMilli)||!t(c.comfortLowMilli)||!t(c.comfortHighMilli)||c.comfortLowMilli>=c.comfortHighMilli)return "survival: comfortLow < comfortHigh";
    if(!(c.severeColdMilli<c.coldMilli&&c.coldMilli<c.coreNormalMilli&&c.coreNormalMilli<c.hotMilli&&c.hotMilli<c.severeHotMilli)||!t(c.severeColdMilli)||!t(c.severeHotMilli))return "survival: severeCold < cold < coreNormal < hot < severeHot";
    for(auto v:{c.coolingMicro,c.heatingMicro,c.recoveryMicro,c.exertionHeatMicro,c.dryPerDegreeMicro})if(v<0||v>1000000)return "survival: micro rates 0..1000000";
    for(auto v:{c.hungerPerTick,c.thirstPerTick,c.staminaDrain,c.staminaRegen,c.lowAt,c.dryPerTick,c.starveDamage,c.thirstDamage,c.coldDamage,c.heatDamage,c.conditionRegen})if(!m(v))return "survival: meter rates 0..100000";
    for(auto v:{c.exertionHungerMilli,c.shiverHungerMilli,c.sweatThirstMilli,c.wetChillMilli})if(v<0||v>100000)return "survival: milli factors 0..100000";
    if(!valid(c.weather))return "survival.weather: invalid";
    return {};
}
inline bool valid(const SurvivalConfig& c){return survivalConfigError(c).empty();}
enum SurvivalStatus:uint32_t {
    kSurvivalCold=1,kSurvivalFreezing=2,kSurvivalHot=4,kSurvivalOverheating=8,kSurvivalHungry=16,kSurvivalStarving=32,
    kSurvivalThirsty=64,kSurvivalDehydrated=128,kSurvivalExhausted=256,kSurvivalWet=512,kSurvivalSoaked=1024
};
inline constexpr uint32_t kSurvivalStatusMask=2047;
struct SurvivalBody {
    int64_t id=0,coreMicro=37000000,wetness=0,hunger=kSurvivalMeterMax,thirst=kSurvivalMeterMax,stamina=kSurvivalMeterMax,condition=kSurvivalMeterMax;
    uint32_t status=0;bool dead=false;
    bool operator==(const SurvivalBody&)const=default;
};
struct SurvivalState {std::map<int64_t,SurvivalBody> bodies;int64_t lastTick=0;bool initialized=false;}; // record kind 106
struct SurvivalClothing {int64_t insulationMilli=0,windproofMilli=0,waterproofMilli=0;}; // 0..1000 each
struct SurvivalBodyInput {
    bool sheltered=false;int64_t shelterMilli=15000,shelterInsulationMilli=800; // inside: felt moves toward shelterMilli, no wind/rain
    int64_t heatMilli=0;bool submerged=false;      // nearby fire/heater adds felt degrees; water sets wetness to max
    SurvivalClothing clothing;
    int64_t exertionMilli=0;bool resting=false;    // 0..1000 activity level; sleeping/sitting
    int64_t eatHunger=0,eatThirst=0;               // gained this tick from food/drink the owner consumed
};
struct SurvivalInput {SurvivalWeather weather;std::map<int64_t,SurvivalBodyInput> bodies;}; // bodies without an entry: outdoors, no clothing, idle
enum class SurvivalEventKind:uint8_t {StatusOn,StatusOff,Died};   // value = status bit
struct SurvivalEvent {SurvivalEventKind kind=SurvivalEventKind::StatusOn;int64_t body=0;uint32_t value=0;};
struct SurvivalReport {std::map<int64_t,int64_t> feltMilli;std::vector<SurvivalEvent> events;SurvivalWeatherEffects weather;};

inline bool valid(const SurvivalBody& b){
    auto m=[](int64_t v){return v>=0&&v<=kSurvivalMeterMax;};
    return b.id>0&&b.coreMicro>=0&&b.coreMicro<=60000000&&m(b.wetness)&&m(b.hunger)&&m(b.thirst)&&m(b.stamina)&&m(b.condition)&&(b.status&~kSurvivalStatusMask)==0;
}
inline bool valid(const SurvivalState& s){
    if(s.bodies.size()>kSurvivalMaxBodies||s.lastTick<0)return false;
    for(const auto& [id,b]:s.bodies)if(id!=b.id||!valid(b))return false;
    return true;
}
inline bool valid(const SurvivalBodyInput& in){
    auto k=[](int64_t v){return v>=0&&v<=1000;};
    return in.shelterMilli>= -kSurvivalTempLimit&&in.shelterMilli<=kSurvivalTempLimit&&k(in.shelterInsulationMilli)&&in.heatMilli>=0&&in.heatMilli<=kSurvivalTempLimit
        &&k(in.clothing.insulationMilli)&&k(in.clothing.windproofMilli)&&k(in.clothing.waterproofMilli)&&k(in.exertionMilli)
        &&in.eatHunger>=0&&in.eatHunger<=kSurvivalMeterMax&&in.eatThirst>=0&&in.eatThirst<=kSurvivalMeterMax;
}
inline bool survivalAddBody(const SurvivalConfig& c,SurvivalState& s,int64_t id){
    if(id<=0||s.bodies.contains(id)||s.bodies.size()>=kSurvivalMaxBodies)return false;
    SurvivalBody b;b.id=id;b.coreMicro=c.coreNormalMilli*1000;s.bodies.emplace(id,b);return true;
}
// Felt temperature for one body: weather, wind and wetness on exposed skin, shelter, heat sources.
inline int64_t survivalFelt(const SurvivalConfig& c,const SurvivalWeatherEffects& w,const SurvivalBody& b,const SurvivalBodyInput& in){
    int64_t felt=w.ambientMilli;
    if(in.sheltered)felt+=(in.shelterMilli-felt)*in.shelterInsulationMilli/1000;
    else felt-=w.windChillMilli*(1000-in.clothing.windproofMilli)/1000;
    felt-=b.wetness*c.wetChillMilli/kSurvivalMeterMax;
    return std::clamp(felt+in.heatMilli,-kSurvivalTempLimit,kSurvivalTempLimit);
}
// Colony/mood bridge: discomfort as a negative mood value (cold/hot/wet/hungry/thirsty/exhausted), 0 when fine.
inline int64_t survivalMood(const SurvivalBody& b){
    int64_t m=0;
    if(b.status&kSurvivalFreezing)m-=25000;else if(b.status&kSurvivalCold)m-=10000;
    if(b.status&kSurvivalOverheating)m-=25000;else if(b.status&kSurvivalHot)m-=10000;
    if(b.status&kSurvivalSoaked)m-=6000;else if(b.status&kSurvivalWet)m-=2000;
    if(b.status&kSurvivalExhausted)m-=5000;
    return m;
}
inline std::optional<SurvivalReport> stepSurvival(const SurvivalConfig& c,SurvivalState& state,int64_t tick,const SurvivalInput& in){
    if(tick<0||(state.initialized&&tick<=state.lastTick)||!valid(c)||!valid(state)||!valid(in.weather))return std::nullopt;
    for(const auto& [id,b]:in.bodies)if(!state.bodies.contains(id)||!valid(b))return std::nullopt;
    auto s=state;SurvivalReport r;r.weather=survivalWeatherEffects(c.weather,in.weather);const auto& w=r.weather;
    const SurvivalBodyInput outdoors{};const auto normal=c.coreNormalMilli*1000;
    for(auto& [id,b]:s.bodies){
        if(b.dead)continue;
        auto it=in.bodies.find(id);const auto& e=it==in.bodies.end()?outdoors:it->second;
        // Wetness: water sets it, precipitation on exposed bodies adds, warmth dries.
        const auto felt0=survivalFelt(c,w,b,e);
        if(e.submerged)b.wetness=kSurvivalMeterMax;
        else{
            if(!e.sheltered)b.wetness+=w.wetPerTick*(1000-e.clothing.waterproofMilli)/1000;
            b.wetness-=c.dryPerTick+std::max<int64_t>(0,felt0-c.comfortLowMilli)*c.dryPerDegreeMicro/1000000;
            b.wetness=std::clamp<int64_t>(b.wetness,0,kSurvivalMeterMax);
        }
        const auto felt=survivalFelt(c,w,b,e);r.feltMilli[id]=felt;
        // Core temperature: exposure outside the comfort band, insulation against cold (and a heat penalty).
        const bool fed=b.hunger>0&&b.thirst>0;
        if(felt<c.comfortLowMilli)b.coreMicro-=(c.comfortLowMilli-felt)*c.coolingMicro/1000*(1000-e.clothing.insulationMilli)/1000;
        else if(felt>c.comfortHighMilli)b.coreMicro+=(felt-c.comfortHighMilli)*c.heatingMicro/1000*(1000+e.clothing.insulationMilli)/1000;
        else{auto step=fed?c.recoveryMicro:c.recoveryMicro/2;b.coreMicro=b.coreMicro<normal?std::min(normal,b.coreMicro+step):std::max(normal,b.coreMicro-step);}
        b.coreMicro+=e.exertionMilli*c.exertionHeatMicro/1000;
        b.coreMicro=std::clamp<int64_t>(b.coreMicro,0,60000000);
        // Meters: exertion and shivering burn food, heat makes thirsty; eating/drinking from the owner.
        const bool shivering=b.coreMicro<normal-500000;
        const auto hungerRate=c.hungerPerTick*(1000+e.exertionMilli*c.exertionHungerMilli/1000+(shivering?c.shiverHungerMilli:0))/1000;
        const auto thirstRate=c.thirstPerTick*(1000+std::max<int64_t>(0,felt-c.comfortHighMilli)*c.sweatThirstMilli/1000)/1000;
        b.hunger=std::clamp<int64_t>(b.hunger-hungerRate+e.eatHunger,0,kSurvivalMeterMax);
        b.thirst=std::clamp<int64_t>(b.thirst-thirstRate+e.eatThirst,0,kSurvivalMeterMax);
        int64_t regen=e.exertionMilli>0?0:(e.resting?2*c.staminaRegen:c.staminaRegen);
        if(b.hunger<c.lowAt)regen/=2;
        if(b.thirst<c.lowAt)regen/=2;
        if(!fed)regen=0;
        const auto cap=kSurvivalMeterMax*(500+b.hunger*500/kSurvivalMeterMax)/1000;
        b.stamina=std::clamp<int64_t>(b.stamina-e.exertionMilli*c.staminaDrain/1000+regen,0,std::max<int64_t>(cap,0));
        // Condition: damage from deprivation and severe exposure, slow regeneration otherwise.
        int64_t damage=0;const auto core=b.coreMicro/1000;
        if(b.hunger==0)damage+=c.starveDamage;
        if(b.thirst==0)damage+=c.thirstDamage;
        if(core<c.severeColdMilli)damage+=c.coldDamage;
        if(core>c.severeHotMilli)damage+=c.heatDamage;
        if(damage)b.condition=std::max<int64_t>(0,b.condition-damage);
        else if(b.hunger>=c.lowAt&&b.thirst>=c.lowAt&&core>=c.coldMilli&&core<=c.hotMilli)b.condition=std::min(kSurvivalMeterMax,b.condition+c.conditionRegen);
        uint32_t st=0;
        if(core<c.coldMilli)st|=kSurvivalCold;
        if(core<c.severeColdMilli)st|=kSurvivalFreezing;
        if(core>c.hotMilli)st|=kSurvivalHot;
        if(core>c.severeHotMilli)st|=kSurvivalOverheating;
        if(b.hunger<c.lowAt)st|=kSurvivalHungry;
        if(b.hunger==0)st|=kSurvivalStarving;
        if(b.thirst<c.lowAt)st|=kSurvivalThirsty;
        if(b.thirst==0)st|=kSurvivalDehydrated;
        if(b.stamina<c.lowAt)st|=kSurvivalExhausted;
        if(b.wetness>=kSurvivalMeterMax/4)st|=kSurvivalWet;
        if(b.wetness>=kSurvivalMeterMax*3/4)st|=kSurvivalSoaked;
        for(uint32_t bit=1;bit<=kSurvivalSoaked;bit<<=1){
            if((st&bit)&&!(b.status&bit))r.events.push_back({SurvivalEventKind::StatusOn,id,bit});
            if(!(st&bit)&&(b.status&bit))r.events.push_back({SurvivalEventKind::StatusOff,id,bit});
        }
        b.status=st;
        if(b.condition==0){b.dead=true;r.events.push_back({SurvivalEventKind::Died,id,st});}
    }
    s.lastTick=tick;s.initialized=true;state=std::move(s);return r;
}
}
