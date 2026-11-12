#pragma once
#include "AnimationContacts.h"
namespace aiverse::render {
struct SurfaceLocomotionReport {
    bool active=false,attached=false;
    int supports=0,stepping=0;
    float footError=0,penetration=0;
    float3 position{},normal{0,1,0};
};
struct SurfaceLocomotionState {
    struct Leg {bool planted=false,swing=false;float elapsed=0;float3 anchor{},normal{0,1,0},start{},target{},targetNormal{0,1,0};uint64_t surface=0;bool framed=false,targetFramed=false;float3 localAnchor{},localNormal{0,1,0},localTarget{},localTargetNormal{0,1,0};};
    bool initialized=false;float resetToken=0;
    float3 position{},normal{0,1,0};quat rotation;
    mat4 host,supportFrame;uint64_t support=0;bool supportFramed=false;
    size_t next=0;
    std::vector<Leg> legs;
    SurfaceLocomotionReport report;
    void reset(){*this={};}
};
// Presentation motor and gait planner. Hosts retain gameplay authority.
class FullBodyIK;
class SurfaceLocomotion {
public:
    struct Leg {int hip=-1,knee=-1,foot=-1;float3 home{},pole{};float reach=0;};
    static std::shared_ptr<const SurfaceLocomotion> compile(const Model&,const Json&,const AnimationParameters&);
    void solve(const Model&,const AnimationParameters&,Pose&,float dt,SurfaceLocomotionState&,float weight=1) const;
    const std::vector<Leg>& legs()const{return legs_;}
    const AnimationContacts& surfaces()const{return *surfaces_;}
private:
    std::shared_ptr<const FullBodyIK> collisionSolver_;
    bool followSurface_=true;
    int root_=0,minSupport_=3,maxSwing_=1;
    float clearance_=.5f,probe_=.8f,stepDistance_=.3f,stepHeight_=.15f,stepDuration_=.25f,turnSpeed_=4,maxSpeed_=2,bodyRadius_=.2f;
    std::string velocity_,adhesion_,reset_;
    std::vector<Leg> legs_;
    std::shared_ptr<const AnimationContacts> surfaces_;
};
}
