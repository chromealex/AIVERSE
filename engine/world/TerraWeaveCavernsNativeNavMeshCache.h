#pragma once
// Stage43: shared authored NavMesh cache keyed by immutable SDF and
// NEREID/PHYSIS environment epochs. Built once per active epoch/profile,
// not once per route query. No genre or legacy MapGrid nav duplication.
#include "engine/world/TerraWeaveCavernsNavigationAdapters.h"
#include "engine/world/TerraWeaveCavernsPathRouter.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
class CaveNativeNavMeshCache {
    struct Key {
        uint64_t sdf=0,env=0;
        CaveNavConfig cfg{};
        CaveAgentProfile agent{};
        bool operator<(const Key& b)const {
            return std::tie(sdf,env,
                cfg.width,cfg.depth,cfg.originXcm,cfg.originZcm,cfg.columnCm,
                cfg.minYcm,cfg.maxYcm,cfg.verticalStepCm,cfg.agentHeightCm,
                cfg.maxStepCm,cfg.maxSlopePermille,cfg.maxSpansPerColumn,
                cfg.tileColumns,agent.traversal,agent.radiusCm,agent.heightCm,
                agent.maxStepCm,agent.groundProbeCm,agent.sampleCm,
                agent.areaMask)<
                std::tie(b.sdf,b.env,
                b.cfg.width,b.cfg.depth,b.cfg.originXcm,b.cfg.originZcm,
                b.cfg.columnCm,b.cfg.minYcm,b.cfg.maxYcm,
                b.cfg.verticalStepCm,b.cfg.agentHeightCm,b.cfg.maxStepCm,
                b.cfg.maxSlopePermille,b.cfg.maxSpansPerColumn,
                b.cfg.tileColumns,b.agent.traversal,b.agent.radiusCm,
                b.agent.heightCm,b.agent.maxStepCm,b.agent.groundProbeCm,
                b.agent.sampleCm,b.agent.areaMask);
        }
    };
    struct Entry {
        std::unique_ptr<nav::Mesh> mesh;
        uint64_t lastUse=0;
    };
public:
    explicit CaveNativeNavMeshCache(size_t capacity=4):capacity_(capacity){
        if(capacity==0||capacity>64)
            throw std::invalid_argument("Caverns navmesh cache count");
    }
    CavePathResult find(
        const CavePathQuery& query,
        std::shared_ptr<const CaveNavigationDataProvider> source,
        CaveNavConfig config) {
        CavePathResult result;
        result.sourceRevision=query.sourceRevision;
        result.environmentRevision=source?source->environmentRevision():0;
        if(!source||source->revision()!=query.sourceRevision||
           (query.agent.traversal!=CaveTraversal::Ground &&
            query.agent.traversal!=CaveTraversal::Climbing))
            return result;
        validateCaveNav(config);
        CaveNavigationDataProvider::validateProfile(query.agent);
        Key key{source->revision(),source->environmentRevision(),config,
                query.agent};
        auto it=meshes_.find(key);
        if(it==meshes_.end()){
            CaveSharedNavSource surface(source,config,query.agent);
            auto candidate=std::make_unique<nav::Mesh>();
            std::string error;
            if(!candidate->build(surface,caveNavMeshConfig(config),&error))
                return result;
            while(meshes_.size()>=capacity_)evictOne();
            it=meshes_.emplace(key,Entry{std::move(candidate),++clock_}).first;
        } else it->second.lastUse=++clock_;
        nav::Query params;
        params.maxVisits=query.maxVisits;
        const auto vec=[](PointCm p)->Vec3 {
            return {Fixed::fromRatio(p.x,100),
                    Fixed::fromRatio(p.y,100),
                    Fixed::fromRatio(p.z,100)};
        };
        std::vector<Vec3> waypoints;
        if(!it->second.mesh->findPath(vec(query.start),vec(query.goal),
                                     params,waypoints))return result;
        result.reached=true;
        result.waypoints.reserve(waypoints.size());
        for(const auto& p:waypoints)
            result.waypoints.push_back({
                p.x.raw*100/Fixed::kOne,
                p.y.raw*100/Fixed::kOne,
                p.z.raw*100/Fixed::kOne});
        return result;
    }
    void invalidate(){meshes_.clear();}
    size_t resident()const noexcept{return meshes_.size();}
private:
    void evictOne(){
        auto oldest=meshes_.begin();
        for(auto it=meshes_.begin();it!=meshes_.end();++it)
            if(it->second.lastUse<oldest->second.lastUse)oldest=it;
        meshes_.erase(oldest);
    }
    size_t capacity_;
    uint64_t clock_=0;
    std::map<Key,Entry> meshes_;
};
} // namespace aiverse::terraweave::caverns
