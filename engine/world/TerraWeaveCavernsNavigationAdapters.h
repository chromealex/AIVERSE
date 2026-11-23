#pragma once
// TerraWeave Caverns Stage41: projection of shared navigation DATA into
// existing engine/nav/NavMesh.h. Other solvers consume CaveNavigationGridView
// or CaveNavigationDataProvider directly; no algorithm is reimplemented.
#include "engine/world/TerraWeaveCavernsNavigationGraph.h"
#include "engine/world/TerraWeaveCavernsMultiNav.h"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
class CaveSharedNavSource final:public nav::Source {
public:
    CaveSharedNavSource(
        std::shared_ptr<const CaveNavigationDataProvider> provider,
        CaveNavConfig config,CaveAgentProfile agent={})
        :provider_(std::move(provider)),config_(config),agent_(agent){
        if(!provider_)throw std::invalid_argument("Caverns shared nav provider");
        validateCaveNav(config_);
        CaveNavigationDataProvider::validateProfile(agent_);
        if(agent_.traversal!=CaveTraversal::Ground &&
           agent_.traversal!=CaveTraversal::Climbing)
            throw std::invalid_argument(
                "Caverns 2.5D nav::Mesh requires supported ground/climb profile");
    }
    void spans(int32_t x,int32_t z,std::vector<nav::Span>& output)const override{
        if(x<0||z<0||x>=config_.width||z>=config_.depth)return;
        std::vector<PointCm> walk;
        sampleColumn(x,z,walk);
        for(const auto& point:walk) {
            const auto info=provider_->sample(point,agent_);
            output.push_back({Fixed::fromRatio(point.y,100),info.area});
        }
    }
    bool link(int32_t x0,int32_t z0,uint32_t a,
              int32_t x1,int32_t z1,uint32_t b)const override{
        if(x0<0||z0<0||x1<0||z1<0||
           x0>=config_.width||x1>=config_.width||
           z0>=config_.depth||z1>=config_.depth||
           std::abs(int64_t(x0)-x1)+std::abs(int64_t(z0)-z1)!=1)
            return false;
        std::vector<PointCm> from,to;
        sampleColumn(x0,z0,from);
        sampleColumn(x1,z1,to);
        if(a>=from.size()||b>=to.size())return false;
        if(std::abs(from[a].y-to[b].y)>config_.maxStepCm)
            return false;
        return provider_->canTraverse(from[a],to[b],agent_);
    }
    uint64_t sourceRevision()const noexcept{return provider_->revision();}
private:
    void sampleColumn(int32_t x,int32_t z,
                      std::vector<PointCm>& out)const{
        out.clear();
        const int64_t px=config_.originXcm+
            int64_t(x)*config_.columnCm+config_.columnCm/2;
        const int64_t pz=config_.originZcm+
            int64_t(z)*config_.columnCm+config_.columnCm/2;
        bool previous=false;
        for(int64_t y=config_.minYcm;y<=config_.maxYcm;
            y+=config_.verticalStepCm){
            const PointCm p{px,y,pz};
            const bool yes=provider_->sample(p,agent_).traversable;
            if(yes&&!previous){
                out.push_back(p);
                if(out.size()>config_.maxSpansPerColumn)
                    throw std::length_error(
                        "Caverns shared NavMesh vertical span budget");
            }
            previous=yes;
            if(config_.maxYcm-y<config_.verticalStepCm)break;
        }
    }
    std::shared_ptr<const CaveNavigationDataProvider> provider_;
    CaveNavConfig config_{};
    CaveAgentProfile agent_{};
};
} // namespace aiverse::terraweave::caverns
