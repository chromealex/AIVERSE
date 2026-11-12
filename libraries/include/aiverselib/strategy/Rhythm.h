#pragma once
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace aiverse::lib::strategy {
inline constexpr int64_t kRhythmHorizon=86400000000LL; // 24 hours, integer microseconds
struct TempoPoint {int64_t pulse=0,usPerQuarter=500000;};
struct TempoMap {int64_t ppq=480;std::vector<TempoPoint> points;};
// Each segment contributes exact numerator units; rounding occurs once at the end.
inline std::optional<int64_t> pulseTime(const TempoMap& map,int64_t pulse) {
    if(pulse<0||pulse>1000000000||map.ppq<1||map.ppq>96000||map.points.empty()||map.points.size()>4096||map.points.front().pulse!=0)return {};
    int64_t numerator=0,previous=-1;
    for(size_t i=0;i<map.points.size();++i){
        auto point=map.points[i];
        if(point.pulse<=previous||point.pulse>1000000000||point.usPerQuarter<1||point.usPerQuarter>60000000)return {};
        previous=point.pulse;
        int64_t end=i+1<map.points.size()?std::min(pulse,map.points[i+1].pulse):pulse;
        if(end<point.pulse)continue;
        int64_t add=(end-point.pulse)*point.usPerQuarter;
        if(add>kRhythmHorizon*map.ppq-numerator)return {};
        numerator+=add;
    }
    return numerator/map.ppq;
}
struct RhythmNote {uint32_t lane=0;int64_t start=0,end=0;}; // end==start: tap
struct RhythmChart {uint32_t lanes=4;int64_t perfect=25000,good=80000;std::vector<RhythmNote> notes;};
enum class Judgement:uint8_t {Pending,Holding,Perfect,Good,Miss};
struct RhythmState {
    int64_t time=-1000000,calibration=0;
    uint64_t lastCommand=0,score=0;
    uint32_t combo=0,maxCombo=0;
    std::vector<uint8_t> held;
    std::vector<Judgement> results,heads;
    bool operator==(const RhythmState&)const=default;
};
inline bool valid(const RhythmChart& c) {
    if(!c.lanes||c.lanes>32||c.perfect<0||c.good<c.perfect||c.good>1000000||c.notes.size()>100000)return false;
    std::vector<int64_t> ends(c.lanes,-1);int64_t previous=-1;
    for(const auto& n:c.notes){
        if(n.lane>=c.lanes||n.start<0||n.start<previous||n.end<n.start||n.end>kRhythmHorizon||n.start<=ends[n.lane])return false;
        ends[n.lane]=n.end;previous=n.start;
    }
    return true;
}
inline bool valid(const RhythmState& s) {
    if(s.time< -1000000||s.time>kRhythmHorizon+1000001||s.calibration< -1000000||s.calibration>1000000||s.held.empty()||s.held.size()>32||s.results.size()>100000||s.heads.size()!=s.results.size()||s.combo>s.maxCombo||s.maxCombo>s.results.size()||s.score>s.results.size()*1000ULL)return false;
    for(auto v:s.held)if(v>1)return false;
    uint64_t score=0;
    for(size_t i=0;i<s.results.size();++i){auto result=s.results[i],head=s.heads[i];
        if(result>Judgement::Miss||head>Judgement::Miss)return false;
        if(result==Judgement::Holding&&head!=Judgement::Perfect&&head!=Judgement::Good)return false;
        if(result==Judgement::Perfect)score+=1000;
        if(result==Judgement::Good)score+=500;
    }
    return score==s.score;
}
inline bool compatible(const RhythmChart& c,const RhythmState& s) {
    if(!valid(c)||!valid(s)||c.lanes!=s.held.size()||c.notes.size()!=s.results.size())return false;
    std::vector<uint8_t> holding(c.lanes);
    for(size_t i=0;i<c.notes.size();++i)if(s.results[i]==Judgement::Holding){auto lane=c.notes[i].lane;if(!s.held[lane]||holding[lane]||c.notes[i].start==c.notes[i].end)return false;holding[lane]=1;}
    return true;
}
inline std::optional<RhythmState> beginRhythm(const RhythmChart& c,int64_t calibration=0) {
    if(!valid(c)||calibration< -1000000||calibration>1000000)return {};
    RhythmState s;s.calibration=calibration;s.held.resize(c.lanes);s.results.resize(c.notes.size());s.heads.resize(c.notes.size());return s;
}
namespace rhythm_detail {
inline void finish(RhythmState& s,size_t i,Judgement result){
    s.results[i]=result;
    if(result==Judgement::Miss)s.combo=0;
    else{s.score+=result==Judgement::Perfect?1000:500;++s.combo;s.maxCombo=std::max(s.maxCombo,s.combo);}
}
// Process deadline events in timestamp/index order; batching must not change combo history.
inline void advance(const RhythmChart& c,RhythmState& s,int64_t now){
    struct Event {int64_t time;size_t note;};std::vector<Event> events;
    for(size_t i=0;i<c.notes.size();++i){
        if(s.results[i]==Judgement::Pending&&c.notes[i].start+c.good<now)events.push_back({c.notes[i].start+c.good+1,i});
        else if(s.results[i]==Judgement::Holding&&c.notes[i].end<=now)events.push_back({c.notes[i].end,i});
    }
    std::sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.time!=b.time?a.time<b.time:a.note<b.note;});
    for(auto e:events)finish(s,e.note,s.results[e.note]==Judgement::Holding?s.heads[e.note]:Judgement::Miss);
    s.time=now;
}
}
inline bool advanceRhythm(const RhythmChart& c,RhythmState& s,int64_t now) {
    if(!compatible(c,s)||now<s.time||now>kRhythmHorizon+1000001)return false;
    rhythm_detail::advance(c,s,now);return true;
}
// Caller supplies validated audio-clock timestamps. Apply calibration once; submit inputs
// before advancing beyond their timestamps. Late/repeated/out-of-order inputs are rejected.
inline bool rhythmInput(const RhythmChart& c,RhythmState& s,uint32_t lane,bool down,int64_t timestamp,uint64_t sequence) {
    if(!compatible(c,s)||lane>=c.lanes||sequence<=s.lastCommand||timestamp<0||timestamp>kRhythmHorizon||bool(s.held[lane])==down)return false;
    int64_t now=timestamp-s.calibration;if(now<s.time||now>kRhythmHorizon+1000001)return false;
    auto out=s;rhythm_detail::advance(c,out,now);out.lastCommand=sequence;out.held[lane]=down?1:0;
    if(down){
        size_t chosen=c.notes.size();int64_t distance=c.good+1;
        for(size_t i=0;i<c.notes.size();++i)if(c.notes[i].lane==lane&&out.results[i]==Judgement::Pending){
            auto delta=now-c.notes[i].start;auto abs=delta<0?-delta:delta;
            if(abs<distance){chosen=i;distance=abs;}
        }
        if(chosen<c.notes.size()){
            auto result=distance<=c.perfect?Judgement::Perfect:Judgement::Good;out.heads[chosen]=result;
            if(c.notes[chosen].end>c.notes[chosen].start)out.results[chosen]=Judgement::Holding;
            else rhythm_detail::finish(out,chosen,result);
        }
    }else{
        for(size_t i=0;i<c.notes.size();++i)if(c.notes[i].lane==lane&&out.results[i]==Judgement::Holding){
            rhythm_detail::finish(out,i,now>=c.notes[i].end-c.good?out.heads[i]:Judgement::Miss);break;
        }
    }
    s=std::move(out);return true;
}
}
