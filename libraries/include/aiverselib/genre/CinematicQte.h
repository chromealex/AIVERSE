#pragma once
// Quick-time events judged on authoritative input ticks (never on device/audio time). Kinds: Press (a timing
// window [open, close] in ticks after the prompt, optional perfect sub-window; an early press fails), Mash
// (count presses before close), Hold (press before close, keep it down for `hold` ticks) and Sequence (ordered
// buttons, each with its own window counted from the previous success). Difficulty and accessibility scale the
// windows/counts/hold symmetrically in integer milli; autoSucceed resolves at the prompt. Results are authoritative.
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr uint32_t kQteMaxTicks=100000,kQteMaxCount=1000;
inline constexpr std::size_t kQteMaxSteps=32;
inline constexpr int64_t kQteMinScale=100,kQteMaxScale=10000;
enum class QteKind:uint8_t {Press,Mash,Hold,Sequence};
enum class QteOutcome:uint8_t {None,Success,Perfect,Fail,AutoSuccess,Skipped};
inline bool qteSucceeded(QteOutcome o){return o==QteOutcome::Success||o==QteOutcome::Perfect||o==QteOutcome::AutoSuccess;}
struct QteStep {int64_t button=0;uint32_t window=30;};
// at: scene position (domain steps) where the prompt holds the timeline. onSuccess/onFail: sync point id to branch
// to (0 = continue after the QTE); failAborts ends the scene on failure instead.
struct QteDef {
    int64_t id=0;uint32_t at=0;QteKind kind=QteKind::Press;int64_t button=0;
    uint32_t open=1,close=30,perfectOpen=0,perfectClose=0,count=1,hold=0;std::vector<QteStep> steps;
    bool strict=false;int64_t onSuccess=0,onFail=0;bool failAborts=false;
};
// windowScaleMilli: difficulty (750 hard, 1000 normal, 1500 easy); accessibleWindowMilli: accessibility extension
// multiplied on top (1000 = off, 2000 = double windows); mash/hold scale required count / hold length.
struct QteOptions {int64_t windowScaleMilli=1000,accessibleWindowMilli=1000,mashScaleMilli=1000,holdScaleMilli=1000;bool autoSucceed=false;};
struct QteInput {uint32_t tick=0;int64_t button=0;bool down=true;}; // authoritative input tick of the press/release
struct QteProgress {
    int64_t qte=0;uint32_t start=0,base=0,open=0,close=0,perfectOpen=0,perfectClose=0,count=0,hold=0,step=0,pressed=0,heldAt=0;
    int64_t windowMilli=1000;bool holding=false;
};
inline bool valid(const QteOptions& o){
    auto in=[](int64_t v,int64_t lo){return v>=lo&&v<=kQteMaxScale;};
    return in(o.windowScaleMilli,kQteMinScale)&&in(o.accessibleWindowMilli,1000)&&in(o.mashScaleMilli,kQteMinScale)&&in(o.holdScaleMilli,kQteMinScale);
}
// Empty when valid; otherwise "field: reason" (callers prefix the path).
inline std::string qteError(const QteDef& q){
    if(q.id<=0)return "id: must be positive";
    if(uint8_t(q.kind)>uint8_t(QteKind::Sequence))return "kind: unknown";
    if(q.onSuccess<0||q.onFail<0)return "onSuccess: branch targets must be 0 or sync ids";
    if(q.kind!=QteKind::Sequence&&q.button<=0)return "button: must be positive";
    if(q.close<1||q.close>kQteMaxTicks)return "close: must be 1..100000 ticks";
    switch(q.kind){
    case QteKind::Press:
        if(q.open<1||q.open>q.close)return "open: must be 1..close";
        if((q.perfectOpen||q.perfectClose)&&(q.perfectOpen<q.open||q.perfectOpen>q.perfectClose||q.perfectClose>q.close))return "perfect: must lie inside [open, close]";
        break;
    case QteKind::Mash:if(q.count<1||q.count>kQteMaxCount)return "count: must be 1..1000";break;
    case QteKind::Hold:if(q.hold<1||q.hold>kQteMaxTicks)return "hold: must be 1..100000 ticks";break;
    case QteKind::Sequence:
        if(q.steps.empty()||q.steps.size()>kQteMaxSteps)return "steps: expected 1..32 steps";
        for(auto st:q.steps)if(st.button<=0||st.window<1||st.window>kQteMaxTicks)return "steps: buttons positive, windows 1..100000";
        break;
    }
    return {};
}
inline bool valid(const QteDef& q){return qteError(q).empty();}
// Scale an inclusive window [open, close] around its centre; the result stays >= 1 tick wide and opens at >= 1.
inline std::pair<uint32_t,uint32_t> scaleQteWindow(uint32_t open,uint32_t close,int64_t milli){
    int64_t span=int64_t(close)-open+1,scaled=std::max<int64_t>(1,span*milli/1000),twice=int64_t(open)*2+span-1;
    int64_t from=std::max<int64_t>(1,(twice-(scaled-1))/2);return {uint32_t(from),uint32_t(std::min<int64_t>(from+scaled-1,kQteMaxTicks*kQteMaxScale/1000))};
}
inline int64_t qteWindowMilli(const QteOptions& o){return o.windowScaleMilli*o.accessibleWindowMilli/1000;}
inline QteProgress startQte(const QteDef& q,const QteOptions& o,uint32_t tick){
    QteProgress p;p.qte=q.id;p.start=p.base=tick;p.windowMilli=qteWindowMilli(o);
    if(q.kind==QteKind::Press){
        std::tie(p.open,p.close)=scaleQteWindow(q.open,q.close,p.windowMilli);
        if(q.perfectClose){std::tie(p.perfectOpen,p.perfectClose)=scaleQteWindow(q.perfectOpen,q.perfectClose,p.windowMilli);p.perfectOpen=std::max(p.perfectOpen,p.open);p.perfectClose=std::min(p.perfectClose,p.close);}
    }else if(q.kind==QteKind::Sequence)p.close=scaleQteWindow(1,q.steps[0].window,p.windowMilli).second;
    else p.close=scaleQteWindow(1,q.close,p.windowMilli).second;
    p.count=uint32_t(std::max<int64_t>(1,(int64_t(q.count)*o.mashScaleMilli+999)/1000));
    p.hold=uint32_t(std::max<int64_t>(1,int64_t(q.hold)*o.holdScaleMilli/1000));
    return p;
}
// Pending judgement: inputs must be sorted by tick; only ticks in (start, tick] count. Returns None while open.
inline QteOutcome judgeQte(const QteDef& q,QteProgress& p,std::span<const QteInput> inputs,uint32_t tick){
    for(const auto& in:inputs){
        if(in.tick<=p.start||in.tick>tick)continue;
        if(q.kind==QteKind::Hold&&p.holding&&in.tick>=p.heldAt+p.hold)return QteOutcome::Success;
        auto offset=in.tick-p.base;
        switch(q.kind){
        case QteKind::Press:
            if(!in.down)break;
            if(in.button!=q.button){if(q.strict)return QteOutcome::Fail;break;}
            if(offset<p.open||offset>p.close)return QteOutcome::Fail;
            return p.perfectClose&&offset>=p.perfectOpen&&offset<=p.perfectClose?QteOutcome::Perfect:QteOutcome::Success;
        case QteKind::Mash:
            if(!in.down)break;
            if(in.button!=q.button){if(q.strict)return QteOutcome::Fail;break;}
            if(offset>p.close)return QteOutcome::Fail;
            if(++p.pressed>=p.count)return QteOutcome::Success;
            break;
        case QteKind::Hold:
            if(in.button!=q.button){if(q.strict&&in.down)return QteOutcome::Fail;break;}
            if(in.down&&!p.holding){if(offset>p.close)return QteOutcome::Fail;p.holding=true;p.heldAt=in.tick;}
            else if(!in.down&&p.holding)return QteOutcome::Fail; // released before the hold completed
            break;
        case QteKind::Sequence:{
            if(!in.down)break;
            const auto& step=q.steps[p.step];
            if(in.button!=step.button){if(q.strict)return QteOutcome::Fail;break;}
            if(offset>p.close)return QteOutcome::Fail;
            if(++p.step==q.steps.size())return QteOutcome::Success;
            p.base=in.tick;p.close=scaleQteWindow(1,q.steps[p.step].window,p.windowMilli).second;break;}
        }
    }
    if(q.kind==QteKind::Hold&&p.holding)return tick>=p.heldAt+p.hold?QteOutcome::Success:QteOutcome::None;
    return tick>=p.base+p.close?QteOutcome::Fail:QteOutcome::None;
}
// Pausing shifts every tick-anchored field so a paused prompt keeps its remaining window.
inline void shiftQte(QteProgress& p,uint32_t delta){p.start+=delta;p.base+=delta;if(p.holding)p.heldAt+=delta;}
inline bool valid(const QteProgress& p){
    return p.qte>=0&&p.base>=p.start&&p.windowMilli>=kQteMinScale&&p.windowMilli<=kQteMaxScale*kQteMaxScale/1000&&p.step<=kQteMaxSteps&&p.count<=kQteMaxCount*kQteMaxScale/1000+1&&p.pressed<=p.count&&(!p.holding||p.heldAt>p.start);
}
}
