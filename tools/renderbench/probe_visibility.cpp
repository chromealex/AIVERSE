// Offline probe-visibility diagnostics for cached probe GI (.avgic).
// Reproduces the High (eight-probe) interpolation of LightShaders.h on the CPU for camera-ray
// receivers and compares the cached plane visibility with exact BVH visibility.
#include "apps/renderbench/LightCacheKey.h"
#include "apps/renderbench/ShadowGeometry.h"
#include "engine/core/Hash.h"
#include "engine/data/Json.h"
#include "engine/platform/Platform.h"
#include "engine/render/LightGridCache.h"
#include "engine/render/Model.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace aiverse;
using namespace aiverse::render;

namespace {
float3 v3(const Json& j) {return {float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}

uint64_t recipeKey(const Json& config) {return bench::lightCacheKey(config);}

// Same math as probePlaneVisible / probeVisibility(highQuality) in LightShaders.h.
float planeVisible(float4 plane,float3 q,float distance) {
    float3 n{plane.y,plane.z,plane.w};float normal2=dot(n,n),ratio=dot(n,q);
    if(ratio>0&&ratio*ratio>.0001f*normal2*distance*distance)return ratio<=1+.03f*std::sqrt(normal2)?1.0f:0.0f;
    return distance-.03f<=plane.x?1.0f:0.0f;
}
void cubeTexel(float3 q,int& face,float2& texel) {
    float3 a{std::abs(q.x),std::abs(q.y),std::abs(q.z)};float2 uv;
    if(a.x>=a.y&&a.x>=a.z){face=q.x>0?0:1;uv={(q.x>0?-q.z:q.z)/a.x,q.y/a.x};}
    else if(a.y>=a.z){face=q.y>0?2:3;uv={q.x/a.y,(q.y>0?-q.z:q.z)/a.y};}
    else{face=q.z>0?4:5;uv={(q.z>0?q.x:-q.x)/a.z,q.y/a.z};}
    texel={(uv.x*.5f+.5f)*4-.5f,(uv.y*.5f+.5f)*4-.5f};
}
float planeVisibility(const std::vector<float4>& t,int offset,float3 q,float distance) {
    int face;float2 texel;cubeTexel(q,face,texel);
    float fx=texel.x-std::floor(texel.x),fy=texel.y-std::floor(texel.y),visible=0;
    for(int tap=0;tap<4;++tap){int cx=tap%2,cy=tap/2;
        int px=std::clamp(int(std::floor(texel.x))+cx,0,3),py=std::clamp(int(std::floor(texel.y))+cy,0,3);
        float w=(cx?fx:1-fx)*(cy?fy:1-fy);
        visible+=w*planeVisible(t[size_t(offset+4+face*16+py*4+px)],q,distance);
    }
    return visible;
}

struct Scene {
    Json config;StaticShadowGeometry geometry;StaticLightGrid grid;float3 giSun,giSky;
    std::vector<float4> moments; // grid texels with moment visibility (rebaked)
    float normalBias=-1;bool facing=true; // <0: shader default .24 x probe spacing
};

// Replace the hit-plane visibility of every cached probe with engine-baked distance moments
// (StaticShadowGeometry::bakeMoments) and flag the cells (mask bit 16). Transport is untouched.
std::vector<float4> rebakeMoments(const Scene& s,int sub) {
    auto t=s.grid.texels;const auto o=s.grid.origin,sz=s.grid.size;const float radius=2.5f/o.w;
    std::vector<std::pair<int,float3>> probes;
    for(int z=0;z<int(sz.z);++z)for(int y=0;y<int(sz.y);++y)for(int x=0;x<int(sz.x);++x){
        auto& header=t[size_t(x+sz.x*(y+sz.y*z))];int offset=int(header.z);if(offset<=0)continue;
        header.w=float(int(header.w)|16);
        probes.push_back({offset,float3{o.x,o.y,o.z}+float3{x+.5f,y+.5f,z+.5f}*(1/o.w)});
    }
    std::atomic<size_t> next{0};
    auto work=[&](){for(size_t i;(i=next++)<probes.size();){auto [offset,position]=probes[i];
        auto v=s.geometry.bakeMoments(position,radius,4,sub);
        for(size_t texel=0;texel<96;++texel)t[size_t(offset+4)+texel]={v.distances[texel],v.squares[texel],0,0};
    }};
    std::vector<std::thread> pool;for(unsigned i=0;i<std::max(1u,std::thread::hardware_concurrency());++i)pool.emplace_back(work);for(auto& th:pool)th.join();
    return t;
}
// Chebyshev visibility on bilinearly interpolated moments (four taps, as the plane path).
float momentVisibility(const std::vector<float4>& t,int offset,float3 q,float distance) {
    int face;float2 texel;cubeTexel(q,face,texel);
    float fx=texel.x-std::floor(texel.x),fy=texel.y-std::floor(texel.y),m1=0,m2=0;
    for(int tap=0;tap<4;++tap){int cx=tap%2,cy=tap/2;
        int px=std::clamp(int(std::floor(texel.x))+cx,0,3),py=std::clamp(int(std::floor(texel.y))+cy,0,3);
        float w=(cx?fx:1-fx)*(cy?fy:1-fy);auto m=t[size_t(offset+4+face*16+py*4+px)];m1+=w*m.x;m2+=w*m.y;
    }
    if(distance<=m1)return 1;
    float variance=std::max(m2-m1*m1,1e-4f),delta=distance-m1,v=variance/(variance+delta*delta);
    return v*v*v;
}

void loadScene(Scene& s,const std::string& sceneFile,const std::string& cache) {
    std::string text;if(!platform::readContentFile(sceneFile,text))throw std::runtime_error("cannot read scene");
    s.config=Json::parse(text);
    std::vector<std::string> warnings;
    auto model=loadModel(s.config.getStr("model"),platform::readContentFile,&warnings);
    bench::appendShadowGeometry(s.geometry,*model);model.reset();
    for(const auto& entry:s.config["models"].items()){
        auto m=loadModel(entry.getStr("path"),platform::readContentFile,&warnings);
        bench::appendShadowGeometry(s.geometry,*m,mat4::translate(entry["translation"].isArray()?v3(entry["translation"]):float3{}));
    }
    std::fprintf(stderr,"geometry: %zu triangles\n",s.geometry.triangles.size());
    s.geometry.build();
    std::string bytes;if(!platform::readContentFile(cache,bytes))throw std::runtime_error("cannot read cache");
    s.grid=decodeLightGrid(bytes,recipeKey(s.config));
    const auto& gi=s.config["probeGI"];
    s.giSun=gi["sun"].isArray()?v3(gi["sun"]):float3{};s.giSky=gi["sky"].isArray()?v3(gi["sky"]):float3{};
}

enum Mode {Plane=0,Exact=1,None=2,Moments=3};
// High-quality gridIndirect; returns radiance and total accepted weight.
bool verboseProbes=false;
float3 indirect(const Scene& s,float3 p,float3 n,Mode mode,float& total) {
    const auto& t=s.grid.texels;const auto o=s.grid.origin,sz=s.grid.size;
    float3 c=(p-float3{o.x,o.y,o.z})*o.w-float3{.5f,.5f,.5f};
    float3 base{std::floor(c.x),std::floor(c.y),std::floor(c.z)},f=c-base;
    float3 result{};total=0;
    for(int i=0;i<8;++i){
        float3 corner{float(i%2),float((i/2)%2),float(i/4)},cell=base+corner;
        if(cell.x<0||cell.y<0||cell.z<0||cell.x>=sz.x||cell.y>=sz.y||cell.z>=sz.z)continue;
        int index=int(cell.x+sz.x*(cell.y+sz.y*cell.z));auto header=t[size_t(index)];int offset=int(header.z);if(offset<=0)continue;
        float w=(corner.x>0?f.x:1-f.x)*(corner.y>0?f.y:1-f.y)*(corner.z>0?f.z:1-f.z);if(w<=1e-6f)continue;
        float3 position=float3{o.x,o.y,o.z}+(cell+float3{.5f,.5f,.5f})*(1/o.w);
        float3 q=p-position;float d=length(q);float vis=1;
        if(mode==Moments){
            float3 toProbe=d>1e-5f?q*(-1/d):n;float facing=(dot(toProbe,n)+1)*.5f;if(s.facing)w*=facing*facing+.2f;
            float3 qb=q+n*(s.normalBias<0?.24f/o.w:s.normalBias);float db=length(qb);
            vis=db>1e-5f?std::max(momentVisibility(s.moments,offset,qb,db),.05f):1;
            float weight=w*vis;if(weight<.2f*w&&weight>0)vis*=(weight/w)*(weight/w)/.04f;
        }else if(d>1e-5f){
            if(mode==Plane)vis=planeVisibility(t,offset,q,d);
            else if(mode==Exact)vis=s.geometry.distance(position,q*(1/d),d-.02f)>=d-.02f?1.0f:0.0f;
        }
        w*=vis;if(w<=0)continue;
        float4 nn{1,n.x,n.y,n.z};auto dot4=[&](int k){auto v=t[size_t(offset+k)];return v.x*nn.x+v.y*nn.y+v.z*nn.z+v.w*nn.w;};
        float3 value{};int mask=int(header.w);
        if(mask&2)value=value+float3{dot4(100)*s.giSun.x,dot4(101)*s.giSun.y,dot4(102)*s.giSun.z};
        if(mask&4)value=value+float3{dot4(103)*s.giSky.x,dot4(104)*s.giSky.y,dot4(105)*s.giSky.z};
        value={std::max(value.x,0.0f),std::max(value.y,0.0f),std::max(value.z,0.0f)};
        result=result+value*w;total+=w;
        if(verboseProbes){float sky=(mask&8)?dot4(106):-1;
            std::printf("  probe (%.2f %.2f %.2f) facing %.2f vis %.3f weight %.4f sky %.3f\n",position.x,position.y,position.z,(dot(d>1e-5f?q*(-1/d):n,n)+1)*.5f,vis,w,sky);}
    }
    return total>1e-5f?result*(1/total):float3{};
}

void writePpm(const std::string& path,int w,int h,const std::vector<float3>& px) {
    std::ofstream f(path,std::ios::binary);f<<"P6\n"<<w<<" "<<h<<"\n255\n";
    for(auto c:px)for(float v:{c.x,c.y,c.z}){float x=v/(1+v);f.put(char(std::clamp(int(std::pow(x,1/2.2f)*255+.5f),0,255)));}
}

// Rank sun directions by agreement with a lit/shadow mask of a reference frame (binary PGM, any size:
// 255 lit, 0 shadow, other values ignored). Score = balanced accuracy over lit and shadow pixels.
int sunFit(const Scene& s,const std::string& maskPath,int width,int height) {
    std::ifstream f(maskPath,std::ios::binary);std::string magic;int w=0,h=0,maxv=0;f>>magic>>w>>h>>maxv;f.get();
    if(magic!="P5"||w<=0||h<=0||maxv!=255)throw std::runtime_error("mask must be a binary 8-bit PGM");
    std::vector<uint8_t> mask(size_t(w*h));f.read(reinterpret_cast<char*>(mask.data()),std::streamsize(mask.size()));
    const auto& cam=s.config["cameras"][0];float3 eye=v3(cam["eye"]),target=v3(cam["target"]);
    float fov=float(cam["fovYDegrees"].toDouble(s.config["fovYDegrees"].toDouble(60)))*3.14159265f/180;
    float3 forward=normalize(target-eye),right=normalize(cross(forward,{0,1,0})),up=cross(right,forward);
    float scale=std::tan(fov*.5f),aspect=float(width)/height;
    struct Px{float3 p,n;bool lit;};std::vector<Px> px;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){uint8_t m=mask[size_t(y*w+x)];if(m!=0&&m!=255)continue;
        auto ray=normalize(forward+right*((2*(x+.5f)/w-1)*scale*aspect)+up*((1-2*(y+.5f)/h)*scale));float3 n;
        float d=s.geometry.distance(eye,ray,500,&n);if(d>=500)continue;if(dot(n,ray)>0)n=n*-1.0f;
        px.push_back({eye+ray*d+n*.03f,n,m==255});}
    struct Cand{float elevation,azimuth,score;};std::vector<Cand> cands;
    for(int e=15;e<=85;e+=5)for(int a=0;a<360;a+=10)cands.push_back({float(e),float(a),0});
    std::atomic<size_t> next{0};
    auto evaluate=[&](){next=0;auto work=[&](){for(size_t i;(i=next++)<cands.size();){auto& c=cands[i];float er=c.elevation*.0174533f,ar=c.azimuth*.0174533f;
        float3 sun{std::cos(er)*std::cos(ar),std::sin(er),std::cos(er)*std::sin(ar)};double litHit=0,litN=0,darkHit=0,darkN=0;
        for(const auto& q:px){bool on=dot(q.n,sun)>0&&s.geometry.distance(q.p,sun,200)>=200;
            if(q.lit){litN+=1;litHit+=on;}else{darkN+=1;darkHit+=!on;}}
        c.score=float(.5*(litHit/std::max(litN,1.0)+darkHit/std::max(darkN,1.0)));}};
        std::vector<std::thread> pool;for(unsigned i=0;i<std::max(1u,std::thread::hardware_concurrency());++i)pool.emplace_back(work);for(auto& t:pool)t.join();
        std::sort(cands.begin(),cands.end(),[](const Cand& a,const Cand& b){return a.score>b.score;});};
    evaluate();
    // Refine around the coarse optimum: 1 degree elevation, 1.5 degree azimuth.
    const Cand best=cands.front();cands.clear();
    for(float e=best.elevation-6;e<=best.elevation+6.01f;e+=1)for(float a=best.azimuth-12;a<=best.azimuth+12.01f;a+=1.5f)
        if(e>2&&e<89)cands.push_back({e,std::fmod(a+360.0f,360.0f),0});
    evaluate();
    for(size_t i=0;i<std::min<size_t>(10,cands.size());++i){const auto& c=cands[i];float er=c.elevation*.0174533f,ar=c.azimuth*.0174533f;
        std::printf("elevation %2.0f azimuth %3.0f score %.3f dir [%.4f, %.4f, %.4f]\n",c.elevation,c.azimuth,c.score,std::cos(er)*std::cos(ar),std::sin(er),std::cos(er)*std::sin(ar));}
    return 0;
}
// Lit/shadow image of the first camera for one sun (elevation, azimuth in degrees): 255 lit, 0 shadow.
int sunImage(const Scene& s,float elevation,float azimuth,int w,int h,const std::string& out) {
    const auto& cam=s.config["cameras"][0];float3 eye=v3(cam["eye"]),target=v3(cam["target"]);
    float fov=float(cam["fovYDegrees"].toDouble(s.config["fovYDegrees"].toDouble(60)))*3.14159265f/180;
    float3 forward=normalize(target-eye),right=normalize(cross(forward,{0,1,0})),up=cross(right,forward);
    float scale=std::tan(fov*.5f),aspect=float(w)/h,er=elevation*.0174533f,ar=azimuth*.0174533f;
    float3 sun{std::cos(er)*std::cos(ar),std::sin(er),std::cos(er)*std::sin(ar)};
    std::vector<uint8_t> img(size_t(w*h),0);std::atomic<int> row{0};
    auto work=[&](){for(int y;(y=row++)<h;)for(int x=0;x<w;++x){
        auto ray=normalize(forward+right*((2*(x+.5f)/w-1)*scale*aspect)+up*((1-2*(y+.5f)/h)*scale));float3 n;
        float d=s.geometry.distance(eye,ray,500,&n);if(d>=500){img[size_t(y*w+x)]=128;continue;}
        if(dot(n,ray)>0)n=n*-1.0f;
        float ndl=dot(n,sun);img[size_t(y*w+x)]=ndl>0&&s.geometry.distance(eye+ray*d+n*.03f,sun,200)>=200?uint8_t(60+195*ndl):0;}};
    std::vector<std::thread> pool;for(unsigned i=0;i<std::max(1u,std::thread::hardware_concurrency());++i)pool.emplace_back(work);for(auto& t:pool)t.join();
    std::ofstream f(out,std::ios::binary);f<<"P5\n"<<w<<" "<<h<<"\n255\n";f.write(reinterpret_cast<const char*>(img.data()),std::streamsize(img.size()));
    return 0;
}
int pixelProbe(const Scene& s,int px,int py,int width,int height) {
    const auto& cam=s.config["cameras"][0];float3 eye=v3(cam["eye"]),target=v3(cam["target"]);
    float fov=float(cam["fovYDegrees"].toDouble(s.config["fovYDegrees"].toDouble(60)))*3.14159265f/180;
    float3 forward=normalize(target-eye),right=normalize(cross(forward,{0,1,0})),up=cross(right,forward);
    float scale=std::tan(fov*.5f),aspect=float(width)/height;
    auto ray=normalize(forward+right*((2*(px+.5f)/width-1)*scale*aspect)+up*((1-2*(py+.5f)/height)*scale));float3 n;
    float d=s.geometry.distance(eye,ray,500,&n);if(d>=500){std::printf("sky\n");return 1;}
    if(dot(n,ray)>0)n=n*-1.0f;
    float3 p=eye+ray*d+n*.02f;
    const float3 sun=normalize(v3(s.config["sunDirection"]));
    // Reference: cosine-weighted hemisphere, one bounce of unit sun with display radiance albedo*ndl.
    float3 t=std::abs(n.y)<.9f?normalize(cross(n,{0,1,0})):normalize(cross(n,{1,0,0}));float3 b=cross(n,t);
    double E[3]={0,0,0},skyView=0;const int N=4096;
    for(int i=0;i<N;++i){float u=(i+.5f)/N,phi=i*2.39996323f,r=std::sqrt(u);
        float3 dir=normalize(t*(r*std::cos(phi))+b*(r*std::sin(phi))+n*std::sqrt(1-u));float3 hn,alb;
        float h=s.geometry.distance(p,dir,60,&hn,&alb);if(h>=60){if(dir.y>0)skyView+=1;continue;}
        if(dot(hn,dir)>0)hn=hn*-1.0f;
        float3 q=p+dir*h+hn*.04f;float ndl=std::max(0.0f,dot(hn,sun));
        if(ndl<=0||s.geometry.distance(q,sun,60)<60)continue;
        E[0]+=alb.x*ndl;E[1]+=alb.y*ndl;E[2]+=alb.z*ndl;}
    // cosine-weighted estimator: E = pi * mean(L) in physical units; engine display convention divides by pi.
    for(double& e:E)e/=N;
    verboseProbes=true;float tot;auto cache=indirect(s,p,n,s.moments.empty()?Plane:Moments,tot);verboseProbes=false;
    std::printf("pixel %d,%d pos (%.2f %.2f %.2f) n (%.2f %.2f %.2f)\n reference one-bounce sun (per unit sun): %.4f %.4f %.4f\n probe cache sun term: %.4f %.4f %.4f (weight %.3f)\n",
        px,py,p.x,p.y,p.z,n.x,n.y,n.z,E[0],E[1],E[2],cache.x,cache.y,cache.z,tot);
    std::printf(" reference sky view (cosine-weighted): %.3f\n",skyView/N);
    return 0;
}
int diagnose(const Scene& s,int x0,int y0,int w,int h,int width,int height,const std::string& prefix) {
    const auto& cam=s.config["cameras"][0];float3 eye=v3(cam["eye"]),target=v3(cam["target"]);
    float fov=float(cam["fovYDegrees"].toDouble(s.config["fovYDegrees"].toDouble(60)))*3.14159265f/180;
    float3 forward=normalize(target-eye),right=normalize(cross(forward,{0,1,0})),up=cross(right,forward);
    float scale=std::tan(fov*.5f),aspect=float(width)/height;
    std::vector<float3> img[4];for(auto& v:img)v.assign(size_t(w*h),{});
    std::vector<float> totals[4];for(auto& v:totals)v.assign(size_t(w*h),0);
    std::atomic<int> row{0};
    auto work=[&](){for(int y;(y=row++)<h;)for(int x=0;x<w;++x){
        float sx=(x0+x+.5f)/width,sy=(y0+y+.5f)/height;
        auto ray=normalize(forward+right*((2*sx-1)*scale*aspect)+up*((1-2*sy)*scale));float3 n;
        float d=s.geometry.distance(eye,ray,500,&n);if(d>=500)continue;
        if(dot(n,ray)>0)n=n*-1.0f;
        float3 p=eye+ray*d+n*.01f;
        for(int m=0;m<(s.moments.empty()?3:4);++m){float t;img[m][size_t(y*w+x)]=indirect(s,p,n,Mode(m),t);totals[m][size_t(y*w+x)]=t;}
    }};
    std::vector<std::thread> pool;for(unsigned i=0;i<std::max(1u,std::thread::hardware_concurrency());++i)pool.emplace_back(work);for(auto& t:pool)t.join();
    const char* names[4]={"plane","exact","none","moments"};
    for(int m=0;m<(s.moments.empty()?3:4);++m)writePpm(prefix+names[m]+".ppm",w,h,img[m]);
    // Error of plane vs exact visibility, relative to the unoccluded result.
    double err=0,errExactNone=0;int falseHidden=0,falseVisible=0,count=0;std::vector<float3> diff(size_t(w*h));
    for(size_t i=0;i<diff.size();++i){
        auto a=img[0][i],b=img[1][i],c=img[2][i];float la=a.x+a.y+a.z,lb=b.x+b.y+b.z,lc=c.x+c.y+c.z;if(lc<=0)continue;++count;
        err+=std::abs(la-lb)/std::max(lc,1e-3f);errExactNone+=std::abs(lb-lc)/std::max(lc,1e-3f);
        float tp=totals[0][i],te=totals[1][i];if(tp+.25f<te)++falseHidden;if(te+.25f<tp)++falseVisible;
        float r=(la-lb)/std::max(lc,1e-3f);diff[i]=r>0?float3{r*4,0,0}:float3{0,-r*4,0};
    }
    writePpm(prefix+"diff.ppm",w,h,diff);
    std::printf("{\"pixels\":%d,\"meanRelErrPlaneVsExact\":%.4f,\"meanRelDiffExactVsNone\":%.4f,\"falseHiddenPixels\":%d,\"falseVisiblePixels\":%d}\n",
        count,err/std::max(count,1),errExactNone/std::max(count,1),falseHidden,falseVisible);
    return 0;
}
}

int aiverseMain(int argc,char** argv) {
    std::string scene,cache,prefix="/tmp/pv_";std::string sunMask;float sunElevation=-1,sunAzimuth=0;int sub=0,pixelX=-1,pixelY=-1;float bias=-1;bool facing=true;std::string write;int x0=0,y0=0,w=320,h=180,width=1280,height=720;
    for(int i=1;i+1<argc;i+=2){std::string a=argv[i],v=argv[i+1];
        if(a=="--scene")scene=v;else if(a=="--cache")cache=v;else if(a=="--prefix")prefix=v;
        else if(a=="--rect"){std::sscanf(v.c_str(),"%d,%d,%d,%d",&x0,&y0,&w,&h);}
        else if(a=="--moments")sub=std::atoi(v.c_str());
        else if(a=="--write")write=v;
        else if(a=="--facing")facing=std::atoi(v.c_str())!=0;
        else if(a=="--bias")bias=float(std::atof(v.c_str()));
        else if(a=="--pixel")std::sscanf(v.c_str(),"%d,%d",&pixelX,&pixelY);
        else if(a=="--sun-image")std::sscanf(v.c_str(),"%f,%f",&sunElevation,&sunAzimuth);
        else if(a=="--sun-fit")sunMask=v;
        else if(a=="--size"){std::sscanf(v.c_str(),"%dx%d",&width,&height);}
    }
    if(scene.empty()||cache.empty()){std::fprintf(stderr,"usage: aiverse_probe_visibility --scene recipe.json --cache gi.avgic [--moments 4 [--write new.avgic]] [--rect x,y,w,h] [--size WxH] [--prefix out_] [--bias m] [--facing 0|1]\n");return 2;}
    try{
        auto dir=std::filesystem::path(scene).parent_path();if(!dir.empty())std::filesystem::current_path(dir);
        Scene s;loadScene(s,std::filesystem::path(scene).filename().string(),cache);s.normalBias=bias;s.facing=facing;
        if(int(s.grid.texels.at(0).w)&16)s.moments=s.grid.texels;   // cache already stores distance moments
        if(sub>0){auto start=std::chrono::steady_clock::now();s.moments=rebakeMoments(s,sub);
            std::fprintf(stderr,"moments rebake: %.1f s\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());}
        if(!write.empty()){
            if(s.moments.empty())throw std::runtime_error("--write requires --moments N");
            if(std::filesystem::exists(write))throw std::runtime_error("output already exists: "+write);
            StaticLightGrid out=s.grid;out.texels=s.moments;
            auto bytes=encodeLightGrid(out,recipeKey(s.config));decodeLightGrid(bytes,recipeKey(s.config));
            std::ofstream f(write,std::ios::binary);f.write(bytes.data(),std::streamsize(bytes.size()));if(!f)throw std::runtime_error("cannot write cache");
            std::fprintf(stderr,"wrote %s (%zu bytes)\n",write.c_str(),bytes.size());return 0;
        }
        if(sunElevation>=0)return sunImage(s,sunElevation,sunAzimuth,width,height,prefix+"sun.pgm");
        if(!sunMask.empty())return sunFit(s,sunMask,width,height);
        if(pixelX>=0)return pixelProbe(s,pixelX,pixelY,width,height);
        return diagnose(s,x0,y0,w,h,width,height,prefix);
    }catch(const std::exception& e){std::fprintf(stderr,"probe_visibility: %s\n",e.what());return 1;}
}
