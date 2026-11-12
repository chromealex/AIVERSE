#pragma once
// Race bot policy (vehicle-controllers): follows an owner-authored racing line (closed loop of points), brakes for
// corners from the line curvature (corner speed sqrt(lateralAccel / curvature), braking distance v^2 = vc^2 + 2ad),
// overtakes slower cars ahead with a lane offset and scales its pace with a rubber-band difficulty knob.
// It only returns a genre::VehicleInput (the same struct a player produces); the owner steps the vehicle.
// Fixed math, integer ticks, no randomness.
#include "../genre/VehicleControllers.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib::bots {
inline constexpr std::size_t kRaceBotMaxPoints=4096,kRaceBotMaxOpponents=256;
// laneStep: lane offset change per tick (metres).
// Immutable racing line: points (y ignored for steering), cumulative distance, curvature (1/m) per point.
struct RaceLine{std::vector<Vec3> points;std::vector<Fixed> distance,curvature;Fixed length{};};
struct RaceBotConfig{
    Fixed lookahead=Fixed::fromInt(6),lookaheadTime=Fixed::fromRatio(35,100);   // metres + seconds * speed
    Fixed maxSpeed=Fixed::fromInt(60),cornerAccel=Fixed::fromInt(9),brakeDecel=Fixed::fromInt(8),brakeHorizon=Fixed::fromInt(90),speedMargin=1_fx;
    Fixed steerGain=Fixed::fromRatio(15,10),maxSteer=Fixed::fromRatio(1,10);    // controller maxSteer (turns)
    Fixed laneOffset=Fixed::fromRatio(25,10),laneStep=Fixed::fromRatio(5,100),overtakeDistance=Fixed::fromInt(14),laneWidth=Fixed::fromInt(3);
    Fixed carLength=Fixed::fromInt(5),carWidth=Fixed::fromInt(2),followDistance=Fixed::fromInt(10);   // traffic awareness
    Fixed handbrakeAngle{},handbrakeSpeed=Fixed::fromInt(15);                    // heading error (turns) for a handbrake turn; 0 = never
    int64_t skillMilli=1000,rubberBandMilli=0;                                   // pace scale; +/- pace from last place to leader
};
struct RaceBotState{uint32_t segment=0,lastTick=0;Fixed lane{},laneTarget{};int64_t overtaking=0;bool initialized=false;};
struct RaceBotOpponent{int64_t id=0;Vec3 position{},velocity{};};
struct RaceBotView{Vec3 position{},velocity{};Fixed yaw{};uint32_t place=1,racers=1;std::span<const RaceBotOpponent> opponents;};
struct RaceBotOutput{genre::VehicleInput input;Vec3 target{};Fixed targetSpeed{};int64_t overtaking=0;};

namespace raceBotDetail {
inline Vec2 flat(Vec3 v){return {v.x,v.z};}
inline Fixed wrap(Fixed t){return genre::vehicleDetail::wrap(t);}
inline Fixed distanceToSegment(Vec2 p,Vec2 a,Vec2 b,Fixed& t){
    auto ab=b-a;auto l=ab.lengthSq();t=l>Fixed{}?fxClamp((p-a).dot(ab)/l,Fixed{},1_fx):Fixed{};return (a+ab*t-p).length();
}
}
inline bool valid(const RaceBotConfig& c){
    using genre::vehicleDetail::vehicleNonneg;using genre::vehicleDetail::vehiclePositive;
    return vehicleNonneg(c.lookahead,Fixed::fromInt(1000))&&vehicleNonneg(c.lookaheadTime,Fixed::fromInt(10))&&vehiclePositive(c.maxSpeed,genre::kVehicleMaxSpeed)&&vehiclePositive(c.cornerAccel,Fixed::fromInt(1000))
        &&vehiclePositive(c.brakeDecel,Fixed::fromInt(1000))&&vehicleNonneg(c.brakeHorizon,Fixed::fromInt(10000))&&vehicleNonneg(c.speedMargin,Fixed::fromInt(100))&&vehicleNonneg(c.steerGain,Fixed::fromInt(100))
        &&vehiclePositive(c.maxSteer,Fixed::fromRatio(1,4))&&vehicleNonneg(c.laneOffset,Fixed::fromInt(100))&&vehicleNonneg(c.laneStep,Fixed::fromInt(10))&&vehicleNonneg(c.overtakeDistance,Fixed::fromInt(1000))
        &&vehicleNonneg(c.laneWidth,Fixed::fromInt(100))&&vehicleNonneg(c.carLength,Fixed::fromInt(100))&&vehicleNonneg(c.carWidth,Fixed::fromInt(100))&&vehicleNonneg(c.followDistance,Fixed::fromInt(1000))&&vehicleNonneg(c.handbrakeAngle,Fixed::fromRatio(1,2))&&vehicleNonneg(c.handbrakeSpeed,genre::kVehicleMaxSpeed)
        &&c.skillMilli>=100&&c.skillMilli<=2000&&c.rubberBandMilli>=0&&c.rubberBandMilli<=500;
}
inline bool valid(const RaceLine& l){return l.points.size()>=3&&l.points.size()<=kRaceBotMaxPoints&&l.distance.size()==l.points.size()&&l.curvature.size()==l.points.size()&&l.length>Fixed{};}
inline bool valid(const RaceBotState& s){return genre::vehicleDetail::vehicleNonneg(fxAbs(s.lane),Fixed::fromInt(100))&&fxAbs(s.laneTarget)<=Fixed::fromInt(100)&&s.overtaking>=0;}
// Closed loop of at least 3 distinct consecutive points; nullopt otherwise.
inline std::optional<RaceLine> makeRaceLine(std::span<const Vec3> points){
    using namespace raceBotDetail;
    if(points.size()<3||points.size()>kRaceBotMaxPoints)return {};RaceLine l;l.points.assign(points.begin(),points.end());
    const auto n=points.size();
    for(std::size_t i=0;i<n;++i){
        if(!genre::vehicleDetail::vehicleBounded(points[i],genre::kVehicleCoordLimit))return {};
        auto seg=(flat(points[(i+1)%n])-flat(points[i])).length();if(seg<=Fixed{})return {};
        l.distance.push_back(l.length);l.length+=seg;
    }
    for(std::size_t i=0;i<n;++i){
        auto a=flat(points[(i+n-1)%n]),b=flat(points[i]),c=flat(points[(i+1)%n]);
        auto d1=b-a,d2=c-b;auto turn=wrap(fxAtan2Turns(d2.x,d2.y)-fxAtan2Turns(d1.x,d1.y));
        auto span=(d1.length()+d2.length())/2;l.curvature.push_back(fxAbs(turn)*genre::kVehicleTwoPi/span);
    }
    return l;
}
// Distance along the line of the point nearest to p, searching segments from `hint` (wrapping) within `window`.
inline Fixed raceLineProgress(const RaceLine& l,Vec3 p,uint32_t& hint,std::size_t window=0){
    using namespace raceBotDetail;const auto n=l.points.size();if(!window||window>n)window=n;
    std::size_t best=hint%n;Fixed bestDistance{},bestT{};bool found=false;
    auto start=(hint+n-std::min<std::size_t>(2,n))%n;
    for(std::size_t k=0;k<window;++k){
        auto i=(start+k)%n;Fixed t;auto d=distanceToSegment(flat(p),flat(l.points[i]),flat(l.points[(i+1)%n]),t);
        if(!found||d<bestDistance){found=true;best=i;bestDistance=d;bestT=t;}
    }
    hint=uint32_t(best);auto seg=(flat(l.points[(best+1)%n])-flat(l.points[best])).length();return l.distance[best]+seg*bestT;
}
// Point at distance s along the loop plus the right-hand normal of its segment.
inline std::pair<Vec3,Vec2> raceLinePoint(const RaceLine& l,Fixed s){
    using namespace raceBotDetail;const auto n=l.points.size();
    s=Fixed::fromRaw(((s.raw%l.length.raw)+l.length.raw)%l.length.raw);
    std::size_t i=std::size_t(std::upper_bound(l.distance.begin(),l.distance.end(),s)-l.distance.begin());i=i?i-1:0;
    auto a=l.points[i],b=l.points[(i+1)%n];auto seg=(flat(b)-flat(a)).length();auto t=seg>Fixed{}?(s-l.distance[i])/seg:Fixed{};
    auto dir=flat(b)-flat(a);auto len=dir.length();Vec2 right=len>Fixed{}?Vec2{dir.y/len,-dir.x/len}:Vec2{};
    return {a+(b-a)*t,right};
}

// One decision per bot per strictly increasing tick. Returns nullopt (state untouched) for invalid input.
inline std::optional<RaceBotOutput> stepRaceBot(const RaceBotConfig& c,const RaceLine& line,RaceBotState& s,uint32_t tick,const RaceBotView& view){
    using namespace raceBotDetail;
    if(!valid(c)||!valid(line)||!valid(s)||view.opponents.size()>kRaceBotMaxOpponents||view.racers<1||view.place<1||view.place>view.racers
        ||!genre::vehicleDetail::vehicleBounded(view.position,genre::kVehicleCoordLimit)||!genre::vehicleDetail::vehicleBounded(view.velocity,genre::kVehicleMaxSpeed)||(s.initialized&&tick<=s.lastTick))return {};
    auto next=s;RaceBotOutput out;const auto n=line.points.size();
    auto progress=raceLineProgress(line,view.position,next.segment,s.initialized?std::min<std::size_t>(n,24):n);
    auto fwd=genre::vehicleDetail::forward(view.yaw);auto speed=flat(view.velocity).dot(fwd);
    // Pace: skill scale, rubber band (+band for last place, -band for the leader).
    int64_t pace=c.skillMilli;
    if(view.racers>1&&c.rubberBandMilli)pace=pace*(1000+c.rubberBandMilli*(2*int64_t(view.place-1)-int64_t(view.racers-1))/int64_t(view.racers-1))/1000;
    auto paceScale=Fixed::fromRatio(pace,1000);
    // Target speed: minimum over the corners within the braking horizon. Pace scales the straight-line speed; it
    // only ever lowers the cornering limit (a rubber-band boost never pushes a car past its authored grip).
    auto target=c.maxSpeed*paceScale;auto cornerAccel=c.cornerAccel*fxMin(paceScale,1_fx);
    for(std::size_t k=1;k<=n;++k){
        auto i=(next.segment+k)%n;auto along=line.distance[i]-progress;if(along<Fixed{})along+=line.length;if(along>c.brakeHorizon)break;
        auto kappa=line.curvature[i];if(kappa<=Fixed::fromRatio(1,10000))continue;
        auto corner=fxSqrt(cornerAccel/kappa);auto allowed=fxSqrt(corner*corner+c.brakeDecel*along*2);target=fxMin(target,allowed);
    }
    // Traffic: an opponent alongside (|ahead| <= carLength, inside the lane band) pushes us to the other lane; the
    // nearest slower opponent ahead inside overtakeDistance is passed on the side away from it; an opponent right in
    // front (|side| < carWidth) caps the target speed so the bot follows instead of ramming it.
    int64_t blocker=0,beside=0;Fixed blockerAhead{},blockerSide{},besideSide{},besideAhead{};auto right=genre::vehicleDetail::rightOf(fwd);
    for(const auto& o:view.opponents){
        auto d=flat(o.position)-flat(view.position);auto ahead=d.dot(fwd),side=d.dot(right);auto theirs=flat(o.velocity).dot(fwd);
        if(fxAbs(side)>c.laneWidth)continue;
        if(fxAbs(ahead)<=c.carLength){if(!beside||fxAbs(ahead)<besideAhead||(fxAbs(ahead)==besideAhead&&o.id<beside)){beside=o.id;besideAhead=fxAbs(ahead);besideSide=side;}continue;}
        if(ahead<=Fixed{}||ahead>c.overtakeDistance)continue;
        if(fxAbs(side)<c.carWidth&&ahead<=c.followDistance)target=fxMin(target,fxMax(Fixed{},theirs+(ahead-c.carLength)/2));
        if(theirs>=speed+Fixed::fromRatio(1,2))continue;
        if(!blocker||ahead<blockerAhead||(ahead==blockerAhead&&o.id<blocker)){blocker=o.id;blockerAhead=ahead;blockerSide=side;}
    }
    if(beside){next.laneTarget=besideSide>=Fixed{}? -c.laneOffset:c.laneOffset;next.overtaking=0;}
    else if(blocker){if(blocker!=next.overtaking)next.laneTarget=blockerSide>=Fixed{}? -c.laneOffset:c.laneOffset;next.overtaking=blocker;}
    else{next.overtaking=0;next.laneTarget={};}
    next.lane=genre::vehicleDetail::toward(next.lane,next.laneTarget,c.laneStep);
    auto [point,normal]=raceLinePoint(line,progress+c.lookahead+fxMax(speed,Fixed{})*c.lookaheadTime);
    out.target={point.x+normal.x*next.lane,point.y,point.z+normal.y*next.lane};
    auto desired=fxAtan2Turns(out.target.x-view.position.x,out.target.z-view.position.z);
    auto error=wrap(desired-view.yaw);
    out.input.steer=fxClamp(error*c.steerGain/c.maxSteer,-1_fx,1_fx);
    if(speed<target-c.speedMargin){out.input.throttle=1_fx;}
    else if(speed>target+c.speedMargin){out.input.brake=fxClamp((speed-target)/(c.speedMargin*4+1_fx),Fixed::fromRatio(1,4),1_fx);}
    else out.input.throttle=Fixed::fromRatio(1,2);
    out.input.handbrake=c.handbrakeAngle>Fixed{}&&fxAbs(error)>=c.handbrakeAngle&&speed>=c.handbrakeSpeed;
    out.targetSpeed=target;out.overtaking=next.overtaking;
    next.lastTick=tick;next.initialized=true;s=next;return out;
}
}
