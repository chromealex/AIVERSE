// Generic bind-relative skeleton retargeting for compatible transform rigs.
// This is not humanoid muscle/Avatar retargeting and does not solve IK.
#pragma once
#include "engine/render/Animation.h"
#include <cmath>
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

struct RetargetOptions {
    bool scaleTranslation = true;
    bool copyScale = false;
    bool copyMorph = false;
};
class GenericRetargetMap {
public:
    struct Pair { int source=-1,target=-1; float translationRatio=1; };
    // aliases: source full bone path -> destination full bone path.
    static GenericRetargetMap compile(
            const render::Model& src,const render::Model& dst,
            const std::map<std::string,std::string>& aliases={}) {
        GenericRetargetMap out;
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
            out.pairs_.push_back({int(i),target->second,scale});
        }
        return out;
    }
    const std::vector<Pair>& pairs() const { return pairs_; }
    // Reset destination to bind pose before transferring local deltas.
    // Source and destination Models must be the same ones used in compile().
    void apply(const render::Model& src,const render::Model& dst,
               const render::Pose& srcPose,render::Pose& dstPose,
               RetargetOptions options={}) const {
        if (srcPose.size()!=src.nodes.size())
            throw std::invalid_argument("retarget pose size mismatch");
        render::bindPose(dst,dstPose);
        for (auto pair:pairs_) {
            if (pair.source<0 || pair.target<0 ||
                size_t(pair.source)>=src.nodes.size() ||
                size_t(pair.target)>=dst.nodes.size())
                throw std::invalid_argument("retarget model layout changed");
            const auto& bindA=src.nodes[size_t(pair.source)];
            const auto& bindB=dst.nodes[size_t(pair.target)];
            const auto& a=srcPose[size_t(pair.source)];
            auto& b=dstPose[size_t(pair.target)];
            const float factor=options.scaleTranslation?pair.translationRatio:1.f;
            b.t=bindB.t+(a.t-bindA.t)*factor;
            const render::quat inv{-bindA.r.x,-bindA.r.y,-bindA.r.z,bindA.r.w};
            b.r=render::qnormalize(bindB.r*render::qnormalize(inv*a.r));
            if (options.copyScale) {
                const auto safe=[](float x){return std::abs(x)>1.e-6f?x:1.f;};
                b.s={bindB.s.x*a.s.x/safe(bindA.s.x),
                     bindB.s.y*a.s.y/safe(bindA.s.y),
                     bindB.s.z*a.s.z/safe(bindA.s.z)};
            }
            if (options.copyMorph)
                for (int k=0;k<render::kMaxMorphTargets;++k)
                    b.w[k]=a.w[k];
        }
    }
private:
    std::vector<Pair> pairs_;
};
} // namespace aiverse::lib::animation
