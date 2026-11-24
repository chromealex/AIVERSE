#pragma once
// A planetary cave region is a rigid local chart, not a global heightfield.
// Persist the authored anchor (body ID, face and fixed face coordinates).
// Surface's simulation anchor is distinct from the orbit/render centre.
#include "engine/game/Surface.h"
#include "engine/core/FixedMath.h"
#include "engine/world/TerraWeaveCavernsPlanetRegion.h"
#include <memory>
#include <string>
#include <stdexcept>

namespace aiverse::terraweave::caverns {
class CavePlanetFrame {
public:
    static CavePlanetFrame make(std::shared_ptr<const game::Surface> surface,
                               const CavePlanetAnchor& anchor){
        if(!surface||!surface->tab||anchor.bodyId.empty()||anchor.bodyId.size()>128||
           anchor.face<0||anchor.face>=6||anchor.halfExtentCm<1||anchor.halfExtentCm>100000)
            throw std::invalid_argument("Caverns planet anchor");
        const int body=surface->findBody(anchor.bodyId);
        if(body<0||size_t(body)>=surface->bodies.size())throw std::invalid_argument("Caverns planet body not found");
        const auto& def=surface->bodies[size_t(body)];
        if((def.kind!=game::BodyKind::Planet&&def.kind!=game::BodyKind::Moon)||
           !def.hasSurface()||def.radius<=Fixed{}||def.radius>Fixed::fromInt(1500)||
           anchor.a<Fixed{}||anchor.b<Fixed{}||anchor.a>Fixed::fromInt(def.faceSize)||anchor.b>Fixed::fromInt(def.faceSize))
            throw std::invalid_argument("Caverns planet body or face extent");
        const auto extent=Fixed::fromRatio(anchor.halfExtentCm,100);
        // A bounded chart stays away from the centre and adjacent body's
        // simulation namespace. Large regions need multiple owned charts.
        if(extent>def.radius/8)throw std::invalid_argument("Caverns planet chart exceeds curvature budget");
        CavePlanetFrame out;
        out.anchor_=anchor;out.body_=body;out.surface_=std::move(surface);
        const auto local=out.surface_->facePoint(body,anchor.face,anchor.a,anchor.b,false);
        out.origin_=local;
        out.up_=fxNormalize(local);
        Vec3 east,south;
        game::CubeSphere::faceTangents(anchor.face,out.up_,east,south);
        // Face tangents on warped cube faces need not be orthogonal.
        // Construct a right-handed basis with +Y radial outward.
        out.east_=fxNormalize(east-out.up_*east.dot(out.up_));
        out.south_=fxNormalize(fxCross(out.east_,out.up_));
        out.east_=fxNormalize(fxCross(out.up_,out.south_));
        const auto tolerance=Fixed::fromRatio(1,256);
        const auto unit=Fixed::fromInt(1);
        for(auto v:{out.east_,out.up_,out.south_})
            if(fxAbs(v.lengthSq()-unit)>tolerance)throw std::invalid_argument("Caverns planet invalid chart basis");
        if(fxAbs(out.east_.dot(out.up_))>tolerance||fxAbs(out.east_.dot(out.south_))>tolerance||
           fxAbs(out.up_.dot(out.south_))>tolerance)throw std::invalid_argument("Caverns planet nonorthogonal chart");
        return out;
    }
    const CavePlanetAnchor& anchor()const noexcept{return anchor_;}
    int body()const noexcept{return body_;}
    const game::Surface& surface()const noexcept{return *surface_;}
    Vec3 originBody()const noexcept{return origin_;}
    Vec3 east()const noexcept{return east_;}
    Vec3 up()const noexcept{return up_;}
    Vec3 south()const noexcept{return south_;}
    Vec3 vectorToBody(Vec3 v)const{return east_*v.x+up_*v.y+south_*v.z;}
    Vec3 vectorFromBody(Vec3 v)const{return {v.dot(east_),v.dot(up_),v.dot(south_)};}
    Vec3 toBody(Vec3 local)const{checkLocal(local);return origin_+vectorToBody(local);}
    Vec3 fromBody(Vec3 p)const{auto local=vectorFromBody(p-origin_);checkLocal(local);return local;}
    Vec3 toSimulation(Vec3 local)const{return game::Surface::anchor(body_)+toBody(local);}
    Vec3 fromSimulation(Vec3 p)const{
        if(!game::Surface::onSphere(p)||surface_->bodyAt(p)!=body_)throw std::invalid_argument("Caverns position belongs to another body");
        return fromBody(p-game::Surface::anchor(body_));
    }
    Vec3 toRenderWorld(Vec3 local,Fixed seconds)const{return surface_->bodyPosition(body_,seconds)+toBody(local);}
    Vec3 fromRenderWorld(Vec3 world,Fixed seconds)const{return fromBody(world-surface_->bodyPosition(body_,seconds));}
    static Vec3 metres(PointCm p){
        validatePoint(p);
        return {Fixed::fromRatio(p.x,100),Fixed::fromRatio(p.y,100),Fixed::fromRatio(p.z,100)};
    }
    static PointCm centimetres(Vec3 p){
        const auto cm=[](Fixed v){const auto n=v.raw*100;return n>=0?(n+Fixed::kOne/2)/Fixed::kOne:(n-Fixed::kOne/2)/Fixed::kOne;};
        PointCm out{cm(p.x),cm(p.y),cm(p.z)};validatePoint(out);return out;
    }
    bool contains(PointCm p)const noexcept{
        const int64_t limit=anchor_.halfExtentCm;
        return p.x>=-limit&&p.x<=limit&&p.y>=-limit&&p.y<=limit&&p.z>=-limit&&p.z<=limit;
    }
    void checkLocal(Vec3 p)const{
        const auto cap=Fixed::fromRatio(anchor_.halfExtentCm,100);
        if(fxAbs(p.x)>cap||fxAbs(p.y)>cap||fxAbs(p.z)>cap)
            throw std::out_of_range("Caverns outside planetary chart");
    }
private:
    CavePlanetAnchor anchor_{};
    int body_=-1;
    std::shared_ptr<const game::Surface> surface_;
    Vec3 origin_{},east_{},up_{},south_{};
};
} // namespace aiverse::terraweave::caverns
