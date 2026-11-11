#pragma once
#include "Animation.h"
#include "../data/Json.h"
#include <functional>
#include <memory>
namespace aiverse::render {
struct AnimationParameters;
struct IKHit { float3 point{},normal{0,1,0};float depth=0;uint64_t surface=0; };
// Queries use world coordinates. Hosts own callbacks; the solver never changes collision/game state.
struct AnimationEnvironment {
    mat4 modelToWorld;
    std::function<bool(float3 start,float3 end,IKHit&)> raycast;
    std::function<bool(float3 center,float radius,IKHit&)> sphereContact;
    // Stable world surface ID -> current local-to-world frame. False means removed.
    std::function<bool(uint64_t surface,mat4&)> surfaceTransform;
};
struct AnimationContactReport { int supports=0,collisions=0;float penetration=0,balanceError=0,supportError=0;bool supported=false,stable=false; };
struct AnimationContactState {
    struct Foot {bool planted=false;float3 anchor{},normal{0,1,0};uint64_t surface=0;};
    bool active=false;
    std::vector<Foot> feet;
    AnimationContactReport report;
    void reset(){active=false;feet.clear();report={};}
};
struct IKPositionGoal {int bone=-1;float3 offset{},target{};float weight=1;};
struct IKRotationGoal {int bone=-1;quat target;float weight=1;};
class AnimationContacts {
public:
    struct Foot {int bone=-1;float3 offset{},up{0,1,0};std::string plant;float radius=.08f,probeUp=.3f,probeDown=.6f,release=.5f,weight=20,align=.5f;};
    struct Capsule {int bone=-1,end=-1;float3 offset{};float radius=.1f;int samples=4;};
    struct Shape {std::string kind,positionParameter;float3 point{},normal{0,1,0},extent{.5f,.5f,.5f};float radius=.5f;};
    struct Mass {int bone=-1;float mass=1;float3 offset{};};
    static std::shared_ptr<const AnimationContacts> compile(const Model&,const Json&,const AnimationParameters&);
    void plant(const Model&,const AnimationParameters&,const Pose&,const std::vector<mat4>&,AnimationContactState&) const;
    void goals(const Model&,const AnimationParameters&,const Pose&,const std::vector<mat4>&,const AnimationContactState&,std::vector<IKPositionGoal>&,std::vector<IKRotationGoal>&) const;
    float3 balance(const AnimationParameters&,const std::vector<mat4>&,const AnimationContactState&,float* error=nullptr) const;
    AnimationContactReport report(const Model&,const AnimationParameters&,const Pose&,const std::vector<mat4>&,const AnimationContactState&) const;
    bool contact(const AnimationParameters&,float3 center,float radius,IKHit&) const;
    bool ground(const AnimationParameters&,float3 start,float3 end,IKHit&,bool anySurface=false) const;
    bool surfaceFrame(const AnimationParameters&,uint64_t surface,mat4&) const;
    std::vector<int> bones() const;
    const std::vector<Foot>& feet() const{return feet_;}
    const std::vector<Shape>& shapes() const{return shapes_;}
    const std::vector<Capsule>& capsules() const{return capsules_;}
    float rootAdjust=.5f,collisionWeight=30,maxCorrection=.05f;
    float3 up{0,1,0};
private:
    float maxSlope_=.7071f,balanceStrength_=0,margin_=.02f;
    bool world_=true;
    std::vector<Foot> feet_;std::vector<Capsule> capsules_;std::vector<Shape> shapes_;std::vector<Mass> masses_;
};
// Exact sphere-vs-primitive query reused by runtime host adapters and rig preview.
bool ikRayBox(float3 start,float3 end,float3 minimum,float3 maximum,const mat4& transform,IKHit&);
bool ikSphereOBB(float3 center,float radius,float3 minimum,float3 maximum,const mat4& transform,IKHit&);
bool ikSpherePlane(float3 center,float radius,float3 point,float3 normal,IKHit&);
bool ikSphereBox(float3 center,float radius,float3 minimum,float3 maximum,IKHit&);
bool ikSphereSphere(float3 center,float radius,float3 obstacle,float obstacleRadius,IKHit&);
}
