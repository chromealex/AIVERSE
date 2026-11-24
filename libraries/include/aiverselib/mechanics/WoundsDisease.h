#pragma once
#include "engine/mechanics/Namespace.h"
// wounds-disease: injuries and illnesses for survival-ecology bodies. Wounds of data types (cut, burn, bite, fracture...)
// on body parts with severity, bleeding, pain, natural healing, infection risk while untreated and treatments that stop
// bleeding / allow healing / prevent infection (bandage, splint, disinfect as bits). Diseases with incubation, a sick
// phase with symptom penalties and damage, recovery with immunity, cures, and contagion between bodies the owner reports
// as in contact this tick. Integer math in milli units; per-body seeded rolls derived from (seed, tick, body, id), so
// results do not depend on iteration order. Outputs: blood loss, pain, speed/work penalty, death and events.
// Survival.h's body model stays the owner of temperature and food; feed `bloodMilli`/`painMilli` into it. Record kind 208.
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
enum TreatmentBits:uint32_t {TreatBandage=1,TreatSplint=2,TreatDisinfect=4,TreatSurgery=8};
struct WoundTypeDef {int64_t id=0;int64_t bleedPerTickMilli=0,painPerSeverity=1;uint32_t healTicksPerSeverity=60;uint32_t stopsBleeding=TreatBandage,needsToHeal=0;
    int64_t infectionPerTickMicro=0;uint32_t preventsInfection=TreatDisinfect;int64_t disease=0;};   // infection starts `disease` (0 = none)
struct DiseaseDef {int64_t id=0;uint32_t incubationTicks=600,sickTicks=3000,immunityTicks=30000;int64_t contagionPerContactMicro=0,damagePerTickMilli=0,
    penaltyMilli=300,painMilli=200;uint32_t curedBy=0;};
struct HealthData {std::map<int64_t,WoundTypeDef> wounds;std::map<int64_t,DiseaseDef> diseases;uint64_t seed=1;};
struct Wound {int64_t type=0,part=0;int64_t severity=100;uint32_t treated=0,since=0;bool bleeding=true,infected=false;};   // severity 0..1000
enum class DiseasePhase:uint8_t {Incubating,Sick};
struct DiseaseCase {DiseasePhase phase=DiseasePhase::Incubating;uint32_t since=0;};
struct BodyHealth {std::map<int64_t,Wound> wounds;std::map<int64_t,DiseaseCase> diseases;std::map<int64_t,uint32_t> immuneUntil;int64_t bloodMilli=1000000,nextWound=1;bool dead=false;};
struct HealthState {std::map<int64_t,BodyHealth> bodies;uint32_t lastTick=0;bool initialized=false;};
struct NewWound {int64_t body=0,type=0,part=0,severity=100;};
struct Treatment {int64_t body=0,wound=0;uint32_t bits=0;int64_t cure=0;};   // wound 0 = all wounds; cure: disease id cured when bits match curedBy
struct Contact {int64_t a=0,b=0;};
enum class HealthEventKind:uint8_t {Wounded,Healed,Infected,Contracted,FellSick,Recovered,Cured,Died};
struct HealthEvent {HealthEventKind kind{};int64_t body=0,id=0;};
struct BodyStatus {int64_t body=0,bloodMilli=0,painMilli=0,penaltyMilli=0;bool bleeding=false,dead=false;};
struct HealthReport {std::vector<BodyStatus> bodies;std::vector<HealthEvent> events;};

inline bool valid(const HealthData& d){for(auto& [id,w]:d.wounds)if(id!=w.id||id<=0||w.bleedPerTickMilli<0||w.infectionPerTickMicro<0||w.infectionPerTickMicro>1000000||(w.disease&&!d.diseases.contains(w.disease)))return false;
    for(auto& [id,x]:d.diseases)if(id!=x.id||id<=0||x.sickTicks==0||x.contagionPerContactMicro<0||x.contagionPerContactMicro>1000000||x.penaltyMilli<0||x.penaltyMilli>1000)return false;return true;}
inline bool roll(const HealthData& d,uint32_t tick,int64_t body,int64_t what,int64_t chanceMicro){
    if(chanceMicro<=0)return false;Hasher h;h.u64(d.seed);h.u32(tick);h.i64(body);h.i64(what);return int64_t(h.value()%1000000)<chanceMicro;}
inline void contract(const HealthData& d,BodyHealth& b,int64_t body,int64_t disease,uint32_t tick,std::vector<HealthEvent>& ev){
    if(b.dead||b.diseases.contains(disease))return;auto im=b.immuneUntil.find(disease);if(im!=b.immuneUntil.end()&&tick<im->second)return;
    b.diseases[disease]={DiseasePhase::Incubating,tick};ev.push_back({HealthEventKind::Contracted,body,disease});(void)d;}

inline std::optional<HealthReport> stepHealth(const HealthData& d,HealthState& state,uint32_t tick,std::span<const NewWound> wounds,std::span<const Treatment> treatments,
    std::span<const Contact> contacts){
    if(!valid(d)||!policyTickOk(state.initialized,state.lastTick,tick)||wounds.size()>kPolicyMaxCommands||treatments.size()>kPolicyMaxCommands||contacts.size()>kPolicyMaxObjects*4)return std::nullopt;
    for(auto& w:wounds)if(w.body<=0||!d.wounds.contains(w.type)||w.severity<1||w.severity>1000)return std::nullopt;
    for(auto& t:treatments)if(!state.bodies.contains(t.body)||(t.cure&&!d.diseases.contains(t.cure)))return std::nullopt;
    for(auto& c:contacts)if(c.a<=0||c.b<=0||c.a==c.b)return std::nullopt;
    auto s=state;s.initialized=true;s.lastTick=tick;HealthReport r;auto& ev=r.events;
    for(auto& w:wounds){auto& b=s.bodies[w.body];if(b.dead)continue;auto id=b.nextWound++;b.wounds[id]={w.type,w.part,w.severity,0,tick,d.wounds.at(w.type).bleedPerTickMilli>0,false};ev.push_back({HealthEventKind::Wounded,w.body,id});}
    for(auto& t:treatments){auto& b=s.bodies.at(t.body);
        for(auto& [id,w]:b.wounds)if(!t.wound||t.wound==id){w.treated|=t.bits;if(w.treated&d.wounds.at(w.type).stopsBleeding)w.bleeding=false;}
        if(t.cure){auto& dd=d.diseases.at(t.cure);if(b.diseases.contains(t.cure)&&dd.curedBy&&(t.bits&dd.curedBy)==dd.curedBy){b.diseases.erase(t.cure);b.immuneUntil[t.cure]=tick+dd.immunityTicks;ev.push_back({HealthEventKind::Cured,t.body,t.cure});}}}
    // Contagion: decide from the bodies' state at the start of the tick (order independent).
    std::vector<std::pair<int64_t,int64_t>> infect;
    auto sickWith=[&](int64_t body){std::vector<int64_t> v;auto it=s.bodies.find(body);if(it==s.bodies.end()||it->second.dead)return v;for(auto& [id,c]:it->second.diseases)if(c.phase==DiseasePhase::Sick)v.push_back(id);return v;};
    for(auto& c:contacts){for(int k=0;k<2;++k){int64_t from=k?c.b:c.a,to=k?c.a:c.b;
        for(auto dis:sickWith(from))if(roll(d,tick,to,dis*1000003+from,d.diseases.at(dis).contagionPerContactMicro))infect.push_back({to,dis});}}
    std::sort(infect.begin(),infect.end());infect.erase(std::unique(infect.begin(),infect.end()),infect.end());
    for(auto [body,dis]:infect)contract(d,s.bodies[body],body,dis,tick,ev);
    for(auto& [id,b]:s.bodies){
        BodyStatus st{id};if(b.dead){st.dead=true;r.bodies.push_back(st);continue;}
        std::vector<int64_t> healed;
        for(auto& [wid,w]:b.wounds){const auto& def=d.wounds.at(w.type);
            if(w.bleeding){b.bloodMilli-=def.bleedPerTickMilli*w.severity/100;st.bleeding=true;}
            st.painMilli+=def.painPerSeverity*w.severity;
            if(!w.infected&&!(w.treated&def.preventsInfection)&&roll(d,tick,id,wid,def.infectionPerTickMicro)){w.infected=true;ev.push_back({HealthEventKind::Infected,id,wid});
                if(def.disease)contract(d,b,id,def.disease,tick,ev);}
            bool canHeal=!w.bleeding&&(w.treated&def.needsToHeal)==def.needsToHeal&&!w.infected;
            if(canHeal&&def.healTicksPerSeverity&&(tick-w.since)%def.healTicksPerSeverity==0&&tick!=w.since){if(--w.severity<=0)healed.push_back(wid);}
        }
        for(auto wid:healed){b.wounds.erase(wid);ev.push_back({HealthEventKind::Healed,id,wid});}
        std::vector<int64_t> recovered;
        for(auto& [dis,c]:b.diseases){const auto& def=d.diseases.at(dis);
            if(c.phase==DiseasePhase::Incubating&&tick-c.since>=def.incubationTicks){c.phase=DiseasePhase::Sick;c.since=tick;ev.push_back({HealthEventKind::FellSick,id,dis});}
            if(c.phase==DiseasePhase::Sick){b.bloodMilli-=def.damagePerTickMilli;st.penaltyMilli+=def.penaltyMilli;st.painMilli+=def.painMilli;if(tick-c.since>=def.sickTicks)recovered.push_back(dis);}}
        for(auto dis:recovered){b.diseases.erase(dis);b.immuneUntil[dis]=tick+d.diseases.at(dis).immunityTicks;ev.push_back({HealthEventKind::Recovered,id,dis});}
        std::erase_if(b.immuneUntil,[&](auto& e){return e.second<=tick;});
        st.painMilli=std::min<int64_t>(st.painMilli,1000);st.penaltyMilli=std::min<int64_t>(1000,st.penaltyMilli+st.painMilli/2);
        if(b.bloodMilli<=0){b.bloodMilli=0;b.dead=true;st.dead=true;ev.push_back({HealthEventKind::Died,id,0});}
        st.bloodMilli=b.bloodMilli;r.bodies.push_back(st);
    }
    state=std::move(s);return r;
}
inline void io(Archive& a,Wound& w){a(w.type,w.part,w.severity,w.treated,w.since,w.bleeding,w.infected);}
inline void io(Archive& a,DiseaseCase& c){a(c.phase,c.since);}
inline void io(Archive& a,BodyHealth& b){a(b.wounds,b.diseases,b.immuneUntil,b.bloodMilli,b.nextWound,b.dead);}
inline void io(Archive& a,HealthState& s){a(s.bodies,s.lastTick,s.initialized);}
inline uint16_t stateKind(const HealthState&){return 208;}
inline bool valid(const HealthState& s){if(!policyPositiveKeys(s.bodies))return false;for(auto& [id,b]:s.bodies){if(b.bloodMilli<0||b.nextWound<1)return false;
    for(auto& [w,x]:b.wounds)if(w<=0||w>=b.nextWound||x.severity<=0||x.severity>1000)return false;for(auto& [k,c]:b.diseases)if(k<=0||uint8_t(c.phase)>1)return false;}return true;}
}
