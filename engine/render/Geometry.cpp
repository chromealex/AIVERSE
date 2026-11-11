#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <numeric>

namespace aiverse::render {
GeometryPolicy parseGeometryPolicy(const Json& j) {
    GeometryPolicy p;
    if (j.isNull()) return p;
    if (!j.isObject()) throw std::runtime_error("geometry: expected an object");
    for (auto& [k, v] : j.members()) {
        if (k == "errorPixels") {
            if (!v.isNumber() || !std::isfinite(v.toDouble()) || v.toDouble() < .1 || v.toDouble() > 16)
                throw std::runtime_error("geometry.errorPixels: expected 0.1..16");
            p.errorPixels = float(v.toDouble());
        } else if (k == "clusters" || k == "occlusion" || k == "occluder") {
            if (v.type() != Json::Type::Bool) throw std::runtime_error("geometry." + k + ": expected boolean");
            if (k == "clusters") p.clusters = v.boolean();
            if (k == "occlusion") p.occlusion = v.boolean();
            if (k == "occluder") p.occluder = v.boolean();
        } else throw std::runtime_error("geometry: unknown key '" + k + "'");
    }
    return p;
}
namespace {
using Triangle = std::array<uint32_t, 3>;
float axis(float3 v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
bool finite(float4 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w); }
// Attribute seams and cluster boundaries are locked. Endpoint collapse uses original vertices;
// accumulated edge lengths bound displacement, including successive collapses.
std::vector<Triangle> coarse(const Model& m, std::vector<Triangle> tris, float& error) {
    std::map<std::pair<uint32_t,uint32_t>, int> edges;
    for (auto t : tris) for (int i=0;i<3;++i) ++edges[std::minmax(t[i],t[(i+1)%3])];
    std::set<uint32_t> locked;
    for (auto [e,n] : edges) if (n != 2) { locked.insert(e.first); locked.insert(e.second); }
    std::map<uint32_t,float> displacement;
    const size_t target = tris.size()/2;
    for (size_t pass=0;pass<128 && tris.size()>target;++pass) {
        float best=std::numeric_limits<float>::infinity(); uint32_t from=0,to=0;
        for (auto t:tris) for(int e=0;e<3;++e) {
            const auto a=t[e],b=t[(e+1)%3];
            if (locked.count(a) || locked.count(b)) continue;
            const auto& x=m.vertices[a]; const auto& y=m.vertices[b];
            if (x.color!=y.color || x.tangent.w!=y.tangent.w || dot(float3{x.tangent.x,x.tangent.y,x.tangent.z},float3{y.tangent.x,y.tangent.y,y.tangent.z})<.999f || dot(x.nrm,y.nrm)<.999f || std::fabs(x.uv.x-y.uv.x)>.02f || std::fabs(x.uv.y-y.uv.y)>.02f) continue;
            const float d=length(x.pos-y.pos);
            if (d>=best) continue;
            std::set<uint32_t> na,nb,opposite;int incident=0;
            for(auto q:tris){bool ha=false,hb=false;for(auto v:q){ha|=v==a;hb|=v==b;}
                if(ha)for(auto v:q)if(v!=a)na.insert(v);
                if(hb)for(auto v:q)if(v!=b)nb.insert(v);
                if(ha&&hb){++incident;for(auto v:q)if(v!=a&&v!=b)opposite.insert(v);}}
            std::set<uint32_t> common;for(auto v:na)if(nb.count(v))common.insert(v);
            if(incident!=2||common!=opposite)continue; // manifold link condition
            bool valid=true;std::set<Triangle> faces;
            for(auto q:tris) {
                auto r=q;for(auto& v:r)if(v==a)v=b;
                if(r[0]==r[1]||r[1]==r[2]||r[0]==r[2])continue;
                auto face=r;std::sort(face.begin(),face.end());
                if(!faces.insert(face).second){valid=false;break;} // never erase an isolated closed component
                const auto oldN=cross(m.vertices[q[1]].pos-m.vertices[q[0]].pos,m.vertices[q[2]].pos-m.vertices[q[0]].pos);
                const auto newN=cross(m.vertices[r[1]].pos-m.vertices[r[0]].pos,m.vertices[r[2]].pos-m.vertices[r[0]].pos);
                if(dot(oldN,newN)<=0) {valid=false;break;}
            }
            if(valid){best=d;from=a;to=b;}
        }
        if(!std::isfinite(best))break;
        displacement[to]=std::max(displacement[to],displacement[from]+best);
        error=std::max(error,displacement[to]);
        for(auto& t:tris)for(auto& v:t)if(v==from)v=to;
        std::erase_if(tris,[](auto t){return t[0]==t[1]||t[1]==t[2]||t[0]==t[2];});
    }
    return tris;
}
}
void prepareGeometry(Model& m, uint32_t size) {
    if(m.geometry || m.vb.valid() || m.vertices.empty())return;
    for(const auto& v:m.vertices)if(!std::isfinite(v.pos.x)||!std::isfinite(v.pos.y)||!std::isfinite(v.pos.z))throw std::runtime_error("geometry: nonfinite vertex");
    size=std::clamp(size,32u,256u);
    auto data=std::make_shared<GeometryData>();
    for(const auto& mesh:m.meshes) for(const auto& p:mesh.primitives) {
        data->primitives.emplace_back();data->occluders.emplace_back();
        if(!p.jointPalette.empty()||p.morphTargets||!m.skins.empty()||!m.clips.empty())continue;
        if(uint64_t(p.firstIndex)+p.indexCount>m.indices.size() || p.indexCount%3)throw std::runtime_error("geometry: invalid primitive index range");
        std::vector<Triangle> ts;
        for(uint32_t i=0;i<p.indexCount;i+=3){Triangle t;
            for(int k=0;k<3;++k){t[k]=m.indices[p.firstIndex+i+uint32_t(k)];if(t[k]>=m.vertices.size())throw std::runtime_error("geometry: invalid vertex index");}
            ts.push_back(t);
        }
        // A bounded subset still provides valid coverage; omitting triangles cannot hide an object.
        for(size_t i=0;i<ts.size() && i<1024;++i){auto t=ts[i];data->occluders.back().push_back({m.vertices[t[0]].pos,m.vertices[t[1]].pos,m.vertices[t[2]].pos});}
        if(ts.size()<size*2)continue;
        auto partition=[&](auto&& self,size_t begin,size_t end)->void {
            float3 mn{1e30f,1e30f,1e30f},mx{-1e30f,-1e30f,-1e30f};
            for(size_t i=begin;i<end;++i)for(auto v:ts[i]){mn=min3(mn,m.vertices[v].pos);mx=max3(mx,m.vertices[v].pos);}
            if(end-begin>size){auto extent=mx-mn;int a=extent.y>extent.x?1:0;if(extent.z>axis(extent,a))a=2;
                std::stable_sort(ts.begin()+begin,ts.begin()+end,[&](auto x,auto y){float sx=0,sy=0;for(int k=0;k<3;++k){sx+=axis(m.vertices[x[k]].pos,a);sy+=axis(m.vertices[y[k]].pos,a);}return sx<sy;});
                const size_t mid=(begin+end)/2;self(self,begin,mid);self(self,mid,end);return;}
            GeometryCluster c;c.min=mn;c.max=mx;c.firstIndex=uint32_t(m.indices.size());c.indexCount=uint32_t((end-begin)*3);
            for(size_t i=begin;i<end;++i)m.indices.insert(m.indices.end(),ts[i].begin(),ts[i].end());
            auto reduced=coarse(m,{ts.begin()+begin,ts.begin()+end},c.error);
            c.coarseFirst=uint32_t(m.indices.size());c.coarseCount=uint32_t(reduced.size()*3);
            for(auto t:reduced)m.indices.insert(m.indices.end(),t.begin(),t.end());
            data->primitives.back().push_back(c);
        };partition(partition,0,ts.size());
    }
    // Keep fine cluster ranges contiguous so a visible run can use a single indexed draw.
    const auto prepared=m.indices;
    size_t base=0;for(const auto& mesh:m.meshes)for(const auto& p:mesh.primitives)base=std::max(base,size_t(p.firstIndex)+p.indexCount);
    m.indices.resize(base);
    for(auto& clusters:data->primitives){
        for(auto& c:clusters){auto first=c.firstIndex;c.firstIndex=uint32_t(m.indices.size());m.indices.insert(m.indices.end(),prepared.begin()+first,prepared.begin()+first+c.indexCount);}
        for(auto& c:clusters){auto first=c.coarseFirst;c.coarseFirst=uint32_t(m.indices.size());m.indices.insert(m.indices.end(),prepared.begin()+first,prepared.begin()+first+c.coarseCount);}
    }
    m.geometry=std::move(data);
}
bool geometryBoxVisible(const mat4& clip,float3 mn,float3 mx) {
    int out[6]={};
    for(int c=0;c<8;++c){auto p=clip*float4{c&1?mx.x:mn.x,c&2?mx.y:mn.y,c&4?mx.z:mn.z,1};if(!finite(p))return true;
        out[0]+=p.x < -p.w;out[1]+=p.x>p.w;out[2]+=p.y < -p.w;out[3]+=p.y>p.w;out[4]+=p.z < -p.w;out[5]+=p.z>p.w;}
    for(int n:out)if(n==8)return false;
    return true;
}
float geometryErrorPixels(const mat4& clip,float3 mn,float3 mx,float error,int width,int height) {
    float w=1e30f,x=0,y=0;
    for(int c=0;c<8;++c){auto p=clip*float4{c&1?mx.x:mn.x,c&2?mx.y:mn.y,c&4?mx.z:mn.z,1};
        if(!finite(p)||p.w<=0||p.z < 0)return 1e30f;
        w=std::min(w,p.w);x=std::max(x,std::fabs(p.x));y=std::max(y,std::fabs(p.y));}
    auto norm=[&](int r){return length({clip.at(r,0),clip.at(r,1),clip.at(r,2)});};
    const float dw=norm(3)*error;
    if(w<=dw)return 1e30f;
    return .5f*std::max(width*error*(norm(0)*w+x*norm(3)),height*error*(norm(1)*w+y*norm(3)))/(w*(w-dw));
}
void GeometryOcclusion::clear(bool z){zeroToOne_=z;depth_.fill(std::numeric_limits<float>::infinity());}
void GeometryOcclusion::triangle(const mat4& clip,const std::array<float3,3>& v,bool doubleSided){
    float3 p[3];float z=-1;
    for(int i=0;i<3;++i){auto q=clip*float4{v[i].x,v[i].y,v[i].z,1};
        if(!finite(q)||q.w<=1e-6f||q.z<(zeroToOne_?0:-q.w)||q.z>q.w)return;
        p[i]={(q.x/q.w*.5f+.5f)*kWidth,(q.y/q.w*.5f+.5f)*kHeight,q.z/q.w};
        if(!std::isfinite(p[i].x)||!std::isfinite(p[i].y)||!std::isfinite(p[i].z))return;z=std::max(z,p[i].z);}
    auto edge=[](float3 a,float3 b,float x,float y){return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);};
    float area=edge(p[0],p[1],p[2].x,p[2].y);if(!std::isfinite(area)||std::fabs(area)<1e-6f)return;
    if(area<0){if(!doubleSided)return;std::swap(p[1],p[2]);}
    // Clamp before integer conversion even for enormous offscreen triangles.
    int x0=int(std::clamp(std::floor(std::min({p[0].x,p[1].x,p[2].x})),0.f,float(kWidth)));
    int x1=int(std::clamp(std::ceil(std::max({p[0].x,p[1].x,p[2].x})),0.f,float(kWidth)));
    int y0=int(std::clamp(std::floor(std::min({p[0].y,p[1].y,p[2].y})),0.f,float(kHeight)));
    int y1=int(std::clamp(std::ceil(std::max({p[0].y,p[1].y,p[2].y})),0.f,float(kHeight)));
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){bool full=true;
        for(int e=0;e<3;++e)for(int c=0;c<4;++c)if(edge(p[e],p[(e+1)%3],float(x+(c&1)),float(y+((c>>1)&1)))<1e-4f)full=false;
        if(full)depth_[size_t(y*kWidth+x)]=std::min(depth_[size_t(y*kWidth+x)],z+1e-4f);
    }
}
bool GeometryOcclusion::hidden(const mat4& clip,float3 mn,float3 mx)const{
    float x0=kWidth,y0=kHeight,x1=0,y1=0,z=1;
    for(int c=0;c<8;++c){auto q=clip*float4{c&1?mx.x:mn.x,c&2?mx.y:mn.y,c&4?mx.z:mn.z,1};
        if(!finite(q)||q.w<=1e-6f||q.z<(zeroToOne_?0:-q.w)||q.z>q.w)return false;
        float x=(q.x/q.w*.5f+.5f)*kWidth,y=(q.y/q.w*.5f+.5f)*kHeight;
        if(x<0||x>=kWidth||y<0||y>=kHeight)return false;
        x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);z=std::min(z,q.z/q.w);}
    for(int y=int(y0);y<=int(y1);++y)for(int x=int(x0);x<=int(x1);++x)if(z<=depth_[size_t(y*kWidth+x)])return false;
    return true;
}
}
