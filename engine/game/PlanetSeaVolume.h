// Optional conserved-volume mean sea for canonical GameCore heightfields.
// The existing NEREID WaterSphereComponent remains the only live sea level.
// Discrete radial column model: cell-centre ground and triangle direction
// determinants, centimetre radii, integer cm^3. Not analytic shoreline
// clipping, disconnected basins, waves or a second water flow solver.
#pragma once
#include "PlanetTerrainWater.h"
#include "engine/sim/Simulation.h"
#include <algorithm>
#include <map>
#include <optional>
#include <vector>

namespace aiverse::game {
struct PlanetSeaVolumeComponent {
    uint64_t targetCm3=0;
    uint64_t unresolvedCm3=0; // target minus volume represented by quantized level
};
inline void io(Archive& a,PlanetSeaVolumeComponent& v){
    a(v.targetCm3,v.unresolvedCm3);
    if(!v.targetCm3||v.targetCm3>30000000000000000ull||v.unresolvedCm3>v.targetCm3)a.fail();
}
inline void registerPlanetSeaVolumePersistence(Simulation& sim){
    sim.registerPersistentComponent<PlanetSeaVolumeComponent>("nereid.planet_sea_volume",1);
}

namespace planet_sea_detail {
inline constexpr int32_t kMaxRadiusCm=150000;
inline constexpr int kMaxFaceSize=128; // explicit synchronous solve budget
inline constexpr uint64_t kWeightScale=1ull<<24;
inline constexpr uint64_t kDenominator=6*kWeightScale;
inline int32_t roundedCm(Fixed v){
    const auto raw=v.raw*100;
    return int32_t(raw>=0?(raw+Fixed::kOne/2)/Fixed::kOne:-((-raw+Fixed::kOne/2)/Fixed::kOne));
}
inline uint64_t determinant(Vec3 a,Vec3 b,Vec3 c){
    const auto det=a.x.raw*(b.y.raw*c.z.raw-b.z.raw*c.y.raw)+
        a.y.raw*(b.z.raw*c.x.raw-b.x.raw*c.z.raw)+
        a.z.raw*(b.x.raw*c.y.raw-b.y.raw*c.x.raw);
    return uint64_t(det<0?-det:det);
}
inline uint64_t cube(int32_t cm){
    return uint64_t(cm)*uint64_t(cm)*uint64_t(cm);
}
// Split before multiplication. Bounds: radius <=150000 cm, total
// direction weight <=32*2^24, remainder <6*2^24.
inline uint64_t weightedVolume(uint64_t radialCube,uint64_t weight){
    return (radialCube/kDenominator)*weight+
        ((radialCube%kDenominator)*weight)/kDenominator;
}
struct Columns {
    std::map<int32_t,uint64_t> weightsByFloor;
    int32_t minimumLevel=1;
    uint64_t volume(int32_t level)const{
        uint64_t result=0;
        const auto upper=cube(level);
        for(const auto& [floor,weight]:weightsByFloor){
            if(floor>=level)break;
            result+=weightedVolume(upper-cube(floor),weight);
        }
        return result;
    }
};
inline std::optional<Columns> buildColumns(const Surface& surface,int body,Fixed floorRadius){
    if(!surface.tab||body<0||size_t(body)>=surface.bodies.size()||
       floorRadius<Fixed{}||floorRadius>=Fixed::fromInt(1500))return {};
    const auto& def=surface.bodies[size_t(body)];
    const int n=def.faceSize;
    if(n<2||n>kMaxFaceSize||def.radius<=Fixed{}||def.radius>Fixed::fromInt(1500))return {};
    const auto& cs=surface.cubeOf(body);
    if(cs.faceSize()!=n)return {};
    const size_t per=size_t(n+1)*size_t(n+1);
    if(def.cornerBase>surface.corners.size()||6*per>surface.corners.size()-def.cornerBase)return {};
    const auto floorCm=int32_t((floorRadius.raw*100+Fixed::kOne-1)/Fixed::kOne);
    Columns out;out.minimumLevel=floorCm+1;
    uint64_t totalWeight=0;
    // Cache directions per face to avoid four normalizations per cell.
    std::vector<Vec3> directions(per);
    for(int face=0;face<6;++face){
        for(int y=0;y<=n;++y)for(int x=0;x<=n;++x)
            directions[size_t(y)*(n+1)+x]=cs.cornerDir(face,x,y);
        for(int y=0;y<n;++y)for(int x=0;x<n;++x){
            const size_t k=size_t(y)*(n+1)+x;
            const auto a=directions[k],b=directions[k+1],c=directions[k+n+1],d=directions[k+n+2];
            const auto weight=(determinant(a,b,d)+determinant(a,d,c))>>24;
            if(!weight)return {};
            totalWeight+=weight;
            if(totalWeight>32*kWeightScale)return {};
            const auto ground=def.radius+surface.cellHeight(surface.gridCell(body,face,x,y));
            if(ground<=Fixed{}||ground>Fixed::fromInt(1500))return {};
            out.weightsByFloor[std::max(floorCm,roundedCm(ground))]+=weight;
        }
    }
    return out;
}
inline int seaBody(const Surface& s,const lib::nereid::WaterSphereComponent& sea){
    if(!lib::nereid::validWaterSphere(sea))return -1;
    for(size_t b=0;b<s.bodies.size();++b)
        if(s.bodies[b].hasSurface()&&sea.center==Surface::anchor(int(b)))return int(b);
    return -1;
}
} // namespace planet_sea_detail

struct PlanetSeaVolumeSolution {
    Fixed radius{};
    uint64_t representedCm3=0,unresolvedCm3=0;
};
inline std::optional<PlanetSeaVolumeSolution> solvePlanetSeaVolume(
        const Surface& surface,int body,Fixed floorRadius,uint64_t targetCm3){
    using namespace planet_sea_detail;
    if(!targetCm3||targetCm3>30000000000000000ull)return {};
    const auto columns=buildColumns(surface,body,floorRadius);
    if(!columns||columns->volume(kMaxRadiusCm)<targetCm3||
       columns->volume(columns->minimumLevel)>targetCm3)return {};
    int32_t lo=columns->minimumLevel,hi=kMaxRadiusCm;
    while(lo<hi){
        const auto mid=lo+(hi-lo+1)/2;
        if(columns->volume(mid)<=targetCm3)lo=mid;else hi=mid-1;
    }
    const auto represented=columns->volume(lo);
    return PlanetSeaVolumeSolution{Fixed::fromRatio(lo,100),represented,targetCm3-represented};
}
// Explicit setup only. No gameplay waves (their volume is not conserved).
// Quantize authored mean level to nearest cm once; volume is defined by
// that initial level in the same discrete model used for later edits.
inline bool initializePlanetSeaVolume(World& world,const Surface& surface,Entity e){
    using namespace lib::nereid;
    using namespace planet_sea_detail;
    const auto* sea=world.tryGetConst<WaterSphereComponent>(e);
    const auto* waves=world.tryGetConst<WaterSphereWavesComponent>(e);
    if(!world.alive(e)||!sea||world.has<PlanetSeaVolumeComponent>(e)||
       (waves&&waves->amplitude!=Fixed{})||sea->surfaceRadius>Fixed::fromInt(1500))return false;
    const int body=seaBody(surface,*sea);
    const auto columns=buildColumns(surface,body,sea->floorRadius);
    if(!columns)return false;
    const int32_t level=roundedCm(sea->surfaceRadius);
    if(level<columns->minimumLevel||level>kMaxRadiusCm)return false;
    const auto volume=columns->volume(level);
    if(!volume)return false;
    world.add<PlanetSeaVolumeComponent>(e,PlanetSeaVolumeComponent{volume,0});
    world.get<WaterSphereComponent>(e).surfaceRadius=Fixed::fromRatio(level,100);
    return true;
}
// The terrain owner passes its staged World. All opted-in seas are solved
// against the candidate before any writes. A failure rejects the edit.
// No automatic water volume exchange between bodies or rainfall sources.
inline bool reconcilePlanetSeaVolumes(World& world,const Surface& previous,const Surface& candidate){
    using namespace lib::nereid;
    using namespace planet_sea_detail;
    if(previous.tab!=candidate.tab||world.countExisting<PlanetSeaVolumeComponent>()>size_t(Surface::kMaxBodies))return false;
    struct Change {Entity entity;PlanetSeaVolumeSolution solution;};
    std::vector<Change> changes;
    std::vector<int> bodies;
    bool valid=true;
    world.eachExistingConst<PlanetSeaVolumeComponent>([&](Entity e,const PlanetSeaVolumeComponent& volume){
        if(!world.alive(e))return;
        const auto* sea=world.tryGetConst<WaterSphereComponent>(e);
        const auto* waves=world.tryGetConst<WaterSphereWavesComponent>(e);
        if(!sea||(waves&&waves->amplitude!=Fixed{})||volume.unresolvedCm3>volume.targetCm3){
            valid=false;return;
        }
        const int body=seaBody(previous,*sea);
        if(body<0||std::find(bodies.begin(),bodies.end(),body)!=bodies.end()){
            valid=false;return;
        }
        bodies.push_back(body);
        const auto& def=previous.bodies[size_t(body)];
        const size_t n=6*size_t(def.faceSize+1)*size_t(def.faceSize+1);
        if(previous.corners.size()!=candidate.corners.size()||def.cornerBase>previous.corners.size()||
           n>previous.corners.size()-def.cornerBase){valid=false;return;}
        const auto first=previous.corners.begin()+ptrdiff_t(def.cornerBase);
        if(std::equal(first,first+ptrdiff_t(n),candidate.corners.begin()+ptrdiff_t(def.cornerBase)))return;
        const auto solution=solvePlanetSeaVolume(candidate,body,sea->floorRadius,volume.targetCm3);
        if(!solution){valid=false;return;}
        changes.push_back({e,*solution});
    });
    if(!valid)return false;
    for(const auto& change:changes){
        world.get<WaterSphereComponent>(change.entity).surfaceRadius=change.solution.radius;
        world.get<PlanetSeaVolumeComponent>(change.entity).unresolvedCm3=change.solution.unresolvedCm3;
    }
    return true;
}
} // namespace aiverse::game
