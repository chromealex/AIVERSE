#pragma once
#include "engine/world/TerraWeaveCavernsPlanetAuthority.h"
#include "engine/world/TerraWeaveCavernsNavigationData.h"
#include "engine/physis/PhysisGravity.h"

namespace aiverse::terraweave::caverns {
struct CavePlanetGravitySample {
    Vec3 simulationAcceleration{};
    Vec3 chartAcceleration{};
    Vec3 chartUp{};
    bool groundedChartCompatible=false;
    bool groundedCompatible=false;
};
// No second gravity policy. The PHYSIS fields must be authored in the
// simulation anchor coordinates, NOT in the moving orbit/render space.
inline CavePlanetGravitySample sampleCavePlanetGravity(const CavePlanetFrame& frame,
        const World& frozen,PointCm point){
    const auto sim=frame.toSimulation(CavePlanetFrame::metres(point));
    CavePlanetGravitySample out;
    out.simulationAcceleration=physis::gravityAt(frozen,sim);
    out.chartAcceleration=frame.vectorFromBody(out.simulationAcceleration);
    const auto magnitude=out.chartAcceleration.length();
    if(magnitude>Fixed::fromRatio(1,100)){
        out.chartUp=fxNormalize(out.chartAcceleration*Fixed::fromInt(-1));
        out.groundedCompatible=true;
        // The legacy 2.5D NavMesh still requires chart-Y support.
        out.groundedChartCompatible=out.chartUp.y>=Fixed::fromRatio(99,100);
    }
    return out;
}
// Existing A*/HPA*/flow providers run in chart centimetres. Every sampled
// ground point consults the same frozen PHYSIS world; non-ground modes keep
// their existing policies. Caller supplies an actual native residency proof.
inline std::shared_ptr<const CaveNavigationDataProvider> makeCavePlanetNavigationProvider(
        CavePlanetFrame frame,std::shared_ptr<const CaveSnapshotSpatialIndex> index,
        std::shared_ptr<const World> frozen,uint64_t environmentRevision,
        CaveNavigationCallbacks callbacks,CaveResidencyCheck nativeResidency,uint64_t scopeId=0){
    if(!index||!frozen||!environmentRevision||!nativeResidency)
        throw std::invalid_argument("Caverns planetary navigation owner proof");
    validateCavePlanetVolume(frame,index->snapshot());
    if(callbacks.groundUp)throw std::invalid_argument("Caverns PHYSIS is the ground-up owner");
    callbacks.groundUp=[frame,frozen](PointCm p)->std::optional<Vec3>{
        if(!frame.contains(p))return {};
        const auto gravity=sampleCavePlanetGravity(frame,*frozen,p);
        if(!gravity.groundedCompatible)return {};
        return gravity.chartUp;
    };
    return std::make_shared<const CaveNavigationDataProvider>(std::move(index),std::move(callbacks),
        environmentRevision,[frame,nativeResidency=std::move(nativeResidency)](PointCm lo,PointCm hi){
            return frame.contains(lo)&&frame.contains(hi)&&nativeResidency(lo,hi);
        },scopeId);
}
class CavePlanetQueries {
public:
    CavePlanetQueries(CavePlanetFrame frame,std::shared_ptr<const CaveSnapshotSpatialIndex> index)
        :frame_(std::move(frame)),index_(std::move(index)){
        if(!index_)throw std::invalid_argument("Caverns planet missing SDF");
        validateCavePlanetVolume(frame_,index_->snapshot());
    }
    const CavePlanetFrame& frame()const noexcept{return frame_;}
    int64_t fieldAtSimulation(Vec3 point)const{
        return index_->field(CavePlanetFrame::centimetres(frame_.fromSimulation(point)));
    }
    CaveMesh generateChunk(ChunkKey3 key,uint32_t resolution,int32_t spacingCm)const{
        if(!resolution||resolution>64||spacingCm<=0||spacingCm>10000)
            throw std::invalid_argument("Caverns planetary lattice");
        const int64_t span=int64_t(resolution)*spacingCm;
        const int64_t cap=frame_.anchor().halfExtentCm;
        const int64_t halo=std::max<int64_t>(1,spacingCm/2);
        for(auto k:{key.x,key.y,key.z}){
            if(k< -cap/span-1||k>cap/span+1||k*span-halo< -cap||(k+1)*span+halo>cap)
                throw std::out_of_range("Caverns planetary chunk/gradient outside chart");
        }
        return generateCaveIndexedSnapshotMesh(*index_,key,resolution,spacingCm);
    }
    std::optional<Vec3> raycastSimulation(Vec3 origin,Vec3 direction,
                 int64_t maxDistanceCm=10000,int64_t stepCm=50)const{
        auto local=frame_.fromSimulation(origin);
        const auto d=fxNormalize(frame_.vectorFromBody(direction));
        const auto q=[](Fixed v){return int32_t(v.raw*16);};
        const CaveRayCm ray{CavePlanetFrame::centimetres(local),q(d.x),q(d.y),q(d.z)};
        // Do not create a fake zero crossing at chart boundaries.
        const auto end=local+d*Fixed::fromRatio(maxDistanceCm,100);
        frame_.checkLocal(end);
        const auto hit=raycastCaveSurface(*index_,ray,maxDistanceCm,stepCm);
        if(!hit)return {};
        return frame_.toSimulation(CavePlanetFrame::metres(hit->point));
    }
    // Adapter for existing local chart pathfinder; callers must additionally
    // check intermediate support and real native collision, not just ends.
    std::optional<PointCm> groundedPoint(Vec3 simPoint,const World& frozen)const{
        auto p=CavePlanetFrame::centimetres(frame_.fromSimulation(simPoint));
        if(!sampleCavePlanetGravity(frame_,frozen,p).groundedCompatible)return {};
        return p;
    }
private:
    CavePlanetFrame frame_;
    std::shared_ptr<const CaveSnapshotSpatialIndex> index_;
};
} // namespace aiverse::terraweave::caverns
