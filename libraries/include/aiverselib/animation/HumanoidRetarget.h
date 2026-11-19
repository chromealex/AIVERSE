// Semantic humanoid retargeting with model-space bind orientation alignment.
// Native C++ policy shared by RPG/FPS/RTS; no Unity muscle curves are assumed.
#pragma once
#include "GenericRetarget.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace aiverse::lib::animation {
enum class HumanoidBone : uint8_t {
    Root,Hips,Spine,Chest,UpperChest,Neck,Head,
    LeftShoulder,LeftUpperArm,LeftLowerArm,LeftHand,
    RightShoulder,RightUpperArm,RightLowerArm,RightHand,
    LeftUpperLeg,LeftLowerLeg,LeftFoot,LeftToes,
    RightUpperLeg,RightLowerLeg,RightFoot,RightToes,Count
};
constexpr size_t kHumanoidBoneCount=size_t(HumanoidBone::Count);
inline render::quat inverseUnit(render::quat q) {
    q=render::qnormalize(q);return {-q.x,-q.y,-q.z,q.w};
}
struct HumanoidAvatar {
    const render::Model* model=nullptr;
    std::array<int,kHumanoidBoneCount> nodes{};
    std::vector<render::quat> bindWorld;
    static HumanoidAvatar compile(const render::Model& m,
                                  const std::map<HumanoidBone,std::string>& paths) {
        if (m.nodes.empty() || m.order.size()!=m.nodes.size())
            throw std::invalid_argument("avatar: model lacks ordered skeleton");
        HumanoidAvatar a;a.model=&m;a.nodes.fill(-1);
        std::set<int> used;
        for (const auto& [role,path]:paths) {
            const auto index=size_t(role);
            if(index>=kHumanoidBoneCount || path.empty())
                throw std::invalid_argument("avatar: invalid role/path");
            const int node=findNodePath(m,path);
            if(node<0 || !used.insert(node).second)
                throw std::invalid_argument("avatar: missing or duplicated bone "+path);
            a.nodes[index]=node;
        }
        if(a.node(HumanoidBone::Hips)<0 || a.node(HumanoidBone::Head)<0)
            throw std::invalid_argument("avatar: hips and head are required");
        a.bindWorld.resize(m.nodes.size());
        std::vector<uint8_t> seen(m.nodes.size(),0);
        for(int i:m.order) {
            if(i<0 || size_t(i)>=m.nodes.size() || seen[size_t(i)])
                throw std::invalid_argument("avatar: corrupt node order");
            const int p=m.nodes[size_t(i)].parent;
            if(p>=0 && (size_t(p)>=m.nodes.size() || !seen[size_t(p)]))
                throw std::invalid_argument("avatar: parents must precede children");
            seen[size_t(i)]=1;
            a.bindWorld[size_t(i)]=render::qnormalize(
                (p>=0?a.bindWorld[size_t(p)]:render::quat{})*m.nodes[size_t(i)].r);
        }
        return a;
    }
    int node(HumanoidBone role) const {return nodes[size_t(role)];}
};
// Semantic shared roles are mapped regardless of asset-specific bone names.
// Intermediate unmapped joints retain destination bind transforms.
class HumanoidRetargeter {
public:
    static HumanoidRetargeter compile(HumanoidAvatar src,HumanoidAvatar dst) {
        if (!src.model || !dst.model)
            throw std::invalid_argument("humanoid retarget: missing avatar");
        HumanoidRetargeter r;
        r.src_=std::move(src);r.dst_=std::move(dst);
        for(size_t i=0;i<kHumanoidBoneCount;++i)
            if(r.src_.nodes[i]>=0 && r.dst_.nodes[i]>=0)
                r.pairs_.push_back({r.src_.nodes[i],r.dst_.nodes[i],HumanoidBone(i)});
        // Avatar height scaling: hips to left/right foot in bind pose.
        auto legLength=[](const HumanoidAvatar& a)->float{
            render::Pose bind;render::bindPose(*a.model,bind);
            std::vector<render::mat4> world;render::worldMatrices(*a.model,bind,world);
            int hip=a.node(HumanoidBone::Hips);
            float total=0;int count=0;
            for(auto role:{HumanoidBone::LeftFoot,HumanoidBone::RightFoot}) {
                int foot=a.node(role);
                if(foot<0)continue;
                render::float3 h{world[size_t(hip)].at(0,3),
                                 world[size_t(hip)].at(1,3),
                                 world[size_t(hip)].at(2,3)};
                render::float3 f{world[size_t(foot)].at(0,3),
                                 world[size_t(foot)].at(1,3),
                                 world[size_t(foot)].at(2,3)};
                total+=render::length(h-f);++count;
            }
            return count?total/float(count):1.f;
        };
        const float from=legLength(r.src_),to=legLength(r.dst_);
        r.heightScale_=from>1.e-5f?std::clamp(to/from,.1f,10.f):1.f;
        return r;
    }
    float heightScale() const {return heightScale_;}
    void evaluate(const render::Pose& source,render::Pose& destination,
                  bool copyRootTranslation=true,bool copyPelvisTranslation=true) const {
        const auto& sm=*src_.model;const auto& dm=*dst_.model;
        if(source.size()!=sm.nodes.size())
            throw std::invalid_argument("humanoid retarget: wrong source pose");
        std::vector<render::quat> srcWorld(sm.nodes.size());
        for(int i:sm.order) {
            const int p=sm.nodes[size_t(i)].parent;
            srcWorld[size_t(i)]=render::qnormalize(
                (p>=0?srcWorld[size_t(p)]:render::quat{})*source[size_t(i)].r);
        }
        render::bindPose(dm,destination);
        // Target bone indices are unique; compute global rotations in
        // topological order so child locals compensate retargeted parents.
        std::vector<int> sourceForTarget(dm.nodes.size(),-1);
        for(const auto& pair:pairs_) sourceForTarget[size_t(pair.target)]=pair.source;
        std::vector<render::quat> dstWorld(dm.nodes.size());
        for(int i:dm.order) {
            const int parent=dm.nodes[size_t(i)].parent;
            const auto parentR=parent>=0?dstWorld[size_t(parent)]:render::quat{};
            if(const int s=sourceForTarget[size_t(i)];s>=0) {
                // Convert source local bind-relative rotation into destination
                // model-space bind orientation, then remove the new parent.
                const auto relative=inverseUnit(src_.bindWorld[size_t(s)])*
                    srcWorld[size_t(s)];
                const auto desired=render::qnormalize(
                    dst_.bindWorld[size_t(i)]*relative);
                destination[size_t(i)].r=render::qnormalize(
                    inverseUnit(parentR)*desired);
            }
            dstWorld[size_t(i)]=render::qnormalize(parentR*destination[size_t(i)].r);
        }
        for(const auto& pair:pairs_) {
            const bool root=pair.role==HumanoidBone::Root;
            const bool hips=pair.role==HumanoidBone::Hips;
            if((root && copyRootTranslation)||(hips && copyPelvisTranslation)) {
                const auto& s=source[size_t(pair.source)];
                const auto& sourceBind=sm.nodes[size_t(pair.source)];
                destination[size_t(pair.target)].t+=
                    (s.t-sourceBind.t)*heightScale_;
            }
        }
    }
    // Optional post-retarget stance correction via existing generic IK.
    // Reuses the same hip/knee/foot solver as other AIVERSE rigs.
    void placeFeet(render::Pose& pose,
        const std::map<HumanoidBone,render::float3>& targets,
        render::float3 leftPole={-1,0,0},
        render::float3 rightPole={1,0,0},
        float weight=1.f) const {
        const auto& m=*dst_.model;
        std::vector<render::mat4> world;
        render::worldMatrices(m,pose,world);
        for(auto side:{HumanoidBone::LeftFoot,HumanoidBone::RightFoot}) {
            auto target=targets.find(side);
            if(target==targets.end())continue;
            const bool left=side==HumanoidBone::LeftFoot;
            const int hip=dst_.node(left?HumanoidBone::LeftUpperLeg:HumanoidBone::RightUpperLeg);
            const int knee=dst_.node(left?HumanoidBone::LeftLowerLeg:HumanoidBone::RightLowerLeg);
            const int foot=dst_.node(side);
            if(hip<0||knee<0||foot<0)continue;
            if(m.nodes[size_t(knee)].parent!=hip || m.nodes[size_t(foot)].parent!=knee)
                continue; // only a direct chain is supported by this solver
            render::solveTwoBoneIK(m,pose,world,hip,knee,foot,target->second,
                                  left?leftPole:rightPole,weight);
        }
    }
private:
    struct Pair {int source,target;HumanoidBone role;};
    HumanoidAvatar src_,dst_;
    std::vector<Pair> pairs_;
    float heightScale_=1.f;
};
} // namespace aiverse::lib::animation
