#pragma once
#include "engine/render/Model.h"
#include "apps/editor/core/MapDoc.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aiverse::editor {
inline bool isTerrainTile(const DocEntity& entity) {
    return entity.type.rfind("TerraTerrain",0)==0 || entity.extra["components"].has("TerraTerrain");
}

// Highest submitted terrain triangle at each half-metre lattice point.
class TerrainTileSurface {
    static constexpr float kStep=.5f;
    int surfaceGridW_,surfaceGridH_;
    std::vector<float> surfaceGrid_;
public:
    TerrainTileSurface(int width,int height)
        :surfaceGridW_(std::clamp(width,0,4096)*2+1),surfaceGridH_(std::clamp(height,0,4096)*2+1) {
        surfaceGridW_=std::min(surfaceGridW_,8192);
        surfaceGridH_=std::min(surfaceGridH_,8192);
        surfaceGrid_.assign(size_t(surfaceGridW_)*surfaceGridH_,-std::numeric_limits<float>::infinity());
    }
    void add(const render::Model& m,const render::mat4& world,const std::vector<render::mat4>& pose={}) {
        if(m.bindWorld.size()!=m.nodes.size())return;
        for(size_t n=0;n<m.nodes.size();++n){
            const auto& node=m.nodes[n];
            if(node.mesh<0||size_t(node.mesh)>=m.meshes.size())continue;
            const render::mat4 xf=world*(pose.size()==m.nodes.size()?pose[n]:m.bindWorld[n]);
            for(const auto& prim:m.meshes[size_t(node.mesh)].primitives){
                if(prim.firstIndex>m.indices.size()||prim.indexCount>m.indices.size()-prim.firstIndex)continue;
                for(size_t t=0;t+2<prim.indexCount;t+=3){
                    render::float3 v[3];bool ok=true;
                    for(int k=0;k<3;++k){const uint32_t idx=m.indices[prim.firstIndex+t+size_t(k)];if(idx>=m.vertices.size()){ok=false;break;}v[k]=xf.transformPoint(m.vertices[idx].pos);}
                    if(!ok)continue;
                    const float area=(v[1].x-v[0].x)*(v[2].z-v[0].z)-(v[1].z-v[0].z)*(v[2].x-v[0].x);
                    if(!std::isfinite(area)||std::fabs(area)<1e-8f)continue;
                    const int x0=std::max(0,int(std::floor(std::clamp(std::min({v[0].x,v[1].x,v[2].x})/kStep,-1.f,8192.f))));
                    const int x1=std::min(surfaceGridW_-1,int(std::ceil(std::clamp(std::max({v[0].x,v[1].x,v[2].x})/kStep,-1.f,8192.f))));
                    const int z0=std::max(0,int(std::floor(std::clamp(std::min({v[0].z,v[1].z,v[2].z})/kStep,-1.f,8192.f))));
                    const int z1=std::min(surfaceGridH_-1,int(std::ceil(std::clamp(std::max({v[0].z,v[1].z,v[2].z})/kStep,-1.f,8192.f))));
                    for(int gz=z0;gz<=z1;++gz)for(int gx=x0;gx<=x1;++gx){
                        const float px=float(gx)*kStep,pz=float(gz)*kStep;
                        const float w0=((v[1].x-px)*(v[2].z-pz)-(v[1].z-pz)*(v[2].x-px))/area;
                        const float w1=((v[2].x-px)*(v[0].z-pz)-(v[2].z-pz)*(v[0].x-px))/area;
                        const float w2=1.f-w0-w1;
                        if(w0<-1e-4f||w1<-1e-4f||w2<-1e-4f)continue;
                        float& cell=surfaceGrid_[size_t(gz)*size_t(surfaceGridW_)+size_t(gx)];
                        cell=std::max(cell,w0*v[0].y+w1*v[1].y+w2*v[2].y);
                    }
                }
            }
        }
    }
    float sample(float x,float z,float legacy) const {
        if(surfaceGrid_.empty())return legacy;
        // Bilinear over covered samples; uncovered -> the legacy surface.
        const float fx=x/kStep,fz=z/kStep;
        if(!(fx>-1 && fz>-1 && fx<surfaceGridW_ && fz<surfaceGridH_))return legacy;
        const int gx=int(std::floor(fx)),gz=int(std::floor(fz));
        float sum=0,weight=0;
        for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){
            const int cx=gx+dx,cz=gz+dz;
            if(cx<0||cz<0||cx>=surfaceGridW_||cz>=surfaceGridH_)continue;
            const float h=surfaceGrid_[size_t(cz)*size_t(surfaceGridW_)+size_t(cx)];
            if(!std::isfinite(h))continue;
            const float w=(dx?fx-float(gx):1.f-(fx-float(gx)))*(dz?fz-float(gz):1.f-(fz-float(gz)));
            sum+=h*w;weight+=w;
        }
        return weight>1e-6f?sum/weight:legacy;
    }
};
}
