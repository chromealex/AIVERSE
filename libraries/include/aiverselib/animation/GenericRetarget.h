// Generic bind-relative skeleton retargeting for compatible transform rigs.
// This is not humanoid muscle/Avatar retargeting and does not solve IK.
#pragma once
#include "engine/render/Animation.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::lib::animation {

inline std::string bonePath(const render::Model& m,int node) {
    if (node<0 || size_t(node)>=m.nodes.size())
        throw std::invalid_argument("invalid bone index");
    std::string path;
    size_t visited=0;
    while (node>=0) {
        if (size_t(node)>=m.nodes.size() || ++visited>m.nodes.size())
            throw std::invalid_argument("invalid/cyclic skeleton hierarchy");
        const auto& bone=m.nodes[size_t(node)];
        if (bone.name.empty()) throw std::invalid_argument("unnamed skeleton node");
        path=bone.name+(path.empty()?std::string{}:std::string("/") + path);
        node=bone.parent;
    }
    return path;
}


 // Resolve an exact hierarchy path (e.g. root/Hips/Head), not a loose
 // node name. Shared by humanoid avatar, montage and offline root baker.
 // Missing paths return -1; malformed model hierarchies fail via bonePath.
inline int findNodePath(const render::Model& model,const std::string& path) {
    if(path.empty())return -1;
    for(size_t i=0;i<model.nodes.size();++i)
        if(bonePath(model,int(i))==path)return int(i);
    return -1;
}

struct RetargetOptions {
    bool scaleTranslation = true;
    bool copyScale = false;
    bool copyMorph = false;
};
class GenericRetargetMap {
public:
    struct Pair { int source=-1,target=-1; float translationRatio=1; };
    struct NodeSignature {
        std::string name;
        int parent=-1;
        render::float3 t{},s{};
        render::quat r{};
    };
    // aliases: source full bone path -> destination full bone path.
    static GenericRetargetMap compile(
            const render::Model& src,const render::Model& dst,
            const std::map<std::string,std::string>& aliases={}) {
        GenericRetargetMap out;
        // Retain value-only rig signatures: stale indexed maps must not
        // silently bind a different hot-reimported skeleton.
        const auto capture=[](const render::Model& model) {
            std::vector<NodeSignature> result;
            result.reserve(model.nodes.size());
            for(const auto& n:model.nodes)
                result.push_back({n.name,n.parent,n.t,n.s,n.r});
            return result;
        };
        out.sourceRig_=capture(src);
        out.targetRig_=capture(dst);
        std::map<std::string,int> dstPaths;
        for (size_t i=0;i<dst.nodes.size();++i) {
            auto p=bonePath(dst,int(i));
            if (!dstPaths.emplace(p,int(i)).second)
                throw std::invalid_argument("ambiguous target bone path: "+p);
        }
        std::set<int> used;
        for (size_t i=0;i<src.nodes.size();++i) {
            std::string from=bonePath(src,int(i));
            const auto alias=aliases.find(from);
            const std::string& to=alias==aliases.end()?from:alias->second;
            const auto target=dstPaths.find(to);
            if (target==dstPaths.end()) continue;
            if (!used.insert(target->second).second)
                throw std::invalid_argument("multiple sources map to target: "+to);
            const auto a=src.nodes[i].t;
            const auto c=dst.nodes[size_t(target->second)].t;
            const float srcLength=render::length(a);
            const float dstLength=render::length(c);
            const float scale=srcLength>1.e-5f && dstLength>1.e-5f
                ?dstLength/srcLength:1.f;
            if(!std::isfinite(scale) || scale<0.f)
                throw std::invalid_argument("nonfinite retarget translation ratio");
            out.pairs_.push_back({int(i),target->second,scale});
        }
        return out;
    }
    const std::vector<Pair>& pairs() const { return pairs_; }
    // Validate exact hierarchy+rest transforms before writing a single
    // target pose. Same-size reordered, reparented or reused Model memory
    // MUST NOT silently retarget through a stale index map.
    bool matches(const render::Model& src,const render::Model& dst) const noexcept {
        const auto equal=[](const render::Model& model,
                            const std::vector<NodeSignature>& saved) {
            if(model.nodes.size()!=saved.size())return false;
            for(size_t i=0;i<saved.size();++i) {
                const auto& a=model.nodes[i];
                const auto& b=saved[i];
                if(a.name!=b.name || a.parent!=b.parent ||
                   a.t.x!=b.t.x || a.t.y!=b.t.y || a.t.z!=b.t.z ||
                   a.s.x!=b.s.x || a.s.y!=b.s.y || a.s.z!=b.s.z ||
                   a.r.x!=b.r.x || a.r.y!=b.r.y ||
                   a.r.z!=b.r.z || a.r.w!=b.r.w)return false;
            }
            return true;
        };
        return equal(src,sourceRig_) && equal(dst,targetRig_);
    }

    // Strong output guarantee: malformed input or a changed rig leaves the
    // previous destination pose untouched. The evaluated pose is committed
    // only after all mapped local transforms and channels are validated.
    void apply(const render::Model& src,const render::Model& dst,
               const render::Pose& srcPose,render::Pose& dstPose,
               RetargetOptions options={}) const {
        if(srcPose.size()!=src.nodes.size() || !matches(src,dst))
            throw std::invalid_argument("retarget source/target rig changed");
        render::Pose next;
        render::bindPose(dst,next);
        const auto finite=[](float v){return std::isfinite(v);};
        for(const auto& pair:pairs_) {
            if(pair.source<0 || pair.target<0 ||
               size_t(pair.source)>=src.nodes.size() ||
               size_t(pair.target)>=dst.nodes.size() ||
               !finite(pair.translationRatio))
                throw std::invalid_argument("retarget model layout changed");
            const auto& bindA=src.nodes[size_t(pair.source)];
            const auto& bindB=dst.nodes[size_t(pair.target)];
            const auto& a=srcPose[size_t(pair.source)];
            for(float v:{bindA.t.x,bindA.t.y,bindA.t.z,
                         bindA.s.x,bindA.s.y,bindA.s.z,
                         bindA.r.x,bindA.r.y,bindA.r.z,bindA.r.w,
                         bindB.t.x,bindB.t.y,bindB.t.z,
                         bindB.s.x,bindB.s.y,bindB.s.z,
                         bindB.r.x,bindB.r.y,bindB.r.z,bindB.r.w,
                         a.t.x,a.t.y,a.t.z,a.r.x,a.r.y,a.r.z,a.r.w,
                         a.s.x,a.s.y,a.s.z})
                if(!finite(v))
                    throw std::invalid_argument("nonfinite retarget transform");
            auto& b=next[size_t(pair.target)];
            const float factor=options.scaleTranslation?pair.translationRatio:1.f;
            b.t=bindB.t+(a.t-bindA.t)*factor;
            const render::quat inv{-bindA.r.x,-bindA.r.y,-bindA.r.z,bindA.r.w};
            b.r=render::qnormalize(bindB.r*render::qnormalize(inv*a.r));
            if(options.copyScale) {
                if(std::abs(bindA.s.x)<1.e-6f ||
                   std::abs(bindA.s.y)<1.e-6f ||
                   std::abs(bindA.s.z)<1.e-6f)
                    throw std::invalid_argument("degenerate source bind scale");
                b.s={bindB.s.x*a.s.x/bindA.s.x,
                     bindB.s.y*a.s.y/bindA.s.y,
                     bindB.s.z*a.s.z/bindA.s.z};
            }
            if(options.copyMorph) {
                // Morph indices are mesh-local. Preserve the existing
                // opt-in index transfer only when channel counts match;
                // complex cross-LOD channel remaps use ModelPoseRetarget.
                const auto morphCount=[](const render::Model& model,int node) {
                    int count=0;
                    const int mesh=model.nodes[size_t(node)].mesh;
                    if(mesh>=0 && size_t(mesh)<model.meshes.size())
                        for(const auto& prim:model.meshes[size_t(mesh)].primitives)
                            count=std::max(count,prim.morphTargets);
                    return count;
                };
                const int from=morphCount(src,pair.source);
                const int to=morphCount(dst,pair.target);
                if(from!=to)
                    throw std::invalid_argument("morph channels require explicit remap");
                for(int k=0;k<from && k<render::kMaxMorphTargets;++k) {
                    if(!finite(a.w[k]))
                        throw std::invalid_argument("nonfinite morph weight");
                    b.w[k]=a.w[k];
                }
            }
            for(float v:{b.t.x,b.t.y,b.t.z,b.r.x,b.r.y,b.r.z,b.r.w,
                         b.s.x,b.s.y,b.s.z})
                if(!finite(v))
                    throw std::invalid_argument("nonfinite retarget result");
        }
        dstPose=std::move(next);
    }
private:
    std::vector<Pair> pairs_;
    std::vector<NodeSignature> sourceRig_,targetRig_;
};
} // namespace aiverse::lib::animation
