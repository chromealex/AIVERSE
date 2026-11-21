// Ragdoll authoring from semantic humanoid bone links.
// Produces the physics::World scene JSON and an inverse-bind pose bridge.
// Physics simulation remains an optional host module (Box3D).
#pragma once
#include "HumanoidRetarget.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace aiverse::lib::animation {
struct RagdollBuildOptions {
    float radiusRatio=.17f,density=750.f,friction=.65f;
    float jointLimit=.85f;
    int collisionGroup=-1; // unique negative id per ragdoll disables self-collisions
    uint32_t tickRate=60;
    uint32_t substeps=4;
};
struct RagdollBodyPose {
    std::string id;
    render::float3 position{};
    render::quat rotation{};
};
inline render::quat axisToY(render::float3 direction) {
    const auto n=render::normalize(direction);
    const render::float3 y{0,1,0};
    const float d=std::clamp(render::dot(y,n),-1.f,1.f);
    if (d>.99999f) return {};
    if (d<-.99999f) return render::quat::axisAngle({1,0,0},3.1415926535f);
    const auto axis=render::normalize(render::cross(y,n));
    return render::quat::axisAngle(axis,std::acos(d));
}
class RagdollAsset {
public:
    struct Binding {
        std::string id;
        HumanoidBone role;
        int bone=-1;
        render::mat4 bodyToBone; // body rigid world -> bind bone rigid world
    };
    static RagdollAsset compile(const HumanoidAvatar& avatar,
        render::mat4 modelToWorld={},RagdollBuildOptions opt={}) {
        if(!avatar.model || !std::isfinite(opt.radiusRatio) ||
           opt.radiusRatio<.05f || opt.radiusRatio>.5f ||
           opt.tickRate<10 || opt.tickRate>240 || opt.substeps<1 || opt.substeps>16 ||
           !std::isfinite(opt.density) || opt.density<=0 ||
           !std::isfinite(opt.friction) || opt.friction<0 || opt.friction>2 ||
           !std::isfinite(opt.jointLimit) || opt.jointLimit<=0 || opt.jointLimit>3.1f ||
           opt.collisionGroup < -32767 || opt.collisionGroup > 32767)
            throw std::invalid_argument("ragdoll: invalid authoring settings");
        RagdollAsset r;r.model_ = avatar.model;r.modelToWorld_=modelToWorld;
        render::Pose bind;render::bindPose(*avatar.model,bind);
        std::vector<render::mat4> boneWorld;
        render::worldMatrices(*avatar.model,bind,boneWorld);
        const auto at=[&](HumanoidBone role) {
            int node=avatar.node(role);
            const auto& m=boneWorld[size_t(node)];
            return modelToWorld.transformPoint({m.at(0,3),m.at(1,3),m.at(2,3)});
        };
        struct Desc {HumanoidBone role,end,parent;bool sphere;};
        constexpr Desc desc[]={
            {HumanoidBone::Hips,HumanoidBone::Chest,HumanoidBone::Hips,false},
            {HumanoidBone::Chest,HumanoidBone::Neck,HumanoidBone::Hips,false},
            {HumanoidBone::Head,HumanoidBone::Head,HumanoidBone::Chest,true},
            {HumanoidBone::LeftUpperArm,HumanoidBone::LeftLowerArm,HumanoidBone::Chest,false},
            {HumanoidBone::LeftLowerArm,HumanoidBone::LeftHand,HumanoidBone::LeftUpperArm,false},
            {HumanoidBone::RightUpperArm,HumanoidBone::RightLowerArm,HumanoidBone::Chest,false},
            {HumanoidBone::RightLowerArm,HumanoidBone::RightHand,HumanoidBone::RightUpperArm,false},
            {HumanoidBone::LeftUpperLeg,HumanoidBone::LeftLowerLeg,HumanoidBone::Hips,false},
            {HumanoidBone::LeftLowerLeg,HumanoidBone::LeftFoot,HumanoidBone::LeftUpperLeg,false},
            {HumanoidBone::RightUpperLeg,HumanoidBone::RightLowerLeg,HumanoidBone::Hips,false},
            {HumanoidBone::RightLowerLeg,HumanoidBone::RightFoot,HumanoidBone::RightUpperLeg,false}
        };
        struct Built {HumanoidBone role,parent;render::float3 center;render::quat rot;};
        std::vector<Built> built;
        Json bodies=Json::array();
        const auto v3=[](render::float3 v) {
            Json j=Json::array();
            j.push(Json::number(double(v.x),6));j.push(Json::number(double(v.y),6));
            j.push(Json::number(double(v.z),6));return j;
        };
        for(const auto& d:desc) {
            int node=avatar.node(d.role);
            // A compact humanoid skeleton can omit Neck while keeping
            // Chest and Head. In that case use the Head as the endpoint
            // for the torso capsule, retaining a separate Head sphere.
            HumanoidBone endRole=d.end;
            int end=avatar.node(endRole);
            if(d.role==HumanoidBone::Chest && end<0 &&
               avatar.node(HumanoidBone::Head)>=0) {
                endRole=HumanoidBone::Head;
                end=avatar.node(endRole);
            }
            if (node<0 || (end<0 && !d.sphere)) continue;
            const auto start=at(d.role);
            const auto finish=d.sphere?start:at(endRole);
            const auto delta=finish-start;
            const float span=render::length(delta);
            if (!d.sphere && span<.05f)continue;
            const float radius=std::clamp((d.sphere?.28f:span*opt.radiusRatio),.03f,.35f);
            const auto center=d.sphere?start:render::lerp(start,finish,.5f);
            const auto q=d.sphere?render::quat{}:axisToY(delta);
            const std::string id="bone_"+std::to_string(int(d.role));
            Json body=Json::object();
            body.set("id",Json::string(id));body.set("motion",Json::string("dynamic"));
            body.set("shape",Json::string(d.sphere?"sphere":"capsule"));
            body.set("position",v3(center));body.set("radius",Json::number(double(radius),6));
            if(!d.sphere)body.set("halfHeight",Json::number(double(std::max(.02f,span*.5f-radius)),6));
            const float halfAngle=std::acos(std::clamp(q.w,-1.f,1.f));
            const float sinHalf=std::sqrt(std::max(0.f,1.f-q.w*q.w));
            const render::float3 axis=sinHalf<1.e-6f?render::float3{0,1,0}:
                render::float3{q.x/sinHalf,q.y/sinHalf,q.z/sinHalf};
            body.set("axis",v3(axis));body.set("angle",Json::number(double(halfAngle*2.f),6));
            body.set("density",Json::number(double(opt.density),3));
            body.set("friction",Json::number(double(opt.friction),3));
            body.set("collisionGroup",Json::number(int64_t(opt.collisionGroup)));
            bodies.push(body);
            built.push_back({d.role,d.parent,center,q});
            const auto bindBone=modelToWorld*boneWorld[size_t(node)];
            const auto bindRigid=render::mat4::trs(
                {bindBone.at(0,3),bindBone.at(1,3),bindBone.at(2,3)},
                render::rotationOf(bindBone),{1,1,1});
            const auto bodyRigid=render::mat4::trs(center,q,{1,1,1});
            r.bindings_.push_back({id,d.role,node,bodyRigid.inverse()*bindRigid});
        }
        auto findBuilt=[&](HumanoidBone role)->const Built* {
            for(const auto& x:built)if(x.role==role)return &x;return nullptr;
        };
        Json joints=Json::array();
        for(const auto& child:built) {
            if(child.role==HumanoidBone::Hips)continue;
            const auto* parent=findBuilt(child.parent);
            if(!parent)continue; // skip disjoint segments, never create bad joint
            const auto anchor=at(child.role);
            const auto a=render::qnormalize(parent->rot);
            const auto c=render::qnormalize(child.rot);
            const render::quat qa{-a.x,-a.y,-a.z,a.w};
            const render::quat qc{-c.x,-c.y,-c.z,c.w};
            Json j=Json::object();
            j.set("kind",Json::string("hinge"));
            j.set("a",Json::string("bone_"+std::to_string(int(parent->role))));
            j.set("b",Json::string("bone_"+std::to_string(int(child.role))));
            j.set("anchorA",v3(qa.rotate(anchor-parent->center)));
            j.set("anchorB",v3(qc.rotate(anchor-child.center)));
            Json limit=Json::array();
            limit.push(Json::number(double(-opt.jointLimit),6));
            limit.push(Json::number(double(opt.jointLimit),6));
            j.set("limits",limit);
            joints.push(j);
        }
        r.scene_=Json::object();
        r.scene_.set("tickRate",Json::number(int64_t(opt.tickRate)));
        r.scene_.set("substeps",Json::number(int64_t(opt.substeps)));
        r.scene_.set("gravity",v3({0,-9.81f,0}));
        r.scene_.set("bodies",bodies);
        r.scene_.set("joints",joints);
        if(r.bindings_.size()<2)
            throw std::invalid_argument("ragdoll: avatar needs at least two physics bodies");
        return r;
    }
    const Json& scene() const {return scene_;}
    const std::vector<Binding>& bindings() const{return bindings_;}
    // Host supplies current Box3D rigid-body world positions/quaternions
    // after fixed-step physics; convert back to model-space LOCAL bone pose.
    // Missing body poses preserve input animation (partial blend supported).
    void applyBodyPoses(const std::vector<RagdollBodyPose>& bodies,
                        render::Pose& pose) const {
        const auto& model=*model_;
        if(pose.size()!=model.nodes.size())
            throw std::invalid_argument("ragdoll: wrong target pose");
        std::map<std::string,render::mat4> incoming;
        for(const auto& b:bodies) {
            if(!std::isfinite(b.position.x) || !std::isfinite(b.position.y) ||
               !std::isfinite(b.position.z) || !std::isfinite(b.rotation.w))
                throw std::invalid_argument("ragdoll: nonfinite physical transform");
            incoming.emplace(b.id,render::mat4::trs(
                b.position,render::qnormalize(b.rotation),{1,1,1}));
        }
        std::vector<const Binding*> byNode(model.nodes.size(),nullptr);
        for(const auto& binding:bindings_)byNode[size_t(binding.bone)]=&binding;
        std::vector<render::mat4> output(model.nodes.size());
        const auto inverseHost=modelToWorld_.inverse();
        for(int i:model.order) {
            const int parent=model.nodes[size_t(i)].parent;
            const auto parentGlobal=parent>=0?output[size_t(parent)]:render::mat4{};
            if(const auto* bind=byNode[size_t(i)];bind) {
                const auto body=incoming.find(bind->id);
                if(body!=incoming.end()) {
                    const auto target=inverseHost*body->second*bind->bodyToBone;
                    const auto local=parentGlobal.inverse()*target;
                    pose[size_t(i)].t={local.at(0,3),local.at(1,3),local.at(2,3)};
                    pose[size_t(i)].r=render::rotationOf(local);
                }
            }
            const auto& node=pose[size_t(i)];
            const auto local=render::mat4::trs(node.t,node.r,node.s);
            output[size_t(i)]=parent>=0?parentGlobal*local:local;
        }
    }
private:
    const render::Model* model_=nullptr;
    render::mat4 modelToWorld_;
    Json scene_;
    std::vector<Binding> bindings_;
};
} // namespace aiverse::lib::animation
