#pragma once
// TerraWeave Caverns: CPU-only world-space density and marching tetrahedra mesh.
// Positive density = cave void. Visual mesh is chunk-local in metres.
#include "TerraWeaveCaverns.h"
#include <array>
#include <cmath>
namespace aiverse::terraweave::caverns {
struct MeshVertex { float x=0,y=0,z=0,nx=0,ny=1,nz=0; };
struct CaveMesh { ChunkKey3 key{}; std::vector<MeshVertex> vertices; std::vector<uint32_t> indices; };
inline int64_t caveFieldCmUnchecked(const Settings& s, PointCm p) {
    constexpr int64_t cap=100000;
    if(p.y<s.minYcm || p.y>=s.maxYcm) return -cap;
    const int64_t cx=floorDiv(p.x,s.cellCm),cy=floorDiv(p.y,s.cellCm),cz=floorDiv(p.z,s.cellCm);
    int64_t best=-cap;
    auto eval=[&](PointCm q,PointCm a,PointCm b,int64_t r){
        const int64_t dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z;
        const int64_t vx=q.x-a.x,vy=q.y-a.y,vz=q.z-a.z;
        const int64_t den=dx*dx+dy*dy+dz*dz,dot=vx*dx+vy*dy+vz*dz;
        const int64_t t=den?std::clamp<int64_t>(dot*65536/den,0,65536):0;
        const int64_t ex=vx-dx*t/65536,ey=vy-dy*t/65536,ez=vz-dz*t/65536;
        best=std::max(best,std::clamp<int64_t>((r*r-ex*ex-ey*ey-ez*ez)/(2*r),-cap,cap));
    };
    for(int64_t z=cz-1;z<=cz+1;++z)
    for(int64_t y=cy-1;y<=cy+1;++y)
    for(int64_t x=cx-1;x<=cx+1;++x) {
        const PointCm a=center(s,x,y,z);
        if(hash(s.seed,x,y,z,19)%1000<s.chamberChance)eval(p,a,a,s.chamberRadiusCm);
        for(int axis=0;axis<3;++axis) {
            if(hash(s.seed,x,y,z,101+uint64_t(axis))%1000>=s.tunnelChance)continue;
            eval(p,a,center(s,x+(axis==0),y+(axis==1),z+(axis==2)),s.tunnelRadiusCm);
        }
    }
    return std::min({best,p.y-s.minYcm,s.maxYcm-p.y});
}
inline int64_t caveFieldCm(const Settings& s,PointCm p) {
    validate(s);validatePoint(p);return caveFieldCmUnchecked(s,p);
}
template<class Field>
inline CaveMesh generateCaveMeshSampled(const Settings& s,ChunkKey3 key,uint32_t resolution,int32_t spacingCm,Field field) {
    validate(s);
    if(resolution==0||resolution>64||spacingCm<=0||spacingCm>10000)
        throw std::invalid_argument("TerraWeave Caverns mesh settings");
    const int64_t span=int64_t(resolution)*spacingCm,maxKey=(kLimitCm-span-spacingCm)/span;
    if(key.x< -maxKey||key.x>maxKey||key.y< -maxKey||key.y>maxKey||
       key.z< -maxKey||key.z>maxKey)throw std::out_of_range("TerraWeave Caverns mesh extent");
    CaveMesh out;out.key=key;
    const PointCm origin{key.x*span,key.y*span,key.z*span};
    const size_t n=size_t(resolution)+1;
    std::vector<int64_t> samples(n*n*n);
    const auto idx=[n](size_t x,size_t y,size_t z){return (z*n+y)*n+x;};
    for(uint32_t z=0;z<=resolution;++z)
    for(uint32_t y=0;y<=resolution;++y)
    for(uint32_t x=0;x<=resolution;++x)
        samples[idx(x,y,z)]=field({origin.x+int64_t(x)*spacingCm,
            origin.y+int64_t(y)*spacingCm,origin.z+int64_t(z)*spacingCm});
    struct Node{PointCm p;int64_t v;};
    constexpr int corners[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},
        {0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    // Globally constant cube decomposition: face diagonals agree at chunk borders.
    constexpr int tetra[6][4]={{0,1,2,6},{0,2,3,6},{0,3,7,6},
        {0,7,4,6},{0,4,5,6},{0,5,1,6}};
    auto vert=[&](const Node& a,const Node& b)->MeshVertex {
        const double t=a.v==b.v?0.5:std::clamp(double(a.v)/(double(a.v)-double(b.v)),0.0,1.0);
        const PointCm p{int64_t(std::llround(double(a.p.x)+double(b.p.x-a.p.x)*t)),
            int64_t(std::llround(double(a.p.y)+double(b.p.y-a.p.y)*t)),
            int64_t(std::llround(double(a.p.z)+double(b.p.z-a.p.z)*t))};
        const int64_t d=std::max<int64_t>(1,spacingCm/2);
        const double gx=double(field({p.x+d,p.y,p.z})-field({p.x-d,p.y,p.z}));
        const double gy=double(field({p.x,p.y+d,p.z})-field({p.x,p.y-d,p.z}));
        const double gz=double(field({p.x,p.y,p.z+d})-field({p.x,p.y,p.z-d}));
        const double len=std::sqrt(gx*gx+gy*gy+gz*gz);
        return {float(double(p.x-origin.x)/100),float(double(p.y-origin.y)/100),
            float(double(p.z-origin.z)/100),float(len?-gx/len:0),
            float(len?-gy/len:1),float(len?-gz/len:0)};
    };
    auto emit=[&](MeshVertex a,MeshVertex b,MeshVertex c){
        const double ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
        const double vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        const double facing=(uy*vz-uz*vy)*a.nx+(uz*vx-ux*vz)*a.ny+(ux*vy-uy*vx)*a.nz;
        if(facing<0)std::swap(b,c);
        const uint32_t i=uint32_t(out.vertices.size());
        out.vertices.push_back(a);out.vertices.push_back(b);out.vertices.push_back(c);
        out.indices.push_back(i);out.indices.push_back(i+1);out.indices.push_back(i+2);
    };
    for(uint32_t z=0;z<resolution;++z)
    for(uint32_t y=0;y<resolution;++y)
    for(uint32_t x=0;x<resolution;++x) {
        std::array<Node,8> c{};bool pos=false,neg=false;
        for(int j=0;j<8;++j){
            const uint32_t xx=x+corners[j][0],yy=y+corners[j][1],zz=z+corners[j][2];
            c[j]={{origin.x+int64_t(xx)*spacingCm,origin.y+int64_t(yy)*spacingCm,
                origin.z+int64_t(zz)*spacingCm},samples[idx(xx,yy,zz)]};
            pos|=c[j].v>0;neg|=c[j].v<=0;
        }
        if(!(pos&&neg))continue;
        for(const auto& t:tetra){
            int inside[4],outside[4],ic=0,oc=0;
            for(int j=0;j<4;++j)(c[t[j]].v>0?inside[ic++]:outside[oc++])=t[j];
            if(ic==0||ic==4)continue;
            auto edge=[&](int i,int j){return vert(c[i],c[j]);};
            if(ic==1)emit(edge(inside[0],outside[0]),edge(inside[0],outside[1]),edge(inside[0],outside[2]));
            else if(ic==3)emit(edge(outside[0],inside[0]),edge(outside[0],inside[1]),edge(outside[0],inside[2]));
            else {
                auto a=edge(inside[0],outside[0]),b=edge(inside[0],outside[1]);
                auto d=edge(inside[1],outside[0]),e=edge(inside[1],outside[1]);
                emit(a,b,d);emit(b,e,d);
            }
        }
    }
    return out;
}
inline CaveMesh generateCaveMesh(const Settings& s,ChunkKey3 key,uint32_t resolution=16,int32_t spacingCm=100) {
    return generateCaveMeshSampled(s,key,resolution,spacingCm,
        [&](PointCm p){return caveFieldCmUnchecked(s,p);});
}
} // namespace aiverse::terraweave::caverns
