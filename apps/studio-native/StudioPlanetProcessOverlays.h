// Authored-only read-only overlay geometry for the existing native Scene.
// Runtime state and document mutations stay with their respective owners.
#pragma once
#include "apps/studio-native/StudioPlanetPreview.h"
#include "apps/editor/PhysisAuthoring.h"
#include "engine/game/Surface.h"
#include "libraries/include/aiverselib/world/EvolveArea.h"

namespace aiverse::studio {
struct PlanetProcessOverlayLine {
    render::float3 a{},b{};
    uint32_t color=0;
    std::string owner;
};
struct PlanetProcessOverlays {
    std::vector<PlanetProcessOverlayLine> lines;
    bool truncated=false,invalidPhysis=false,regionChartClipped=false;
};
namespace planet_overlay_detail {
inline render::float3 surfacePoint(const editor::DocBody& body,
        const game::CubeSphere& cube,Vec3 direction,float radius,float lift){
    int face;Fixed a,b;cube.faceFromDir(direction,face,a,b);
    const auto x=float(std::clamp(a.toDouble(),0.,double(body.faceSize)));
    const auto y=float(std::clamp(b.toDouble(),0.,double(body.faceSize)));
    return StudioPlanetPreview::f3(direction)*
        (radius+StudioPlanetPreview::height(body,face,x,y)+lift);
}
// Invert EVOLVE's azimuthal-equidistant chart. Surface::frame projects
// both face axes independently; solve their Gram matrix rather than
// assuming they are perpendicular. The chart ends at the antipode.
inline std::optional<Vec3> regionDirection(Vec3 up,Vec3 east,Vec3 south,
        Fixed radius,Fixed x,Fixed z){
    if(radius.raw<=0)return {};
    const auto d=east.dot(south),den=Fixed::fromInt(1)-d*d;
    if(den<Fixed::fromRatio(1,100))return {};
    const auto tangent=east*((x-d*z)/den)+south*((z-d*x)/den);
    const auto distance=tangent.length();
    const auto turns=distance/(radius*game::kFxTwoPi);
    if(turns>=Fixed::fromRatio(1,2))return {};
    if(distance.raw==0)return up;
    return fxNormalize(up*fxCosTurns(turns)+
        (tangent/distance)*fxSinTurns(turns));
}
}
inline PlanetProcessOverlays buildPlanetProcessOverlays(
        const editor::MapDoc& doc,int bodyIndex,const StudioPlanetPreview& preview){
    PlanetProcessOverlays out;
    if(bodyIndex<0||size_t(bodyIndex)>=doc.data.bodies.size())return out;
    const auto& body=doc.data.bodies[size_t(bodyIndex)];
    if(body.id!=preview.bodyId||body.faceSize!=preview.faceSize||
       body.faceSize<=0||preview.radius<=0)return out;
    game::CubeSphere cube(body.faceSize);
    const auto radius=StudioPlanetPreview::fixed(preview.radius);
    const float lift=std::clamp(preview.radius*.004f,.12f,2.f);
    constexpr size_t budget=8192;
    const auto add=[&](render::float3 a,render::float3 b,uint32_t color,const std::string& owner){
        if(out.lines.size()>=budget){out.truncated=true;return;}
        out.lines.push_back({a,b,color,owner});
    };
    for(const auto& entity:doc.data.entities){
        if(out.truncated)break;
        if(entity.at.getStr("body")!=body.id||entity.id.empty())continue;
        const auto owner=preview.authoredIds.find(entity.id);
        if(owner==preview.authoredIds.end())continue;
        const auto& c=entity.extra["components"];
        const auto& v=c["EvolveArea"];
        if(!c["EvolveRegion"].isObject()||!v.isObject()||!v.getBool("enabled",true))continue;
        evolve::Area area{v.getInt("offsetXcm",0),v.getInt("offsetZcm",0),
            v.getInt("halfWidthCm",1000),v.getInt("halfDepthCm",1000),true};
        if(!evolve::valid(area))continue;
        const auto up=cube.dirFromFace(int(entity.at.getInt("face")),entity.at["x"].fx(),entity.at["y"].fx());
        int face;Fixed a,b;cube.faceFromDir(up,face,a,b);
        Vec3 east,south;game::CubeSphere::faceTangents(face,up,east,south);
        const int64_t x0=area.centerXcm-area.halfWidthCm,x1=area.centerXcm+area.halfWidthCm;
        const int64_t z0=area.centerZcm-area.halfDepthCm,z1=area.centerZcm+area.halfDepthCm;
        const std::array<std::pair<int64_t,int64_t>,4> corners{{{x0,z0},{x1,z0},{x1,z1},{x0,z1}}};
        for(size_t edge=0;edge<4;++edge){
            const auto [ax,az]=corners[edge];const auto [bx,bz]=corners[(edge+1)%4];
            std::optional<render::float3> prev;
            for(int i=0;i<=16;++i){
                const auto x=Fixed::fromRatio(ax*(16-i)+bx*i,1600);
                const auto z=Fixed::fromRatio(az*(16-i)+bz*i,1600);
                const auto dir=planet_overlay_detail::regionDirection(up,east,south,radius,x,z);
                if(!dir){prev.reset();out.regionChartClipped=true;continue;}
                const auto point=planet_overlay_detail::surfacePoint(body,cube,*dir,preview.radius,lift);
                if(prev)add(*prev,point,render::rgba(105,235,125),owner->second);
                prev=point;
            }
        }
    }
    const auto& config=editor::physis_authoring::read(doc);
    if(config.isNull()||!config.getBool("enabled",true))return out;
    try{
        physis::InstallOptions options;
        options.enableGravityFields=config.getBool("enableGravityFields",false);
        options.enableWindZones=config.getBool("enableWindZones",false);
        options.enableStorms=config.getBool("enableStorms",false);
        const auto fields=physis::parseFieldAssets(config,options);
        const auto anchor=game::Surface::anchor(bodyIndex);
        // Each arrow represents an individual authored field's contribution,
        // NOT effective combined runtime gravity or the host baseline.
        for(const auto& asset:fields){
            if(out.truncated)break;
            if(asset.kind!=physis::AuthoredFieldKind::Gravity)continue;
            const auto& field=std::get<physis::GravityField>(asset.data);
            if(!field.enabled)continue;
            const auto color=field.mode==physis::GravityMode::Repel?render::rgba(255,125,130):
                field.mode==physis::GravityMode::Attract?render::rgba(115,180,255):render::rgba(240,200,100);
            for(int f=0;f<6;++f)for(int y=0;y<4;++y)for(int x=0;x<4;++x){
                if(out.truncated)break;
                const auto dir=cube.dirFromFace(f,Fixed::fromRatio((2*x+1)*body.faceSize,8),
                    Fixed::fromRatio((2*y+1)*body.faceSize,8));
                const auto ground=planet_overlay_detail::surfacePoint(body,cube,dir,preview.radius,0);
                const Vec3 pos=anchor+Vec3{StudioPlanetPreview::fixed(ground.x),
                    StudioPlanetPreview::fixed(ground.y),StudioPlanetPreview::fixed(ground.z)};
                const auto weight=physis::gravityWeight(field,pos);
                if(weight.raw<=0)continue;
                const auto g=physis::gravityContribution(field,pos,weight);
                const auto magnitude=g.length();
                if(magnitude<Fixed::fromRatio(1,100))continue;
                const auto arrow=StudioPlanetPreview::f3(g/magnitude);
                const float len=std::clamp(float(magnitude.toDouble())*.2f,.1f,
                    std::max(.1f,preview.radius*.12f));
                // Place inward arrows above ground, so their tips remain visible.
                const auto start=ground+StudioPlanetPreview::f3(dir)*(lift+len);
                const auto end=start+arrow*len;
                add(start,end,color,{});
                const auto side=render::normalize(render::cross(arrow,
                    std::abs(arrow.y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                add(end,end-arrow*(len*.25f)+side*(len*.14f),color,{});
                add(end,end-arrow*(len*.25f)-side*(len*.14f),color,{});
            }
        }
    }catch(const std::exception&){out.invalidPhysis=true;}
    return out;
}
} // namespace aiverse::studio
