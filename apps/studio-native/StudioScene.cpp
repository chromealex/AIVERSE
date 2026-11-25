#include "engine/view/MaterialLibraryFiles.h"
#include "engine/view/AppearanceAuthoringService.h"
#include "engine/render/TextureCache.h"
#include "apps/editor/AuthoringWater.h"
#include "apps/studio-native/StudioScene.h"
#include "apps/studio-native/StudioAttachmentPreview.h"
#include "apps/editor/core/TextureGraphGpuPreview.h"
#include "engine/view/AppearanceAssetRegistry.h"
#include "apps/studio-native/StudioPlanetLiveBudget.h"
#include "apps/studio-native/StudioPlanetLiveTerrain.h"
#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveRootModel.h"
#include "engine/render/TerraWeaveArborWind.h"
#include "engine/render/TerraWeaveBotanicalForest.h"
#include "engine/world/TerraWeaveForestSpeciesManifest.h"
#include "apps/studio-native/StudioFont6x11.h"
#include "engine/render/CameraPng.h"
#include "engine/render/EnvironmentIbl.h"
#include "engine/render/EnvironmentIblGpu.h"
#include "engine/render/ExrTexture.h"
#include "apps/studio-native/StudioLightingPreview.h"
#include "engine/render/GiModelGeometry.h"
#include "apps/editor/SceneRenderable.h"
#include "apps/studio-native/StudioVisualPicking.h"
#include "apps/studio-native/StudioTerrainGeometry.h"
#include "apps/studio-native/StudioDocumentVisuals.h"
#include "apps/studio-native/StudioAuthoredLights.h"
#include "apps/studio-native/StudioPbrMaterialPreview.h"
#include "engine/view/AppearanceChoiceAuthoring.h"
#include "engine/view/game/GameShapeModels.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <cmath>
#include <stdexcept>

namespace aiverse::studio {
using namespace aiverse::render;

// A single presentation owner on the Scene render thread. The preview renderer
// is isolated from the Scene's camera, lighting, packet cache and post-FX.
struct StudioScene::GraphPresentation {
    using Snapshot=view::AppearanceAssetRegistry::Snapshot;
    struct Capture {
        std::string state="pending",error,hex;
        int width=0,height=0;
        std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    };
    std::map<const Model*,Snapshot> scene;
    std::map<std::string,std::vector<Material>> merged;
    ModelImageRollbackPool rollback;
    std::unique_ptr<Model> sample;
    Snapshot sampleAppearance;
    std::unique_ptr<Renderer3D> sampleRenderer;
    gfx::TextureHandle color{},depth{},environment{},residentHdr{};
    std::shared_ptr<Capture> hdrCapture;
    std::string residentHdrRequest;
    int residentHdrMipCount=0;
    std::string environmentPath;
    environment_ibl::GpuPrefilter environmentGpu;
    std::string environmentGpuRequest,environmentGpuState="idle",environmentGpuError;
    int environmentGpuSide=0;
    bool environmentGpuCancelled=false;
    float environmentYaw=0.f,environmentIntensity=1.f;
    std::shared_ptr<Capture> capture;
    bool draw=false;
    float yaw=.65f,pitch=.42f,zoom=1.f,light=1.25f,lightYaw=.6f;
    uint64_t captureSerial=0;
    size_t sampleMaterial=0;
    std::string sampleShape;
    static Snapshot snapshot(const Model& model){
        auto p=std::make_shared<view::AppearanceAssetRegistry::Asset>();
        p->model=&model;p->materials=model.materials;p->version=1;
        p->modelPublicationGeneration=view::ModelPublicationEpoch::next();
        return p;
    }
    Snapshot& target(Model& model){
        auto& s=scene[&model];if(!s)s=snapshot(model);return s;
    }
    void forget(const Model& model){scene.erase(&model);merged.clear();}
    const std::vector<Material>* materials(const Model& model,const std::string& id,const std::vector<Material>* authored){
        auto it=scene.find(&model);if(it==scene.end())return authored;
        const auto& live=it->second->materials;
        if(!authored)return &live;
        // Preserve existing per-instance factors and any unrelated imported bindings.
        auto& result=merged[id];result=*authored;
        constexpr int Material::* slots[]={&Material::baseTex,&Material::normalTex,&Material::mrTex,&Material::occlusionTex,&Material::emissiveTex,&Material::teamTex};
        for(size_t i=0;i<std::min(result.size(),live.size());++i){
            bool shaderOverride=false;for(const auto& pin:live[i].graphSourcePins)if(pin)shaderOverride=true;
            if(shaderOverride){
                result[i].shaderGraph=live[i].shaderGraph;result[i].graphSourcePins=live[i].graphSourcePins;
                for(size_t c=0;c<4;++c)result[i].graphTextures[c]=live[i].graphTextures[c];
            }
            if(!live[i].proceduralGraphPins)continue;
            for(size_t c=0;c<6;++c)if(const auto& pin=(*live[i].proceduralGraphPins)[c]){
                result[i].*slots[c]=live[i].*slots[c];
                result[i].proceduralGraphPins=withGraphMaterialPin(result[i].proceduralGraphPins,c,pin);
            }
        }
        return &result;
    }
    void resetSample(gfx::Device& dev){
        if(sample){if(sampleRenderer)sampleRenderer->invalidateModelCachesFor(sample.get());sampleAppearance.reset();releaseModel(dev,*sample);sample.reset();}
        capture.reset();draw=false;
    }
    bool environmentGpuPending()const{return environmentGpu.source.valid();}
    Json environmentGpuStatus()const{
        Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("request",Json::string(environmentGpuRequest));
        r.set("state",Json::string(environmentGpuState));r.set("reason",Json::string(environmentGpuError));
        r.set("environment",Json::string(environmentGpuState=="ready"?"@graph":""));
        r.set("backend",Json::string("gpu"));r.set("prefilterBackend",Json::string("gpu"));
        r.set("width",Json::number(int64_t(environmentGpuSide)));r.set("height",Json::number(int64_t(environmentGpuSide/2)));return r;
    }
    void serviceEnvironmentGpu(gfx::Device& dev){
        if(!environmentGpuPending())return;
        auto& job=environmentGpu;
        if(!job.fence&&!environmentGpuCancelled){
            try{if(!job.encode(dev))environmentGpuError="GPU IBL dispatch or copy rejected";}
            catch(const std::exception& ex){environmentGpuError=ex.what();}
            if(job.fence)return;
        }
        if(job.fence&&dev.gpuCompletedFence()<job.fence)return;
        if(environmentGpuCancelled||!job.encoded||!dev.gpuHealthy()){
            environmentGpuState=environmentGpuCancelled?"cancelled":"failed";
            if(!environmentGpuCancelled&&environmentGpuError.empty())environmentGpuError="GPU IBL completion rejected";
        }else{
            if(!dev.waitForResourceIdle())return;
            if(sampleRenderer)sampleRenderer->setModelEnvironment({});
            if(environment.valid())dev.destroyTexture(environment);
            environment=job.atlas;job.atlas={};environmentPath="@graph";environmentGpuState="ready";
        }
        job.releaseAfterIdle(dev);
    }
    void replaceEnvironment(gfx::Device& dev,const RadianceImage& hdr,std::string identity){
        if(dev.maxFragmentTextureSlots()<14||!dev.supports(gfx::PixelFormat::RGBA16F))throw std::runtime_error("Native HDR environment unsupported by this GPU");
        const auto atlas=environment_ibl::convolveWithBrdf(hdr);
        if(!dev.waitForResourceIdle())throw std::runtime_error("HDR environment retirement failed");
        std::string error;auto candidate=uploadRadianceHdr(dev,atlas,error);
        if(!candidate.valid())throw std::runtime_error(error);
        if(sampleRenderer)sampleRenderer->setModelEnvironment({});
        if(environment.valid())dev.destroyTexture(environment);
        environment=candidate;environmentPath=std::move(identity);
    }
    void setEnvironment(gfx::Device& dev,const std::string& project,const std::string& relative,bool reload){
        namespace fs=std::filesystem;
        if(relative=="@graph"){
            if(environmentPath!=relative||!environment.valid())throw std::runtime_error("Generate graph environment first");
            if(reload)throw std::invalid_argument("Regenerate graph environment instead of reloading a file");
            return;
        }
        if(relative==environmentPath&&!reload)return;
        if(relative.empty()){
            if(environment.valid()){
                if(!dev.waitForResourceIdle())throw std::runtime_error("HDR environment retirement failed");
                if(sampleRenderer)sampleRenderer->setModelEnvironment({});
                dev.destroyTexture(environment);
            }
            environment={};environmentPath.clear();return;
        }
        const fs::path rel(relative);
        if(relative.size()>512||relative.find('\0')!=std::string::npos||relative.find('\\')!=std::string::npos||relative.find(':')!=std::string::npos||rel.is_absolute()||rel.empty()||*rel.begin()!="content")throw std::invalid_argument("HDR environment must be inside project content");
        auto path=fs::canonical(project);
        for(const auto& part:rel){if(part==".."||part=="."||part.empty())throw std::invalid_argument("Invalid HDR environment path");path/=part;if(fs::is_symlink(path))throw std::invalid_argument("HDR environment symlink rejected");}
        if(!fs::is_regular_file(path))throw std::runtime_error("HDR environment missing");
        const auto bytes=fs::file_size(path);
        if(!bytes||bytes>32u*1024u*1024u)throw std::runtime_error("HDR environment exceeds encoded file budget");
        if(dev.maxFragmentTextureSlots()<14||!dev.supports(gfx::PixelFormat::RGBA16F))throw std::runtime_error("Native HDR environment unsupported by this GPU");
        std::string encoded(size_t(bytes),'\0');std::ifstream stream(path,std::ios::binary);
        if(!stream.read(encoded.data(),std::streamsize(encoded.size()))||stream.peek()!=std::char_traits<char>::eof())throw std::runtime_error("HDR environment changed during read");
        RadianceImage hdr;std::string error;
        const bool exr=encoded.size()>=4&&encoded.compare(0,4,"\x76\x2f\x31\x01",4)==0;
        if(!(exr?parseOpenExr(encoded,hdr,error,64u*1024u*1024u):parseRadianceHdr(encoded,hdr,error,64u*1024u*1024u)))throw std::runtime_error(error);
        replaceEnvironment(dev,hdr,relative);
    }
    void shutdown(gfx::Device& dev){
        environmentGpu.releaseAfterIdle(dev);
        scene.clear();merged.clear();resetSample(dev);
        if(sampleRenderer){sampleRenderer->shutdown();sampleRenderer.reset();}
        if(color.valid())dev.destroyTexture(color);
        if(depth.valid())dev.destroyTexture(depth);
        if(environment.valid())dev.destroyTexture(environment);
        if(residentHdr.valid())dev.destroyTexture(residentHdr);
        residentHdr={};residentHdrRequest.clear();residentHdrMipCount=0;hdrCapture.reset();
        environment={};environmentPath.clear();color={};depth={};(void)rollback.drain(dev);
    }
    static Json info(const Snapshot& s,size_t index){
        Json r=Json::object();r.set("ok",Json::fromBool(true));
        r.set("expectedVersion",Json::string(std::to_string(s->version)));
        r.set("expectedModelPublication",Json::string(std::to_string(s->modelPublicationGeneration)));
        r.set("materialIndex",Json::number(int64_t(index)));
        r.set("materialName",Json::string(s->materials.at(index).name));return r;
    }
    static view::LiveAppearanceMaterialTarget stamp(const Json& p,size_t index){
        return {view::parseAppearanceIdentity(p.getStr("expectedModelPublication")),view::parseAppearanceIdentity(p.getStr("expectedVersion")),index};
    }
    static size_t index(const Json& p,const Model& m){
        const auto i=p["materialIndex"].integer(-1);
        if(!p["materialIndex"].isNumber()||i<0||size_t(i)>=m.materials.size())throw std::invalid_argument("Invalid material index");
        return size_t(i);
    }
    Json captureStatus()const {
        Json r=Json::object();r.set("ok",Json::fromBool(true));
        r.set("capture",Json::string(std::to_string(captureSerial)));
        r.set("state",Json::string(capture?capture->state:"idle"));
        r.set("environment",Json::string(environmentPath));
        if(capture){r.set("reason",Json::string(capture->error));if(capture->state=="ready"){
            r.set("width",Json::number(int64_t(capture->width)));r.set("height",Json::number(int64_t(capture->height)));r.set("rgbaHex",Json::string(capture->hex));
        }}
        return r;
    }
    void renderSample(gfx::Device& dev){
        if(!draw||!sample||!sampleAppearance||!capture)return;
        draw=false;
        constexpr int side=256;
        Camera cam;const auto lo=sample->boundsMin,hi=sample->boundsMax,size=hi-lo;
        const float radius=std::max(.05f,.5f*std::sqrt(dot(size,size)));
        cam.target=(lo+hi)*.5f;cam.yaw=yaw;cam.pitch=pitch;cam.ortho=false;
        cam.distance=radius/std::sin(cam.fovY*.5f)*zoom;
        cam.zNear=std::max(.01f,cam.distance-radius*2.f);cam.zFar=cam.distance+radius*4.f;
        if(!sampleRenderer->setModelEnvironment(environment,environmentYaw,environmentIntensity)){capture->state="failed";capture->error="Native HDR environment rejected";return;}
        sampleRenderer->clearModelError();
        sampleRenderer->beginScene(cam,side,side);
        sampleRenderer->lighting=Lighting{};
        sampleRenderer->lighting.sunIntensity=light;
        sampleRenderer->lighting.sunDir=normalize(float3{std::cos(lightYaw),1.f,std::sin(lightYaw)});
        sampleRenderer->lighting.skyColor={.46f,.54f,.68f};sampleRenderer->lighting.groundColor={.29f,.29f,.32f};
        sampleRenderer->lighting.fogDensity=0;
        ModelDraw d;d.model=sample.get();d.materials=&sampleAppearance->materials;
        sampleRenderer->drawModel(d);
        gfx::PassDesc pass;pass.colorTarget=color;pass.depthTarget=depth;
        pass.clearColor[0]=.075f;pass.clearColor[1]=.09f;pass.clearColor[2]=.12f;pass.clearColor[3]=1.f;
        sampleRenderer->render(pass);dev.endPass();
        if(environment.valid()&&!sampleRenderer->modelError().empty()){capture->state="failed";capture->error=sampleRenderer->modelError();return;}
        auto result=capture;
        if(!dev.captureTexture(color,[result](int w,int h,std::vector<uint8_t> rgba){
            if(result->state!="pending")return;
            if(w!=side||h!=side||rgba.size()!=size_t(side)*side*4){result->state="failed";result->error="Native PBR capture returned invalid pixels";return;}
            result->hex.reserve(rgba.size()*2);
            for(size_t i=0;i<rgba.size();++i){const auto v=i%4==3?uint8_t(255):rgba[i];result->hex+="0123456789abcdef"[v>>4];result->hex+="0123456789abcdef"[v&15];}
            result->width=w;result->height=h;result->state="ready";
        })){capture->state="failed";capture->error="Native texture capture rejected";}
    }
};

namespace { struct StudioFrameScope {
    gfx::Device& device;
    ~StudioFrameScope(){device.endPass();device.endFrame();}
}; }
namespace {
// Shared layout of the native orientation gizmo (top-right corner).
struct GizmoLayout { float cx, cy, r; };
GizmoLayout gizmoLayout(float width,float height) {
    const float r=std::clamp(std::min(width,height)*0.065f,24.0f,160.0f);
    return {width-r*1.75f,r*1.75f,r};
}
struct GizmoBasis { float3 right, up, forward; };
GizmoBasis gizmoBasis(float yaw,float pitch) {
    const float cp=std::cos(pitch),sp=std::sin(pitch);
    const float3 forward{-std::sin(yaw)*cp,-sp,-std::cos(yaw)*cp};
    const float3 right{std::cos(yaw),0,-std::sin(yaw)};
    return {right,cross(right,forward),forward};
}
float3 gizmoAxis(int arm) {
    float3 a{0,0,0};
    const float sign=(arm&1)?-1.0f:1.0f;
    if(arm/2==0)a.x=sign; else if(arm/2==1)a.y=sign; else a.z=sign;
    return a;
}
}
namespace {
// Flat screen-space overlay drawn just beyond the near plane (always on top).
struct Overlay2D {
    Renderer3D& r; const Camera& view; float W, H, depth;
    bool at(float x,float y,float3& out) const {
        float3 o,d; if(!view.screenRay(x,y,W,H,o,d))return false;
        out=o+d*depth; return true;
    }
    void tri(float ax,float ay,float bx,float by,float cx,float cy,uint32_t col) const {
        float3 a,b,c; if(at(ax,ay,a)&&at(bx,by,b)&&at(cx,cy,c))r.triangle(a,b,c,col);
    }
    void segment(float x0,float y0,float x1,float y1,float w,uint32_t col) const {
        const float lx=x1-x0,ly=y1-y0,l=std::hypot(lx,ly); if(l<1e-3f)return;
        const float nx=-ly/l*w*.5f,ny=lx/l*w*.5f;
        tri(x0+nx,y0+ny,x1+nx,y1+ny,x1-nx,y1-ny,col);tri(x0+nx,y0+ny,x1-nx,y1-ny,x0-nx,y0-ny,col);
    }
};
// Screen layout of the transform handles for a pivot along three axis directions.
struct AxisHandles { bool valid=false; float px=0,py=0; float tip[3][2]{}; float pick=8; float lengthPx=1; float worldLength=1; };
AxisHandles axisHandles(const Camera& view,float3 pivot,const float3* axes,float W,float H) {
    AxisHandles h; float2 p;
    if(!view.worldToScreen(pivot,W,H,p))return h;
    const float lengthPx=std::min(W,H)*0.16f;
    // World length that projects to ~lengthPx at the pivot depth (foreshortened per axis).
    float worldPerPixel;
    if(view.ortho) worldPerPixel=2.0f*view.orthoHalfHeight()/H;
    else {
        const float3 eye=view.eye();const float3 fwd=normalize(view.target-eye);
        const float depth=std::max(0.05f,dot(pivot-eye,fwd));
        worldPerPixel=2.0f*depth*std::tan(view.fovY*0.5f)/H;
    }
    const float S=lengthPx*worldPerPixel;
    for(int a=0;a<3;++a){
        float2 q;
        if(!view.worldToScreen(pivot+axes[a]*S,W,H,q))return h;
        h.tip[a][0]=q.x;h.tip[a][1]=q.y;
    }
    h.px=p.x;h.py=p.y;h.pick=std::max(6.0f,lengthPx*0.09f);h.lengthPx=lengthPx;h.worldLength=S;h.valid=true;return h;
}
// Orthonormal basis (u, v) of the plane perpendicular to n.
void ringBasis(float3 n,float3& u,float3& v){
    const float3 ref=std::fabs(n.y)<0.9f?float3{0,1,0}:float3{1,0,0};
    u=normalize(cross(n,ref));v=cross(n,u);
}
constexpr int kRingSegments=64;
// Move plane handle (normal axis n): small square spanned by the two other axes.
bool planeQuad(const Camera& view,float3 pivot,const float3* axes,int n,float worldLength,float W,float H,float2 out[4]){
    const float3 a=axes[(n+1)%3],b=axes[(n+2)%3];
    const float s0=worldLength*0.18f,s1=worldLength*0.42f;
    const float3 c[4]={pivot+a*s0+b*s0,pivot+a*s1+b*s0,pivot+a*s1+b*s1,pivot+a*s0+b*s1};
    for(int i=0;i<4;++i)if(!view.worldToScreen(c[i],W,H,out[i]))return false;
    return true;
}
bool insideQuad(const float2* q,float x,float y){
    bool pos=false,neg=false;
    for(int i=0;i<4;++i){const float2 a=q[i],b=q[(i+1)%4];
        const float c=(b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);pos|=c>0;neg|=c<0;}
    return !(pos&&neg);
}
// Tiny 7-segment text for gizmo readouts ("-12.5°"): the Metal view covers HTML.
float overlayTextWidth(const std::string& text,float h){
    float w=0;for(char c:text)w+=(c=='.'?0.3f:c=='\xb0'?0.45f:c==' '?0.4f:0.7f)*h;return w;
}
bool rayPlane(float3 o,float3 d,float3 p,float3 n,float3& hit){
    const float den=dot(d,n);if(std::fabs(den)<1e-5f)return false;
    const float t=dot(p-o,n)/den;if(!(t>0)||!std::isfinite(t))return false;
    hit=o+d*t;return true;
}
void overlayText(const Overlay2D& o,float x,float y,float h,const std::string& text,uint32_t col){
    // segments: a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle
    static const uint8_t digits[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    const float w=h*0.5f,t=std::max(1.2f,h*0.12f);
    // dark plate for contrast
    const float pw=overlayTextWidth(text,h)+h*0.5f;
    o.tri(x-h*0.25f,y-h*0.25f,x+pw-h*0.25f,y-h*0.25f,x+pw-h*0.25f,y+h*1.25f,rgba(10,16,26,200));
    o.tri(x-h*0.25f,y-h*0.25f,x+pw-h*0.25f,y+h*1.25f,x-h*0.25f,y+h*1.25f,rgba(10,16,26,200));
    for(char c:text){
        uint8_t m=0;
        if(c>='0'&&c<='9')m=digits[c-'0']; else if(c=='-')m=0x40; else if(c=='+')m=0x40;
        const float X=x,Y=y,M=y+h*0.5f,B=y+h;
        if(m&0x01)o.segment(X,Y,X+w,Y,t,col);
        if(m&0x02)o.segment(X+w,Y,X+w,M,t,col);
        if(m&0x04)o.segment(X+w,M,X+w,B,t,col);
        if(m&0x08)o.segment(X,B,X+w,B,t,col);
        if(m&0x10)o.segment(X,M,X,B,t,col);
        if(m&0x20)o.segment(X,Y,X,M,t,col);
        if(m&0x40)o.segment(X,M,X+w,M,t,col);
        if(c=='+')o.segment(X+w*0.5f,M-w*0.5f,X+w*0.5f,M+w*0.5f,t,col);
        if(c=='.')o.segment(x,B-t*0.5f,x+t,B-t*0.5f,t,col);
        if(c=='\xb0'){o.segment(X,Y,X+h*0.25f,Y,t*0.8f,col);o.segment(X+h*0.25f,Y,X+h*0.25f,Y+h*0.25f,t*0.8f,col);
            o.segment(X+h*0.25f,Y+h*0.25f,X,Y+h*0.25f,t*0.8f,col);o.segment(X,Y+h*0.25f,X,Y,t*0.8f,col);}
        if(c=='x'){o.segment(X,M-w*0.45f,X+w,M+w*0.55f,t,col);o.segment(X+w,M-w*0.45f,X,M+w*0.55f,t,col);} // ×
        x+=(c=='.'?0.3f:c=='\xb0'?0.45f:c==' '?0.4f:0.7f)*h;
    }
}
// "+1.25" / "-2" / "+0.5": signed, at most 2 decimals, no trailing zeros.
std::string gizmoNumber(float v,bool sign=true){
    char b[32];std::snprintf(b,sizeof b,sign?"%+.2f":"%.2f",double(v));
    std::string s=b;
    if(s.find('.')!=std::string::npos){while(s.back()=='0')s.pop_back();if(s.back()=='.')s.pop_back();}
    if(s=="+0"||s=="-0")s="0";
    return s;
}
float segmentDistance(float px,float py,float ax,float ay,float bx,float by){
    const float vx=bx-ax,vy=by-ay,l2=vx*vx+vy*vy;
    const float t=l2>0?std::clamp(((px-ax)*vx+(py-ay)*vy)/l2,0.0f,1.0f):0.0f;
    return std::hypot(px-(ax+vx*t),py-(ay+vy*t));
}
// Parameter t of the point on the axis line (pivot + a*t) closest to a ray.
bool axisParameter(float3 pivot,float3 a,float3 o,float3 d,float& t){
    const float3 w0=pivot-o;const float b=dot(a,d),dw=dot(a,w0),ew=dot(d,w0);
    const float denom=1.0f-b*b; if(std::fabs(denom)<1e-5f)return false;
    t=(b*ew-dw)/denom; return std::isfinite(t);
}
}
struct StudioScene::GraphParticlePreview {
    explicit GraphParticlePreview(gfx::Device& device):device(device){}
    ~GraphParticlePreview(){
        system.shutdown();
        for(const auto& [path,texture]:textures)device.destroyTexture(texture);
    }
    gfx::Device& device;
    fx::ParticleSystem system;
    std::map<std::string,gfx::TextureHandle> textures;
    std::chrono::steady_clock::time_point lastFrame{};
};
std::string StudioScene::shaderPreviewError() const {
    if(!graphPreviewError_.empty())return graphPreviewError_;
    if(graphParticles_){
        const auto& errors=graphParticles_->system.errors();
        return errors.empty()?graphParticles_->system.lastError():errors.back();
    }
    return graphPreviewPost_?graphPreviewPost_->lastError():std::string{};
}
StudioScene::StudioScene(std::unique_ptr<gfx::Device> device)
    : device_(std::move(device)) {
    if (!device_) throw std::runtime_error("Scene GPU device unavailable");
    deviceLifetime_=std::make_unique<render::RenderDeviceLifetime>(*device_);
    facialGpu_.setDeviceLease(deviceLifetime_->lease());
    renderer_.setGpuDeviceLease(deviceLifetime_->lease());
    if (!renderer_.init(*device_)) throw std::runtime_error("AIVERSE Renderer3D init failed");
    lodDiagnostics_=std::getenv("AIVERSE_LOD_DIAGNOSTICS")!=nullptr;
    renderer_.setLodDiagnosticsEnabled(lodDiagnostics_);
    if (!post_.init(*device_)) throw std::runtime_error("Studio HDR init failed");
    PrismaSettings postSettings;postSettings.hdr=HdrMode::On;
    post_.setSettings(postSettings);post_.setQuality(PrismaQuality::High);
    lodDiagnostics_=std::getenv("AIVERSE_LOD_DIAGNOSTICS")!=nullptr;
    renderer_.setLodDiagnosticsEnabled(lodDiagnostics_);
    antialiasing_.setGpuDeviceLease(deviceLifetime_->lease());
    if(!antialiasing_.init(*device_))throw std::runtime_error("Studio anti-aliasing init failed");
    PostFxSettings aa;aa.hdr=HdrMode::Off;
    aa.effects.push_back(defaultPostFxEffect(PostFxType::Fxaa));
    antialiasing_.setSettings(std::move(aa));
    antialiasing_.setQuality(PostFxQuality::Medium);
    renderer_.setShadowQuality(ShadowQuality::Off); // cheap editor preview until full scene parity
    MeshBuilder builder;
    builder.color(rgba(255,255,255));
    builder.box({0,0,0}, {1,1,1});
    cube_ = builder.upload(*device_);
    if (!cube_.vb.valid() || !cube_.ib.valid()) throw std::runtime_error("Preview cube mesh upload failed");
    studioCyclorama_=vivid::humanStudioCyclorama().upload(*device_);
    if(!studioCyclorama_.vb.valid()||!studioCyclorama_.ib.valid())throw std::runtime_error("Studio cyclorama upload failed");
    camera_.pitch = 0.85f;
    camera_.yaw = 0.55f;
}
void StudioScene::setTransformTool(TransformTool tool) { tool_ = tool; clearTransformPreview(); }
void StudioScene::clearTransformPreview() { cancelPlanetRegionDrag(); cancelPlanetGravityDrag(); drag_ = TransformDrag{}; planetDragId_.clear(); planetDragPlacement_=Json{}; planetDragOriginal_=Json{}; }
bool StudioScene::setBuildingPreview(
    std::unique_ptr<render::Model> next,std::string* error){
    // Called only by StudioHost::onScene outside GPU passes.
    if(!device_ || !deviceLifetime_ ||
       !deviceLifetime_->lease().validFor(*device_)){
        if(error)*error="Studio building preview GPU device unavailable";
        return false;
    }
    if(buildingPreview_){
        // Never invalidate a Model still retained in in-flight draw packets.
        if(!device_->waitForResourceIdle()){
            if(error)*error="Building preview GPU retirement barrier failed";
            return false;
        }
        releasePreviewModel(*buildingPreview_);
        buildingPreview_.reset();
        buildingPreviewGpuReady_=false;
    }
    if(next){
        next->batching.mergeRigid=false;
        next->batching.autoLod=false;
        next->batching.geometryPoolPolicy=
            render::Model::Batching::GeometryPoolPolicy::Dedicated;
        render::prepareModelUpload(*next);
        if(!render::uploadModel(*device_,*next,true,false)){
            if(!device_->waitForResourceIdle()){
                // Keep a failed partial upload alive until another verified
                // GPU-safe cleanup call; no CPU Model can dangle on failure.
                buildingPreview_=std::move(next);
                buildingPreviewGpuReady_=false;
                if(error)*error="Building preview upload failed and GPU retirement is pending";
                return false;
            }
            releasePreviewModel(*next);
            if(error)*error="Building preview model GPU upload failed";
            return false;
        }
        const auto min=next->boundsMin,max=next->boundsMax;
        camera_.target={(min.x+max.x)*.5f,(min.y+max.y)*.5f,
                        (min.z+max.z)*.5f};
        const auto extent=max-min;
        camera_.distance=std::max({extent.x,extent.y,extent.z,1.f})*1.9f;
        camera_.yaw=.55f;camera_.pitch=.45f;camera_.ortho=false;
        tween_.active=false;cameraInitialized_=true;
        buildingPreview_=std::move(next);
        buildingPreviewGpuReady_=true;
    }
    lastFrameSignature_=0;
    if(error)error->clear();
    return true;
}
void StudioScene::releasePreviewModel(render::Model& model) {
    // Retire CPU packets/layouts before the Model and its geometry disappear.
    renderer_.invalidateModelCachesFor(&model);
    if(graphPresentation_)graphPresentation_->forget(model);
    windRigs_.erase(&model);
    render::releaseModel(*device_,model);
}
const render::Model* StudioScene::rtsShapeModel(const Json& type) {
    if (!shapesBuilt_ && device_) {
        shapesBuilt_=true;
        for (auto& [name,model]:game::buildRtsShapeModels()) {
            if (!model) continue;
            render::prepareModelUpload(*model);
            if (render::uploadModel(*device_,*model,true)) shapeModels_.emplace(name,std::move(model));
        }
    }
    const bool structure=type.has("structure") && !type["structure"].isNull() && type["structure"].boolean(true);
    std::string name=type["view"].getStr("shape",structure ? (type.has("resourceNode") ? "mine" : "structure") : "generic");
    auto found=shapeModels_.find(name);
    if (found==shapeModels_.end()) found=shapeModels_.find(structure ? "structure" : "generic");
    return found==shapeModels_.end() ? nullptr : found->second.get();
}
void StudioScene::requestThumbnail(const std::string& projectPath,const std::string& asset,const std::string& file) {
    for(const auto& j:thumbJobs_)if(j.asset==asset && j.project==projectPath)return;
    if(thumbJobs_.size()>=256)return;
    thumbJobs_.push_back({projectPath,asset,file});
}
std::vector<std::string> StudioScene::takeFinishedThumbnails() {
    std::lock_guard<std::mutex> lock(thumbShared_->mutex);
    return std::exchange(thumbShared_->done,{});
}
static bool readPreviewAsset(const std::filesystem::path& root,const std::string& name,std::string& out) {
    namespace fs=std::filesystem;
    const fs::path rel(name);
    if(name.empty()||name.size()>512||rel.is_absolute()||rel.has_root_name())return false;
    fs::path file=root;
    for(const auto& part:rel){
        if(part.empty()||part=="."||part=="..")return false;
        file/=part;
        std::error_code ec;
        if(fs::is_symlink(fs::symlink_status(file,ec))||ec)return false;
    }
    std::error_code ec;
    if(!fs::is_regular_file(file,ec))return false;
    const auto size=fs::file_size(file,ec);
    if(ec||size>256ull*1024ull*1024ull)return false;
    std::ifstream input(file,std::ios::binary);if(!input)return false;
    out.resize(size_t(size));
    return bool(input.read(out.data(),std::streamsize(size)));
}
std::unique_ptr<render::Model> StudioScene::loadContentModel(const std::string& projectPath,const std::string& asset) {
    if(asset.rfind("content/",0)!=0 || asset.size()>512 || projectPath.empty())return nullptr;
    auto read=[&projectPath](const std::string& name,std::string& out){
        return name.rfind("content/",0)==0&&readPreviewAsset(projectPath,name,out);
    };
    try {
        auto model=render::loadModel(asset,read);
        if(!model||model->vertices.empty()||model->indices.empty()||model->vertices.size()>2000000)return nullptr;
        model->batching.autoLod=false;model->batching.mergeRigid=false;model->batching.allowGeometryPool=false;
        render::prepareModelUpload(*model);
        if(!render::uploadModel(*device_,*model,false)){releasePreviewModel(*model);return nullptr;}
        return model;
    } catch(const std::exception&) { return nullptr; }
}
void StudioScene::renderThumbnail() {
    if(thumbJobs_.empty())return;
    const ThumbJob job=thumbJobs_.front();thumbJobs_.pop_front();
    auto shared=thumbShared_;
    auto finish=[shared](const std::string& asset){std::lock_guard<std::mutex> lock(shared->mutex);shared->done.push_back(asset);};
    constexpr int kSide=160;
    if(!thumbColor_.valid()) {
        gfx::TextureDesc color;color.width=color.height=kSide;color.format=gfx::PixelFormat::RGBA8;color.renderTarget=true;color.label="Studio thumbnail";
        thumbColor_=device_->createTexture(color);
        gfx::TextureDesc depth=color;depth.format=gfx::PixelFormat::Depth;depth.label="Studio thumbnail depth";
        thumbDepth_=device_->createTexture(depth);
    }
    const render::Model* model=nullptr;
    if(auto it=documentModels_.find(job.asset);it!=documentModels_.end())model=it->second.get();
    else if(auto it2=thumbModels_.find(job.asset);it2!=thumbModels_.end())model=it2->second.get();
    else {
        if(thumbModels_.size()>=32){releasePreviewModel(*thumbModels_.begin()->second);thumbModels_.erase(thumbModels_.begin());}
        auto loaded=loadContentModel(job.project,job.asset);
        if(loaded)model=thumbModels_.emplace(job.asset,std::move(loaded)).first->second.get();
    }
    if(!model||!thumbColor_.valid()||!thumbDepth_.valid()){finish(job.asset);return;}
    const float3 lo=model->boundsMin,hi=model->boundsMax,size=hi-lo;
    const float radius=std::max(0.05f,0.5f*std::sqrt(dot(size,size)));
    render::Camera cam;
    cam.target=(lo+hi)*0.5f;cam.yaw=0.65f;cam.pitch=0.42f;cam.ortho=false;
    cam.distance=radius/std::sin(cam.fovY*0.5f)*1.05f;
    cam.minDistance=0.01f;cam.maxDistance=1e6f;
    cam.zNear=std::max(0.01f,cam.distance-radius*2.0f);cam.zFar=cam.distance+radius*4.0f;
    renderer_.beginScene(cam,kSide,kSide);
    renderer_.clearDynamicGiOverlay();
    renderer_.lighting=render::Lighting{};
    renderer_.lighting.sunIntensity=1.25f;
    renderer_.lighting.skyColor={.46f,.54f,.68f};
    renderer_.lighting.groundColor={.3f,.29f,.32f};
    renderer_.lighting.fogDensity=0;
    render::ModelDraw draw;draw.model=model;
    renderer_.drawModel(draw);
    gfx::PassDesc pass;pass.colorTarget=thumbColor_;pass.depthTarget=thumbDepth_;
    pass.clearColor[0]=.105f;pass.clearColor[1]=.125f;pass.clearColor[2]=.16f;pass.clearColor[3]=1.f;
    renderer_.render(pass);
    device_->endPass();
    const std::string file=job.file,asset=job.asset;
    const bool queued=device_->captureTexture(thumbColor_,[file,asset,finish](int w,int h,std::vector<uint8_t> rgba) {
        if(w>0&&h>0&&rgba.size()>=size_t(w)*size_t(h)*4) {
            std::error_code ec;std::filesystem::create_directories(std::filesystem::path(file).parent_path(),ec);
            for(size_t i=3;i<rgba.size();i+=4)rgba[i]=255;
            if(render::cameraWritePngRgba8(file+".pending.png",w,h,rgba))std::filesystem::rename(file+".pending.png",file,ec);
        }
        finish(asset);
    });
    if(!queued)finish(asset);
}
void StudioScene::clearDocumentModels() {
    if (device_) for (auto& [name,model]:thumbModels_) if (model) releasePreviewModel(*model);
    thumbModels_.clear();
    if (device_) for (auto& [path,model]:documentModels_)
        if (model) releasePreviewModel(*model);
    if (device_) for (auto& [name,model]:shapeModels_)
        if (model) releasePreviewModel(*model);
    shapeModels_.clear();shapesBuilt_=false;instanceTeams_.clear();
    modelInstances_.clear();
    documentModels_.clear();
    rejectedDocumentModels_.clear();
    documentModelTimes_.clear();
    instanceMaterials_.clear();
    proxyDraws_.clear();
    sceneCacheValid_=false;
}

void StudioScene::syncDocumentModels(
    const editor::MapDoc& doc,const std::string& projectPath,
    const Json& rules,const std::vector<StudioDocumentProxy>& proxies) {
    modelInstances_.clear();
    instanceTeams_.clear();
    if (projectPath.empty()) { loadProgress_={}; modelsPending_=false; return; }
    // Streaming budget: always load at least one new model per frame, then stop
    // after ~30 ms; the rest load on the following frames (progress is reported).
    const auto loadStart=std::chrono::steady_clock::now();
    int loadedThisFrame=0;bool pending=false;std::string lastLoaded;
    namespace fs=std::filesystem;
    const fs::path project=projectPath;
    const fs::path root=project/"content";
    // Without a content/ folder only procedural RTS shapes can be shown.
    const bool contentOk=fs::is_directory(root) && !fs::is_symlink(fs::symlink_status(root));
    // The proxy list is the authoritative list of actual document objects.
    // Only existing, explicitly authored content/ model paths are accepted.
    const auto& entities=doc.data.entities;
    std::set<std::string> active;
    {
        // materials.json drives Mesh Renderer material overrides in the preview.
        const fs::path libraryPath=project/"materials.json";
        std::error_code timeError;
        const auto modified=fs::last_write_time(libraryPath,timeError);
        if(libraryPath.string()!=materialLibraryPath_ || (!timeError && modified!=materialLibraryTime_)) {
            materialLibraryPath_=libraryPath.string();
            materialLibraryTime_=timeError?fs::file_time_type{}:modified;
            materialLibrary_=Json::object();
            std::ifstream input(libraryPath,std::ios::binary);
            if(input) {
                const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
                try { const Json parsed=view::materialLibraryFromIndex(Json::parse(text),[&](const std::string& path,std::string& bytes){return readPreviewAsset(project,path,bytes);}); if(parsed.isObject())materialLibrary_=parsed; }
                catch(const std::exception&) {}
            }
            instanceMaterials_.clear();
        }
    }
    std::set<std::string> overridden;
    // One filesystem check per unique model path per sync: hundreds of entities
    // share one file and the project may live on a slow external disk.
    std::map<std::string,std::optional<fs::file_time_type>> checkedFiles;
    for (const auto& proxy:proxies) {
        if (proxy.id.rfind("entity:",0)!=0) continue;
        const int index=studioMapEntityIndex(proxy.id,doc);
       if(index<0)continue;
       const auto& entity=entities[size_t(index)];
       const render::mat4* planetFrame=nullptr;
       if(planetMode_){
           if(!planetPreview_)continue;
           const auto it=planetPreview_->modelFrames.find(proxy.id);
           if(it==planetPreview_->modelFrames.end())continue;
           planetFrame=&it->second;
       }
       if(entity.extra["components"].has("NereidWater"))continue;
       const auto& view=rules["types"][entity.type]["view"];
       const Json& raw=view["model"];
       std::string path=editor::authoredModelAsset(entity);
       if(path.empty()) {
           if(raw.isString())path=raw.str();
           else if(raw.isArray()&&raw.size()){const auto& first=raw[size_t(0)];path=first.isString()?first.str():first.getStr("file");}
       }
        // RTS types without a model file are drawn by the game as built-in procedural
        // shapes: show the same shape here instead of a grey box.
        if (path.empty() && !raw.isString() && !raw.isArray() && rules["types"].has(entity.type) &&
            !entity.extra["components"].has("StaticModel")) {
            const Json& type=rules["types"][entity.type];
            const render::Model* shape=rtsShapeModel(type);
            if (!shape) continue;
            if (modelInstances_.size()>=2000) break;
            const float k=float(std::clamp(view["scale"].isNumber() ? view["scale"].toDouble() : 1.0,.05,50.0));
            const bool structure=type.has("structure") && !type["structure"].isNull() && type["structure"].boolean(true);
            render::mat4 scale=render::mat4::scale({k,k,k});
            if (structure) { // the game stretches structure shapes over their footprint
                const Json& footprint=type["structure"]["footprint"];
                const float sx=float(std::clamp(footprint.isArray() && footprint.size()>0 ? footprint[size_t(0)].toDouble() : 1.0,.25,64.0));
                const float sz=float(std::clamp(footprint.isArray() && footprint.size()>1 ? footprint[size_t(1)].toDouble() : 1.0,.25,64.0));
                scale=render::mat4::scale({sx*k,k*std::max(sx,sz)*.75f,sz*k});
            }
            const render::mat4 placement=planetFrame?*planetFrame:editor::authoredEntity(entity) ? editor::authoredModelMatrix(entity) :
                render::mat4::translate({proxy.center.x,proxy.center.y-proxy.half.y,proxy.center.z})*
                (structure ? render::mat4{} : render::mat4::rotateY(proxy.yaw));
            modelInstances_.push_back({proxy.id,shape,placement*scale});
            instanceTeams_[proxy.id]=entity.owner==1 ? render::float3{.18f,.42f,.95f} :
                                     entity.owner==2 ? render::float3{.9f,.22f,.16f} :
                                     entity.owner==0 ? render::float3{.62f,.64f,.66f} : render::float3{.85f,.7f,.25f};
            continue;
        }
        // Never load project-root arbitrary files or external paths.
        if (!contentOk || path.rfind("content/",0)!=0 || path.size()>512) continue;
        auto checked=checkedFiles.find(path);
        if (checked==checkedFiles.end()) {
            // nullopt: rejected path (outside content/, symlink, missing file).
            std::optional<fs::file_time_type> result;
            const fs::path relativePath(path.substr(8));
            bool allowed=!relativePath.empty() && !relativePath.is_absolute();
            fs::path current=root;
            if (allowed) for (const auto& segment:relativePath) {
                if (segment.empty() || segment=="." || segment=="..") {allowed=false;break;}
                current/=segment;
                std::error_code ec;
                const auto status=fs::symlink_status(current,ec);
                if (ec || fs::is_symlink(status)) {allowed=false;break;}
            }
            std::error_code regularError;
            if (allowed && fs::is_regular_file(current,regularError)) {
                std::error_code modifiedError;
                auto modified=fs::last_write_time(current,modifiedError);
                if (!modifiedError) {
                    // Hierarchical wind sidecar edits reload the model too.
                    std::error_code sidecarError;
                    fs::path sidecar=current; sidecar+=".wind.json";
                    const auto sidecarTime=fs::last_write_time(sidecar,sidecarError);
                    if(!sidecarError && sidecarTime>modified) modified=sidecarTime;
                    result=modified;
                }
            }
            checked=checkedFiles.emplace(path,result).first;
        }
        if (!checked->second) continue;
        if (modelInstances_.size()>=2000) break;
        active.insert(path);
        const auto modified=*checked->second;
        const auto previousTime=documentModelTimes_.find(path);
        if (previousTime!=documentModelTimes_.end() &&
            previousTime->second!=modified) {
            auto old=documentModels_.find(path);
            if (old!=documentModels_.end()) {
                releasePreviewModel(*old->second);
                documentModels_.erase(old);
            }
            rejectedDocumentModels_.erase(path);
        }
        documentModelTimes_[path]=modified;
        if (!documentModels_.count(path) && !rejectedDocumentModels_.count(path)) {
            if (documentModels_.size()>=512) continue;
            if (loadedThisFrame>0 && std::chrono::steady_clock::now()-loadStart>std::chrono::milliseconds(30)) { pending=true; continue; }
            ++loadedThisFrame;lastLoaded=path;
            // Model loaders may request .bin, textures, MTL beside the model.
            // Resolve each request with the same root/symlink checks.
            auto read=[root](const std::string& name,std::string& out)->bool {
                if (name.rfind("content/",0)!=0 || name.size()>512) return false;
                const fs::path rel(name.substr(8));
                if (rel.empty() || rel.is_absolute()) return false;
                fs::path file=root;
                for(const auto& part:rel) {
                    if(part.empty()||part=="."||part=="..") return false;
                    file/=part;
                    std::error_code ec;
                    const auto st=fs::symlink_status(file,ec);
                    if(ec || fs::is_symlink(st)) return false;
                }
                if (!fs::is_regular_file(file)) return false;
                std::error_code sizeError;
                const auto bytes=fs::file_size(file,sizeError);
                if (sizeError || bytes>256ull*1024ull*1024ull) return false;
                std::ifstream input(file,std::ios::binary);
                if (!input) return false;
                out.assign(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
                return bool(input) || input.eof();
            };
            try {
                auto model=render::loadModel(path,read);
                if (!model || model->skinned() || model->vertices.empty() ||
                    model->indices.empty()) {
                    rejectedDocumentModels_.insert(path);
                    continue;
                }
                bool deformable=false;
                for (const auto& mesh:model->meshes) deformable |= mesh.deformable;
                // Rigid opaque/masked models render with their authored LODs.
                // CPU picking uses base geometry; alpha holes/LOD silhouette
                // are approximate until GPU ID/depth picking is available.
                bool nonOpaque=false;
                for(const auto& material:model->materials)
                    nonOpaque |= material.alpha==render::AlphaMode::Blend ||
                                 material.transparent || material.baseColor.w<.999f;
                if (deformable || nonOpaque ||
                    model->vertices.size()>1000000 || model->indices.size()>3000000) {
                    rejectedDocumentModels_.insert(path);
                    continue;
                }
                model->batching.autoLod=false;
                model->batching.mergeRigid=false;
                model->batching.allowGeometryPool=false;
                render::prepareModelUpload(*model);
                if (!render::uploadModel(*device_,*model,true)) {
                    releasePreviewModel(*model);
                    rejectedDocumentModels_.insert(path);
                    continue;
                }
                documentModels_.emplace(path,std::move(model));
            } catch (const std::exception&) {
                // Invalid resource stays a visible proxy rather than hiding
                // the map object or pretending a model was rendered.
                rejectedDocumentModels_.insert(path);
                continue;
            }
        }
        auto found=documentModels_.find(path);
        if (found==documentModels_.end()) continue;
        // Anchor model to the same authored entity location as its proxy.
        // The source file's own node transforms are applied by drawModel and picker.
        const Json& overrides=entity.extra["components"]["StaticModel"]["materials"];
        if(overrides.isObject() && overrides.size()) {
            // Model pointer: a hot-reloaded file gets a new Model and must re-resolve.
            const std::string signature=path+"|"+std::to_string(reinterpret_cast<uintptr_t>(found->second.get()))+"|"+overrides.dump(-1);
            auto& cached=instanceMaterials_[proxy.id];
            if(cached.signature!=signature || cached.materials.size()<found->second->materials.size()) {
                cached.signature=signature;
                cached.materials=found->second->materials;
                Json authored=Json::object();authored.set("materials",overrides);
                const auto plan=view::AppearanceChoiceAuthoring::resolve(*found->second,authored);
                for(size_t i=0;i<plan.materialLayers.size() && i<cached.materials.size();++i)
                    for(const auto& layer:plan.materialLayers[i])
                        view::AppearanceAuthoringService::applyMaterial(cached.materials[i],materialLibrary_,layer,
                            [&](const std::string& source)->int{
                                if(source.empty()||source.rfind("content/",0)!=0||source.size()>512||
                                   source.find('\\')!=std::string::npos||source.find(':')!=std::string::npos||
                                   source.find('\0')!=std::string::npos||!contentOk)return -1;
                                auto& model=*found->second;
                                for(size_t j=0;j<model.images.size();++j)
                                    if(!runtimeGraphImage(model.images[j])&&model.images[j].name==source&&model.images[j].tex.valid())return int(j);
                                const fs::path relative(source);
                                fs::path file=project;
                                std::error_code ec;
                                for(const auto& part:relative){
                                    if(part.empty()||part=="."||part=="..")return -1;
                                    file/=part;const auto st=fs::symlink_status(file,ec);
                                    if(ec||fs::is_symlink(st))return -1;
                                }
                                // Explicit HDR and BC DDS assets; bound encoded bytes and retained slots.
                                if(file.extension()!=".exr"&&file.extension()!=".hdr"&&file.extension()!=".dds"&&file.extension()!=".ktx2")return -1;
                                if(!fs::is_regular_file(file,ec)||ec)return -1;
                                const auto bytes=fs::file_size(file,ec);
                                if(ec||!bytes||bytes>32u*1024u*1024u)return -1;
                                size_t resident=0;
                                for(const auto& image:model.images)if(image.tex.valid()&&image.width>0&&image.height>0)
                                    resident+=size_t(image.width)*size_t(image.height)*8u;
                                std::string encoded(size_t(bytes),'\0');std::ifstream stream(file,std::ios::binary);
                                if(!stream.read(encoded.data(),std::streamsize(encoded.size()))||stream.peek()!=std::char_traits<char>::eof())return -1;
                                render::Image image;image.name=source;
                                if(!render::decodeMaterialImage(encoded,image)||(!image.importedLinearRadiance&&!image.compressed)||
                                   resident>64u*1024u*1024u||size_t(image.width)*size_t(image.height)*8u>64u*1024u*1024u-resident)return -1;
                                const auto slot=render::addImage(*device_,model,std::move(image));
                                if(slot>=0)renderer_.invalidateModelCachesFor(&model);
                                return slot;
                            });
            }
            overridden.insert(proxy.id);
        }
        modelInstances_.push_back({proxy.id,found->second.get(),
           planetFrame?*planetFrame:editor::authoredEntity(entity)?editor::authoredModelMatrix(entity):
           render::mat4::translate({proxy.center.x,proxy.center.y-proxy.half.y,proxy.center.z})*
           render::mat4::rotateY(proxy.yaw)});
    }
    {
        int done=0;
        for(const auto& path:active)done+=documentModels_.count(path)||rejectedDocumentModels_.count(path);
        loadProgress_.done=done;loadProgress_.total=int(active.size());
        if(!lastLoaded.empty())loadProgress_.current=lastLoaded;
        // Lighting was built from the partial set: rebuild once everything is in.
        if(modelsPending_ && !pending)lightingRevision_=std::numeric_limits<uint64_t>::max();
        modelsPending_=pending;
    }
    for (auto it=instanceMaterials_.begin();it!=instanceMaterials_.end();) {
        if (!overridden.count(it->first)) it=instanceMaterials_.erase(it);
        else ++it;
    }
    for (auto it=documentModelTimes_.begin();it!=documentModelTimes_.end();) {
        if (!active.count(it->first)) it=documentModelTimes_.erase(it);
        else ++it;
    }
    for (auto it=rejectedDocumentModels_.begin();it!=rejectedDocumentModels_.end();) {
        if (!active.count(*it)) it=rejectedDocumentModels_.erase(it);
        else ++it;
    }
    for (auto it=documentModels_.begin();it!=documentModels_.end();) {
        if (!active.count(it->first)) {
            releasePreviewModel(*it->second);
            it=documentModels_.erase(it);
        } else ++it;
    }
}

Json StudioScene::textureGraphCapabilities() const {
    const bool available=device_&&device_->gpuHealthy()&&device_->supportsCompute()&&device_->supportsOffscreenFrames();
    Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("available",Json::fromBool(available));
    if(device_)r.set("backend",Json::string(device_->info().backend==gfx::Backend::Metal?"metal":device_->info().backend==gfx::Backend::Vulkan?"vulkan":"unsupported"));
    r.set("reason",Json::string(available?"Native offscreen GPU preview and validated bake":"Offscreen compute unavailable on current device"));
    r.set("residentApply",Json::fromBool(false));
    r.set("studioResidentApply",Json::fromBool(available&&device_->supportsStorageBufferTextureCopy()));
    r.set("studioPbrPreview",Json::fromBool(available&&device_->supportsStorageBufferTextureCopy()));
    r.set("studioGpuIbl",Json::fromBool(available&&environment_ibl::gpuPrefilterSupported(*device_)));
    r.set("studioPbrEnvironment",Json::fromBool(available&&device_->maxFragmentTextureSlots()>=14&&device_->supports(gfx::PixelFormat::RGBA16F)));
    r.set("studioResidentHdr16",Json::fromBool(available&&device_->supportsStorageBufferTextureCopy()&&device_->supports(gfx::PixelFormat::RGBA16F)));
    r.set("studioResidentHdr32",Json::fromBool(available&&device_->supportsStorageBufferTextureCopy()&&device_->supports(gfx::PixelFormat::RGBA32F)));
    r.set("studioResidentHdrMips",Json::fromBool(available&&device_->supportsHdrTextureMipGeneration(gfx::PixelFormat::RGBA16F)));return r;
}
Json StudioScene::textureGraphOperation(editor::Workspace& ws,const Json& p) {
    if(!device_ || !deviceLifetime_ || !device_->supportsOffscreenFrames() || !device_->gpuHealthy())
        throw std::runtime_error("Studio offscreen GPU device unavailable");
    if(ws.project.dir.empty())throw std::runtime_error("Open a project first");
    const auto action=p.getStr("action");
    // Legacy type/choice targeting is distinct from the Studio document host.
    if(action=="materialTarget"||action=="applyMaterial"||action=="materialApplyStatus"||p.getBool("incrementalOutputs"))
        throw std::runtime_error("Use Studio model or PBR preview targets; legacy type/choice reuse is not supported here");
    if(!textureGraph_)textureGraph_=std::make_unique<editor::TextureGraphGpuPreview>();
    auto& owner=*textureGraph_;
    owner.checkProject(ws.project.dir);
    const auto lease=deviceLifetime_->lease();
    if(action.rfind("studio",0)==0){
        if(!graphPresentation_)graphPresentation_=std::make_unique<GraphPresentation>();
        auto& presentation=*graphPresentation_;
        if(action=="studioEnvironmentGpuRead"||action=="studioEnvironmentGpuCancel"){
            if(p.getStr("request")!=presentation.environmentGpuRequest||presentation.environmentGpuRequest.empty())throw std::invalid_argument("Stale GPU IBL request");
            if(action=="studioEnvironmentGpuCancel"&&presentation.environmentGpuPending())presentation.environmentGpuCancelled=true;
            return presentation.environmentGpuStatus();
        }
        if(action=="studioEnvironmentApplyResident"){
            if(presentation.environmentGpuPending()||(presentation.capture&&presentation.capture->state=="pending"))throw std::runtime_error("Finish current environment or capture first");
            if(!environment_ibl::gpuPrefilterSupported(*device_))throw std::runtime_error("GPU IBL unavailable");
            const auto request=p.getStr("request");
            auto source=owner.takeResidentRadiance(*device_,lease,ws.project.dir,request);
            gfx::TextureInfo info{};
            if(!device_->textureInfo(source,info)||info.width!=info.height||info.format!=gfx::PixelFormat::RGBA16F){device_->destroyTexture(source);throw std::runtime_error("GPU IBL requires resident RGBA16F");}
            presentation.environmentGpu.source=source;presentation.environmentGpu.side=info.width;
            presentation.environmentGpuSide=info.width;presentation.environmentGpuRequest=request;
            presentation.environmentGpuState="pending";presentation.environmentGpuError.clear();presentation.environmentGpuCancelled=false;
            return presentation.environmentGpuStatus();
        }
        if(action=="studioEnvironmentGenerate"||action=="studioEnvironmentApplyGpu"){
            if(presentation.environmentGpuPending())throw std::runtime_error("GPU IBL pending");
            if(presentation.capture&&presentation.capture->state=="pending")throw std::runtime_error("PBR capture pending");
            auto install=[&](int size,std::span<const float> rgba){
                auto source=environment_ibl::fromGraphLatLong(size,rgba);
                presentation.replaceEnvironment(*device_,source,"@graph");
                Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string("ready"));
                r.set("environment",Json::string("@graph"));r.set("width",Json::number(int64_t(source.width)));r.set("height",Json::number(int64_t(source.height)));
                r.set("prefilterBackend",Json::string("cpu"));return r;
            };
            if(action=="studioEnvironmentApplyGpu")return owner.consumeRadiance(ws.project.dir,p.getStr("request"),install);
            const auto program=texture_graph::compile(p["graph"],p.has("overrides")?p["overrides"]:Json::object(),editor::textureGraphImageResolver(ws));
            const auto target=program.outputs.find("radiance");
            if(target==program.outputs.end())throw std::invalid_argument("Bind a radiance output first");
            const auto rgba=texture_graph::evaluateRadiance(program,target->second);
            auto r=install(program.size,rgba);r.set("backend",Json::string("cpu"));return r;
        }
        if(action=="studioHdrApplyPbr"||action=="studioHdrApplyModel"){
            const auto channel=p.getStr("channel","emissive");
            if(channel!="color"&&channel!="emissive"&&channel!="graph")throw std::invalid_argument("Invalid HDR material channel");
            Model* model=nullptr;GraphPresentation::Snapshot* snapshot=nullptr;size_t index=0;
            if(action=="studioHdrApplyPbr"){
                if(!presentation.sample||!presentation.sampleAppearance)throw std::runtime_error("Prepare PBR model first");
                model=presentation.sample.get();snapshot=&presentation.sampleAppearance;index=presentation.sampleMaterial;
            }else{
                auto found=documentModels_.find(p.getStr("asset"));
                if(found==documentModels_.end()||!found->second)throw std::runtime_error("Studio target model not resident");
                model=found->second.get();snapshot=&presentation.target(*model);index=GraphPresentation::index(p,*model);
            }
            std::string error;
            const auto target=GraphPresentation::stamp(p,index);
            bool applied=false;
            if(channel=="graph"){
                if(!p["shaderGraph"].isObject()||p["shaderGraph"].dump(-1).size()>256u*1024u)throw std::invalid_argument("Invalid HDR Shader Graph document");
                const auto program=graph::compile(p["shaderGraph"],"material");
                applied=owner.publishResidentRadianceGraph(ws.project.dir,p.getStr("request"),*snapshot,target,*model,*device_,lease,presentation.rollback,program,&error);
            }else applied=owner.publishResidentRadianceColor(ws.project.dir,p.getStr("request"),*snapshot,target,*model,*device_,lease,presentation.rollback,channel=="color",&error);
            if(!applied)throw std::runtime_error(error);
            if(action=="studioHdrApplyPbr")presentation.sampleRenderer->invalidateModelCachesFor(model);
            else{renderer_.invalidateModelCachesFor(model);presentation.merged.clear();lastFrameSignature_=0;}
            auto r=GraphPresentation::info(*snapshot,index);r.set("state",Json::string("applied"));return r;
        }
        if(action=="studioHdrAdopt"){
            if(!device_->waitForResourceIdle())throw std::runtime_error("resident HDR replacement retirement failed");
            const bool mipmaps=owner.status().getBool("gpuTextureMipmaps");
            auto candidate=owner.takeResidentRadiance(*device_,lease,ws.project.dir,p.getStr("request"));
            gfx::TextureInfo info;
            if(!device_->textureInfo(candidate,info)){device_->destroyTexture(candidate);throw std::runtime_error("resident HDR metadata unavailable");}
            if(presentation.residentHdr.valid())device_->destroyTexture(presentation.residentHdr);
            const int levels=mipmaps?gfx::maxTextureMipLevels(info.width,info.height,1):1;
            presentation.residentHdr=candidate;presentation.residentHdrRequest=p.getStr("request");presentation.residentHdrMipCount=levels;presentation.hdrCapture.reset();
            Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string("resident"));r.set("request",Json::string(presentation.residentHdrRequest));
            r.set("mipLevels",Json::number(int64_t(levels)));r.set("size",Json::number(int64_t(info.width)));return r;
        }
        if(action=="studioHdrRelease"){
            if(presentation.residentHdr.valid()){
                if(!device_->waitForResourceIdle())throw std::runtime_error("resident HDR retirement failed");
                device_->destroyTexture(presentation.residentHdr);
            }
            presentation.residentHdr={};presentation.residentHdrRequest.clear();presentation.residentHdrMipCount=0;presentation.hdrCapture.reset();
            Json r=Json::object();r.set("ok",Json::fromBool(true));return r;
        }
        if(action=="studioHdrCapture"){
            if(!presentation.residentHdr.valid()||p.getStr("request")!=presentation.residentHdrRequest)throw std::runtime_error("no matching resident HDR texture");
            const int mip=render::texture_graph::integer(p["mip"],0,0,presentation.residentHdrMipCount-1);
            gfx::TextureInfo info;if(!device_->textureInfo(presentation.residentHdr,info))throw std::runtime_error("resident HDR metadata unavailable");
            if(std::max(1,info.width>>mip)>128||std::max(1,info.height>>mip)>128)throw std::runtime_error("choose a mip of at most 128 pixels for diagnostic capture");
            if(presentation.hdrCapture&&presentation.hdrCapture->state=="pending")throw std::runtime_error("HDR diagnostic capture pending");
            auto capture=std::make_shared<GraphPresentation::Capture>();
            gfx::TextureReadbackDesc rd;rd.mipLevel=mip;
            if(!device_->beginOffscreenFrame())throw std::runtime_error("HDR diagnostic frame unavailable");
            bool encoded=false;
            try{encoded=device_->readTextureAsync(presentation.residentHdr,rd,[capture](auto result){
                try{
                    const size_t bpp=result.format==gfx::PixelFormat::RGBA16F?8u:result.format==gfx::PixelFormat::RGBA32F?16u:0u;
                    if(!result.success()||!bpp||result.width>128||result.height>128||result.pixels.size()!=size_t(result.width)*result.height*bpp)throw std::runtime_error("invalid HDR diagnostic pixels");
                    capture->width=result.width;capture->height=result.height;capture->hex.reserve(size_t(result.width)*result.height*8);
                    for(size_t i=0;i<size_t(result.width)*result.height;++i)for(size_t c=0;c<4;++c){
                        float v=1.f;
                        if(c!=3){const size_t at=i*bpp+c*(bpp/4);
                            if(bpp==8)v=render::cameraHalfToFloat(uint16_t(result.pixels[at])|(uint16_t(result.pixels[at+1])<<8));
                            else std::memcpy(&v,result.pixels.data()+at,4);
                            if(!std::isfinite(v))throw std::runtime_error("nonfinite HDR diagnostic sample");
                            v=render::texture_graph::mapRadiance(v,1.f,true);
                        }
                        const auto byte=uint8_t(std::lround(std::clamp(v,0.f,1.f)*255.f));
                        capture->hex+="0123456789abcdef"[byte>>4];capture->hex+="0123456789abcdef"[byte&15];
                    }
                    capture->state="ready";
                }catch(const std::exception& e){capture->state="failed";capture->error=e.what();}
            });}catch(...){device_->endFrame();throw;}
            device_->endFrame();
            if(!encoded)throw std::runtime_error("HDR diagnostic readback rejected");
            presentation.hdrCapture=capture;
            Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string("pending"));return r;
        }
        if(action=="studioHdrRead"){
            if(p.getStr("request")!=presentation.residentHdrRequest||!presentation.hdrCapture)throw std::runtime_error("no matching HDR diagnostic capture");
            const auto& capture=*presentation.hdrCapture;
            Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("state",Json::string(capture.state));r.set("error",Json::string(capture.error));
            if(capture.state=="ready"){r.set("width",Json::number(int64_t(capture.width)));r.set("height",Json::number(int64_t(capture.height)));r.set("rgbaHex",Json::string(capture.hex));}return r;
        }
        if(action=="studioModelTargets"){
            Json list=Json::array();
            for(auto& [path,model]:documentModels_)if(model&&model->materials.size()){
                auto& snap=presentation.target(*model);
                Json row=Json::object(),materials=Json::array();row.set("asset",Json::string(path));
                for(size_t i=0;i<snap->materials.size();++i)materials.push(GraphPresentation::info(snap,i));
                row.set("materials",materials);list.push(row);
            }
            Json r=Json::object();r.set("ok",Json::fromBool(true));r.set("models",list);return r;
        }
        if(action=="studioModelApply"){
            const auto found=documentModels_.find(p.getStr("asset"));
            if(found==documentModels_.end()||!found->second)throw std::runtime_error("Studio target model not resident");
            auto& model=*found->second;auto& snap=presentation.target(model);
            const auto index=GraphPresentation::index(p,model);std::string error;
            if(!owner.publishCompletedGpuSnapshot(ws.project.dir,p.getStr("request"),snap,GraphPresentation::stamp(p,index),model,*device_,lease,presentation.rollback,nullptr,&error))
                throw std::runtime_error(error);
            renderer_.invalidateModelCachesFor(&model);presentation.merged.clear();lastFrameSignature_=0;
            auto r=GraphPresentation::info(snap,index);r.set("state",Json::string("applied"));return r;
        }
        if(action=="studioPbrPrepare"){
            const auto shape=p.getStr("shape","sphere");
            if(shape!="sphere"&&shape!="cube"&&shape!="plane"&&shape!="model")throw std::invalid_argument("Invalid preview shape");
            if(!device_->waitForResourceIdle())throw std::runtime_error("PBR preview retirement failed");
            std::unique_ptr<Model> model;
            if(shape=="model")model=loadContentModel(ws.project.dir,p.getStr("asset"));
            else{
                MeshBuilder mesh;mesh.color(rgba(255,255,255));
                if(shape=="sphere")mesh.sphere({0,0,0},1.f,32,48);
                else if(shape=="cube")mesh.box({0,0,0},{1.7f,1.7f,1.7f});
                else mesh.quad({-1,0,-1},{-1,0,1},{1,0,1},{1,0,-1});
                Material base;base.name="Graph PBR";base.vertexColors=false;base.baseColor={1,1,1,1};base.metallic=1.f;base.roughness=1.f;base.doubleSided=shape=="plane";
                model=modelFromParts("studio/texture-graph-preview",{{shape,&mesh,base,mat4{}}});
                if(model){
                    for(auto& v:model->vertices){
                        if(shape=="sphere")v.uv={.5f+std::atan2(v.pos.z,v.pos.x)/6.2831853f,.5f-std::asin(std::clamp(v.pos.y,-1.f,1.f))/3.14159265f};
                        else if(shape=="plane")v.uv={(v.pos.x+1.f)*.5f,(v.pos.z+1.f)*.5f};
                        else if(std::abs(v.nrm.x)>.5f)v.uv={v.pos.z/1.7f+.5f,v.pos.y/1.7f+.5f};
                        else if(std::abs(v.nrm.y)>.5f)v.uv={v.pos.x/1.7f+.5f,v.pos.z/1.7f+.5f};
                        else v.uv={v.pos.x/1.7f+.5f,v.pos.y/1.7f+.5f};
                    }
                    generateTangents(model->vertices,model->indices,0,uint32_t(model->indices.size()));
                    model->batching.autoLod=false;prepareModelUpload(*model);
                    if(!uploadModel(*device_,*model)){releaseModel(*device_,*model);model.reset();}
                }
            }
            if(!model)throw std::runtime_error("PBR preview model unavailable");
            size_t index=0;
            try {
                if(model->materials.empty())throw std::runtime_error("PBR preview model has no materials");
                index=p.has("materialIndex")?GraphPresentation::index(p,*model):0;
            }catch(...){releaseModel(*device_,*model);throw;}
            if(!presentation.sampleRenderer){
                auto renderer=std::make_unique<Renderer3D>();renderer->setGpuDeviceLease(lease);
                if(!renderer->init(*device_)){renderer->shutdown();releaseModel(*device_,*model);throw std::runtime_error("PBR renderer unavailable");}
                renderer->setShadowQuality(ShadowQuality::Off);renderer->setMaterialQuality(MaterialQuality::High);
                renderer->setGpuAuthoredDrawEnabled(false);renderer->setModelMultiDrawEnabled(false);renderer->setModelInstancingEnabled(false);
                presentation.sampleRenderer=std::move(renderer);
            }
            if(!presentation.color.valid()||!presentation.depth.valid()){
                gfx::TextureDesc td;td.width=td.height=256;td.format=gfx::PixelFormat::RGBA8;td.renderTarget=true;td.label="Texture Graph PBR preview";
                if(!presentation.color.valid())presentation.color=device_->createTexture(td);
                td.format=gfx::PixelFormat::Depth;td.label="Texture Graph PBR preview depth";
                if(!presentation.depth.valid())presentation.depth=device_->createTexture(td);
                if(!presentation.color.valid()||!presentation.depth.valid()){releaseModel(*device_,*model);throw std::runtime_error("PBR preview target unavailable");}
            }
            presentation.resetSample(*device_);presentation.sample=std::move(model);
            presentation.sampleAppearance=GraphPresentation::snapshot(*presentation.sample);
            presentation.sampleMaterial=index;presentation.sampleShape=shape;
            return GraphPresentation::info(presentation.sampleAppearance,index);
        }
        if(action=="studioPbrApply"){
            if(!presentation.sample||!presentation.sampleAppearance)throw std::runtime_error("Prepare PBR model first");
            std::string error;
            if(!owner.publishCompletedGpuSnapshot(ws.project.dir,p.getStr("request"),presentation.sampleAppearance,
                 GraphPresentation::stamp(p,presentation.sampleMaterial),*presentation.sample,*device_,lease,presentation.rollback,nullptr,&error))
                throw std::runtime_error(error);
            presentation.sampleRenderer->invalidateModelCachesFor(presentation.sample.get());
            auto r=GraphPresentation::info(presentation.sampleAppearance,presentation.sampleMaterial);r.set("state",Json::string("applied"));return r;
        }
        if(action=="studioPbrRender"){
            if(presentation.environmentGpuPending())throw std::runtime_error("GPU IBL pending");
            if(!presentation.sample||!presentation.sampleAppearance)throw std::runtime_error("Prepare PBR preview first");
            if(presentation.capture&&presentation.capture->state=="pending")throw std::runtime_error("PBR capture pending");
            presentation.yaw=previewScalar(p["yaw"],.65f,-100.f,100.f);presentation.pitch=previewScalar(p["pitch"],.42f,-1.4f,1.4f);
            presentation.zoom=previewScalar(p["zoom"],1.f,.6f,3.f);presentation.light=previewScalar(p["light"],1.25f,0.f,8.f);
            presentation.lightYaw=previewScalar(p["lightYaw"],.6f,-6.3f,6.3f);
            presentation.environmentYaw=previewScalar(p["environmentYaw"],0.f,-6.3f,6.3f);
            presentation.environmentIntensity=previewScalar(p["environmentIntensity"],1.f,0.f,8.f);
            presentation.setEnvironment(*device_,ws.project.dir,p.getStr("environment"),p.getBool("reloadEnvironment"));
            presentation.capture=std::make_shared<GraphPresentation::Capture>();++presentation.captureSerial;presentation.draw=true;
            return presentation.captureStatus();
        }
        if(action=="studioPbrRead")return presentation.captureStatus();
        throw std::invalid_argument("Unknown Studio graph presentation action");
    }
    if(action=="start")return owner.start(p,ws.project.dir,lease,editor::textureGraphImageResolver(ws));
    if(action=="read")return owner.status();
    if(action=="cancel")return owner.cancel(p,ws.project.dir);
    if(action=="publish")return owner.publish(ws,p);
    if(action=="enqueue")return owner.enqueue(p,ws.project.dir,lease,render::texture_graph::integer(p["priority"],1,0,3),editor::textureGraphImageResolver(ws));
    if(action=="queueRead")return owner.queueStatus();
    if(action=="queueCancel")return owner.cancelTicket(p.getStr("ticket"),ws.project.dir);
    if(action=="queueRelease")return owner.releaseTicket(p.getStr("ticket"),ws.project.dir,p.getBool("discardResult"));
    throw std::invalid_argument("unknown Studio GPU preview action");
}
void StudioScene::invalidateTextureGraphProject() {
    if(textureGraph_)textureGraph_->checkProject("");
    if(graphPresentation_){
        if(!device_->waitForResourceIdle())throw std::runtime_error("Graph presentation retirement failed");
        graphPresentation_->shutdown(*device_);graphPresentation_.reset();
        lastFrameSignature_=0;
    }
}
bool StudioScene::serviceTextureGraph(const std::string& project) {
    if(!textureGraph_)return false;
    textureGraph_->checkProject(project);
    if(graphPresentation_&&graphPresentation_->rollback.pendingImages())(void)graphPresentation_->rollback.collect(*device_);
    if(!device_->beginOffscreenFrame())throw std::runtime_error("Studio GPU offscreen frame unavailable");
    try {
        textureGraph_->tick(*device_,project);
        if(graphPresentation_)graphPresentation_->serviceEnvironmentGpu(*device_);
        if(graphPresentation_&&graphPresentation_->draw)graphPresentation_->renderSample(*device_);
    }catch(...) {device_->endPass();device_->endFrame();throw;}
    device_->endFrame();
    bool more=false;
    if(graphPresentation_){
        auto& p=*graphPresentation_;
        if(p.capture&&p.capture->state=="pending"&&std::chrono::steady_clock::now()>p.capture->deadline){p.capture->state="failed";p.capture->error="Native PBR capture timed out";}
        if(p.hdrCapture&&p.hdrCapture->state=="pending"&&std::chrono::steady_clock::now()>p.hdrCapture->deadline){p.hdrCapture->state="failed";p.hdrCapture->error="HDR diagnostic capture timed out";}
        more=p.environmentGpuPending()||p.draw||(p.capture&&p.capture->state=="pending")||(p.hdrCapture&&p.hdrCapture->state=="pending")||p.rollback.pendingImages()!=0;
    }
    return textureGraph_->needsFrame()||more;
}

// The preview terrain is a closed cube-sphere shell. A sphere contained
// inside all its triangle planes is a conservative occluder even with relief.
static float studioPlanetInnerRadius(const render::MeshBuilder& mesh){
    const auto& vertices=mesh.vertices();const auto& indices=mesh.indices();
    if(indices.empty()||indices.size()%3)return 0.f;
    float inner=std::numeric_limits<float>::infinity();
    for(size_t i=0;i<indices.size();i+=3){
        if(indices[i]>=vertices.size()||indices[i+1]>=vertices.size()||indices[i+2]>=vertices.size())return 0.f;
        const auto a=vertices[indices[i]].pos,b=vertices[indices[i+1]].pos,c=vertices[indices[i+2]].pos;
        const auto n=render::cross(b-a,c-a);const float len=std::sqrt(render::dot(n,n));
        if(!std::isfinite(len)||len<=1e-8f)return 0.f;
        const float distance=std::abs(render::dot(n,a))/len;
        if(!std::isfinite(distance))return 0.f;
        inner=std::min(inner,distance);
    }
    return std::isfinite(inner)?std::max(0.f,inner*.995f-.01f):0.f;
}
static bool studioPlanetOccludesSphere(const render::Camera& view,render::float3 center,float bound,float inner){
    if(!(inner>0.f&&bound>=0.f))return false;
    const auto eye=view.eye();
    if(view.ortho){
        const auto axis=render::normalize(eye-view.target);
        const float along=render::dot(center,axis);
        const auto perpendicular=center-axis*along;
        return along+bound<0.f&&std::sqrt(render::dot(perpendicular,perpendicular))+bound<inner;
    }
    const auto toCenter=center-eye;
    const float e=std::sqrt(render::dot(eye,eye)),d=std::sqrt(render::dot(toCenter,toCenter));
    if(!(e>inner&&d>bound&&d-bound>std::sqrt(e*e-inner*inner)))return false;
    const float angle=std::acos(std::clamp(render::dot(toCenter,eye*(-1.f))/(d*e),-1.f,1.f));
    return angle+std::asin(std::clamp(bound/d,0.f,1.f))<std::asin(std::clamp(inner/e,0.f,1.f));
}
void StudioScene::clearLivePlanetTerrain(){
    for(auto& face:planetLiveTerrainFaces_){
        if(face.vb.valid())device_->destroyBuffer(face.vb);
        if(face.ib.valid())device_->destroyBuffer(face.ib);
        face={};
    }
    planetLiveTerrainFaceSignatures_={};planetLiveTerrainGrid_=Json{};
    planetLiveTerrainCpu_.reset();planetLiveTerrainVisible_=false;planetLiveOccluderRadius_=0.f;
}
bool StudioScene::syncLivePlanetTerrain(const editor::MapDoc& doc,const std::string& projectPath){
    const auto& layer=planetLiveSnapshot_["forestTerrain"];
    if(!planetForestLiveCurrent(doc,projectPath)||!layer.getBool("active")||
       layer.getInt("tick",-1)!=planetLiveSnapshot_["forest"].getInt("tick",-2)||
       layer["grid"].getStr("body")!=planetPreview_->bodyId)return false;
    const auto& grid=layer["grid"];
    const auto ready=[](const render::Mesh& m){return m.vb.valid()&&m.ib.valid();};
    if(grid==planetLiveTerrainGrid_&&planetLiveTerrainCpu_&&std::all_of(planetLiveTerrainFaces_.begin(),planetLiveTerrainFaces_.end(),ready))return true;
    const int bi=doc.data.findBody(planetPreview_->bodyId);if(bi<0)return false;
    const auto& body=doc.data.bodies[size_t(bi)];
    auto mesh=buildStudioLivePlanetTerrain(grid,body,*planetPreview_);
    if(!mesh||!device_->waitForResourceIdle())return false;
    std::array<render::Mesh,6> replacements{};
    std::array<Json,6> signatures{};
    std::array<bool,6> changed{};
    const auto release=[&](render::Mesh& m){
        if(m.vb.valid())device_->destroyBuffer(m.vb);
        if(m.ib.valid())device_->destroyBuffer(m.ib);m={};
    };
    for(int f=0;f<6;++f){
        signatures[f]=studioLiveTerrainFaceSignature(grid,f);
        changed[f]=!ready(planetLiveTerrainFaces_[f])||!(signatures[f]==planetLiveTerrainFaceSignatures_[f]);
        if(!changed[f])continue;
        const auto face=buildStudioLivePlanetTerrain(grid,body,*planetPreview_,f);
        if(face)replacements[f]=face->upload(*device_);
        if(!ready(replacements[f])){
            for(auto& m:replacements)release(m);
            return false;
        }
    }
    // Commit all face replacements and CPU picking mesh together, never mix
    // successful new faces with a failed upload from the same completed tick.
    for(int f=0;f<6;++f)if(changed[f]){
        release(planetLiveTerrainFaces_[f]);planetLiveTerrainFaces_[f]=replacements[f];
    }
    planetLiveTerrainFaceSignatures_=std::move(signatures);planetLiveTerrainGrid_=grid;
    planetLiveOccluderRadius_=studioPlanetInnerRadius(*mesh);
    planetLiveTerrainCpu_=std::move(mesh);return true;
}
void StudioScene::clearPlanetForestFarMesh(){
    if(device_)for(auto& [cell,chunk]:planetForestFarChunks_){
        if(chunk.mesh.vb.valid())device_->destroyBuffer(chunk.mesh.vb);
        if(chunk.mesh.ib.valid())device_->destroyBuffer(chunk.mesh.ib);
    }
    planetForestFarChunks_.clear();planetForestFarBody_.clear();planetForestFarProject_.clear();
    planetForestFarPath_.clear();planetForestFarEpoch_.clear();planetForestFarRevision_=0;
}
void StudioScene::clearPlanetForestModels(){
    clearPlanetForestFarMesh();
    if(device_&&planetForestBlendProxy_.vb.valid())device_->destroyBuffer(planetForestBlendProxy_.vb);
    if(device_&&planetForestBlendProxy_.ib.valid())device_->destroyBuffer(planetForestBlendProxy_.ib);
    planetForestBlendProxy_={};planetForestDetailWeights_.clear();planetForestBlendTime_={};planetForestBlendIdentity_.clear();
    if(device_)for(auto& [id,model]:planetForestModels_)if(model)releasePreviewModel(*model);
    if(device_)for(auto& [key,model]:planetForestRootModels_)if(model)releasePreviewModel(*model);
    if(device_)for(auto& [id,model]:planetForestLogModels_)if(model)releasePreviewModel(*model);
    planetForestRootModels_.clear();planetForestRejectedRoots_.clear();planetForestLogModels_.clear();planetForestRejectedLogs_.clear();
    planetForestTextureStamp_.clear();planetForestTextureCheck_={};
    planetForestModels_.clear();planetForestRejectedModels_.clear();planetForestEvergreen_.clear();planetForestSeasonMaterials_.clear();
    planetForestModelBytes_=0;planetForestModelCatalog_=Json{};planetForestModelProject_.clear();planetForestModelsPending_=false;
}
bool StudioScene::planetForestLiveCurrent(const editor::MapDoc& doc,const std::string& projectPath)const{
    return planetPreview_&&planetLiveSnapshot_.getBool("active")&&planetLiveSnapshot_.getStr("topology")=="planet"&&
        planetLiveSnapshot_["forest"].getBool("active")&&
        (planetLiveSnapshot_["forest"].getStr("focusBody").empty()||planetLiveSnapshot_["forest"].getStr("focusBody")==planetPreview_->bodyId)&&planetLiveProject_==projectPath&&
        planetLivePath_==doc.path&&planetLiveRevision_==doc.revision&&!doc.dirty;
}
namespace {
std::optional<render::float3> studioForestVec(const Json& value){
    if(!value.isArray()||value.size()!=3)return {};
    const auto& a=value.items();
    render::float3 v{float(a[0].toDouble(NAN)),float(a[1].toDouble(NAN)),float(a[2].toDouble(NAN))};
    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return {};
    return v;
}
std::optional<render::mat4> studioForestFrame(render::float3 origin,render::float3 up,render::float3 east){
    if(render::dot(up,up)<.5f)return {};
    up=render::normalize(up);east=east-up*render::dot(east,up);
    if(render::dot(east,east)<.0001f)return {};
    east=render::normalize(east);
    const auto forward=render::normalize(render::cross(east,up));
    render::mat4 basis;
    const render::float3 axes[3]={east,up,forward};
    for(int i=0;i<3;++i){basis.at(0,i)=axes[i].x;basis.at(1,i)=axes[i].y;basis.at(2,i)=axes[i].z;}
    return render::mat4::translate(origin)*basis;
}
uint32_t studioForestRootTier(const Json& row){
    if(!row.getBool("visibleRoots"))return 0;
    const auto depth=row.getInt("rootDepthCm"),exposed=row.getInt("exposedCm");
    if(depth<=0||depth>10000||exposed<=0||exposed>depth)return 0;
    // Same three depth tiers as ArborForestAdapter::exposedRootTier.
    return uint32_t(std::clamp<int64_t>((exposed*3+depth-1)/depth,1,3));
}
}
void StudioScene::syncPlanetForestModels(const editor::MapDoc& doc,const std::string& projectPath){
    namespace fs=std::filesystem;namespace tw=terraweave;
    const auto& catalog=doc.data.worldExtra["terraweaveForest"];
    const auto now=std::chrono::steady_clock::now();
    const bool identityChanged=planetForestModelProject_!=projectPath||!(planetForestModelCatalog_==catalog);
    if(identityChanged||now>=planetForestTextureCheck_){
        auto stamp=studioForestTextureStamp(projectPath,catalog);
        if(identityChanged||stamp!=planetForestTextureStamp_){
            if((!planetForestModels_.empty()||!planetForestRootModels_.empty()||!planetForestLogModels_.empty())&&!device_->waitForResourceIdle())return;
            clearPlanetForestModels();planetForestModelProject_=projectPath;planetForestModelCatalog_=catalog;
            planetForestTextureStamp_=std::move(stamp);
        }
        planetForestTextureCheck_=now+std::chrono::seconds(1);
    }
    planetForestModelsPending_=false;
    if(projectPath.empty()||!planetPreview_)return;
    const bool live=planetForestLiveCurrent(doc,projectPath);
    std::set<uint32_t> needed;
    std::set<ForestRootModelKey> rootsNeeded;
    std::set<uint32_t> logsNeeded;
    if(live){
        for(const auto& row:planetLiveSnapshot_["forest"]["plants"].items()){
            if(row.getStr("body")!=planetPreview_->bodyId||row.getBool("spent"))continue;
            if(row.getBool("physical")&&row["capsule"].isObject()){
                const auto species=uint32_t(row.getInt("species"));
                if(planetForestOverlays_.arborTrunks.count(species)&&!planetForestRejectedModels_.count(species))
                    needed.insert(species);
                else logsNeeded.insert(species);
                continue;
            }
            if(row.getBool("fallen")||row.getBool("harvested"))continue;
            const auto id=uint32_t(row.getInt("species"));if(!id)continue;
            needed.insert(id);
            if(const auto tier=studioForestRootTier(row);tier)rootsNeeded.emplace(id,row.getStr("habit")=="shrub",tier);
        }
    }else if(!planetForestOverlays_.invalid)for(const auto& plant:planetForestOverlays_.picks)needed.insert(plant.species);
    const auto dir=catalog.getStr("botanicalTextures");
    constexpr size_t budget=192u*1024u*1024u;
    // Generate at most one new model per Scene frame. Shared renderer owns LOD and batching.
    const auto read=studioForestTextureReader(projectPath);
    for(const auto id:logsNeeded){
        if(planetForestLogModels_.count(id)||planetForestRejectedLogs_.count(id))continue;
        if(planetForestLogModels_.size()>=16||planetForestModelBytes_>=budget){planetForestRejectedLogs_.insert(id);continue;}
        std::unique_ptr<render::Model> model;
        try{
            constexpr int segments=16;
            render::MeshBuilder side,ends;
            side.color(render::rgba(255,255,255));side.cylinder({0,-.5f,0},.5f,1.f,segments,false);
            ends.color(render::rgba(255,255,255));
            ends.disc({0,.5f,0},.5f,segments);
            ends.transform(render::mat4::translate({0,-.5f,0})*render::mat4::rotateX(3.14159265359f)).disc({0,0,0},.5f,segments);
            render::Material bark;bark.name="Forest consequence bark";bark.baseColor={.31f,.18f,.11f,1};bark.roughness=.9f;
            render::Material wood;wood.name="Forest exposed wood";wood.baseColor={.61f,.41f,.22f,1};wood.roughness=.85f;
            model=render::modelFromParts("studio/forest/log/"+std::to_string(id),{{"Bark",&side,bark,render::mat4{}},{"Wood ends",&ends,wood,render::mat4{}}});
            if(!model||model->vertices.size()<size_t(segments)*4)throw std::runtime_error("Forest log mesh");
            // MeshBuilder cylinder emits four independent vertices per segment.
            // Unwrap the seam explicitly; leave end faces on their own material.
            for(int i=0;i<segments;++i){
                const float a=float(i)/segments,b=float(i+1)/segments;
                auto* v=model->vertices.data()+i*4;
                v[0].uv={a,0};v[1].uv={b,0};v[2].uv={a,1};v[3].uv={b,1};
            }
            render::generateTangents(model->vertices,model->indices,0,uint32_t(model->indices.size()));
            if(!dir.empty()&&catalog["species"].isArray()&&catalog["species"].size()<=64){
                for(const auto& def:catalog["species"].items())if(def.getInt("id")==int64_t(id)){
                    const auto* preset=tw::findBotanicalPreset(def.getStr("preset"));
                    if(preset)tw::applyBotanicalTexture(*model,0,dir,std::string(preset->barkMaterial),read);
                    break;
                }
            }
            size_t bytes=model->vertices.size()*sizeof(render::ModelVertex)+model->indices.size()*sizeof(uint32_t);
            for(auto& image:model->images){bytes+=size_t(image.width)*size_t(image.height)*8u;image.name=(fs::path(projectPath)/"content"/image.name).generic_string();}
            if(bytes>budget-planetForestModelBytes_)throw std::length_error("Forest log cache budget");
            model->path=projectPath+"/"+model->path;
            render::prepareModelUpload(*model);
            if(!render::uploadModel(*device_,*model,false,false))throw std::runtime_error("Forest log upload");
            planetForestLogModels_.emplace(id,std::move(model));planetForestModelBytes_+=bytes;
        }catch(const std::exception&){if(model)releasePreviewModel(*model);planetForestRejectedLogs_.insert(id);}
        planetForestModelsPending_=true;return;
    }
    if(!catalog["species"].isArray()||catalog["species"].size()>64)return;
    for(const auto& def:catalog["species"].items()){
        const auto id=uint32_t(def.getInt("id",0));
        if(!needed.count(id)||planetForestModels_.count(id)||planetForestRejectedModels_.count(id))continue;
        if(planetForestModels_.size()>=16||planetForestModelBytes_>=budget){planetForestRejectedModels_.insert(id);continue;}
        std::unique_ptr<render::Model> model;
        try{
            const auto recipe=world::parseForestSpeciesRecipe(def);
            tw::TreeLodSettings settings;settings.mesh=recipe.mesh;
            std::vector<int32_t> owners;
            model=tw::makeArborTreeLodModel(recipe.profile,recipe.seed,settings,&owners);
            if(!model||model->vertices.size()>1000000||model->indices.size()>6000000)
                throw std::length_error("Studio forest prototype geometry budget");
            if(recipe.preset&&!dir.empty()&&world::safeAssetPath(dir)){
                tw::applyBotanicalTexture(*model,0,dir,std::string(recipe.preset->barkMaterial),read);
                tw::applyBotanicalTexture(*model,1,dir,std::string(recipe.preset->leafMaterial),read);
            }
            // Existing shared GPU branch hierarchy; explicit per-draw tangent wind.
            const render::wind::WindMaskLibrary masks;
            const auto atlas=render::wind::WindMaskAtlas::fromLibrary(masks);
            tw::applyArborWind(*model,tw::generateTree(recipe.profile,recipe.seed,tw::TreeLOD::High),owners,render::wind::WindProfile{},masks,*atlas);
            size_t bytes=model->vertices.size()*sizeof(render::ModelVertex)+model->indices.size()*sizeof(uint32_t);
            for(auto& image:model->images){
                bytes+=size_t(image.width)*size_t(image.height)*8u; // conservative mip estimate
                image.name=(fs::path(projectPath)/"content"/image.name).generic_string();
            }
            if(bytes>budget-planetForestModelBytes_)throw std::length_error("Studio forest model cache budget");
            model->path=projectPath+"/"+model->path;
            render::prepareModelUpload(*model);
            if(!render::uploadModel(*device_,*model,false,false))throw std::runtime_error("Studio forest model upload");
            planetForestModels_.emplace(id,std::move(model));planetForestEvergreen_[id]=tw::treeIsEvergreen(recipe.profile);planetForestModelBytes_+=bytes;
        }catch(const std::exception&){
            if(model)releasePreviewModel(*model);
            planetForestRejectedModels_.insert(id);
        }
        planetForestModelsPending_=true;
        return;
    }
    for(const auto& key:rootsNeeded){
        if(planetForestRootModels_.count(key)||planetForestRejectedRoots_.count(key))continue;
        if(planetForestRootModels_.size()>=32||planetForestModelBytes_>=budget){planetForestRejectedRoots_.insert(key);continue;}
        const auto [id,shrub,tier]=key;
        const auto& defs=catalog["species"].items();
        const auto def=std::find_if(defs.begin(),defs.end(),[&](const Json& j){return j.getInt("id")==int64_t(id);});
        if(def==defs.end()){planetForestRejectedRoots_.insert(key);continue;}
        std::unique_ptr<render::Model> model;
        try{
            const auto recipe=world::parseForestSpeciesRecipe(*def);
            model=tw::makeExposedRootModel(id,recipe.seed,tw::defaultRootProfile(shrub?tw::PlantHabit::Shrub:tw::PlantHabit::Tree),tier);
            const size_t bytes=model->vertices.size()*sizeof(render::ModelVertex)+model->indices.size()*sizeof(uint32_t);
            if(bytes>budget-planetForestModelBytes_)throw std::length_error("Forest root cache budget");
            model->path=projectPath+"/"+model->path+(shrub?"/shrub":"/tree");
            render::prepareModelUpload(*model);
            if(!render::uploadModel(*device_,*model,false,false))throw std::runtime_error("Forest root upload");
            planetForestRootModels_.emplace(key,std::move(model));planetForestModelBytes_+=bytes;
        }catch(const std::exception&){if(model)releasePreviewModel(*model);planetForestRejectedRoots_.insert(key);}
        planetForestModelsPending_=true;return;
    }
}
void StudioScene::drawPlanetForestFarChunks(const Json& rows,float flora,int width,int height){
    const auto body=planetPreview_->bodyId;
    const auto epoch=planetLiveSnapshot_.getStr("epoch");
    const bool changed=planetForestFarBody_!=body||planetForestFarProject_!=planetLiveProject_||
        planetForestFarPath_!=planetLivePath_||planetForestFarEpoch_!=epoch||planetForestFarRevision_!=planetLiveRevision_;
    bool idle=false;
    const auto acquireIdle=[&](){if(!idle)idle=device_->waitForResourceIdle();return idle;};
    if(changed){
        if(!acquireIdle())return;
        clearPlanetForestFarMesh();planetForestFarBody_=body;planetForestFarProject_=planetLiveProject_;
        planetForestFarPath_=planetLivePath_;planetForestFarEpoch_=epoch;planetForestFarRevision_=planetLiveRevision_;
    }
    std::map<int64_t,Json> requested;
    for(const auto& row:rows.items()){
        const auto cell=row.getInt("cell",-1);if(cell<0)continue;
        auto it=requested.find(cell);
        if(it==requested.end())it=requested.emplace(cell,Json::array()).first;
        it->second.push(row);
    }
    // Bound the actual proxy geometry, rather than culling a cell centre.
    // Clip planes are extracted from the same view/projection as the Scene.
    const auto view=viewCamera();
    const auto clip=view.proj(float(std::max(1,width))/float(std::max(1,height)),false)*view.view();
    const auto eye=view.eye();
    struct Candidate { int64_t cell; bool horizon; float distance; size_t bytes; };
    std::vector<Candidate> order;
    size_t frustumRejected=0,horizonRejected=0;
    const float inner=planetLiveTerrainVisible_?planetLiveOccluderRadius_:planetAuthoredOccluderRadius_;
    constexpr size_t gpuBudget=64u*1024u*1024u;
    // Each proxy is six vertices and twelve uint32 indices. Use the actual
    // CPU upload vertex type and a conservative overhead per resident chunk.
    render::MeshBuilder layout;
    const size_t proxyBytes=6*sizeof(layout.vertices()[0])+12*sizeof(uint32_t);
    for(const auto& [cell,chunkRows]:requested){
        render::float3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
        for(const auto& row:chunkRows.items()){
            const auto p=studioForestVec(row["position"]);if(!p)continue;
            const float h=flora*float(std::clamp<int64_t>(row.getInt("sizePermille"),0,1000))*.006f;
            lo={std::min(lo.x,p->x-h),std::min(lo.y,p->y-h),std::min(lo.z,p->z-h)};
            hi={std::max(hi.x,p->x+h),std::max(hi.y,p->y+h),std::max(hi.z,p->z+h)};
        }
        if(!std::isfinite(lo.x))continue;
        const auto center=(lo+hi)*.5f,half=(hi-lo)*.5f;
        bool outside=false;
        for(int axis=0;axis<3&&!outside;++axis)for(const float sign:{-1.f,1.f}){
            const float a=clip.at(3,0)+sign*clip.at(axis,0),b=clip.at(3,1)+sign*clip.at(axis,1);
            const float c=clip.at(3,2)+sign*clip.at(axis,2),d=clip.at(3,3)+sign*clip.at(axis,3);
            // Small padded frustum to prefetch at viewport boundaries.
            const float margin=planetForestFarChunks_.count(cell)?.08f:.04f;
            const float pad=margin*(std::abs(a)+std::abs(b)+std::abs(c))*std::max(1.f,view.distance);
            if(a*center.x+b*center.y+c*center.z+d+std::abs(a)*half.x+std::abs(b)*half.y+std::abs(c)*half.z+pad<0)outside=true;
        }
        if(outside){++frustumRejected;continue;}
        if(studioPlanetOccludesSphere(view,center,std::sqrt(render::dot(half,half)),inner)){++horizonRejected;continue;}
        const auto delta=center-eye;
        // Non-occluded back-facing relief remains eligible, at lower priority.
        const bool behind=render::dot(center,view.ortho?eye-view.target:eye-center)<0.f;
        order.push_back({cell,behind,render::dot(delta,delta),chunkRows.size()*proxyBytes+4096u});
    }
    std::sort(order.begin(),order.end(),[](const Candidate& a,const Candidate& b){
        return std::tie(a.horizon,a.distance,a.cell)<std::tie(b.horizon,b.distance,b.cell);
    });
    std::set<int64_t> resident;
    size_t plannedBytes=0,budgetRejected=0;
    for(const auto& c:order){
        if(c.bytes>gpuBudget-plannedBytes){++budgetRejected;continue;}
        plannedBytes+=c.bytes;resident.insert(c.cell);
    }
    // Stable identity order prevents camera ranking alone from invalidating a mesh.
    for(auto& [cell,chunkRows]:requested){
        auto sorted=chunkRows.items();
        std::sort(sorted.begin(),sorted.end(),[](const Json& a,const Json& b){
            return std::make_pair(a.getInt("species"),a.getStr("key"))<std::make_pair(b.getInt("species"),b.getStr("key"));
        });
        chunkRows=Json::array();for(auto& row:sorted)chunkRows.push(std::move(row));
    }
    for(auto it=planetForestFarChunks_.begin();it!=planetForestFarChunks_.end();){
        if(resident.count(it->first)){++it;continue;}
        if(!acquireIdle()){++it;continue;}
        if(it->second.mesh.vb.valid())device_->destroyBuffer(it->second.mesh.vb);
        if(it->second.mesh.ib.valid())device_->destroyBuffer(it->second.mesh.ib);
        it=planetForestFarChunks_.erase(it);
    }
    size_t drawn=0,pending=0,uploads=0;
    constexpr size_t maxUploadsPerFrame=16;
    size_t residentBytes=0;
    for(const auto& [cell,chunk]:planetForestFarChunks_)residentBytes+=chunk.bytes;
    std::vector<int64_t> drawCells,pendingCells;
    for(const auto& candidate:order){
        const auto cell=candidate.cell;if(!resident.count(cell))continue;
        const auto& chunkRows=requested.at(cell);
        auto found=planetForestFarChunks_.find(cell);
        if(found==planetForestFarChunks_.end()||!(found->second.rows==chunkRows)||found->second.scale!=flora){
            if(uploads>=maxUploadsPerFrame||!acquireIdle()){++pending;pendingCells.push_back(cell);continue;}
            // Prefer allocating the replacement before releasing the old mesh.
            if(candidate.bytes>gpuBudget-std::min(gpuBudget,residentBytes)){
                // If temporary double allocation cannot fit, free only this
                // obsolete mesh and use the current-tick fallback until retry.
                if(found!=planetForestFarChunks_.end()){
                    if(found->second.mesh.vb.valid())device_->destroyBuffer(found->second.mesh.vb);
                    if(found->second.mesh.ib.valid())device_->destroyBuffer(found->second.mesh.ib);
                    residentBytes-=found->second.bytes;planetForestFarChunks_.erase(found);
                }
                if(candidate.bytes>gpuBudget-std::min(gpuBudget,residentBytes)){++pending;pendingCells.push_back(cell);continue;}
            }
            render::MeshBuilder builder;
            for(const auto& row:chunkRows.items()){
                const auto p=studioForestVec(row["position"]),up=studioForestVec(row["up"]),east=studioForestVec(row["east"]);
                const auto size=row.getInt("sizePermille");
                if(!p||!up||!east||size<=0||size>1000)continue;
                const auto frame=studioForestFrame(*p,*up,*east);if(!frame)continue;
                const float h=flora*float(size)*.001f*(row.getBool("shrub")?1.5f:6.f);
                const float cover=float(std::clamp<int64_t>(row.getInt("foliagePermille"),0,1000))*.001f;
                const auto u=render::normalize(*up),e=render::normalize(*east),n=render::normalize(render::cross(u,e));
                const float w=h*(.035f+.24f*cover);
                const auto color=cover>0.f?render::rgba(55,105,48):render::rgba(100,72,48);
                // Two crossed, double-sided tapered silhouettes in the local tangent frame.
                for(const auto side:{e,n}){
                    const auto normal=render::normalize(render::cross(side,u));
                    const auto b=uint32_t(builder.vertexCount());
                    builder.addVertex(*p-side*w,normal,color);builder.addVertex(*p+side*w,normal,color);builder.addVertex(*p+u*h,normal,color);
                    builder.addTriangle(b,b+1,b+2);builder.addTriangle(b+2,b+1,b);
                }
            }

            ++uploads;
            if(builder.vertexCount()==0)continue;
            auto next=builder.upload(*device_);
            if(!next.vb.valid()||!next.ib.valid()){
                if(next.vb.valid())device_->destroyBuffer(next.vb);
                if(next.ib.valid())device_->destroyBuffer(next.ib);
                ++pending;pendingCells.push_back(cell);continue;
            }
            auto& chunk=planetForestFarChunks_[cell];
            if(chunk.mesh.vb.valid())device_->destroyBuffer(chunk.mesh.vb);
            if(chunk.mesh.ib.valid())device_->destroyBuffer(chunk.mesh.ib);
            residentBytes-=chunk.bytes;chunk.mesh=next;chunk.rows=chunkRows;chunk.scale=flora;chunk.bytes=candidate.bytes;residentBytes+=chunk.bytes;
            found=planetForestFarChunks_.find(cell);
        }
        if(found!=planetForestFarChunks_.end()&&found->second.mesh.vb.valid()&&found->second.mesh.ib.valid()){
            drawCells.push_back(cell);
            drawn+=chunkRows.size();
        }
    }
    // Do not destroy or upload a chunk after submitting a draw for it.
    for(const auto cell:drawCells)renderer_.drawMesh(planetForestFarChunks_.at(cell).mesh,render::mat4{},{1,1,1,1},render::kDrawNoCastShadow);
    // A stale chunk can contain harvested plants or newly promoted detail.
    // Never draw it while a rebuild is pending. Draw bounded, current-tick
    // tangent proxies using the already resident primitive instead.
    size_t pendingProxies=0;
    constexpr size_t maxPendingProxies=2048;
    for(const auto cell:pendingCells){
        for(const auto& row:requested.at(cell).items()){
            if(pendingProxies>=maxPendingProxies)break;
            const auto p=studioForestVec(row["position"]),up=studioForestVec(row["up"]),east=studioForestVec(row["east"]);
            const auto size=row.getInt("sizePermille");
            if(!p||!up||!east||size<=0||size>1000)continue;
            const auto frame=studioForestFrame(*p,*up,*east);if(!frame)continue;
            const float h=flora*float(size)*.001f*(row.getBool("shrub")?1.5f:6.f);
            const float cover=float(std::clamp<int64_t>(row.getInt("foliagePermille"),0,1000))*.001f;
            const float w=h*(.035f+.24f*cover);
            const render::float4 color=cover>0.f?render::float4{.13f,.31f,.10f,1}:render::float4{.27f,.15f,.08f,1};
            renderer_.drawMesh(cube_,*frame*render::mat4::translate({0,h*.5f,0})*render::mat4::scale({w*2,h,w*2}),color,render::kDrawNoCastShadow);
            ++pendingProxies;
        }
        if(pendingProxies>=maxPendingProxies)break;
    }
    lightingStatus_+=" · far forest proxies "+std::to_string(drawn)+" · resident chunks "+std::to_string(drawCells.size())+"/"+std::to_string(requested.size());
    if(pendingProxies)lightingStatus_+=" · temporary proxies "+std::to_string(pendingProxies);
    lightingStatus_+=" · frustum rejected "+std::to_string(frustumRejected)+" · horizon rejected "+std::to_string(horizonRejected)+" · budget rejected "+std::to_string(budgetRejected)+" · estimated GPU KiB "+std::to_string(residentBytes/1024);
    if(pending)lightingStatus_+=" · pending far chunks "+std::to_string(pending);
}
void StudioScene::drawLivePlanetForestMeshes(const editor::MapDoc& doc,const std::string& projectPath,int width,int height){
    if(!planetForestLiveCurrent(doc,projectPath)||planetForestModelProject_!=projectPath||
       !(planetForestModelCatalog_==doc.data.worldExtra["terraweaveForest"]))return;
    const auto index=doc.data.findBody(planetPreview_->bodyId);
    if(index<0)return;
    const auto& scale=doc.data.bodies[size_t(index)].meta["visual"]["flora"]["scale"];
    const float flora=scale.isNumber()?std::clamp(float(scale.toDouble()),.001f,10.f):.06f;
    const auto now=std::chrono::steady_clock::now();
    const std::string identity=planetLiveProject_+"\n"+planetLivePath_+"\n"+planetPreview_->bodyId+"\n"+
        planetLiveSnapshot_.getStr("epoch")+"\n"+std::to_string(planetLiveRevision_);
    const bool first=planetForestBlendIdentity_!=identity||planetForestBlendTime_==std::chrono::steady_clock::time_point{}||
        now-planetForestBlendTime_>std::chrono::seconds(1);
    if(first){planetForestDetailWeights_.clear();planetForestBlendIdentity_=identity;}
    const float dt=first?0.f:std::clamp(std::chrono::duration<float>(now-planetForestBlendTime_).count(),0.f,.1f);
    planetForestBlendTime_=now;
    const auto visualId=[](const Json& row){return ForestVisualId{row.getInt("species"),row.getStr("key")};};
    const auto standing=[&](const Json& row){
        return row.getStr("body")==planetPreview_->bodyId&&!row.getBool("spent")&&!row.getBool("physical")&&
            !row.getBool("fallen")&&!row.getBool("harvested")&&row.getStr("habit")!="grass";
    };
    const auto proxyRow=[](const Json& row){
        Json proxy=Json::object();
        for(const char* key:{"cell","species","key","position","up","east","sizePermille","foliagePermille"})proxy.set(key,row[key]);
        proxy.set("shrub",Json::fromBool(row.getStr("habit")=="shrub"));return proxy;
    };
    // Shared unit silhouette. Transitional draws do not rebuild the cell mesh
    // each frame and do not retain any previous ECS pose or retirement state.
    if(!planetForestBlendProxy_.vb.valid()&&device_->waitForResourceIdle()){
        render::MeshBuilder mesh;
        for(const auto side:{render::float3{1,0,0},render::float3{0,0,1}}){
            const auto n=render::normalize(render::cross(side,render::float3{0,1,0}));
            const auto b=uint32_t(mesh.vertexCount());const auto c=render::rgba(255,255,255);
            mesh.addVertex(side*(-1.f),n,c);mesh.addVertex(side,n,c);mesh.addVertex({0,1,0},n,c);
            mesh.addTriangle(b,b+1,b+2);mesh.addTriangle(b+2,b+1,b);
        }
        auto next=mesh.upload(*device_);
        if(next.vb.valid()&&next.ib.valid())planetForestBlendProxy_=next;
        else{if(next.vb.valid())device_->destroyBuffer(next.vb);if(next.ib.valid())device_->destroyBuffer(next.ib);}
    }
    const bool canBlend=planetForestBlendProxy_.vb.valid()&&planetForestBlendProxy_.ib.valid();
    Json far=Json::array(),detail=Json::array();
    std::map<ForestVisualId,float> nextWeights;
    std::vector<std::pair<Json,float>> fadingProxies;
    constexpr size_t maxTransitions=256;
    constexpr float blendSeconds=.30f;
    const auto weight=[&](const ForestVisualId& id,float target){
        const auto it=planetForestDetailWeights_.find(id);
        const float previous=it==planetForestDetailWeights_.end()?0.f:it->second;
        if(first||!canBlend||fadingProxies.size()>=maxTransitions)return target;
        return previous+std::clamp(target-previous,-dt/blendSeconds,dt/blendSeconds);
    };
    size_t fallback=0;
    for(const auto& row:planetLiveSnapshot_["forest"]["plants"].items()){
        if(!standing(row)){detail.push(row);continue;}
        const auto id=visualId(row);
        if(!planetForestModels_.count(uint32_t(id.first))){far.push(proxyRow(row));++fallback;continue;}
        const float w=weight(id,1.f);nextWeights.emplace(id,w);
        if(w<.999f){fadingProxies.emplace_back(proxyRow(row),1.f-w);}
        detail.push(row);
    }
    for(const auto& row:planetLiveSnapshot_["forest"]["farPlants"].items()){
        const auto id=visualId(row);
        const float w=planetForestModels_.count(uint32_t(id.first))?weight(id,0.f):0.f;
        if(w<=.001f){far.push(row);continue;}
        nextWeights.emplace(id,w);
        fadingProxies.emplace_back(row,1.f-w);
        Json outgoing=row;
        outgoing.set("body",Json::string(planetPreview_->bodyId));
        outgoing.set("habit",Json::string(row.getBool("shrub")?"shrub":"tree"));
        outgoing.set("dead",Json::fromBool(row.getInt("foliagePermille")==0));
        detail.push(std::move(outgoing));
    }
    planetForestDetailWeights_=std::move(nextWeights);
    drawPlanetForestFarChunks(far,flora,width,height);
    for(const auto& [row,opacity]:fadingProxies){
        if(opacity<=.001f)continue;
        const auto p=studioForestVec(row["position"]),up=studioForestVec(row["up"]),east=studioForestVec(row["east"]);
        const auto size=row.getInt("sizePermille");if(!p||!up||!east||size<=0||size>1000)continue;
        const auto frame=studioForestFrame(*p,*up,*east);if(!frame)continue;
        const float h=flora*float(size)*.001f*(row.getBool("shrub")?1.5f:6.f);
        const float cover=float(std::clamp<int64_t>(row.getInt("foliagePermille"),0,1000))*.001f;
        const float w=h*(.035f+.24f*cover);
        const render::float4 color=cover>0.f?render::float4{.13f,.31f,.10f,opacity}:render::float4{.27f,.15f,.08f,opacity};
        renderer_.drawMesh(planetForestBlendProxy_,*frame*render::mat4::scale({w,h,w}),color,render::kDrawNoCastShadow);
    }
    if(fallback)lightingStatus_+=" · prototype fallback "+std::to_string(fallback);
    if(!fadingProxies.empty())lightingStatus_+=" · LOD blends "+std::to_string(fadingProxies.size());
    static const std::vector<uint8_t> barkOnly{1,0};
    planetForestSeasonMaterials_.clear();
    const auto tick=uint64_t(planetLiveSnapshot_["forest"].getInt("tick",0));
    size_t rendered=0,missing=0;
    for(const auto& row:detail.items()){
        if(row.getStr("body")!=planetPreview_->bodyId||row.getBool("spent"))continue;
        if(row.getBool("physical")){
            const auto& cap=row["capsule"];
            const auto center=studioForestVec(cap["center"]),axis=studioForestVec(cap["axis"]);
            const float radius=float(cap["radius"].toDouble()),half=float(cap["halfHeight"].toDouble());
            const auto species=uint32_t(row.getInt("species"));
            if(!center||!axis||radius<=0||radius>1000||half<=0||half>1000){++missing;continue;}
            const auto right=studioForestVec(cap["right"]);
            const auto frame=studioForestFrame(*center,*axis,right.value_or(std::abs(axis->y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
            if(!frame){++missing;continue;}
            const auto trunk=planetForestOverlays_.arborTrunks.find(species);
            const auto prototype=planetForestModels_.find(species);
            if(trunk!=planetForestOverlays_.arborTrunks.end()&&prototype!=planetForestModels_.end()&&right){
                render::ModelDraw draw;
                draw.model=prototype->second.get();
                draw.materialMask=&barkOnly;
                draw.world=studioFallenArborTransform(*frame,trunk->second,radius,half);
                renderer_.drawModel(draw);++rendered;continue;
            }
            const auto log=planetForestLogModels_.find(species);
            if(log==planetForestLogModels_.end()){++missing;continue;}
            // Explicit capsule recipes have no associated structural branches.
            render::ModelDraw draw;draw.model=log->second.get();
            draw.world=*frame*render::mat4::scale({radius*2,(half+radius)*2,radius*2});
            renderer_.drawModel(draw);++rendered;continue;
        }
        if(row.getBool("fallen")||row.getBool("harvested")||row.getStr("habit")=="grass")continue;
        const auto origin=studioForestVec(row["position"]),up=studioForestVec(row["up"]),east=studioForestVec(row["east"]);
        const auto size=row.getInt("sizePermille",-1);
        if(!origin||!up||!east||size<=0||size>1000){++missing;continue;}
        const auto frame=studioForestFrame(*origin,*up,*east);
        if(!frame){++missing;continue;}
        const auto id=uint32_t(row.getInt("species"));
        const bool shrub=row.getStr("habit")=="shrub";
        const float factor=flora*float(size)*.001f*(shrub?.25f:1.f);
        const auto model=planetForestModels_.find(id);
        if(model!=planetForestModels_.end()){
            render::ModelDraw draw;draw.model=model->second.get();
            draw.world=*frame*render::mat4::scale({factor,factor,factor});
            const auto transition=planetForestDetailWeights_.find(visualId(row));
            if(transition!=planetForestDetailWeights_.end()&&transition->second<.999f){
                draw.tint.w=transition->second;draw.flags|=render::kDrawKeepShadow;
            }
            if(draw.tint.w<=.001f)continue;
            const auto foliage=std::clamp<int64_t>(row.getInt("foliagePermille",0),0,1000);
            int season=10;
            if(row["seasonPermille"].isNumber())season=int(std::clamp<int64_t>((row.getInt("seasonPermille")+1000)*10/2000,0,10));
            const bool evergreen=planetForestEvergreen_.count(id)&&planetForestEvergreen_.at(id);
            // Preview mapping of the actual PHYSIS seasonal signal, not an
            // independent climate or calendar. Quantized materials shared per species.
            const int cover=int((foliage*(evergreen?1000:season*100)+99999)/100000);
            if(row.getBool("dead")||cover==0)draw.materialMask=&barkOnly;
            if((season<10||cover<10)&&model->second->materials.size()>1){
                const auto materialKey=std::make_tuple(id,season,cover);
                auto it=planetForestSeasonMaterials_.find(materialKey);
                if(it==planetForestSeasonMaterials_.end()){
                    auto materials=model->second->materials;
                    const float cold=1.f-float(season)*.1f;
                    auto& leaf=materials[1];
                    if(evergreen){leaf.baseColor.x*=1.f-.10f*cold;leaf.baseColor.y*=1.f-.20f*cold;}
                    else{leaf.baseColor.x=std::min(1.f,leaf.baseColor.x*(1.f+.65f*cold));leaf.baseColor.y*=1.f-.58f*cold;leaf.baseColor.z*=1.f-.65f*cold;}
                    // Same partial-coverage policy as ArborForestAdapter: alpha fade,
                    // not opaque summer shadows. Fully bare trees skip the leaf primitive.
                    if(cover>0&&cover<10){leaf.baseColor.w*=float(cover)*.1f;leaf.alpha=render::AlphaMode::Blend;leaf.transparent=true;leaf.castShadow=false;}
                    it=planetForestSeasonMaterials_.emplace(materialKey,std::move(materials)).first;
                }
                draw.materials=&it->second;
            }
            // WindRig uses model-local displacement. The snapshot already
            // projects PHYSIS into this tree's tangent frame (metres/sec).
            render::wind::WindDrawParams wind;
            if(!row.getBool("dead")&&row["windLocal"].isArray()&&row["windLocal"].size()==2){
                const auto& v=row["windLocal"].items();
                wind.windX=float(v[0].toDouble())*.035f;
                wind.windZ=float(v[1].toDouble())*.035f;
                const auto key=std::stoull(row.getStr("key","0"));
                constexpr double tau=6.283185307179586;
                wind.phase=float(std::fmod(tau*double(tick%7680)/120.+double(key%1000)*.001*tau,tau*64.));
                wind.gust=0.f; // actual local PHYSIS gust already included
            }
            draw.useVertexParameters=true;draw.vertexParameters=render::wind::windRigVertexParameters(wind);
            draw.maxVertexOffsetMetres=std::max(.01f,render::wind::windRigMaxOffsetMetres(wind)*factor);
            renderer_.drawModel(draw);++rendered;
        }else ++missing;
        if(const auto tier=studioForestRootTier(row);tier){
            const auto root=planetForestRootModels_.find({id,shrub,tier});
            if(root!=planetForestRootModels_.end()){
                render::ModelDraw draw;draw.model=root->second.get();
                // Root support dimensions are actual metres, not flora visual scale.
                draw.world=*frame;renderer_.drawModel(draw);
            }else ++missing;
        }
    }
    lightingStatus_+=" · live forest meshes "+std::to_string(rendered);
    if(missing)lightingStatus_+=" · missing live models "+std::to_string(missing);
}
StudioScene::~StudioScene() {
    attachmentPreview_.reset();
    if (device_) {
        const bool idle=device_->waitForResourceIdle();
        if(textureGraph_){
            if(idle||!device_->gpuHealthy())textureGraph_->shutdownAfterIdle(*device_);
            else std::fprintf(stderr,"Studio Texture Graph: idle barrier failed; GPU pools left to device teardown\n");
            textureGraph_.reset();
        }
        if(graphPresentation_){if(idle)graphPresentation_->shutdown(*device_);graphPresentation_.reset();}
        try {
            if(!cavernsPreview_.close(*device_))cavernsPreview_.quarantineOnFailedTeardown();
        }catch(...) {cavernsPreview_.quarantineOnFailedTeardown();}
        discardReplayBackup();
        spectraSources_.shutdown();
        spectraPreview_->stop(*device_); // before gfx Device and renderer are destroyed
        clearLivePlanetTerrain();
        clearPlanetForestModels();
        clearDocumentModels();
        if(buildingPreview_){releasePreviewModel(*buildingPreview_);buildingPreview_.reset();}
        lightingRuntime_.shutdown(*device_,renderer_);
        if(lightingModel_) {
            renderer_.invalidateModelCachesFor(lightingModel_.get());
            releaseModel(*device_,*lightingModel_);lightingModel_.reset();
        }
        graphParticles_.reset();
        if(graphPreviewPost_)graphPreviewPost_->shutdown();
        post_.shutdown();
        (void)facialGpu_.clear(*device_,renderer_);
        if(studioCyclorama_.vb.valid())device_->destroyBuffer(studioCyclorama_.vb);
        if(studioCyclorama_.ib.valid())device_->destroyBuffer(studioCyclorama_.ib);
        if (cube_.vb.valid()) device_->destroyBuffer(cube_.vb);
        if (cube_.ib.valid()) device_->destroyBuffer(cube_.ib);
        if(animationModel_)releasePreviewModel(*animationModel_);
        if (materialSphere_) {
            releasePreviewModel(*materialSphere_);
            materialSphere_.reset();
        }
        for (auto* primitive:{&builtinCube_,&builtinSphere_})
            if (*primitive) { releasePreviewModel(**primitive); primitive->reset(); }
        terrainRetirement_.retireBuffer(*device_,planetTerrain_.vb);
        terrainRetirement_.retireBuffer(*device_,planetTerrain_.ib);
        terrainRetirement_.retireBuffer(*device_,terrain_.vb);
        terrainRetirement_.retireBuffer(*device_,terrain_.ib);
        terrainRetirement_.retireBuffer(*device_,planetCutTerrain_.vb);
        terrainRetirement_.retireBuffer(*device_,planetCutTerrain_.ib);
        terrainRetirement_.retireBuffer(*device_,globeMesh_.vb);
        terrainRetirement_.retireBuffer(*device_,globeMesh_.ib);
        (void)terrainRetirement_.completeForShutdown(*device_);
        sparseTerrainPreview_.release([&](auto& model){sparseTerrainRetirement_.retireModel(*device_,model);});
        (void)sparseTerrainRetirement_.completeForShutdown(*device_);
        if (thumbColor_.valid()) device_->destroyTexture(thumbColor_);
        if (thumbDepth_.valid()) device_->destroyTexture(thumbDepth_);
        nereid_.shutdown(*device_);
        antialiasing_.shutdown();
        renderer_.shutdown();
        if(deviceLifetime_)deviceLifetime_->retire();
    }
}
bool StudioScene::sculptPick(float x,float y,float width,float height,
        const vivid::HumanFaceShape& selection,vivid::HumanFaceHit& hit,std::string* error) const noexcept {
    try {
    float3 origin{},direction{};
    // The facial preview renders with camera_ (no tween/ortho view override).
    if(!facialMode_ || !cameraInitialized_ ||
       !camera_.screenRay(x,y,width,height,origin,direction))return false;
    return facialGpu_.sculptRaycast(origin,direction,selection,hit);
    } catch(const std::exception& ex) {
        if(error)*error=ex.what();
        return false;
    } catch(...) {
        if(error)*error="Sculpt pick failed";
        return false;
    }
}
std::string StudioScene::pick(float x, float y, float width, float height,
                                   const editor::MapDoc& map, bool fpsProject) const {
    float3 origin{}, direction{};
    // Facial preview is an imported Model, not an object in the active map.
    // Never route its clicks to unrelated MapDoc entities underneath.
    if (materialMode_ || facialMode_ || buildingPreview_ || !cameraInitialized_ ||
        !viewCamera().screenRay(x,y,width,height,origin,direction)) return {};
    if(map.data.planet())return planetMode_&&planetPreview_&&planetMapPath_==map.path&&planetRevision_==map.revision?
        planetPreview_->pick(origin,direction,hidden_,locked_,modelInstances_,visiblePlanetTerrain()):std::string{};
    // Pick the oriented cube mesh that Renderer3D actually draws, not its
    // expanded axis-aligned bounding box. Empty rotated corners are unpickable.
    auto proxies=studioDocumentProxies(map,fpsProject,terrainLevelHeight_);
    proxies.erase(std::remove_if(proxies.begin(),proxies.end(),[&](const auto& proxy){
        const int index=studioMapEntityIndex(proxy.id,map);
        return hidden_.count(proxy.id) || locked_.count(proxy.id) ||
            (index>=0 && map.data.entities[size_t(index)].extra["components"].has("NereidWater"));
    }),proxies.end());
    // These are the exact model instances submitted by the previous frame.
    if(hidden_.empty() && locked_.empty())return studioPickSceneSurfaces(modelInstances_,proxies,origin,direction);
    std::vector<StudioModelPickInstance> instances;
    for(const auto& instance:modelInstances_)if(!hidden_.count(instance.id)&&!locked_.count(instance.id))instances.push_back(instance);
    return studioPickSceneSurfaces(instances,proxies,origin,direction);
}
std::vector<std::string> StudioScene::idsInRect(float x0,float y0,float x1,float y1,float width,float height,
                                                     const editor::MapDoc& map,bool fpsProject) const {
    std::vector<std::string> out;
    if(materialMode_||!cameraInitialized_||width<2||height<2)return out;
    if(map.data.planet())return out; // planetary marquee is not implemented
    const float lx=std::min(x0,x1),hx=std::max(x0,x1),ly=std::min(y0,y1),hy=std::max(y0,y1);
    const auto view=viewCamera();
    for(const auto& proxy:studioDocumentProxies(map,fpsProject,terrainLevelHeight_)) {
        if(hidden_.count(proxy.id)||locked_.count(proxy.id))continue;
        const int index=studioMapEntityIndex(proxy.id,map);
        if(index>=0 && map.data.entities[size_t(index)].extra["components"].has("NereidWater"))continue;
        float2 p;if(!view.worldToScreen(proxy.center,width,height,p))continue;
        if(p.x>=lx&&p.x<=hx&&p.y>=ly&&p.y<=hy)out.push_back(proxy.id);
    }
    return out;
}
bool StudioScene::caveScreenRay(float x,float y,float width,float height,float3& origin,float3& direction)const {
    return cameraInitialized_&&width>1&&height>1&&viewCamera().screenRay(x,y,width,height,origin,direction);
}
std::optional<Json> StudioScene::forestPick(float x,float y,float w,float h,const std::string& project,const std::string& path,uint64_t revision)const{
    if(!planetMode_||!planetPreview_||!cameraInitialized_||!gizmosVisible_||planetModelProject_!=project||
       planetMapPath_!=path||planetRevision_!=revision||w<2||h<2||x<0||y<0||x>w||y>h)return {};
    if(planetLiveSnapshot_["forest"].getBool("active")&&planetLiveProject_==project&&
       planetLivePath_==path&&planetLiveRevision_==revision)return {};
    const auto view=viewCamera();
    const PlanetForestPick* chosen=nullptr;float best=12.f,bestDepth=std::numeric_limits<float>::infinity();
    for(const auto& pick:planetForestOverlays_.picks){
        if(!pick.markerOwner.empty()&&(hidden_.count(pick.markerOwner)||locked_.count(pick.markerOwner)))continue;
        float2 a,b;if(!view.worldToScreen(pick.ground,w,h,a)||!view.worldToScreen(pick.tip,w,h,b))continue;
        const float dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
        const float t=len>1e-6f?std::clamp(((x-a.x)*dx+(y-a.y)*dy)/len,0.f,1.f):0.f;
        const float sx=a.x+t*dx,sy=a.y+t*dy;
        const float error=std::hypot(x-sx,y-sy);
        if(error>best)continue;
        float3 o,d;if(!view.screenRay(sx,sy,w,h,o,d))continue;
        const auto p=pick.ground+(pick.tip-pick.ground)*t;
        const float depth=dot(p-o,d);
        if(depth<=0||planetPreview_->terrainHit(o,d,visiblePlanetTerrain())+.05f<depth)continue;
        if(error==best&&depth>=bestDepth)continue;
        chosen=&pick;best=error;bestDepth=depth;
    }
    if(!chosen)return {};
    Json result=Json::object();result.set("species",Json::number(int64_t(chosen->species)));
    result.set("key",Json::number(int64_t(chosen->key)));result.set("direct",Json::fromBool(chosen->direct));
    result.set("body",Json::string(planetPreview_->bodyId));return result;
}
std::optional<Json> StudioScene::forestGroundPlacement(float x,float y,float w,float h,const std::string& path,uint64_t revision)const{
    if(planetLiveSnapshot_["forest"].getBool("active")&&planetLivePath_==path&&planetLiveRevision_==revision)return {};
    if(!planetMode_||!planetPreview_||!cameraInitialized_||planetMapPath_!=path||planetRevision_!=revision||
       w<2||h<2||x<0||y<0||x>w||y>h)return {};
    float3 o,d;if(!viewCamera().screenRay(x,y,w,h,o,d))return {};
    return planetPreview_->groundPlacement(o,d,visiblePlanetTerrain());
}
bool StudioScene::groundPoint(float x,float y,float width,float height,float3& out) const {
    float3 o,d;
    if(planetMode_||!cameraInitialized_||!viewCamera().screenRay(x,y,width,height,o,d)||std::fabs(d.y)<1e-5f)return false;
    const float t=-o.y/d.y; if(!(t>0)||!std::isfinite(t))return false;
    out=o+d*t; return true;
}
void StudioScene::frameAll(const editor::MapDoc& map,bool fpsProject) {
    if(map.data.planet()){if(planetPreview_)startTween({},std::max(2.f,planetPreview_->extent*3.f),camera_.yaw,camera_.pitch);return;}
    const auto proxies=studioDocumentProxies(map,fpsProject,terrainLevelHeight_);
    float3 lo{1e30f,1e30f,1e30f},hi{-1e30f,-1e30f,-1e30f};
    for(const auto& p:proxies){lo={std::min(lo.x,p.boundsMin.x),std::min(lo.y,p.boundsMin.y),std::min(lo.z,p.boundsMin.z)};
                               hi={std::max(hi.x,p.boundsMax.x),std::max(hi.y,p.boundsMax.y),std::max(hi.z,p.boundsMax.z)};}
    if(proxies.empty()){lo={0,0,0};hi={float(map.data.w),1.0f,float(map.data.h)};}
    const float3 size=hi-lo;
    startTween((lo+hi)*.5f,std::clamp(std::max({size.x,size.y,size.z,1.0f})*1.2f,2.0f,100000.0f),camera_.yaw,camera_.pitch);
}
render::Camera StudioScene::viewCamera() const {
    render::Camera view=camera_;
    view.zNear=std::clamp(camera_.distance*0.002f,0.05f,0.5f);
    view.zFar=std::max(1000.0f,camera_.distance*6.0f);
    if(view.ortho) {
        view.orthoHalfHeightOverride=std::max(0.01f,camera_.distance*std::tan(camera_.fovY*0.5f));
        view.distance=std::min(camera_.distance,400.0f);
    }
    return view;
}
void StudioScene::startTween(render::float3 target,float distance,float yaw,float pitch) {
    tween_.active=true;
    tween_.fromTarget=camera_.target;tween_.toTarget=target;
    tween_.fromDistance=camera_.distance;tween_.toDistance=distance;
    tween_.fromYaw=camera_.yaw;
    // Shortest rotation; the stored yaw may have accumulated many turns.
    tween_.toYaw=camera_.yaw+std::remainder(yaw-camera_.yaw,6.28318530718f);
    tween_.fromPitch=camera_.pitch;tween_.toPitch=pitch;
    tween_.start=std::chrono::steady_clock::now();
}
void StudioScene::stepTween() {
    if(!tween_.active)return;
    const float seconds=std::chrono::duration<float>(std::chrono::steady_clock::now()-tween_.start).count();
    const float t=std::clamp(seconds/0.24f,0.0f,1.0f);
    const float k=t*t*(3.0f-2.0f*t);
    camera_.target=tween_.fromTarget+(tween_.toTarget-tween_.fromTarget)*k;
    camera_.distance=tween_.fromDistance+(tween_.toDistance-tween_.fromDistance)*k;
    camera_.yaw=tween_.fromYaw+(tween_.toYaw-tween_.fromYaw)*k;
    camera_.pitch=tween_.fromPitch+(tween_.toPitch-tween_.fromPitch)*k;
    if(t>=1.0f)tween_.active=false;
}
void StudioScene::setOrientation(float yaw,float pitch) {
    if(!std::isfinite(yaw)||!std::isfinite(pitch))return;
    startTween(camera_.target,camera_.distance,yaw,std::clamp(pitch,-1.5707963f,1.5707963f));
}
void StudioScene::setOrthographic(bool ortho) { camera_.ortho=ortho; autoIso_=false; }
void StudioScene::captureSelectedBox(const mat4& world,float3 lo,float3 hi) {
    for(int i=0;i<8;++i)
        selectedBox_[i]=world.transformPoint({i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z});
    selectedBoxValid_=true;
}
void StudioScene::drawSelectionBox(const Camera& view,int width,int height) {
    if(!selectedBoxValid_)return;
    const float W=float(width),H=float(height);
    float2 p[8];
    for(int i=0;i<8;++i)if(!view.worldToScreen(selectedBox_[i],W,H,p[i]))return;
    Overlay2D o{renderer_,view,W,H,view.ortho?0.5f:view.zNear*1.6f};
    const float w=std::max(1.5f,std::min(W,H)*0.0025f);
    constexpr int edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
    for(const auto& e:edges)o.segment(p[e[0]].x,p[e[0]].y,p[e[1]].x,p[e[1]].y,w,rgba(255,150,40,230));
}
bool StudioScene::projectPoint(float3 world,float width,float height,float2& out) const {
    return viewCamera().worldToScreen(world,width,height,out);
}
Json StudioScene::textureResources() const {
    static const char* formats[]={"RGBA8","R8","Depth","RGBA32F","ETC2_RGB8","ETC2_RGBA8","BC1","BC3","BC7","ASTC_4x4","ASTC_6x6",
        "RGBA8_sRGB","RGBA16F","BC4","BC5","EAC_R11","RG8","R16F","RG16F","R32F","RG32F","BC6H_UFloat","BC6H_SFloat",
        "BC1_sRGB","BC3_sRGB","BC7_sRGB","ETC2_RGB8_sRGB","ETC2_RGBA8_sRGB","ASTC_4x4_sRGB","ASTC_6x6_sRGB"};
    Json list=Json::array();
    size_t total=0;
    for(const auto& t:device_->textureResources()){
        Json o=Json::object();
        const size_t f=size_t(t.format);
        o.set("id",Json::number(int64_t(t.handle.id)));
        o.set("format",Json::string(f<sizeof(formats)/sizeof(formats[0])?formats[f]:"?"));
        o.set("width",Json::number(int64_t(t.width)));o.set("height",Json::number(int64_t(t.height)));
        o.set("depth",Json::number(int64_t(t.depth)));o.set("layers",Json::number(int64_t(t.arrayLayers)));
        o.set("mips",Json::number(int64_t(t.mipLevels)));o.set("samples",Json::number(int64_t(t.sampleCount)));
        o.set("renderTarget",Json::fromBool(t.renderTarget));o.set("bytes",Json::number(int64_t(t.estimatedBytes)));
        total+=t.estimatedBytes;
        list.push(std::move(o));
    }
    Json out=Json::object();
    out.set("textures",list);out.set("totalBytes",Json::number(int64_t(total)));
    return out;
}
void StudioScene::drawBrushCursor(const Camera& view,int width,int height) {
    const float W=float(width),H=float(height);
    Overlay2D o{renderer_,view,W,H,view.ortho?0.5f:view.zNear*1.6f};
    const float px=std::max(1.0f,std::min(W,H)/800.0f);   // points → framebuffer pixels (approx.)
    for(const auto& s:overlayShapes_){
        const uint32_t color=rgba(s.r,s.g,s.bl,235);
        float2 a,b;
        if(!view.worldToScreen(s.a,W,H,a))continue;
        const float t=s.thickness*px;
        if(s.kind==4){
            // Bitmap text (6x11 glyphs), one horizontal run per lit row segment, with a dark outline pass.
            const float cell=std::max(1.f,px*1.4f);
            for(int pass=0;pass<2;++pass){
                const uint32_t col=pass==0?rgba(0,0,0,170):color;const float off=pass==0?cell*0.6f:0;
                float cx=a.x+6*cell;
                for(unsigned char ch:s.text.substr(0,64)){
                    if(ch<32||ch>126)ch='?';
                    const unsigned char* g=kStudioFont6x11[ch-32];
                    for(int row=0;row<11;++row)for(int col0=0;col0<6;){
                        if(!((g[row]>>col0)&1)){++col0;continue;}
                        int col1=col0;while(col1<6&&((g[row]>>col1)&1))++col1;
                        const float y=a.y-6*cell+row*cell+cell*0.5f+off;
                        o.segment(cx+col0*cell+off,y,cx+col1*cell+off,y,cell,col);
                        col0=col1;
                    }
                    cx+=6*cell;
                }
            }
            continue;
        }
        if(s.kind==1&&view.worldToScreen(s.b,W,H,b))o.segment(a.x,a.y,b.x,b.y,t,color);
        else if(s.kind==2||s.kind==3){
            const float r=(s.kind==2?s.radius:std::max(1.f,s.radius))*px;constexpr int N=32;
            for(int i=0;i<N;++i){
                const float a0=float(i)/N*6.2831853f,a1=float(i+1)/N*6.2831853f;
                o.segment(a.x+std::cos(a0)*r,a.y+std::sin(a0)*r,a.x+std::cos(a1)*r,a.y+std::sin(a1)*r,s.kind==3?r:t,color);
            }
        }
    }
    if(!brushVisible_||brushRadius_<=0)return;
    const float w=std::max(1.5f,std::min(W,H)*0.002f);
    constexpr int N=48;
    float2 prev{};bool prevOk=false;
    const uint32_t ink=brushVolume_&&brushFill_?rgba(255,175,100,220):rgba(120,200,255,220);
    for(int plane=0;plane<(brushVolume_?3:1);++plane){
        prevOk=false;
        for(int i=0;i<=N;++i){
            const float a=float(i)/N*6.2831853f;
            const float u=std::cos(a)*brushRadius_,v=std::sin(a)*brushRadius_;
            const float3 offset=plane==0?float3{u,brushVolume_?0.f:.05f,v}:
                                plane==1?float3{u,v,0}:float3{0,u,v};
            float2 s;const bool ok=view.worldToScreen(brushCenter_+offset,W,H,s);
            if(ok&&prevOk)o.segment(prev.x,prev.y,s.x,s.y,w,ink);
            prev=s;prevOk=ok;
        }
    }
    float2 c;
    if(brushVisible_&&view.worldToScreen(brushCenter_,W,H,c)){o.segment(c.x-4,c.y,c.x+4,c.y,w,rgba(120,200,255,220));o.segment(c.x,c.y-4,c.x,c.y+4,w,rgba(120,200,255,220));}
}
void StudioScene::setSnap(bool enabled,float move,float rotate,float scale) {
    snapEnabled_=enabled;
    if(std::isfinite(move)&&move>0)snapMove_=std::clamp(move,0.001f,1000.0f);
    if(std::isfinite(rotate)&&rotate>0)snapRotate_=std::clamp(rotate,0.1f,180.0f);
    if(std::isfinite(scale)&&scale>0)snapScale_=std::clamp(scale,0.001f,10.0f);
}
void StudioScene::handleAxes(float3 out[3]) const {
    // Scale always uses the object's own axes (a world-axis scale of a rotated
    // object would shear); Move/Rotate follow the Global/Local toggle.
    const bool local=(planetMode_||tool_==TransformTool::Scale||localSpace_)&&selectedAxesValid_;
    for(int a=0;a<3;++a)out[a]=local?selectedAxes_[a]:float3{a==0?1.f:0.f,a==1?1.f:0.f,a==2?1.f:0.f};
}
int StudioScene::transformGizmoHit(float x,float y,float width,float height) const {
    if((planetMode_&&tool_==TransformTool::Move)||materialMode_||!gizmosVisible_||tool_==TransformTool::View||!selectedPivotValid_||width<4||height<4)return -1;
    if(selectedIsLight_&&tool_!=TransformTool::Move)return -1;
    const auto view=viewCamera();
    float3 axes[3];handleAxes(axes);
    const auto h=axisHandles(view,selectedPivot_,axes,width,height);
    if(!h.valid)return -1;
    int best=-1;float bestD=h.pick;
    // Plane squares (all tools): Move = drag in the plane, Scale = both plane axes,
    // Rotate = turn about the plane's normal (same as that axis's ring).
    for(int n=0;n<3;++n){
        if(planetMode_&&tool_==TransformTool::Rotate&&n!=1)continue;
        float2 q[4];
        if(planeQuad(view,selectedPivot_,axes,n,h.worldLength,width,height,q)&&insideQuad(q,x,y))
            return tool_==TransformTool::Rotate?n:4+n;
    }
    if(tool_==TransformTool::Rotate) {
        for(int a=0;a<3;++a){
            if(planetMode_&&a!=1)continue;
            float3 u,v;ringBasis(axes[a],u,v);float2 prev{};bool hasPrev=false;
            for(int i=0;i<=kRingSegments;++i){
                const float t=6.2831853f*float(i)/float(kRingSegments);
                float2 q;if(!view.worldToScreen(selectedPivot_+(u*std::cos(t)+v*std::sin(t))*h.worldLength,width,height,q)){hasPrev=false;continue;}
                if(hasPrev){const float d=segmentDistance(x,y,prev.x,prev.y,q.x,q.y);if(d<bestD){bestD=d;best=a;}}
                prev=q;hasPrev=true;
            }
        }
        return best;
    }
    for(int a=0;a<3;++a){const float d=segmentDistance(x,y,h.px,h.py,h.tip[a][0],h.tip[a][1]);if(d<bestD){bestD=d;best=a;}}
    if(tool_==TransformTool::Scale && std::hypot(x-h.px,y-h.py)<h.pick*1.4f)best=3; // centre: uniform
    return best;
}
bool StudioScene::beginTransformDrag(float x,float y,float width,float height) {
    if(planetMode_&&tool_==TransformTool::Move){
        if(!gizmosVisible_||!planetPreview_||tool_!=TransformTool::Move||selectedId_.empty()||selection_.size()>1)return false;
        float3 o,d;if(!viewCamera().screenRay(x,y,width,height,o,d))return false;
        if(planetPreview_->pick(o,d,hidden_,locked_,modelInstances_,visiblePlanetTerrain())!=selectedId_)return false;
        const auto at=planetPreview_->placements.find(selectedId_);
        if(at==planetPreview_->placements.end())return false;
        planetTransformRevision_=planetRevision_;
        planetDragId_=selectedId_;planetDragOriginal_=at->second;planetDragPlacement_=Json{};
        tween_.active=false;return true;
    }
    const int axis=transformGizmoHit(x,y,width,height);
    if(axis<0)return false;
    const auto view=viewCamera();
    float3 axes[3];handleAxes(axes);
    const auto h=axisHandles(view,selectedPivot_,axes,width,height);
    TransformDrag d;d.axis=axis;d.tool=tool_;d.id=selectedId_;d.pivot=selectedPivot_;
    for(int a=0;a<3;++a)d.axes[a]=axes[a];
    d.dir=axis<3?axes[axis]:float3{1,1,1};d.lengthPx=h.lengthPx;d.worldLength=h.worldLength;
    d.startX=x;d.value=tool_==TransformTool::Scale?1.0f:0.0f;
    if(tool_==TransformTool::Rotate) {
        d.startAngle=d.lastAngle=std::atan2(y-h.py,x-h.px);
        d.facing=dot(d.dir,view.eye()-d.pivot)>0;
        d.screenPivot={h.px,h.py};
        // Direction on the ring plane under the press: the angle readout starts there.
        float3 ro,rd,hit;float3 bu,bv;ringBasis(d.dir,bu,bv);d.startVec=bu;
        if(view.screenRay(x,y,width,height,ro,rd)&&rayPlane(ro,rd,d.pivot,d.dir,hit)){
            const float3 v=hit-d.pivot;const float l=std::sqrt(dot(v,v));if(l>1e-5f)d.startVec=v*(1.0f/l);
        }
    } else if(axis>=4) {
        d.planeNormal=axes[axis-4];
        float3 o,dir;if(!view.screenRay(x,y,width,height,o,dir))return false;
        if(!rayPlane(o,dir,d.pivot,d.planeNormal,d.startHit))return false;
    } else if(axis<3) {
        float3 o,dir;if(!view.screenRay(x,y,width,height,o,dir))return false;
        if(!axisParameter(d.pivot,d.dir,o,dir,d.startT))return false;
    }
    tween_.active=false;
    drag_=d;
    if(planetMode_)planetTransformRevision_=planetRevision_;
    return true;
}
void StudioScene::updateTransformDrag(float x,float y,float width,float height,bool invertSnap) {
    if(!planetDragId_.empty()){
        float3 o,d;planetDragPlacement_=Json{};
        if(planetPreview_&&viewCamera().screenRay(x,y,width,height,o,d))
            if(auto at=planetPreview_->groundPlacement(o,d,visiblePlanetTerrain()))planetDragPlacement_=std::move(*at);
        return;
    }
    if(drag_.axis<0)return;
    const bool snap=snapEnabled_!=invertSnap;
    drag_.snapping=snap;
    if(drag_.tool==TransformTool::Rotate) {
        const float angle=std::atan2(y-drag_.screenPivot.y,x-drag_.screenPivot.x);
        float step=angle-drag_.lastAngle;
        if(step>3.14159265f)step-=6.2831853f; else if(step<-3.14159265f)step+=6.2831853f;
        drag_.lastAngle=angle;drag_.accumulated+=step;
        // Screen y points down: counter-clockwise on screen is a negative angle.
        float degrees=drag_.accumulated*57.2957795f*(drag_.facing?-1.0f:1.0f);
        if(snap)degrees=std::round(degrees/snapRotate_)*snapRotate_;
        drag_.value=std::clamp(degrees,-36000.0f,36000.0f);
        return;
    }
    if(drag_.tool==TransformTool::Scale && drag_.axis==3) {
        float f=1.0f+(x-drag_.startX)/std::max(1.0f,drag_.lengthPx);
        if(snap)f=1.0f+std::round((f-1.0f)/snapScale_)*snapScale_;
        drag_.value=std::clamp(f,0.01f,100.0f);
        return;
    }
    float3 o,d;if(!viewCamera().screenRay(x,y,width,height,o,d))return;
    if(drag_.axis>=4 && drag_.tool==TransformTool::Scale) {
        float3 hit;if(!rayPlane(o,d,drag_.pivot,drag_.planeNormal,hit))return;
        // Both in-plane axes grow together along the square's diagonal.
        const float3 diag=normalize(drag_.axes[(drag_.axis-4+1)%3]+drag_.axes[(drag_.axis-4+2)%3]);
        float f=1.0f+dot(hit-drag_.startHit,diag)/std::max(1e-4f,drag_.worldLength);
        if(snap)f=1.0f+std::round((f-1.0f)/snapScale_)*snapScale_;
        drag_.value=std::clamp(f,0.01f,100.0f);
        return;
    }
    if(drag_.axis>=4) {
        float3 hit;if(!rayPlane(o,d,drag_.pivot,drag_.planeNormal,hit))return;
        float3 delta=hit-drag_.startHit;
        // Snap along the two plane axes (world or local).
        if(snap) for(int k=1;k<=2;++k){const float3 a=drag_.axes[(drag_.axis-4+k)%3];const float c=dot(delta,a);
            delta=delta+a*(std::round(c/snapMove_)*snapMove_-c);}
        const float l=std::sqrt(dot(delta,delta));
        drag_.moveOffset=l>100000.0f?delta*(100000.0f/l):delta;
        drag_.value=l;
        return;
    }
    if(drag_.tool==TransformTool::Move && pivotSnap_) {
        // V held: jump to the nearest other object's pivot within 28 px of the cursor.
        const auto view=viewCamera();float best=28.0f*28.0f;const float3* target=nullptr;
        for(const auto& [id,p]:snapPivots_) {
            if(id==drag_.id)continue;
            float2 s;if(!view.worldToScreen(p,width,height,s))continue;
            const float dd=(s.x-x)*(s.x-x)+(s.y-y)*(s.y-y);
            if(dd<best){best=dd;target=&p;}
        }
        if(target) {
            drag_.moveOffset=*target-drag_.pivot;
            drag_.value=std::sqrt(dot(drag_.moveOffset,drag_.moveOffset));
            return;
        }
    }
    float t=0;if(!axisParameter(drag_.pivot,drag_.dir,o,d,t))return;
    float delta=std::clamp(t-drag_.startT,-100000.0f,100000.0f);
    if(drag_.tool==TransformTool::Move) {
        if(snap)delta=std::round(delta/snapMove_)*snapMove_;
        drag_.value=delta;drag_.moveOffset=drag_.dir*delta;
    } else {
        float f=1.0f+delta/std::max(1e-4f,drag_.worldLength);
        if(snap)f=1.0f+std::round((f-1.0f)/snapScale_)*snapScale_;
        drag_.value=std::clamp(f,0.01f,100.0f);
    }
}
render::mat4 StudioScene::dragPreview() const {
    if(drag_.axis<0)return mat4{};
    const float3 p=drag_.pivot;
    if(drag_.tool==TransformTool::Move)return mat4::translate(drag_.moveOffset);
    if(drag_.tool==TransformTool::Rotate)
        return mat4::translate(p)*mat4::rotate(quat::axisAngle(drag_.dir,drag_.value*0.017453292f))*mat4::translate(p*-1.0f);
    // Scale about the pivot along the object's own axes: B * diag(f) * B^T.
    mat4 basis,basisT;
    for(int c=0;c<3;++c){const float3 a=drag_.axes[c];
        basis.at(0,c)=a.x;basis.at(1,c)=a.y;basis.at(2,c)=a.z;basisT.at(c,0)=a.x;basisT.at(c,1)=a.y;basisT.at(c,2)=a.z;}
    const float f=drag_.value;
    auto on=[&](int a){return drag_.axis==3||drag_.axis==a||(drag_.axis>=4&&drag_.axis-4!=a);};
    const float3 k{on(0)?f:1.f,on(1)?f:1.f,on(2)?f:1.f};
    return mat4::translate(p)*basis*mat4::scale(k)*basisT*mat4::translate(p*-1.0f);
}
std::string StudioScene::transformCaption() const {
    if(!planetDragId_.empty())return planetDragPlacement_.isObject()?"Surface move · face "+std::to_string(planetDragPlacement_.getInt("face")):"Surface move · no hit";
    if(drag_.axis<0)return {};
    static const char* names[7]={"X","Y","Z","XYZ","YZ","XZ","XY"};
    char text[96];
    const char* space=(drag_.tool==TransformTool::Scale||localSpace_)?" (local)":"";
    if(drag_.tool==TransformTool::Move&&drag_.axis>=4)std::snprintf(text,sizeof text,"Move %s (%+.3g, %+.3g, %+.3g)%s",names[drag_.axis],
        double(drag_.moveOffset.x),double(drag_.moveOffset.y),double(drag_.moveOffset.z),space);
    else if(drag_.tool==TransformTool::Move)std::snprintf(text,sizeof text,"Move %s %+.3g%s",names[drag_.axis],double(drag_.value),space);
    else if(drag_.tool==TransformTool::Rotate)std::snprintf(text,sizeof text,"Rotate %s %+.4g°%s",names[drag_.axis],double(drag_.value),space);
    else std::snprintf(text,sizeof text,"Scale %s ×%.3g",names[drag_.axis],double(drag_.value));
    return text;
}
std::optional<StudioScene::TransformCommit> StudioScene::endTransformDrag() {
    if(!planetDragId_.empty()){
        TransformCommit c;c.id=planetDragId_;c.tool=TransformTool::Move;
        c.surfacePlacement=planetDragPlacement_;c.expectedRevision=planetTransformRevision_;
        const bool changed=c.surfacePlacement.isObject()&&c.surfacePlacement!=planetDragOriginal_;
        clearTransformPreview();if(changed)return c;return std::nullopt;
    }
    if(drag_.axis<0)return std::nullopt;
    TransformCommit c;c.id=drag_.id;c.tool=drag_.tool;c.axis=drag_.axis;
    if(planetMode_)c.expectedRevision=planetTransformRevision_;
    if(drag_.tool==TransformTool::Move)c.move=drag_.moveOffset;
    else if(drag_.tool==TransformTool::Rotate)c.degrees=drag_.value;
    else c.factor=drag_.value;
    // The host commits synchronously, then calls clearTransformPreview().
    drag_.axis=-1;
    return c;
}
void StudioScene::drawTransformGizmo(const render::Camera& view,int width,int height) {
    if(!selectedPivotValid_||!gizmosVisible_||tool_==TransformTool::View)return;
    if(selectedIsLight_&&tool_!=TransformTool::Move)return;
    const float W=float(width),H=float(height);
    const bool dragging=drag_.id==selectedId_ && !drag_.id.empty();
    const float3 pivot=selectedPivot_+(dragging&&drag_.tool==TransformTool::Move?drag_.moveOffset:float3{});
    float3 axes[3];handleAxes(axes);
    const auto h=axisHandles(view,pivot,axes,W,H);
    if(!h.valid)return;
    Overlay2D o{renderer_,view,W,H,view.ortho?0.5f:view.zNear*1.5f};
    const uint32_t colors[3]={rgba(232,82,72),rgba(120,205,85),rgba(70,135,240)};
    const uint32_t active=rgba(255,214,64);
    const float u=std::min(W,H)*0.16f/110.0f;
    auto drawPlanes=[&](){
        for(int n=0;n<3;++n){
            if(planetMode_&&tool_==TransformTool::Rotate&&n!=1)continue;
            float2 q[4];if(!planeQuad(view,pivot,axes,n,h.worldLength,W,H,q))continue;
            const bool hot=dragging&&(drag_.axis==4+n||(tool_==TransformTool::Rotate&&drag_.axis==n));
            const uint32_t base=hot?active:colors[n];
            const uint32_t fill=(base&0x00ffffffu)|(hot?0xa0000000u:0x55000000u);
            o.tri(q[0].x,q[0].y,q[1].x,q[1].y,q[2].x,q[2].y,fill);o.tri(q[0].x,q[0].y,q[2].x,q[2].y,q[3].x,q[3].y,fill);
            for(int i=0;i<4;++i)o.segment(q[i].x,q[i].y,q[(i+1)%4].x,q[(i+1)%4].y,1.4f*u,base);
        }
    };
    if(tool_==TransformTool::Rotate) {
        drawPlanes();
        const float3 toEye=normalize(view.eye()-pivot);
        for(int a=0;a<3;++a){
            if(planetMode_&&a!=1)continue;
            const bool hot=dragging&&drag_.axis==a;
            float3 bu,bv;ringBasis(axes[a],bu,bv);
            float2 prev{};bool hasPrev=false;
            for(int i=0;i<=kRingSegments;++i){
                const float t=6.2831853f*float(i)/float(kRingSegments);
                const float3 dir=bu*std::cos(t)+bv*std::sin(t);
                float2 q;if(!view.worldToScreen(pivot+dir*h.worldLength,W,H,q)){hasPrev=false;continue;}
                // The half facing away from the camera is dimmed, like Unity.
                const bool back=dot(dir,toEye)<-0.05f;
                const uint32_t col=hot?active:back?((colors[a]&0x00ffffffu)|0x50000000u):colors[a];
                if(hasPrev)o.segment(prev.x,prev.y,q.x,q.y,(hot?3.0f:2.2f)*u,col);
                prev=q;hasPrev=true;
            }
        }
        if(dragging && drag_.tool==TransformTool::Rotate && drag_.axis>=0 && drag_.axis<3) {
            const float3 axis=drag_.dir,start=drag_.startVec;const float R=h.worldLength;
            const float deg=drag_.value;
            float2 c;if(!view.worldToScreen(pivot,W,H,c))return;
            auto at=[&](float degrees,float radius,float2& out){
                const float3 v=quat::axisAngle(axis,degrees*0.017453292f).rotate(start);
                return view.worldToScreen(pivot+v*radius,W,H,out);
            };
            // Swept sector (how far it turned) + its two edges.
            const int steps=std::clamp(int(std::fabs(deg)/4.0f)+1,1,180);
            float2 prev;bool hasPrev=at(0,R,prev);
            for(int i=1;i<=steps;++i){
                float2 q;if(!at(deg*float(i)/float(steps),R,q)){hasPrev=false;continue;}
                if(hasPrev)o.tri(c.x,c.y,prev.x,prev.y,q.x,q.y,rgba(255,214,64,70));
                prev=q;hasPrev=true;
            }
            float2 a0,a1;
            if(at(0,R,a0))o.segment(c.x,c.y,a0.x,a0.y,1.6f*u,rgba(255,214,64,200));
            if(at(deg,R,a1))o.segment(c.x,c.y,a1.x,a1.y,2.2f*u,active);
            // Snap divisions on the active ring.
            if(drag_.snapping && snapRotate_>0.5f) {
                const int count=std::min(720,int(std::round(360.0f/snapRotate_)));
                for(int i=0;i<count;++i){
                    const float t=float(i)*snapRotate_;
                    const bool major=std::fmod(t,90.0f)<0.01f;
                    float2 p0,p1;
                    if(at(t,R*(major?0.86f:0.92f),p0)&&at(t,R*1.06f,p1))o.segment(p0.x,p0.y,p1.x,p1.y,(major?2.0f:1.3f)*u,rgba(255,240,190,major?230:170));
                }
            }
            // Angle readout next to the moving edge.
            char text[32];std::snprintf(text,sizeof text,"%+.1f\xb0",double(deg));
            std::string label=text;
            if(label.size()>3&&label.compare(label.size()-3,2,".0")==0)label.erase(label.size()-3,2); // "+15°" not "+15.0°"
            float2 lp;if(!at(deg,R*1.25f,lp))lp={c.x+R,c.y};
            const float th=std::max(10.0f,12.0f*u);
            overlayText(o,lp.x-overlayTextWidth(label,th)*0.5f,lp.y-th*0.5f,th,label,rgba(255,240,190));
        }
        return;
    }
    int order[3]={0,1,2};
    const float3 fwd=normalize(view.target-view.eye());
    std::sort(order,order+3,[&](int l,int r){return std::fabs(dot(axes[l],fwd))>std::fabs(dot(axes[r],fwd));});
    for(const int a:order){
        const uint32_t col=dragging&&drag_.axis==a?active:colors[a];
        float tipX=h.tip[a][0],tipY=h.tip[a][1];
        if(tool_==TransformTool::Scale && dragging && (drag_.axis==a||drag_.axis==3||(drag_.axis>=4&&drag_.axis-4!=a))){
            tipX=h.px+(tipX-h.px)*drag_.value;tipY=h.py+(tipY-h.py)*drag_.value;
        }
        const float dx=tipX-h.px,dy=tipY-h.py,l=std::hypot(dx,dy);
        if(l<3*u)continue;
        const float ux=dx/l,uy=dy/l;
        if(tool_==TransformTool::Scale){
            const float b=4.5f*u;
            o.segment(h.px,h.py,tipX-ux*b,tipY-uy*b,2.4f*u,col);
            o.tri(tipX-b,tipY-b,tipX+b,tipY-b,tipX+b,tipY+b,col);o.tri(tipX-b,tipY-b,tipX+b,tipY+b,tipX-b,tipY+b,col);
            continue;
        }
        const float cone=std::min(l*0.4f,14*u),half=5*u;
        const float bx=tipX-ux*cone,by=tipY-uy*cone;
        o.segment(h.px,h.py,bx,by,2.4f*u,col);
        o.tri(tipX,tipY,bx-uy*half,by+ux*half,bx+uy*half,by-ux*half,col);
    }
    drawPlanes();
    const float c=(tool_==TransformTool::Scale?4.5f:3.0f)*u;
    const uint32_t centre=tool_==TransformTool::Scale&&dragging&&drag_.axis==3?active:rgba(235,240,250);
    o.tri(h.px-c,h.py-c,h.px+c,h.py-c,h.px+c,h.py+c,centre);
    o.tri(h.px-c,h.py-c,h.px+c,h.py+c,h.px-c,h.py+c,centre);
    // Live readout: how far it moved (per axis, axis colours) / how much it scaled.
    if(dragging && drag_.axis>=0) {
        const float th=std::max(10.0f,12.0f*u);
        float tx=h.px+16*u,ty=h.py-34*u;
        if(drag_.tool==TransformTool::Move) {
            float2 from;
            if(view.worldToScreen(drag_.pivot,W,H,from))o.segment(from.x,from.y,h.px,h.py,1.4f*u,rgba(255,214,64,180));
            for(int a=0;a<3;++a){
                const bool on=drag_.axis==a||(drag_.axis>=4&&drag_.axis-4!=a);
                if(!on)continue;
                const std::string text=gizmoNumber(dot(drag_.moveOffset,drag_.axes[a]));
                overlayText(o,tx,ty,th,text,colors[a]);
                tx+=overlayTextWidth(text,th)+th*0.6f;
            }
        } else if(drag_.tool==TransformTool::Scale) {
            overlayText(o,tx,ty,th,"x"+gizmoNumber(drag_.value,false),drag_.axis<3?colors[drag_.axis]:rgba(255,240,190));
        }
    }
}
render::Model* StudioScene::builtinPrimitive(bool sphere) {
    auto& slot=sphere?builtinSphere_:builtinCube_;
    if(slot)return slot.get();
    render::MeshBuilder mesh;
    mesh.color(render::rgba(255,255,255));
    if(sphere) mesh.sphere({0,0,0},0.5f,24,32); else mesh.box({0,0,0},{1,1,1});
    render::Material base;base.vertexColors=false;base.name="Default";
    slot=render::modelFromParts(sphere?"builtin:sphere":"builtin:cube",
        {{sphere?"Sphere":"Cube",&mesh,base,render::mat4{}}});
    if(!slot)return nullptr;
    slot->batching.autoLod=false;
    render::prepareModelUpload(*slot);
    if(!render::uploadModel(*device_,*slot)){releasePreviewModel(*slot);slot.reset();return nullptr;}
    return slot.get();
}
std::vector<std::string> StudioScene::modelMaterialSlots(const std::string& asset) const {
    std::vector<std::string> names;
    const auto found=documentModels_.find(asset);
    if(found==documentModels_.end() || !found->second)return names;
    for(const auto& material:found->second->materials)
        if(!material.name.empty() && std::find(names.begin(),names.end(),material.name)==names.end())
            names.push_back(material.name);
    return names;
}
int StudioScene::orientationGizmoHit(float x,float y,float width,float height) const {
    if(!orientationGizmo_ || materialMode_ || width<4 || height<4)return -1;
    const auto g=gizmoLayout(width,height);
    if(std::hypot(x-g.cx,y-g.cy)>g.r*1.45f)return -1;
    if(std::hypot(x-g.cx,y-g.cy)<g.r*0.26f)return 6;
    const auto b=gizmoBasis(camera_.yaw,camera_.pitch);
    int best=-1;float bestScore=1e9f;
    for(int arm=0;arm<6;++arm) {
        const float3 a=gizmoAxis(arm);
        const float tx=g.cx+dot(a,b.right)*g.r,ty=g.cy-dot(a,b.up)*g.r;
        const float d=std::hypot(x-tx,y-ty);
        // Prefer the arm nearer to the viewer when tips overlap.
        const float score=d+dot(a,b.forward)*g.r*0.15f;
        if(d<g.r*0.42f && score<bestScore){bestScore=score;best=arm;}
    }
    return best;
}
void StudioScene::activateOrientationGizmo(int hit) {
    if(hit==6){setOrthographic(!camera_.ortho);return;}
    if(hit<0 || hit>5)return;
    const float sign=(hit&1)?-1.0f:1.0f;
    const int axis=hit/2;
    const float half=1.5707963f;
    if(axis==0)setOrientation(sign*half,0);
    else if(axis==1)setOrientation(0,sign*half);
    else setOrientation(sign>0?0.0f:3.14159265f,0);
    if(!camera_.ortho){camera_.ortho=true;autoIso_=true;}
}
void StudioScene::drawOrientationGizmo(const render::Camera& view,int width,int height) {
    // Drawn flat in screen space (like the HTML gizmo): every vertex is a pixel
    // position unprojected onto a plane just beyond the near plane, so the corner
    // placement never adds perspective skew. Overlays are depth-tested without
    // depth writes; this plane is in front of every scene surface.
    const float W=float(width),H=float(height);
    const auto g=gizmoLayout(W,H);
    const float depth=view.ortho?0.5f:view.zNear*1.5f;
    bool ok=true;
    auto at=[&](float x,float y)->float3 {
        float3 o,d;
        if(!view.screenRay(x,y,W,H,o,d)){ok=false;return {};}
        return o+d*depth;
    };
    auto tri=[&](float ax,float ay,float bx,float by,float cx,float cy,uint32_t col){
        const float3 p=at(ax,ay),q=at(bx,by),r=at(cx,cy);
        if(ok)renderer_.triangle(p,q,r,col);
    };
    auto quad=[&](float ax,float ay,float bx,float by,float cx,float cy,float dx,float dy,uint32_t col){
        tri(ax,ay,bx,by,cx,cy,col);tri(ax,ay,cx,cy,dx,dy,col);
    };
    auto segment=[&](float x0,float y0,float x1,float y1,float w,uint32_t col){
        const float lx=x1-x0,ly=y1-y0,l=std::hypot(lx,ly);
        if(l<1e-3f)return;
        const float nx=-ly/l*w*.5f,ny=lx/l*w*.5f;
        quad(x0+nx,y0+ny,x1+nx,y1+ny,x1-nx,y1-ny,x0-nx,y0-ny,col);
    };
    auto disc=[&](float cx,float cy,float radius,uint32_t col,int segments){
        for(int i=0;i<segments;++i){
            const float a0=6.2831853f*float(i)/segments,a1=6.2831853f*float(i+1)/segments;
            tri(cx,cy,cx+std::cos(a0)*radius,cy+std::sin(a0)*radius,cx+std::cos(a1)*radius,cy+std::sin(a1)*radius,col);
        }
    };
    const float r=g.r,u=r/31.0f; // u: one HTML-gizmo pixel
    disc(g.cx,g.cy,r+11*u,rgba(27,43,65,150),32);
    const auto b=gizmoBasis(view.yaw,view.pitch);
    int order[6]={0,1,2,3,4,5};
    std::sort(order,order+6,[&](int l,int rr){return dot(gizmoAxis(l),b.forward)>dot(gizmoAxis(rr),b.forward);});
    const uint32_t colors[3]={rgba(232,102,95),rgba(134,207,108),rgba(95,151,238)};
    for(const int arm:order) {
        const float3 a=gizmoAxis(arm);
        const bool negative=(arm&1)!=0;
        const uint32_t col=negative?rgba(111,127,150):colors[arm/2];
        const float ax=dot(a,b.right)*r,ay=-dot(a,b.up)*r;
        const float len=std::hypot(ax,ay);
        const float tx=g.cx+ax,ty=g.cy+ay;
        if(len<=4*u){disc(tx,ty,7*u,col,16);continue;}
        const float ux=ax/len,uy=ay/len;
        const float cone=std::min(len,std::max(7*u,15*u*len/r));
        const float bx=tx-ux*cone,by=ty-uy*cone,half=6*u;
        segment(g.cx,g.cy,bx,by,2.4f*u,col);
        tri(tx,ty,bx-uy*half,by+ux*half,bx+uy*half,by-ux*half,col);
        if(negative)continue;
        // Axis letter beyond the tip, drawn as strokes.
        const float lx=tx+ux*9*u,ly=ty+uy*9*u,h=3.6f*u,w=2.4f*u,st=1.6f*u;
        if(arm/2==0){segment(lx-w,ly-h,lx+w,ly+h,st,col);segment(lx+w,ly-h,lx-w,ly+h,st,col);}
        else if(arm/2==1){segment(lx-w,ly-h,lx,ly,st,col);segment(lx+w,ly-h,lx,ly,st,col);segment(lx,ly,lx,ly+h,st,col);}
        else{segment(lx-w,ly-h,lx+w,ly-h,st,col);segment(lx+w,ly-h,lx-w,ly+h,st,col);segment(lx-w,ly+h,lx+w,ly+h,st,col);}
    }
    // Centre square: click toggles Persp/Iso (highlighted while Iso).
    const float c=6.5f*u;
    quad(g.cx-c,g.cy-c,g.cx+c,g.cy-c,g.cx+c,g.cy+c,g.cx-c,g.cy+c,
         view.ortho?rgba(130,181,255):rgba(214,224,240));
}

void StudioScene::orbit(float dx, float dy) {
    tween_.active=false;
    if(autoIso_){camera_.ortho=false;autoIso_=false;}
    // Unity: drag right turns the view right, drag up looks up (pivot stays centred).
    camera_.yaw -= dx * 0.007f;
    camera_.pitch = std::clamp(camera_.pitch + dy * 0.007f, -1.52f, 1.52f);
}
void StudioScene::zoom(float wheel) {
    tween_.active=false;
    // Unity-like dolly: zoom toward the pivot; at the minimum distance keep
    // moving forward by pushing the pivot instead of stopping.
    const float next=camera_.distance*std::exp(-wheel*0.035f);
    if(caveGlobeRequested()){camera_.target={0,0,0};camera_.distance=std::clamp(next,12.f,100.f);return;}
    constexpr float kMinDistance=2.0f;
    if(next<kMinDistance && wheel>0) {
        const float cp=std::cos(camera_.pitch),sp=std::sin(camera_.pitch);
        const float3 forward{-std::sin(camera_.yaw)*cp,-sp,-std::cos(camera_.yaw)*cp};
        camera_.target+=forward*std::max(0.02f,kMinDistance*(1.0f-std::exp(-wheel*0.035f)));
        camera_.distance=kMinDistance;
        return;
    }
    camera_.distance = std::clamp(next, kMinDistance, 100000.0f);
}
void StudioScene::look(float dx, float dy) {
    // First-person free look preserves the camera's world-space eye instead
    // of orbiting around its target. WASD movement then translates the eye.
    tween_.active=false;
    if(autoIso_){camera_.ortho=false;autoIso_=false;}
    const float3 position=camera_.eye();
    camera_.yaw -= dx*0.007f;   // Unity: drag right looks right, drag up looks up
    camera_.pitch=std::clamp(camera_.pitch+dy*0.007f,-1.52f,1.52f);
    const float cp=std::cos(camera_.pitch),sp=std::sin(camera_.pitch);
    const float3 fromTarget{std::sin(camera_.yaw)*cp,sp,std::cos(camera_.yaw)*cp};
    camera_.target=position-fromTarget*camera_.distance;
}
void StudioScene::pan(float dx, float dy) {
    // Editor navigation follows camera orientation, not RTS ground-plane input.
    tween_.active=false;
    const float speed=std::max(0.005f,camera_.distance*0.0018f);
    const float yaw=camera_.yaw;
    const float3 right{std::cos(yaw),0,-std::sin(yaw)};
    const float3 up{-std::sin(yaw)*std::sin(camera_.pitch),
                    std::cos(camera_.pitch),
                    -std::cos(yaw)*std::sin(camera_.pitch)};
    camera_.target += right*(-dx*speed) + up*(dy*speed);
}
void StudioScene::move(float right, float up, float forward) {
    tween_.active=false;
    const float speed=std::max(0.025f,camera_.distance*0.045f);
    const float yaw=camera_.yaw;
    const float3 side{std::cos(yaw),0,-std::sin(yaw)};
    const float3 ahead{-std::sin(yaw)*std::cos(camera_.pitch),
                       -std::sin(camera_.pitch),
                       -std::cos(yaw)*std::cos(camera_.pitch)};
    camera_.target += (side*right + float3{0,1,0}*up + ahead*forward)*speed;
}
void StudioScene::focusSelected(const editor::MapDoc& map,bool fpsProject,
                                      const std::string& selectedId) {
    if(map.data.planet()){
        if(planetPreview_)for(const auto& p:planetPreview_->markers)if(p.id==selectedId){
            const auto dir=normalize(p.center);
            startTween(p.center,std::max(3.f,p.half.x*8.f),std::atan2(dir.x,dir.z),std::asin(std::clamp(dir.y,-1.f,1.f)));break;
        }
        return;
    }
    for (const auto& instance:modelInstances_) {
        if (instance.id!=selectedId || !instance.model) continue;
        const auto lo=instance.model->boundsMin,hi=instance.model->boundsMax;
        float3 worldMin{1e30f,1e30f,1e30f},worldMax{-1e30f,-1e30f,-1e30f};
        for (int corner=0;corner<8;++corner) {
            const float3 local{corner&1?hi.x:lo.x,corner&2?hi.y:lo.y,corner&4?hi.z:lo.z};
            const auto p=instance.world.transformPoint(local);
            worldMin={std::min(worldMin.x,p.x),std::min(worldMin.y,p.y),std::min(worldMin.z,p.z)};
            worldMax={std::max(worldMax.x,p.x),std::max(worldMax.y,p.y),std::max(worldMax.z,p.z)};
        }
        const auto size=worldMax-worldMin;
        startTween((worldMin+worldMax)*.5f,
            std::clamp(std::max({size.x,size.y,size.z,1.0f})*2.5f,2.0f,100000.0f),camera_.yaw,camera_.pitch);
        return;
    }
    auto proxies=studioDocumentProxies(map,fpsProject,terrainLevelHeight_);
    proxies.erase(std::remove_if(proxies.begin(),proxies.end(),[&](const auto& proxy){
        const int index=studioMapEntityIndex(proxy.id,map);
        return index>=0 && map.data.entities[size_t(index)].extra["components"].has("NereidWater");
    }),proxies.end());
    for (const auto& proxy:proxies) {
        if (proxy.id != selectedId) continue;
        const float3 extent=proxy.boundsMax-proxy.boundsMin;
        const float size=std::max({extent.x,extent.y,extent.z,0.5f});
        startTween((proxy.boundsMin+proxy.boundsMax)*0.5f,std::clamp(size*3.0f,2.0f,100000.0f),
            camera_.yaw,camera_.pitch);
        return;
    }
}
void StudioScene::resetView() {
    tween_.active=false;
    camera_.ortho=false;
    cameraInitialized_=false;
    camera_.pitch=0.85f;
    camera_.yaw=0.55f;
}
void StudioScene::setCaveGlobe(std::string body,const std::string& project,const std::string& map){
    if(body==globeBody_&&project==globeProject_&&map==globeMap_)return;
    if(!device_)throw std::runtime_error("Caverns globe device unavailable");
    if(!body.empty()&&!cavernsPreview_.close(*device_))
        throw std::runtime_error("Caverns chart retirement refused");
    terrainRetirement_.retireBuffer(*device_,globeMesh_.vb);
    terrainRetirement_.retireBuffer(*device_,globeMesh_.ib);
    globeMesh_={};globe_.reset();globeDoc_=nullptr;globeRevision_=0;
    globeBody_=std::move(body);globeProject_=project;globeMap_=map;
    globeReady_=false;globeError_.clear();planetCutDrawn_=false;
    cameraInitialized_=false;tween_.active=false;lastFrameSignature_=0;
    terrainRevision_=std::numeric_limits<uint64_t>::max();
    lightingRevision_=terrainRevision_;
}
Json StudioScene::pickCaveGlobe(float x,float y,float width,float height,const editor::MapDoc& doc,
                               const std::string& project)const{
    Json out=Json::object();out.set("hit",Json::fromBool(false));
    if(!globeReady_||!globe_||project!=globeProject_||doc.path!=globeMap_||
       doc.revision!=globeRevision_||x<0||y<0||x>=width||y>=height)return out;
    float3 origin,direction;
    if(!caveScreenRay(x,y,width,height,origin,direction))return out;
    const auto hit=globe_->pick(origin,direction);
    if(!hit)return out;
    out.set("hit",Json::fromBool(true));out.set("body",Json::string(globeBody_));
    out.set("face",Json::number(int64_t(hit->face)));
    out.set("aQ16",Json::number(hit->a.raw));out.set("bQ16",Json::number(hit->b.raw));
    out.set("regionId",Json::string(hit->regionId));
    out.set("mapRevisionText",Json::string(std::to_string(doc.revision)));
    return out;
}
void StudioScene::renderCaveGlobe(int width,int height,const editor::MapDoc& doc){
    (void)terrainRetirement_.collectWithoutIdle(*device_);
    if(globeDoc_!=&doc||globeRevision_!=doc.revision){
        globeReady_=false;globeError_.clear();
        try{
            if(terrainRetirement_.pending()>=64)throw std::runtime_error("Globe retirement budget");
            auto candidate=StudioPlanetGlobe::capture(doc.data,globeBody_);
            auto cpu=candidate.mesh();
            auto mesh=cpu.upload(*device_);
            if(!mesh.vb.valid()||!mesh.ib.valid()){
                terrainRetirement_.retireBuffer(*device_,mesh.vb);
                terrainRetirement_.retireBuffer(*device_,mesh.ib);
                throw std::runtime_error("Globe overview upload failed");
            }
            terrainRetirement_.retireBuffer(*device_,globeMesh_.vb);
            terrainRetirement_.retireBuffer(*device_,globeMesh_.ib);
            globeMesh_=mesh;globe_=std::move(candidate);
        }catch(const std::exception& e){globe_.reset();globeError_=e.what();}
        globeDoc_=&doc;globeRevision_=doc.revision;
    }
    if(!cameraInitialized_){
        camera_.target={0,0,0};camera_.distance=32.f;camera_.ortho=false;
        camera_.pitch=.3f;camera_.yaw=.55f;tween_.active=false;cameraInitialized_=true;
    }
    // No chart or flat-scene object submissions in overview mode.
    camera_.target={0,0,0};camera_.distance=std::clamp(camera_.distance,12.f,100.f);
    tween_.active=false;
    device_->beginFrame(width,height);StudioFrameScope scope{*device_};
    renderer_.beginScene(viewCamera(),width,height);
    renderer_.clearDynamicGiOverlay();
    renderer_.lighting=render::Lighting{};
    renderer_.lighting.sunIntensity=1.2f;renderer_.lighting.skyColor={.44f,.53f,.68f};
    renderer_.lighting.fogDensity=0;
    if(globe_&&globeMesh_.vb.valid()&&globeMesh_.ib.valid()){
        renderer_.drawMesh(globeMesh_,mat4{}, {1,1,1,1},kDrawNoCastShadow);
        for(const auto& marker:globe_->markers)
            renderer_.drawMesh(cube_,mat4::translate(marker.position)*mat4::scale({marker.size,marker.size,marker.size}),
                {.95f,.66f,.18f,1},kDrawNoCastShadow);
    }
    gfx::PassDesc pass;pass.clearColor[0]=.025f;pass.clearColor[1]=.035f;pass.clearColor[2]=.06f;pass.clearColor[3]=1.f;
    renderer_.render(pass,&antialiasing_);renderer_.resolvePostFx();
    globeReady_=globe_.has_value()&&globeMesh_.vb.valid()&&globeMesh_.ib.valid();
    ++framesDrawn_;
}
// Real Scene-device render target placed inside the SpectraFX Graph dock.
// This is deliberately a *mode* on the existing render-thread-owned
// CAMetalLayer/Vulkan surface, NOT a second graphics context or HTML frame.
// The map is not submitted while FX authoring is active.
// All three helpers run between native frames. Never release a runtime
// from frame provenance sampling while an encoder is still open.
void StudioScene::discardReplayBackup() {
    if(!spectraReplayBackup_)return;
    spectraReplayBackup_->stop(*device_);
    spectraReplayBackup_.reset();
}
void StudioScene::rollbackReplayTransaction(const std::string& outcome,const std::string& reason) {
    if(!spectraReplayBackup_)return;
    spectraPreview_->stop(*device_);
    spectraPreview_=std::move(spectraReplayBackup_);
    camera_=replayBackupCamera_;cameraInitialized_=replayBackupCameraInitialized_;
    tween_=replayBackupTween_;
    fxBackground_=replayBackupBackground_;fxGrid_=replayBackupGrid_;fxAxes_=replayBackupAxes_;
    replayOutcome_=outcome;replayError_=reason;
    lastFrameSignature_=0;settleFrames_=30;
}
void StudioScene::finishReplayTransaction() {
    if(!spectraReplayBackup_)return;
    const auto state=spectraPreview_->command(*device_,Json::parse(R"({"action":"status"})"));
    if(state.getBool("replayReady")){
        discardReplayBackup();
        replayOutcome_="committed";replayError_.clear();
    }else if(!state.getBool("replayPending")){
        const auto reason=state.getStr("error");
        rollbackReplayTransaction("failed",reason.empty()?"Replay candidate failed":reason);
    }
}
Json StudioScene::attachmentCommand(const std::string& project,const Json& params){
    if(!attachmentPreview_)attachmentPreview_=std::make_unique<StudioAttachmentPreview>(*device_,renderer_,deviceLifetime_->lease());
    if(params.has("document")){
        auto result=attachmentPreview_->publish(project,params["document"]);
        attachmentPreview_->controls(params);
        return result;
    }
    if(params.getBool("status")){
        Json result=Json::object();result.set("time",Json::number(double(attachmentPreview_->time()),4));return result;
    }
    attachmentPreview_->controls(params);return Json::object();
}
void StudioScene::drawAttachments(int width,int height){
    if(!attachmentPreview_||!attachmentPreview_->ready()||width<2||height<2)return;
    device_->beginFrame(width,height);StudioFrameScope frame{*device_};
    attachmentPreview_->draw(width,height);
}

void StudioScene::drawAnimationPose(int width,int height,const std::shared_ptr<const render::Model>& source,
                                    const render::Pose& pose,float yaw) {
    if(!device_||width<2||height<2)return;
    if(source!=animationSource_){
        if(animationModel_){
            if(!device_->waitForResourceIdle())throw std::runtime_error("Animation preview GPU busy");
            releasePreviewModel(*animationModel_);
            animationModel_.reset();
            animationSource_.reset();
        }
        if(source){
            auto model=std::make_unique<render::Model>(*source);
            try {
                if(!render::uploadModel(*device_,*model))throw std::runtime_error("Animation model upload failed");
            } catch(...){releasePreviewModel(*model);throw;}
            animationModel_=std::move(model);
            animationSource_=source;
        }
    }
    render::Camera camera;
    camera.yaw=yaw;camera.pitch=.15f;camera.minDistance=.01f;camera.fovY=.6f;
    if(source){
        camera.target=(source->boundsMin+source->boundsMax)*.5f;
        const auto size=source->boundsMax-source->boundsMin;
        camera.distance=std::max({size.x,size.y,size.z,.3f})*2.1f*std::max(1.f,float(height)/width);
    }
    device_->beginFrame(width,height);
    StudioFrameScope frame{*device_};
    renderer_.beginScene(camera,width,height);
    renderer_.lighting.skyColor={.8f,.85f,.9f};
    renderer_.lighting.groundColor={.5f,.5f,.5f};
    renderer_.lighting.fogDensity=0;
    std::vector<render::mat4> world;
    if(animationModel_&&pose.size()==animationModel_->nodes.size()){
        render::worldMatrices(*animationModel_,pose,world);
        render::ModelDraw draw;
        draw.model=animationModel_.get();draw.nodeWorld=&world;draw.pose=&pose;
        renderer_.drawModel(draw);
    }
    gfx::PassDesc pass;
    pass.clearColor[0]=.07f;pass.clearColor[1]=.09f;pass.clearColor[2]=.135f;
    renderer_.render(pass,&antialiasing_);
    renderer_.resolvePostFx();
}

void StudioScene::frameSpectraDock(int framebufferWidth,int framebufferHeight) {
    if(!device_ || framebufferWidth<2 || framebufferHeight<2)return;
    // GPU updates/retirement happen between frames, before an active pass.
    if(antialiasing_.pendingTargetRetirements())antialiasing_.collectRetiredTargets();
    if(renderer_.pendingProgramRetirements())renderer_.collectRetiredPrograms();
    if(renderer_.pendingTargetRetirements())renderer_.collectRetiredTargets();
    device_->beginFrame(framebufferWidth,framebufferHeight);
    StudioFrameScope scope{*device_};
    const auto view=viewCamera();
    const float aspect=float(framebufferWidth)/float(framebufferHeight);
    const auto viewProj=view.proj(aspect,device_->info().clipDepthZeroToOne)*view.view();
    if(spectraPreview_->active()) {
        spectraPreview_->prepare(fx::SpectraFxView::fromCamera(
            view,viewProj,framebufferWidth,framebufferHeight),{0,0,0});
    }
    renderer_.beginScene(view,framebufferWidth,framebufferHeight);
    renderer_.clearDynamicGiOverlay();
    renderer_.setSceneSelectionOutline(false);
    renderer_.lighting=render::Lighting{};
    renderer_.lighting.sunIntensity=1.0f;
    renderer_.lighting.skyColor={.14f,.19f,.26f};
    renderer_.lighting.groundColor={.12f,.14f,.18f};
    renderer_.lighting.fogDensity=0.0f;
    // Genre-independent visual reference stage. Never writes to the game
    // map and never changes native particle positions or update cadence.
    gfx::PassDesc pass;
    if(fxBackground_=="black") {
        pass.clearColor[0]=0;pass.clearColor[1]=0;pass.clearColor[2]=0;
    } else if(fxBackground_=="light") {
        pass.clearColor[0]=.62f;pass.clearColor[1]=.67f;pass.clearColor[2]=.73f;
    } else if(fxBackground_=="neutral") {
        pass.clearColor[0]=.18f;pass.clearColor[1]=.20f;pass.clearColor[2]=.23f;
    } else {
        pass.clearColor[0]=.021f;pass.clearColor[1]=.032f;pass.clearColor[2]=.052f;
    }
    pass.clearColor[3]=1.0f;
    if(fxGrid_) {
        const uint32_t major=fxBackground_=="light"?rgba(60,78,95,150):rgba(78,111,146,110);
        const uint32_t minor=fxBackground_=="light"?rgba(82,98,115,110):rgba(53,76,102,72);
        for(int i=-5;i<=5;++i) {
            const auto color=i==0?major:minor;
            renderer_.line({float(i),-.05f,-5},{float(i),-.05f,5},color);
            renderer_.line({-5,-.05f,float(i)},{5,-.05f,float(i)},color);
        }
    }
    if(fxAxes_) {
        renderer_.line({-.65f,0,0},{.65f,0,0},rgba(230,108,98));
        renderer_.line({0,0,-.65f},{0,0,.65f},rgba(104,147,237));
        renderer_.line({0,-.45f,0},{0,.75f,0},rgba(108,214,129));
    }
    float floorHeight=0;
    if(fxBackground_=="light"&&spectraPreview_->volumeStageFloor(floorHeight)){
        renderer_.drawMesh(cube_,render::mat4::translate({0,floorHeight-.05f,0})*
            render::mat4::scale({100,.1f,100}),{.55f,.55f,.55f,1},render::kDrawNoCastShadow);
    }
    const bool volume=spectraPreview_->hasVolume();
    // A separate preview policy, not a change to the authored map HDR settings.
    if(volume!=spectraDockHdr_){
        PostFxSettings aa;aa.hdr=volume?HdrMode::On:HdrMode::Off;
        aa.effects.push_back(defaultPostFxEffect(PostFxType::Fxaa));
        if(volume)aa.effects.push_back(defaultPostFxEffect(PostFxType::Bloom));
        antialiasing_.setSettings(std::move(aa));spectraDockHdr_=volume;
    }
    if(volume)spectraPreview_->contributeVolume(antialiasing_);
    renderer_.render(pass,&antialiasing_);
    if(spectraPreview_->active())spectraPreview_->draw();
    renderer_.resolvePostFx();
    ++framesDrawn_;
    // Record the state actually used by this dock frame, before submission.
    Json provenance=spectraPreviewCommand(Json::parse(R"({"action":"status"})"),spectraProject_,true);
    provenance.set("projectPath",Json::string(spectraProject_));
    provenance.set("frameSerial",Json::number(int64_t(device_->frameSerial())));
    fxFrameProvenance_->emplace_back(device_->frameSerial(),std::move(provenance));
    while(fxFrameProvenance_->size()>64)fxFrameProvenance_->pop_front();
    // StudioFrameScope closes the native pass and frame on owner thread.
}
void StudioScene::frame(int framebufferWidth, int framebufferHeight, const editor::MapDoc& doc,
                             bool fpsProject, const std::string& selectedId,
                             const std::string& previewName, const Json& previewFactors,
                             uint64_t previewRevision, float levelHeight,
                             const std::string& projectPath,const Json& rules,
                             const StudioFacialPreviewBinding* facialBindingIn) {
    static const StudioFacialPreviewBinding kNoFacialBinding{};
    const StudioFacialPreviewBinding& facialBinding=facialBindingIn?*facialBindingIn:kNoFacialBinding;
    const std::string& projectRoot=projectPath;
    // Project changes invalidate runtime particle buffers and preview input
    // so a previous project's effect can never render in the new Scene.
    if(spectraProject_!=projectRoot){
        discardReplayBackup();
        if(spectraPreview_->active())spectraPreview_->stop(*device_);
        spectraProject_=projectRoot;
        replayAttemptGeneration_=0;replayOutcome_.clear();replayError_.clear();
    }
    facialReady_=false;
    facialError_.clear();
    globeReady_=false; // only the actual overview render enables picking
    if (!device_ || framebufferWidth < 2 || framebufferHeight < 2) return;
    if(spectraDockMode_) {
        // A dedicated VFX authoring viewport, not the authored map.
        frameSpectraDock(framebufferWidth,framebufferHeight);
        finishReplayTransaction(); // frame scope has closed its pass and submission
        return;
    }
    if(antialiasing_.pendingTargetRetirements())antialiasing_.collectRetiredTargets();
    if (renderer_.pendingProgramRetirements()) renderer_.collectRetiredPrograms();
    if (renderer_.pendingTargetRetirements()) renderer_.collectRetiredTargets();
    if (renderer_.hasPendingModelPipelineJobs()) renderer_.pollPendingModelPipelineJobs();
    // This is a native isolated 3D preview. Existing Scene viewport
    // orbit/zoom/pan operate on camera_ and never mutate the project.
    if(buildingPreview_ && buildingPreviewGpuReady_){
        lastFrameSignature_=0; // redraw while interactive camera is active
        device_->beginFrame(framebufferWidth,framebufferHeight);
        StudioFrameScope scope{*device_};
        renderer_.beginScene(viewCamera(),framebufferWidth,framebufferHeight);
        renderer_.clearDynamicGiOverlay();
        renderer_.lighting=render::Lighting{};
        renderer_.lighting.sunIntensity=1.25f;
        renderer_.lighting.skyColor={.43f,.5f,.61f};
        renderer_.lighting.groundColor={.25f,.27f,.29f};
        renderer_.lighting.fogDensity=0;
        render::ModelDraw draw;
        draw.model=buildingPreview_.get();
        renderer_.drawModel(draw);
        gfx::PassDesc pass;
        pass.clearColor[0]=.065f;pass.clearColor[1]=.08f;
        pass.clearColor[2]=.11f;pass.clearColor[3]=1.f;
        renderer_.render(pass,&antialiasing_);
        renderer_.resolvePostFx();
        return;
    }
    const bool materialRequested = !previewName.empty() && previewFactors.isObject();
    const bool facialRequested = facialBinding.bound() && !materialRequested;
    // Those previews draw every frame; the map view redraws once they end.
    if(materialRequested || facialRequested) lastFrameSignature_=0;
    if(facialMode_!=facialRequested) {
        facialMode_=facialRequested;
        shadowResolution_={};
        if(facialRequested)renderer_.setShadowQuality(ShadowQuality::High);
        cameraInitialized_=false;
        facialFramedGuid_.clear();
        // Facial preview overwrites renderer_.lighting; re-apply map lighting after it.
        lightingRevision_=std::numeric_limits<uint64_t>::max();
    }
    // A real imported Model is drawn through the SAME Metal Renderer3D used
    // for native materials and the map; no HTML fake character, no second
    // Animator/Graph clock. CPU import is asynchronous; GPU publication and
    // retirement remain on this owner thread before frame submissions.
    if(!facialRequested){renderer_.setModelLodEnabled(true);renderer_.clearShadowBounds();renderer_.setShadowQuality(ShadowQuality::Off);renderer_.lights.setQuality(LightQuality::Medium);}
    if(facialRequested) {
        renderer_.setModelLodEnabled(facialBinding.previewAnimation.automaticLod);
        std::string reason;
        const bool synced=facialGpu_.sync(*device_,renderer_,projectRoot,facialBinding,reason);
        const std::string loading=synced?std::string{}:reason;
        if(synced || facialGpu_.showsCharacter(facialBinding)) {
            // Spans both pose evaluation and the deferred renderer draw list.
            const auto modelRead=facialGpu_.acquireRead();
            if(!modelRead) {
                facialError_="Facial Model read lease unavailable";
                return;
            }
            // Frame every newly shown character once; later edits of the
            // same character keep the artist's camera.
            if(!cameraInitialized_ || (synced && facialFramedGuid_!=facialBinding.guid())) {
                camera_.target={0,0,0};
                camera_.distance=4.2f;
                camera_.pitch=.15f;
                camera_.yaw=.0f;
                camera_.ortho=false;
                tween_.active=false;
                cameraInitialized_=true;
                facialFramedGuid_=facialBinding.guid();
                facialCameraSerial_=facialBinding.cameraSerial;
            }
            if(facialCameraSerial_!=facialBinding.cameraSerial) {
                // The preview normalizes the character to 2 units tall around
                // the origin: head near +0.85, chest near +0.4.
                facialCameraSerial_=facialBinding.cameraSerial;
                const auto& v=facialBinding.cameraView;
                camera_.target=v=="face"?float3{0,.82f,0}:v=="torso"?float3{0,.4f,0}:float3{0,0,0};
                camera_.distance=v=="face"?.85f:v=="torso"?2.f:4.2f;
                camera_.pitch=v=="face"?.05f:.15f;camera_.yaw=0;camera_.ortho=false;tween_.active=false;
            }
            device_->beginFrame(framebufferWidth,framebufferHeight);
            renderer_.beginScene(camera_,framebufferWidth,framebufferHeight);
            renderer_.clearDynamicGiOverlay(); // map GI must not leak into the isolated facial preview
            const auto* model=facialGpu_.model();
            // Match the exact normalization used by StudioFacialGpuPreview::submit.
            const auto extent=model->boundsMax-model->boundsMin;
            const float scale=2.f/std::max({extent.x,extent.y,extent.z,.0001f});
            const float h=std::max(.01f,extent.y*scale);
            const float3 foot{0,-extent.y*.5f*scale,0};
            vivid::applyHumanStudioLighting(renderer_,facialBinding.studioLighting,foot,h);
            vivid::updateHumanStudioShadows(renderer_,shadowResolution_,h,framebufferHeight);
            const auto bg=facialBinding.studioLighting.rig().background;
            if(facialBinding.studioLighting.backdrop)renderer_.drawMesh(studioCyclorama_,mat4::trs(foot,{}, {h,h,h}),{bg.x,bg.y,bg.z,1},kDrawNoCastShadow,0);
            if(facialBinding.showRuler && extent.y>1e-4f) {
                // Height ruler beside the character: pole + a tick every
                // 10 cm (longer every 50 cm), in model units = metres.
                const float bodyH=facialBinding.appearance()?facialBinding.appearance()->get("body.height"):1.f;
                const float metres=extent.y*bodyH,x=extent.x*.5f*scale+.12f,t=.006f;
                const render::float4 ink{.85f,.88f,.95f,1};
                renderer_.drawMesh(cube_,mat4::translate({x,foot.y+metres*scale*.5f,0})*mat4::scale({t,metres*scale,t}),ink,kDrawNoCastShadow);
                for(int cm=0;cm<=int(metres*100.f+.5f)&&cm<=400;cm+=10) {
                    const float len=cm%50==0?.07f:.035f;
                    renderer_.drawMesh(cube_,mat4::translate({x-len*.5f,foot.y+float(cm)*.01f*scale,0})*mat4::scale({len,t*.8f,t}),ink,kDrawNoCastShadow);
                }
            }

            const bool submitted=facialGpu_.submit(
                renderer_,facialBinding,1.f/60.f,reason);
            gfx::PassDesc pass;
            pass.clearColor[0]=bg.x;
            pass.clearColor[1]=bg.y;
            pass.clearColor[2]=bg.z;
            pass.clearColor[3]=1.f;
            renderer_.render(pass,&antialiasing_);
            renderer_.resolvePostFx();
            device_->endPass();
            if(!thumbnailPath_.empty() && submitted && synced) {
                // Square crop of the upper middle (the head in the face
                // camera), box-filtered down to 256 px.
                device_->captureNextFrame([path=std::move(thumbnailPath_),crop=thumbnailCrop_](int w,int h,std::vector<uint8_t> rgba) {
                    if(w<=0||h<=0||rgba.size()<size_t(w)*size_t(h)*4)return;
                    if(!crop) {
                        // Whole frame, integer box downscale to <= 1280 px wide.
                        const int f=std::max(1,(w+1279)/1280),ow=w/f,oh=h/f;
                        std::vector<uint8_t> px(size_t(ow)*oh*4);
                        for(int y=0;y<oh;++y)for(int x=0;x<ow;++x){unsigned acc[3]{};
                            for(int sy=0;sy<f;++sy)for(int sx=0;sx<f;++sx){const auto* p=&rgba[(size_t(y*f+sy)*w+x*f+sx)*4];for(int c=0;c<3;++c)acc[c]+=p[c];}
                            auto* d=&px[(size_t(y)*ow+x)*4];for(int c=0;c<3;++c)d[c]=uint8_t(acc[c]/unsigned(f*f));d[3]=255;}
                        (void)render::cameraWritePngRgba8(path+".pending.png",ow,oh,px);
                        std::error_code ec;std::filesystem::rename(path+".pending.png",path,ec);
                        return;
                    }
                    const int side=std::max(1,std::min(w,h)*3/4),x0=(w-side)/2,y0=std::max(0,(h-side)/2-h/10);
                    const int out=256;std::vector<uint8_t> px(size_t(out)*out*4);
                    for(int y=0;y<out;++y)for(int x=0;x<out;++x){
                        const int sx0=x0+x*side/out,sx1=std::max(sx0+1,x0+(x+1)*side/out);
                        const int sy0=y0+y*side/out,sy1=std::max(sy0+1,y0+(y+1)*side/out);
                        unsigned acc[4]{};unsigned n=0;
                        for(int sy=sy0;sy<std::min(sy1,h);++sy)for(int sx=sx0;sx<std::min(sx1,w);++sx){
                            const auto* p=&rgba[(size_t(sy)*w+sx)*4];for(int c=0;c<4;++c)acc[c]+=p[c];++n;}
                        auto* d=&px[(size_t(y)*out+x)*4];
                        for(int c=0;c<3;++c)d[c]=uint8_t(n?acc[c]/n:0);d[3]=255;
                    }
                    (void)render::cameraWritePngRgba8(path,out,out,px);
                });
                thumbnailPath_.clear();
            }
            device_->endFrame();
            facialReady_=submitted && synced;
            facialError_=!submitted?reason:loading;
            return;
        }
        // Failed bind never displays stale model from a different GUID.
        // The ordinary map remains visible while a diagnostic is returned.
        facialError_=reason;
        cameraInitialized_=false;
    }else if(!facialBinding.bound()) {
        if(!facialGpu_.clear(*device_,renderer_))
            facialError_="Previous facial Model retirement lacks GPU completion";
    }
    if (materialRevision_ != previewRevision) {
        materialRevision_ = previewRevision;
        graphPreviewError_.clear();
        if(graphParticles_){
            if(device_->waitForResourceIdle())graphParticles_.reset();
            else graphPreviewError_="Cannot retire previous particle preview";
        }
        if(graphPreviewPost_){
            if(device_->waitForResourceIdle()){graphPreviewPost_->shutdown();graphPreviewPost_.reset();}
            else graphPreviewError_="Cannot retire previous post effect preview";
        }
        if (materialRequested && graphPreviewError_.empty()) {
            const bool graphDraft=previewFactors.has("studioGraphDraft")&&previewName.rfind("Shader Graph / ",0)==0;
            const Json& previewGraph=previewFactors["studioGraphDraft"];
            if(graphDraft&&previewGraph.getStr("domain")=="particle"){
                auto particles=std::make_unique<GraphParticlePreview>(*device_);
                if(!particles->system.init(device_.get()))graphPreviewError_="Cannot initialize particle renderer";
                else {
                    particles->system.setTextureLoader([preview=particles.get(),root=projectRoot](const std::string& path,fx::FxTexture& out){
                        auto found=preview->textures.find(path);
                        if(found==preview->textures.end()){
                            std::string bytes;std::vector<uint8_t> rgba;
                            gfx::TextureDesc texture;
                            if(!readPreviewAsset(root,path,bytes)||!render::decodeImage(bytes,texture.width,texture.height,rgba))return false;
                            texture.data=rgba.data();texture.repeat=true;texture.mipmaps=true;texture.label=path.c_str();
                            auto handle=preview->device.createTexture(texture);
                            if(!handle.valid())return false;
                            found=preview->textures.emplace(path,handle).first;
                        }
                        out={found->second,0,0,1,1};return true;
                    });
                    fx::EmitterDef emitter;emitter.shaderGraph=previewGraph;emitter.loop=true;
                    emitter.rate={20,20};emitter.duration=2;emitter.life={1.2f,1.2f};
                    emitter.speed={0.5f,1};emitter.size={0.25f,0.35f};
                    fx::EffectDef effect;effect.emitters.push_back(std::move(emitter));
                    const int id=particles->system.define("shader-preview",effect);
                    if(id<0)graphPreviewError_=particles->system.errors().empty()?"Invalid particle graph":particles->system.errors().back();
                    else {
                        particles->system.play(id,{0,0,0});
                        graphParticles_=std::move(particles);
                    }
                }
            }
            if(graphDraft&&previewGraph.getStr("domain")=="postfx"){
                auto chain=std::make_unique<render::Prisma>();
                try{
                    if(!chain->init(*device_))throw std::runtime_error("Cannot initialize the post effect GPU preview");
                    chain->setQuality(render::PrismaQuality::High);
                    chain->setImageLoader([root=projectRoot](const std::string& path,int& w,int& h,std::vector<uint8_t>& rgba){
                        std::string bytes;return readPreviewAsset(root,path,bytes)&&render::decodeImage(bytes,w,h,rgba);
                    });
                    Json effect=Json::object();effect.set("type",Json::string("shaderGraph"));effect.set("graph",previewGraph);
                    Json effects=Json::array();effects.push(std::move(effect));
                    Json settings=Json::object();settings.set("effects",std::move(effects));
                    chain->configure(settings,Json::object());
                    graphPreviewPost_=std::move(chain);
                }catch(const std::exception& e){chain->shutdown();graphPreviewError_=e.what();}
            }
            const bool materialGraph=graphDraft&&previewGraph.getStr("domain")=="material";
            if (!graphParticles_ && (!materialSphere_ || materialGraph || materialSphere_->materials.at(0).shaderGraph)) {
                render::MeshBuilder sphere;
                sphere.color(render::rgba(255,255,255));
                sphere.sphere({0,0,0},1.0f,32,48);
                render::Material base=studioPbrPreviewMaterial(previewFactors,previewName);
                auto next=render::modelFromParts("studio/pbr-preview",{{"PBR sphere",&sphere,base,render::mat4{}}});
                try {
                    if(!next)throw std::runtime_error("Material preview geometry missing");
                    if(materialGraph){
                        Json ref=Json::object();ref.set("shaderGraph",previewFactors["studioGraphDraft"]);
                        view::AppearanceAuthoringService::applyMaterial(next->materials.at(0),Json::object(),ref,
                            [&](const std::string& path){
                                std::string bytes;
                                render::Image image;image.name=path;
                                if(!readPreviewAsset(projectRoot,path,bytes)||!render::decodeImage(bytes,image))
                                    throw std::runtime_error("Cannot load graph texture "+path);
                                next->images.push_back(std::move(image));
                                return int(next->images.size()-1);
                            },[](const std::string& message){throw std::runtime_error(message);});
                    }
                    next->batching.autoLod=false;
                    render::prepareModelUpload(*next);
                    if(!render::uploadModel(*device_,*next))throw std::runtime_error("Material preview GPU upload failed");
                    if(materialSphere_){
                        if(!device_->waitForResourceIdle())throw std::runtime_error("Material preview GPU busy");
                        releasePreviewModel(*materialSphere_);
                    }
                    materialSphere_=std::move(next);
                } catch(const std::exception& e){
                    if(next)releasePreviewModel(*next);
                    graphPreviewError_=e.what();
                }
            }else if(!graphParticles_){
                materialSphere_->materials.at(0)=studioPbrPreviewMaterial(previewFactors,previewName);
                materialSphere_->markMaterialChanged();
            }
        }
    }
    if (materialMode_ != materialRequested) {
        materialMode_=materialRequested;
        lightingRevision_=std::numeric_limits<uint64_t>::max();
        cameraInitialized_=false;
    }
    if (materialMode_) {
        lightingStatus_=graphPreviewError_.empty()
            ? (previewFactors.has("studioGraphDraft")?previewName+" · GPU "+previewFactors["studioGraphDraft"].getStr("domain")+" preview":"Material preview · GI: off · Direct: standard")
            : graphPreviewError_;
        if (!cameraInitialized_) {
            camera_.target={0,0,0};
            camera_.distance=4.3f;
            camera_.pitch=.5f;
            camera_.yaw=.25f;
            camera_.ortho=false;
            tween_.active=false;
            cameraInitialized_=true;
        }
        device_->beginFrame(framebufferWidth,framebufferHeight);
        StudioFrameScope scope{*device_};
        if(graphParticles_&&graphPreviewError_.empty()){
            const auto now=std::chrono::steady_clock::now();
            auto& particles=*graphParticles_;
            float dt=particles.lastFrame==std::chrono::steady_clock::time_point{}?0.1f:
                std::min(0.1f,std::chrono::duration<float>(now-particles.lastFrame).count());
            particles.lastFrame=now;
            render::Camera cam;cam.target={0,.3f,0};cam.distance=3;cam.minDistance=.1f;
            const auto projection=cam.proj(float(framebufferWidth)/framebufferHeight,device_->info().clipDepthZeroToOne);
            particles.system.setView(cam,projection*cam.view(),framebufferWidth,framebufferHeight);
            particles.system.update(dt);
            gfx::PassDesc pass;pass.clearColor[0]=.07f;pass.clearColor[1]=.09f;pass.clearColor[2]=.135f;
            device_->beginPass(pass);device_->setViewport({0,0,framebufferWidth,framebufferHeight});
            particles.system.render({});
            if(!particles.system.lastError().empty())lightingStatus_=particles.system.lastError();
            if(!particles.system.errors().empty())lightingStatus_=particles.system.errors().back();
            return;
        }
        renderer_.beginScene(viewCamera(),framebufferWidth,framebufferHeight);
        renderer_.clearDynamicGiOverlay();
        renderer_.lighting=render::Lighting{};
        renderer_.lighting.sunIntensity=1.2f;
        renderer_.lighting.skyColor={.42f,.51f,.66f};
        renderer_.lighting.groundColor={.27f,.27f,.31f};
        renderer_.lighting.fogDensity=0;
        renderer_.lights.add({1.5f,2.4f,1.6f},6,{1,.9f,.78f},2.5f);
        if (materialSphere_ && graphPreviewError_.empty()) {
            render::ModelDraw sample;
            sample.model=materialSphere_.get();
            renderer_.drawModel(sample);
        }
        renderer_.drawMesh(cube_,render::mat4::translate({0,-1.14f,0})*
            render::mat4::scale({4,.18f,4}),{.22f,.25f,.3f,1},render::kDrawNoCastShadow);
        gfx::PassDesc previewPass;
        previewPass.clearColor[0]=.07f;
        previewPass.clearColor[1]=.09f;
        previewPass.clearColor[2]=.135f;
        render::PostProcess* previewPost=graphPreviewPost_?static_cast<render::PostProcess*>(graphPreviewPost_.get()):&antialiasing_;
        renderer_.render(previewPass,previewPost);
        renderer_.resolvePostFx();
        if(graphPreviewPost_&&!graphPreviewPost_->lastError().empty())lightingStatus_=graphPreviewPost_->lastError();

        return;
    }
    if(doc.data.planet()){
        framePlanet(framebufferWidth,framebufferHeight,doc,selectedId,projectPath,rules);return;
    }
    if(planetMode_){
        if(!device_->waitForResourceIdle())return;
        clearLivePlanetTerrain();
        clearPlanetForestModels();
        planetMode_=false;planetPreview_.reset();planetMapPath_.clear();cameraInitialized_=false;sceneCacheValid_=false;
        lightingRevision_=std::numeric_limits<uint64_t>::max();clearTransformPreview();
    }
    const auto& map = doc.data;
    if(caveGlobeRequested()){
        if(projectPath!=globeProject_||doc.path!=globeMap_||!map.planet()||map.findBody(globeBody_)<0)
            setCaveGlobe({},projectPath,doc.path);
        else {renderCaveGlobe(framebufferWidth,framebufferHeight,doc);return;}
    }
    cavernsPreview_.syncDocument(*device_,doc,projectPath);
    std::optional<terraweave::caverns::CavePlanetAnchor> selectedChart;
    std::string chartIdentity;
    if(map.planet())try{
        const auto id=editor::planet_caves::activeRegionId(map);
        if(!id.empty()){
            selectedChart=editor::planet_caves::readAnchor(map);
            chartIdentity=id+":"+terraweave::caverns::cavePlanetAnchorToJson(*selectedChart).dump(-1);
        }
    }catch(const std::exception&){selectedChart.reset();chartIdentity.clear();}
    const bool planetaryChart=selectedChart.has_value();
    const bool chartChanged=chartIdentity!=planetChartIdentity_;
    if(chartChanged){planetChartIdentity_=chartIdentity;cameraInitialized_=false;sceneCacheValid_=false;terrainRevision_=std::numeric_limits<uint64_t>::max();lightingRevision_=terrainRevision_;}
    const auto now=std::chrono::steady_clock::now();
    if(lastSeenRevision_!=doc.revision){lastSeenRevision_=doc.revision;lastDocChange_=now;}
    {
        // Document-derived scene: rebuilt on change, files re-checked once a second
        // (hot reload), every frame only while models are still streaming in.
        const SceneCacheKey key{&doc,doc.revision,fpsProject,levelHeight,projectPath,&rules};
        const bool documentChanged=!sceneCacheValid_ || !(key==sceneKey_);
        if(documentChanged || modelsPending_ || now>=nextFileCheck_) {
            // The periodic pass also catches document changes that did not bump the
            // revision (direct loads) and rules edits, within a second.
            {
                proxies_=studioDocumentProxies(doc,fpsProject,levelHeight);
                showTerrain_=rules["terrain"].getBool("render",true)&&map.extra.getStr("terrainPresentation","legacy")!="entities";
                lightingSettings_=renderLightingSettings(doc.data.extra["renderLighting"]);
                useLighting_=lightingSettings_.getStr("indirect")!="off" || lightingSettings_.getStr("direct")!="legacy";
                activeLights_=studioAuthoredLights(doc,levelHeight,std::numeric_limits<size_t>::max());
                day_=previewDocumentDay(doc);
                nereid_.configure(fpsProject?Json::object():editor::authoredWaterDocument(doc));
            }
            sceneCacheValid_=false; // stays invalid if the sync below throws
            syncDocumentModels(doc,projectPath,rules,proxies_);
            rebuildProxyDraws(doc);
            sceneKey_=key;sceneCacheValid_=true;
            nextFileCheck_=now+std::chrono::seconds(1);
        }
    }
    const auto& proxies=proxies_;
    const bool showTerrain=planetaryChart||showTerrain_;
    const bool sparseTerrain=!map.planet()&&map.extra["terraweaveSparse"].isObject();
    auto retireSparse=[&](auto& model){if(model)renderer_.invalidateModelCachesFor(model.get());sparseTerrainRetirement_.retireModel(*device_,model);};
    (void)sparseTerrainRetirement_.collectWithoutIdle(*device_);
    if((!sparseTerrain||sparseTerrainProject_!=projectPath)&&(sparseTerrainPreview_.residentCount()||sparseTerrainPreview_.authoredCount()))sparseTerrainPreview_.release(retireSparse);
    sparseTerrainProject_=projectPath;
    snapPivots_.clear();
    // Same pivot as the Move gizmo: bottom centre (lights: centre).
    for (const auto& proxy:proxies) if (!hidden_.count(proxy.id))
        snapPivots_.push_back({proxy.id,proxy.light?proxy.center:float3{proxy.center.x,proxy.boundsMin.y,proxy.center.z}});

    const bool caveAuthored=planetaryChart||(!map.extra["terraweaveCaverns"].isNull()&&!map.planet());
    (void)terrainRetirement_.collectWithoutIdle(*device_);
    const bool planetDocumentChanged=planetaryChart&&
        (planetTerrainDoc_!=&doc||planetTerrainProject_!=projectPath);
    if (terrainRevision_ != doc.revision || terrainLevelHeight_ != levelHeight ||
        terrainWasShown_!=showTerrain || terrainCavernsAuthored_!=caveAuthored||planetDocumentChanged) {
        // Preserve one exact CPU triangle source for both render paths.
        // Never debounce the PBR receiver across a changed cut epoch.
        if (terrainRevision_==doc.revision || caveAuthored || terrainCavernsAuthored_)
            lightingRevision_=std::numeric_limits<uint64_t>::max();
        if(terrainRetirement_.pending()>=64&&!terrainRetirement_.completeForShutdown(*device_))
            throw std::runtime_error("Studio terrain retirement budget exhausted");
        MeshBuilder source;
        TerrainPreviewStats cutStats;
        Mesh replacement{};
        if(showTerrain){
            cutStats=planetaryChart?buildPlanetChartPreview(map,source,doc.revision):buildTerrainPreview(map,source,levelHeight);
            if(!source.empty()){
                replacement=source.upload(*device_);
                if(!replacement.vb.valid()||!replacement.ib.valid()){
                    if(replacement.vb.valid())device_->destroyBuffer(replacement.vb);
                    if(replacement.ib.valid())device_->destroyBuffer(replacement.ib);
                    throw std::runtime_error("Studio terrain upload failed");
                }
            }
        }
        terrainRetirement_.retireBuffer(*device_,terrain_.vb);
        terrainRetirement_.retireBuffer(*device_,terrain_.ib);
        terrain_=replacement;
        terrainCpu_=std::move(source);
        terrainCutStats_=std::move(cutStats);
        terrainLevelHeight_=levelHeight;
        terrainWasShown_=showTerrain;terrainCavernsAuthored_=caveAuthored;
        terrainRevision_=doc.revision;
        planetTerrainDoc_=planetaryChart?&doc:nullptr;
        planetTerrainProject_=planetaryChart?projectPath:std::string{};
    }
    // Stage the replacement using exactly the refined CPU plan used by the
    // collar owner. Do not drop the solid baseline on upload/refusal.
    auto chartPlan=planetaryChart?cavernsPreview_.planetRimPlan():nullptr;
    if(chartPlan!=planetCutPlan_){
        terrainRetirement_.retireBuffer(*device_,planetCutTerrain_.vb);
        terrainRetirement_.retireBuffer(*device_,planetCutTerrain_.ib);
        planetCutTerrain_={};planetCutPlan_=chartPlan;planetCutDrawn_=false;
        planetCutTriangles_=0;planetCutError_.clear();
        if(chartPlan)try{
            if(terrainRetirement_.pending()>=64)throw std::runtime_error("Caverns chart surface retirement pressure");
            MeshBuilder cut;
            planetCutTriangles_=buildPlanetChartCutPreview(*chartPlan,cut);
            if(cut.empty())throw std::runtime_error("Caverns empty chart cut mesh");
            planetCutTerrain_=cut.upload(*device_);
            if(!planetCutTerrain_.vb.valid()||!planetCutTerrain_.ib.valid())
                throw std::runtime_error("Caverns chart cut upload failed");
        }catch(const std::exception& e){
            terrainRetirement_.retireBuffer(*device_,planetCutTerrain_.vb);
            terrainRetirement_.retireBuffer(*device_,planetCutTerrain_.ib);
            planetCutTerrain_={};planetCutError_=e.what();
        }
        lastFrameSignature_=0;
    }
    const Json& settings=lightingSettings_;
    const bool useLighting=useLighting_&&!planetaryChart;
    const auto& activeLights=activeLights_;
    Lighting environment;
    const auto& day=day_;
    const Json& lightingSource=doc.data.extra["lighting"];
    if(lightingSource.isObject()) {
        environment.sunIntensity=day.sun;
        environment.skyColor={.5f*day.ambient,.58f*day.ambient,.7f*day.ambient};
    }
    const bool lightingForced=lightingRevision_==std::numeric_limits<uint64_t>::max();
    const bool lightingDue=lightingForced || continuousRedraw_ || now-lastDocChange_>=std::chrono::milliseconds(200) ||
                           now-lastLightingBuild_>=std::chrono::milliseconds(300);
    if(lightingRevision_!=doc.revision && lightingDue) {
        lastLightingBuild_=now;
        if(!lightingRuntime_.shutdown(*device_,renderer_) || !device_->waitForResourceIdle())
            throw std::runtime_error("Studio lighting retirement failed");
        if(lightingModel_) {
            renderer_.invalidateModelCachesFor(lightingModel_.get());
            releaseModel(*device_,*lightingModel_);lightingModel_.reset();
        }
        lightingRevision_=doc.revision;
        lightingStatus_="Preview geometry · GI: off · Direct: standard";
        if(!planetaryChart&&(useLighting || fpsProject)) {
            std::set<std::string> modelIds;
            for(const auto& instance:modelInstances_)modelIds.insert(instance.id);
            lightingModel_=studioLightingPreview(doc,fpsProject,levelHeight,modelIds,
                (terrainCavernsAuthored_||sparseTerrain)?&terrainCpu_:nullptr);
            StaticShadowGeometry geometry;
            if(lightingModel_) {
                prepareModelUpload(*lightingModel_);
                geometry=giModelGeometry(*lightingModel_);
                if(!uploadModel(*device_,*lightingModel_,true))throw std::runtime_error("Studio lighting upload failed");
            }
            const float3 center=lightingModel_?float3{(lightingModel_->boundsMin.x+lightingModel_->boundsMax.x)*.5f,
                lightingModel_->boundsMin.y+.25f,(lightingModel_->boundsMin.z+lightingModel_->boundsMax.z)*.5f}:float3{map.w*.5f,.25f,map.h*.5f};
            try {
                // Reuse the shared immutable opaque-geometry snapshot for real
                // assets. Reject an oversized snapshot explicitly; never crop it
                // to a misleading subset or substitute proxy geometry silently.
                if(useLighting)for(const auto& instance:modelInstances_) {
                    size_t faces=0;
                    for(const auto& node:instance.model->nodes)if(node.mesh>=0)
                        for(const auto& p:instance.model->meshes[size_t(node.mesh)].primitives) {
                            const auto& material=instance.model->materials[size_t(p.material)];
                            if(material.castShadow&&material.alpha==AlphaMode::Opaque&&!material.transparent)faces+=p.indexCount/3;
                        }
                    if(faces>1000000 || geometry.triangles.size()>1000000-faces)throw std::runtime_error("Studio GI snapshot exceeds one million opaque triangles");
                    auto source=giModelGeometry(*instance.model,instance.world);
                    const auto offset=uint32_t(geometry.albedos.size());
                    geometry.albedos.insert(geometry.albedos.end(),source.albedos.begin(),source.albedos.end());
                    for(auto triangle:source.triangles){triangle.surface+=offset;geometry.triangles.push_back(triangle);}
                }
                if(useLighting)lightingRuntime_.initialize(*device_,renderer_,post_,settings,geometry,activeLights,center,environment);
            } catch(const std::exception& e) {
                const std::string reason=e.what();
                if(!lightingRuntime_.shutdown(*device_,renderer_))throw;
                lightingStatus_="Lighting rejected: "+reason+" · standard preview retained";
            }
        }
    }
    if (!cameraInitialized_ || lastMapWidth_ != map.w || lastMapHeight_ != map.h) {
        camera_.target = {map.w * .5f, 0.0f, map.h * .5f};
        camera_.distance = std::clamp(float(std::max(map.w, map.h)) * 1.4f, 16.0f, 100000.0f);
        if(planetaryChart){
            const auto cap=selectedChart->halfExtentCm;
            camera_.target={0,0,0};camera_.distance=std::clamp(float(cap)*.035f,3.f,100000.f);
        }else if(fpsProject && lightingModel_) {
            const auto lo=lightingModel_->boundsMin,hi=lightingModel_->boundsMax;
            camera_.target=(lo+hi)*.5f;
            camera_.distance=std::clamp(std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z})*1.4f,16.f,100000.f);
        }
        tween_.active = false;
        cameraInitialized_ = true;
        lastMapWidth_ = map.w;
        lastMapHeight_ = map.h;
    }
    stepTween();
    const auto view=viewCamera();
    spectraSources_.synchronize(*device_,doc,projectRoot,hidden_,levelHeight,showTerrain,&nereid_.scene());
    {
        // Redraw only when something visible changed (like an editor viewport with
        // Realtime off), plus settle frames for temporal effects and a 1 s heartbeat.
        constexpr int kSettleFrames=30;
        const uint64_t signature=frameSignature(framebufferWidth,framebufferHeight,doc,fpsProject,selectedId);
        // Pending Assets thumbnails render inside a drawn frame (one per frame).
        const bool animated=continuousRedraw_ || tween_.active || modelsPending_ || cavernsPreview_.needsFrame() || sparseTerrain || !thumbJobs_.empty() ||
            lightingRevision_!=doc.revision || nereid_.configuredSurfaces()>0 ||
            (wind_.enabled && riggedInstances_>0) || day.clouds>0 || lodDiagnostics_ ||
            (lightingRuntime_.indirect && !lightingRuntime_.indirect->converged) ||
            !thumbJobs_.empty() || spectraPreview_->needsFrame() || spectraSources_.animated(); // native FX playback forces draw
        if(animated || signature!=lastFrameSignature_) settleFrames_=kSettleFrames;
        lastFrameSignature_=signature;
        if(settleFrames_>0) --settleFrames_;
        else if(now-lastDrawn_<std::chrono::seconds(1)) { ++framesSkipped_; return; }
        lastDrawn_=now; ++framesDrawn_;
    }
    // proxies/syncDocumentModels ran above: lighting needs modelInstances_ first.
    device_->beginFrame(framebufferWidth, framebufferHeight);
    StudioFrameScope scope{*device_};
    // Stage both preview owners before draw submission. Share the exact
    // surface plan pointer; neither half is drawn before its counterpart.
    bool sparsePrepared=false;
    if(sparseTerrain){
        try{
            if(sparseTerrainRetirement_.pending()>=128)throw std::runtime_error("Sparse terrain GPU retirement pressure");
            const auto cm=[](float v){return int64_t(std::llround(std::clamp(double(v),-9990000.,9990000.)*100.));};
            sparseTerrainPreview_.prepare(*device_,doc,cm(camera_.target.x),cm(camera_.target.z),
                retireSparse,cavernsPreview_.visible());
            sparsePrepared=true;sparseTerrainError_.clear();
        }catch(const std::exception& e){sparseTerrainError_=e.what();}
    }
    auto surfaceGroup=sparsePrepared?sparseTerrainPreview_.surfaceGroup():nullptr;
    const auto chartGroup=planetaryChart&&planetCutTerrain_.vb.valid()&&planetCutTerrain_.ib.valid()
        ?planetCutPlan_:nullptr;
    cavernsPreview_.syncRimGroup(planetaryChart?chartGroup:surfaceGroup);
    cavernsPreview_.beforeRender(*device_,planetaryChart?view.eye():
        brushVisible_&&brushVolume_?brushCenter_:camera_.target);
    if(surfaceGroup&&!surfaceGroup->rims.empty()&&cavernsPreview_.rimRejected(surfaceGroup)){
        const auto why=cavernsPreview_.rimError();
        sparseTerrainPreview_.rejectSurfaceGroup(retireSparse,why.empty()?"Caverns matching volume unavailable":why);
        surfaceGroup.reset();sparsePrepared=false;
        cavernsPreview_.syncRimGroup({});
    }
    const bool pairedRimReady=cavernsPreview_.rimReady(surfaceGroup)&&
        (!surfaceGroup||surfaceGroup->rims.empty()||sparseTerrainPreview_.surfaceGroupUploaded());
    const bool pairedPlanetReady=chartGroup&&cavernsPreview_.rimReady(chartGroup);
    planetCutDrawn_=pairedPlanetReady&&!cavernsPreview_.isolate();
    struct SpectraPoolFrameGuard { StudioSpectraSources& sources; ~SpectraPoolFrameGuard(){sources.finish();} } spectraPoolScope{spectraSources_};
    if(spectraPreview_->active()) {
        // Device-safe point: GPU simulation update before any encoder/pass.
        const float aspect=float(framebufferWidth)/float(framebufferHeight);
        const auto viewProj=view.proj(aspect,device_->info().clipDepthZeroToOne)*view.view();
        spectraPreview_->prepare(fx::SpectraFxView::fromCamera(
            view,viewProj,framebufferWidth,framebufferHeight),camera_.target);
    }
    {
        const float aspect=float(framebufferWidth)/float(framebufferHeight);
        const auto viewProj=view.proj(aspect,device_->info().clipDepthZeroToOne)*view.view();
        spectraSources_.prepare(fx::SpectraFxView::fromCamera(view,viewProj,framebufferWidth,framebufferHeight),1.f/60.f);
    }
    nereid_.prepare(*device_,view.eye());
    const double opticalNow=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    nereid_.updateUnderwater(post_,underwaterFrameTime_>0?float(opticalNow-underwaterFrameTime_):1.f/60.f);
    underwaterFrameTime_=opticalNow;
    renderThumbnail(); // at most one Assets thumbnail, offscreen, before the scene pass
    renderer_.beginScene(view, framebufferWidth, framebufferHeight);
    renderer_.setSceneSelectionOutline(true); // editor-only depth-tested silhouette
    {
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-windClock_).count();
        renderer_.setSceneWind(wind_.sceneWind(seconds));
        renderer_.setWindRigDebugView(wind_.debug==render::wind::WindDebugView::Skeleton ?
            render::wind::WindDebugView::Off : wind_.debug);
    }
    renderer_.lighting=environment;
    if (lightingSource.isObject()) {
        renderer_.lights.setNightGlow(day.night*1.5f);
        renderer_.lights.setClouds(day.clouds,.6f,16.f,0,0);
    }
    if(!planetaryChart&&(!lightingRuntime_.direct || !lightingRuntime_.direct->enabled))
        for(const auto& light:activeLights)renderer_.lights.add(light);
    lightingRuntime_.update(*device_,renderer_,camera_.target);
    if(lightingRuntime_.indirect || lightingRuntime_.direct)lightingStatus_=lightingRuntime_.status();

    // Real map dimensions and positions from the active native Workspace.
    // Temporary primitive preview (NOT legacy editor terrain, animation or materials).
    const float w = float(std::max(map.w, 1));
    const float h = float(std::max(map.h, 1));
    if(lightingModel_&&!cavernsPreview_.isolate()) {
        // Shared lit preview geometry (terrain + document proxies) owned by the lighting runtime.
        ModelDraw draw;draw.model=lightingModel_.get();renderer_.drawModel(draw);
    }
    if(showTerrain&&!cavernsPreview_.isolate()){
    if(lightingModel_) {
        // Terrain is already part of the lit preview model.
    } else if (pairedPlanetReady) {
        renderer_.drawMesh(planetCutTerrain_,mat4{}, {1,1,1,1},kDrawNoCastShadow);
    } else if (terrain_.vb.valid() && terrain_.ib.valid()) {
        renderer_.drawMesh(terrain_, mat4{}, {1,1,1,1}, kDrawNoCastShadow);
    } else if(!terrainCutStats_.handled&&!terrainCavernsAuthored_) {
        // Unsupported planet maps or empty terrain: basic plane only.
        renderer_.drawMesh(cube_,
                           mat4::translate({w*.5f,-0.08f,h*.5f}) * mat4::scale({w,.12f,h}),
                           {0.18f,0.26f,0.33f,1}, kDrawNoCastShadow);
    }
    const int xStep = std::max(1, map.w / 48);
    const int zStep = std::max(1, map.h / 48);
    for (int x=0; !planetaryChart && !fpsProject && x<=map.w; x+=xStep)
        renderer_.line({float(x),.01f,0},{float(x),.01f,h},rgba(55,84,109,120));
    for (int z=0; !planetaryChart && !fpsProject && z<=map.h; z+=zStep)
        renderer_.line({0,.01f,float(z)},{w,.01f,float(z)},rgba(55,84,109,120));

    } // external/Terra surface suppresses the default plane and grid

    selectedId_=selectedId;
    selectedPivotValid_=false;
    selectedBoxValid_=false;
    if(!planetaryChart)for(auto& cached:proxyDraws_) {
        const auto& entity=proxies[cached.proxy];
        if(hidden_.count(entity.id))continue;
        const bool dragged=!drag_.id.empty() && entity.id==drag_.id;
        const mat4 preview=dragged?dragPreview():mat4{};
        const float3 offset=dragged&&drag_.tool==TransformTool::Move?drag_.moveOffset:float3{};
        if(entity.id==selectedId) {
            selectedPivot_=entity.light?entity.center:float3{entity.center.x,entity.boundsMin.y,entity.center.z};
            selectedPivotValid_=true;selectedIsLight_=entity.light;
            selectedAxesValid_=false;
        }
        if(cached.water || cached.hasModel)continue;
        // Authored entities use their full Transform (3D rotation, per-axis scale).
        mat4 world=cached.world;
        const mat4& frame=cached.frame;
        if(entity.id==selectedId && !entity.light) {
            for(int a=0;a<3;++a){const float3 axis=frame.transformDir({a==0?1.f:0.f,a==1?1.f:0.f,a==2?1.f:0.f});
                const float l=std::sqrt(dot(axis,axis));selectedAxes_[a]=l>1e-6f?axis*(1.0f/l):float3{a==0?1.f:0.f,a==1?1.f:0.f,a==2?1.f:0.f};}
            selectedAxesValid_=true;
        }
        world=preview*world;
        if(entity.id==selectedId && !entity.light) captureSelectedBox(world,{-.5f,-.5f,-.5f},{.5f,.5f,.5f});
        // Mesh Renderer with a built-in primitive: sphere, or an assigned material,
        // draws a PBR primitive Model; plain default cubes keep the cheap proxy path.
        if(!lightingModel_ && cached.primitive) {
            render::ModelDraw draw;draw.model=cached.primitive;draw.world=world;draw.materials=&cached.materials;
            if(entity.id==selectedId||selection_.count(entity.id))draw.outline={.25f,.62f,1.0f,.035f};
            else if(entity.id==hover_)draw.outline={.62f,.8f,1.0f,.02f};
            renderer_.drawModel(draw);
            continue;
        }
        if(!lightingModel_ && !entity.light) {
            if(entity.id==selectedId||selection_.count(entity.id)) renderer_.drawSelectionMesh(cube_,world,entity.color,{.25f,.62f,1.0f,1.0f});
            else if(entity.id==hover_) renderer_.drawSelectionMesh(cube_,world,entity.color,{.62f,.8f,1.0f,.55f});
            else renderer_.drawMesh(cube_,world,entity.color,kDrawNoCastShadow);
        } else if(entity.id==selectedId && entity.light) {
            const auto lo=entity.boundsMin+offset,hi=entity.boundsMax+offset;
            const float3 p[8]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},
                {hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,lo.z},
                {hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
            constexpr int edges[12][2]={{0,1},{1,2},{2,3},{3,0},
                {4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for(const auto& edge:edges)
                renderer_.line(p[edge[0]],p[edge[1]],rgba(64,158,255));
        }

    }
    if(graphPresentation_)graphPresentation_->merged.clear();
    riggedInstances_=0; riggedParts_=0;
    const double windSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-windClock_).count();
    if(!planetaryChart)for(const auto& instance:modelInstances_) {
        if(hidden_.count(instance.id))continue;
        render::ModelDraw draw;
        draw.model=instance.model;
        draw.world=instance.world;
        if(!drag_.id.empty() && instance.id==drag_.id) draw.world=dragPreview()*instance.world;
        if(auto custom=instanceMaterials_.find(instance.id);custom!=instanceMaterials_.end())
            draw.materials=&custom->second.materials;
        if(graphPresentation_&&instance.model)draw.materials=graphPresentation_->materials(*instance.model,instance.id,draw.materials);
        if(auto team=instanceTeams_.find(instance.id);team!=instanceTeams_.end())draw.team=team->second;
        if(instance.id!=selectedId && selection_.count(instance.id))draw.outline={.25f,.62f,1.0f,.035f};
        else if(instance.id!=selectedId && instance.id==hover_)draw.outline={.62f,.8f,1.0f,.02f};
        if(instance.id==selectedId) {
            draw.outline={.25f,.62f,1.0f,.035f};
            if(instance.model)captureSelectedBox(draw.world,instance.model->boundsMin,instance.model->boundsMax);
        }
        renderer_.drawModel(draw);
        // Skeleton debug view: parts as lines (depth colour, mask brightness),
        // displaced by the same reference math as the GPU.
        auto cachedRig=windRigs_.find(instance.model);
        if(cachedRig==windRigs_.end())
            cachedRig=windRigs_.emplace(instance.model,render::wind::findWindRig(*instance.model)).first;
        if(const auto& rig=cachedRig->second) {
            ++riggedInstances_; riggedParts_+=rig->decoded.nodes.size();
            if(wind_.debug==render::wind::WindDebugView::Skeleton &&
               (selectedId.empty() || instance.id==selectedId || riggedInstances_<=32)) {
                std::vector<render::wind::WindDebugLine> lines;
                const auto params=render::wind::windRigParamsForDraw(wind_.sceneWind(windSeconds),draw.world);
                render::wind::windRigDebugLines(*rig,draw.world,params,lines,6,1024);
                for(const auto& l:lines)
                    renderer_.line({l.a.x,l.a.y,l.a.z},{l.b.x,l.b.y,l.b.z},l.rgba);
            }
        }
    }
    if(!planetaryChart)nereid_.draw(renderer_);
    // Editor gizmos for authored lights; actual light contribution is passed through
    // Renderer3D::lights, not faked with an overlay screenshot.
    if(!planetaryChart)for(const auto& light:activeLights) {
        const auto at=light.pos;
        const float extent=std::clamp(light.radius*.08f,.2f,2.f);
        const uint32_t color=rgba(245,200,100);
        renderer_.line({at.x-extent,at.y,at.z},{at.x+extent,at.y,at.z},color);
        renderer_.line({at.x,at.y-extent,at.z},{at.x,at.y+extent,at.z},color);
        renderer_.line({at.x,at.y,at.z-extent},{at.x,at.y,at.z+extent},color);
    }
    if(gizmosVisible_)drawSelectionBox(view,framebufferWidth,framebufferHeight);
    drawBrushCursor(view,framebufferWidth,framebufferHeight);
    drawTransformGizmo(view,framebufferWidth,framebufferHeight);
    if(orientationGizmo_&&gizmosVisible_)drawOrientationGizmo(view,framebufferWidth,framebufferHeight);
    if(marquee_.on) {
        const float W=float(framebufferWidth),H=float(framebufferHeight);
        Overlay2D o{renderer_,view,W,H,view.ortho?0.5f:view.zNear*1.4f};
        const float x0=std::min(marquee_.x0,marquee_.x1)*W,x1=std::max(marquee_.x0,marquee_.x1)*W;
        const float y0=std::min(marquee_.y0,marquee_.y1)*H,y1=std::max(marquee_.y0,marquee_.y1)*H;
        o.tri(x0,y0,x1,y0,x1,y1,rgba(130,181,255,40));o.tri(x0,y0,x1,y1,x0,y1,rgba(130,181,255,40));
        const float w=std::max(1.0f,std::min(W,H)*0.0015f);
        o.segment(x0,y0,x1,y0,w,rgba(130,181,255,220));o.segment(x1,y0,x1,y1,w,rgba(130,181,255,220));
        o.segment(x1,y1,x0,y1,w,rgba(130,181,255,220));o.segment(x0,y1,x0,y0,w,rgba(130,181,255,220));
    }
    if(sparseTerrain&&sparsePrepared&&!cavernsPreview_.isolate())
        sparseTerrainPreview_.submit(renderer_,pairedRimReady,true);
    // The exact collar may only draw with its matching surface (or explicit
    // isolate mode). During a refused/pending pair the host hides old collars.
    if(pairedRimReady&&(!chartGroup||pairedPlanetReady||cavernsPreview_.rimRejected(chartGroup)))
        cavernsPreview_.draw(renderer_);
    gfx::PassDesc pass;
    pass.clearColor[0] = 0.038f; pass.clearColor[1] = 0.064f;
    pass.clearColor[2] = 0.098f; pass.clearColor[3] = 1.0f;
    if(skyShot_) {
        // Splash art: real PhysicalSky (atmosphere, clouds, god rays) lit by the requested sun.
        PhysicalSkySettings sky;sky.enabled=true;sky.clouds=true;sky.godRays=true;sky.coverage=skyShot_->coverage;
        post_.setPhysicalSky(sky);post_.setPhysicalSkyTime(30.0);
        const float e=skyShot_->elevationDeg*0.0174533f,a=skyShot_->azimuthDeg*0.0174533f;
        renderer_.lighting.sunDir={std::cos(e)*std::sin(a),std::sin(e),std::cos(e)*std::cos(a)};
        renderer_.lighting.sunColor=skyShot_->sunColor;renderer_.lighting.sunIntensity=1.3f;
    }
    if(spectraPreview_->hasHdrContribution()||spectraSources_.hasVolumes()||spectraSources_.hasVolumetricDepth()){
        post_.setSceneHdrContributor([this](gfx::Device& d,const render::PostFrame& f,gfx::TextureHandle c,gfx::TextureHandle z){
            auto scene=spectraSources_.composeVolumes(d,f,c,z);
            auto preview=spectraPreview_->drawVolume(d,f,scene.valid()?scene:c,z);
            auto base=preview.valid()?preview:scene;
            auto particles=spectraSources_.composeVolumetricDepth(d,f,base.valid()?base:c,z);
            return particles.valid()?particles:base;
        });
    }
    renderer_.render(pass,useLighting?lightingRuntime_.pipeline(post_):static_cast<PostProcess*>(&post_));
    // Renderer3D keeps the HDR scene pass open; GPU/CPU FX draw here,
    // before PRISMA resolves it and before the frame/pass are retired.
    if(spectraPreview_->active())spectraPreview_->draw();
    spectraSources_.draw();
    renderer_.resolvePrisma();
    spectraSources_.finish();
    lightingRuntime_.presented();

    if(lodDiagnostics_&&++diagnosticFrames_%120==0){
        const auto& stats=renderer_.modelStats();
        std::fprintf(stderr,"[studio-lod] instances=%zu assets=%zu cameraTriangles=%llu shadowTriangles=%u lod=%llu/%llu/%llu/%llu chains=%llu blocked=%llu shadowLimited=%llu\n",
            modelInstances_.size(),documentModels_.size(),(unsigned long long)renderer_.profile().cameraModelTriangles,renderer_.profile().triangles[0],
            (unsigned long long)stats.cameraLodNodes[0],(unsigned long long)stats.cameraLodNodes[1],(unsigned long long)stats.cameraLodNodes[2],(unsigned long long)stats.cameraLodNodes[3],
            (unsigned long long)stats.cameraLodChains,(unsigned long long)stats.cameraLodBlocked,(unsigned long long)stats.cameraShadowLimitedLod);
    }
    // StudioFrameScope ends the pass and frame, including on exceptions.
}
bool StudioScene::beginPlanetRegionDrag(const editor::MapDoc& doc,const std::string& id,float x,float y,float width,float height){
    cancelPlanetRegionDrag();
    if(!planetMode_||!planetPreview_||planetMapPath_!=doc.path||!gizmosVisible_||
       hidden_.count(id)||locked_.count(id)||doc.revision!=planetRevision_)return false;
    const auto chart=planetRegionChart(doc,*planetPreview_,id);
    if(!chart)return false;
    const auto& body=doc.data.bodies[size_t(doc.data.findBody(chart->body))];
    const auto view=viewCamera();
    const float lift=std::clamp(planetPreview_->radius*.009f,.25f,3.f);
    float best=11.f;std::optional<PlanetRegionHandle> hit;
    for(auto handle:{PlanetRegionHandle::West,PlanetRegionHandle::East,PlanetRegionHandle::North,
                     PlanetRegionHandle::South,PlanetRegionHandle::Center}){
        const auto [cx,cz]=planetRegionHandleCoordinates(chart->area,handle);
        const auto point=planetRegionHandlePosition(body,*chart,cx,cz,lift);
        float2 p;if(!point||!view.worldToScreen(*point,width,height,p))continue;
        float3 o,d;if(!view.screenRay(p.x,p.y,width,height,o,d))continue;
        const float distance=dot(*point-o,d);
        if(planetPreview_->terrainHit(o,d,visiblePlanetTerrain())+.05f<distance)continue;
        const float error=std::hypot(x-p.x,y-p.y);
        if(error<best){best=error;hit=handle;}
    }
    if(!hit)return false;
    planetRegionDrag_=PlanetRegionDrag{*chart,*hit,planetRevision_,{}};
    return true;
}
std::optional<Json> StudioScene::updatePlanetRegionDrag(const editor::MapDoc& doc,float x,float y,float width,float height,
        bool symmetric,bool snap,bool finish){
    if(!planetRegionDrag_)return {};
    auto& drag=*planetRegionDrag_;drag.candidate.reset();
    if(planetMode_&&planetPreview_&&planetMapPath_==doc.path&&planetRevision_==drag.revision&&
       doc.revision==drag.revision&&planetPreview_->bodyId==drag.chart.body&&
       selectedId_==drag.chart.owner&&!locked_.count(drag.chart.owner)&&!hidden_.count(drag.chart.owner)){
        float3 o,d;
        if(viewCamera().screenRay(x,y,width,height,o,d)){
            const float t=planetPreview_->terrainHit(o,d,visiblePlanetTerrain());
            if(std::isfinite(t)){
                const auto direction=normalize(o+d*t);
                const auto local=planetRegionCoordinates(drag.chart,
                    {StudioPlanetPreview::fixed(direction.x),StudioPlanetPreview::fixed(direction.y),StudioPlanetPreview::fixed(direction.z)});
                if(local)drag.candidate=planetRegionDragArea(drag.chart,drag.handle,*local,symmetric,snap);
            }
        }
    }
    std::optional<Json> out;
    if(finish){
        if(drag.candidate){
            const auto& a=drag.chart.area;const auto& b=*drag.candidate;
            if(a.centerXcm!=b.centerXcm||a.centerZcm!=b.centerZcm||
               a.halfWidthCm!=b.halfWidthCm||a.halfDepthCm!=b.halfDepthCm)
                out=planetRegionEditPayload(drag.chart,b,drag.revision);
        }
        cancelPlanetRegionDrag();
    }
    return out;
}
void StudioScene::drawPlanetRegionHandles(const render::Camera& view,int width,int height,const editor::MapDoc& doc){
    if(!planetPreview_||!gizmosVisible_||locked_.count(selectedId_)||hidden_.count(selectedId_))return;
    auto chart=planetRegionChart(doc,*planetPreview_,selectedId_);
    if(!chart)return;
    if(planetRegionDrag_&&planetRegionDrag_->chart.owner==selectedId_&&planetRegionDrag_->candidate)
        chart->area=*planetRegionDrag_->candidate;
    const auto& body=doc.data.bodies[size_t(doc.data.findBody(chart->body))];
    const float lift=std::clamp(planetPreview_->radius*.009f,.25f,3.f);
    Overlay2D overlay{renderer_,view,float(width),float(height),view.ortho?.5f:view.zNear*1.6f};
    const auto visible=[&](float3 p,float2& screen){
        if(!view.worldToScreen(p,float(width),float(height),screen))return false;
        float3 o,d;if(!view.screenRay(screen.x,screen.y,float(width),float(height),o,d))return false;
        return planetPreview_->terrainHit(o,d,visiblePlanetTerrain())+.05f>=dot(p-o,d);
    };
    for(auto handle:{PlanetRegionHandle::West,PlanetRegionHandle::East,PlanetRegionHandle::North,
                     PlanetRegionHandle::South,PlanetRegionHandle::Center}){
        const auto [x,z]=planetRegionHandleCoordinates(chart->area,handle);
        const auto point=planetRegionHandlePosition(body,*chart,x,z,lift);
        float2 p;if(!point||!visible(*point,p))continue;
        const bool hot=planetRegionDrag_&&planetRegionDrag_->handle==handle;
        const auto color=hot?rgba(255,210,90):rgba(120,245,170);
        const float r=handle==PlanetRegionHandle::Center?6.f:4.f;
        overlay.segment(p.x-r,p.y,p.x+r,p.y,3.f,color);
        overlay.segment(p.x,p.y-r,p.x,p.y+r,3.f,color);
    }
    if(!planetRegionDrag_||!planetRegionDrag_->candidate)return;
    const auto& a=chart->area;
    const std::array<std::pair<int64_t,int64_t>,4> corners{{
        {a.centerXcm-a.halfWidthCm,a.centerZcm-a.halfDepthCm},
        {a.centerXcm+a.halfWidthCm,a.centerZcm-a.halfDepthCm},
        {a.centerXcm+a.halfWidthCm,a.centerZcm+a.halfDepthCm},
        {a.centerXcm-a.halfWidthCm,a.centerZcm+a.halfDepthCm}}};
    for(size_t edge=0;edge<4;++edge){
        const auto [ax,az]=corners[edge];const auto [bx,bz]=corners[(edge+1)%4];
        std::optional<float2> prev;
        for(int i=0;i<=16;++i){
            const auto point=planetRegionHandlePosition(body,*chart,(ax*(16-i)+bx*i)/16,(az*(16-i)+bz*i)/16,lift);
            float2 p;if(!point||!visible(*point,p)){prev.reset();continue;}
            if(prev)overlay.segment(prev->x,prev->y,p.x,p.y,2.f,rgba(255,210,90));
            prev=p;
        }
    }
}

// With no entity selected, Move/Scale exposes authored local gravity
// field centers/radii in x-ray overlay mode (centers may be underground).
bool StudioScene::beginPlanetGravityDrag(const editor::MapDoc& doc,float x,float y,float width,float height){
    cancelPlanetGravityDrag();
    if(!planetMode_||!planetPreview_||!gizmosVisible_||!selectedId_.empty()||
       !selection_.empty()||planetMapPath_!=doc.path||planetRevision_!=doc.revision||
       (tool_!=TransformTool::Move&&tool_!=TransformTool::Scale))return false;
    const auto view=viewCamera();
    const auto normal=normalize(view.target-view.eye());
    auto right=normalize(cross(normal,float3{0,1,0}));
    if(dot(right,right)<.5f)right={1,0,0};
    float best=12.f;
    const bool resize=tool_==TransformTool::Scale;
    for(const auto& field:planetGravityHandles_){
        const auto handle=resize?field.center+right*field.radius:field.center;
        float2 screen;if(!view.worldToScreen(handle,width,height,screen))continue;
        const float error=std::hypot(x-screen.x,y-screen.y);
        if(error>=best)continue;
        float3 o,d,hit;
        if(!view.screenRay(x,y,width,height,o,d)||!rayPlane(o,d,field.center,normal,hit))continue;
        if(resize&&dot(hit-field.center,hit-field.center)<1e-6f)continue;
        best=error;planetGravityDrag_=PlanetGravityDrag{field,field,normal,hit,resize,false,planetRevision_};
    }
    return bool(planetGravityDrag_);
}
std::optional<Json> StudioScene::updatePlanetGravityDrag(const editor::MapDoc& doc,float x,float y,float width,float height,bool snap,bool finish){
    if(!planetGravityDrag_)return {};
    auto& drag=*planetGravityDrag_;drag.valid=false;drag.candidate=drag.original;
    if(planetMode_&&planetPreview_&&selectedId_.empty()&&selection_.empty()&&
       planetMapPath_==doc.path&&planetRevision_==drag.revision&&doc.revision==drag.revision&&
       planetPreview_->bodyId==drag.original.body){
        float3 o,d,hit;
        if(viewCamera().screenRay(x,y,width,height,o,d)&&rayPlane(o,d,drag.original.center,drag.normal,hit)){
            if(drag.resize){
                const auto before=drag.startHit-drag.original.center,after=hit-drag.original.center;
                const float delta=std::sqrt(dot(after,after))-std::sqrt(dot(before,before));
                float r=drag.original.radius+delta;
                if(snap)r=std::round(r);
                drag.candidate.radius=std::clamp(r,1.f,100000.f);
                drag.valid=std::isfinite(r);
            }else{
                auto p=drag.original.center+(hit-drag.startHit);
                if(snap)p={std::round(p.x),std::round(p.y),std::round(p.z)};
                drag.candidate.center=p;
                drag.valid=validPlanetGravityCenter(p);
            }
        }
    }
    std::optional<Json> result;
    if(finish){
        if(drag.valid)result=planetGravityEditPayload(doc,drag.original,drag.candidate.center,drag.candidate.radius,drag.revision);
        cancelPlanetGravityDrag();
    }
    return result;
}
void StudioScene::drawPlanetGravityHandles(const render::Camera& view,int width,int height){
    if(!planetPreview_||!gizmosVisible_||!selectedId_.empty()||!selection_.empty()||
       (tool_!=TransformTool::Move&&tool_!=TransformTool::Scale))return;
    Overlay2D overlay{renderer_,view,float(width),float(height),view.ortho?.5f:view.zNear*1.6f};
    const auto normal=normalize(view.target-view.eye());
    auto right=normalize(cross(normal,float3{0,1,0}));
    if(dot(right,right)<.5f)right={1,0,0};
    const auto up=normalize(cross(right,normal));
    for(const auto& original:planetGravityHandles_){
        const bool hot=planetGravityDrag_&&planetGravityDrag_->original.id==original.id;
        const auto& field=hot&&planetGravityDrag_->valid?planetGravityDrag_->candidate:original;
        const uint32_t color=hot?rgba(255,210,90):rgba(100,180,255);
        float2 center;if(!view.worldToScreen(field.center,float(width),float(height),center))continue;
        std::optional<float2> prev;
        for(int i=0;i<=32;++i){
            const float angle=6.28318530718f*float(i)/32.f;
            const auto p=field.center+(right*std::cos(angle)+up*std::sin(angle))*field.radius;
            float2 screen;if(!view.worldToScreen(p,float(width),float(height),screen)){prev.reset();continue;}
            if(prev)overlay.segment(prev->x,prev->y,screen.x,screen.y,1.2f,color);
            prev=screen;
        }
        float2 p;
        const auto handle=tool_==TransformTool::Scale?field.center+right*field.radius:field.center;
        if(!view.worldToScreen(handle,float(width),float(height),p))continue;
        overlay.segment(p.x-6,p.y,p.x+6,p.y,3.f,color);
        overlay.segment(p.x,p.y-6,p.x,p.y+6,3.f,color);
    }
}

void StudioScene::framePlanet(int width,int height,const editor::MapDoc& doc,const std::string& selectedId,
                              const std::string& projectPath,const Json& rules){
    const bool entering=!planetMode_;
    if(entering){
        if(!lightingRuntime_.shutdown(*device_,renderer_)||!device_->waitForResourceIdle())return;
        clearDocumentModels();sceneCacheValid_=false;cameraInitialized_=false;
        if(lightingModel_){renderer_.invalidateModelCachesFor(lightingModel_.get());releaseModel(*device_,*lightingModel_);lightingModel_.reset();}
        planetMode_=true;planetMapPath_.clear();clearTransformPreview();
    }
    int body=-1;
    const auto explicitBody=planetSceneBodyOverride(projectPath,doc.path);
    if(!explicitBody.empty()){
        body=doc.data.findBody(explicitBody);
        if(body<0||!doc.data.bodies[size_t(body)].surface()){body=-1;planetBodyOverride_.clear();}
    }
    const int selected=studioMapEntityIndex(selectedId,doc);
    if(body<0&&selected>=0)body=doc.data.findBody(doc.data.entities[size_t(selected)].at.getStr("body"));
    if(body<0&&planetPreview_&&planetMapPath_==doc.path)body=doc.data.findBody(planetPreview_->bodyId);
    if(body<0)body=doc.data.findBody(doc.data.worldExtra.getStr("start"));
    if(body<0||!doc.data.bodies[size_t(body)].surface()){
        body=-1;for(size_t i=0;i<doc.data.bodies.size();++i)if(doc.data.bodies[i].surface()){body=int(i);break;}
    }
    const bool changedBody=!planetPreview_||planetMapPath_!=doc.path||planetModelProject_!=projectPath||body<0||
        planetPreview_->bodyId!=doc.data.bodies[size_t(body)].id;
    const bool changedDocument=changedBody||planetRevision_!=doc.revision;
    if(changedDocument){
        if(!device_->waitForResourceIdle())return;
        clearLivePlanetTerrain();
        auto next=StudioPlanetPreview::build(doc,body);
        Mesh replacement;
        if(next)replacement=next->mesh.upload(*device_);
        if(!changedBody && planetRevision_!=doc.revision)clearTransformPreview();
        if(planetTerrain_.vb.valid())device_->destroyBuffer(planetTerrain_.vb);
        if(planetTerrain_.ib.valid())device_->destroyBuffer(planetTerrain_.ib);
        planetTerrain_=replacement;planetPreview_=std::move(next);planetMapPath_=doc.path;planetRevision_=doc.revision;
        planetAuthoredOccluderRadius_=planetPreview_?studioPlanetInnerRadius(planetPreview_->mesh):0.f;
        planetProcessOverlays_=planetPreview_?buildPlanetProcessOverlays(doc,body,*planetPreview_):PlanetProcessOverlays{};
        planetForestOverlayRecipe_=studioForestOverlayRecipe(doc,rules);
        planetForestOverlays_=planetPreview_?buildPlanetForestOverlays(doc,rules,body,*planetPreview_):PlanetForestOverlays{};
        planetGravityHandles_=planetPreview_?planetGravityHandles(doc,*planetPreview_):std::vector<PlanetGravityHandle>{};
        if(changedBody){cameraInitialized_=false;clearTransformPreview();forestDragPreview_=Json{};}
    }
    const auto now=std::chrono::steady_clock::now();
    if(changedDocument||planetModelProject_!=projectPath||planetModelRules_!=&rules||modelsPending_||now>=nextFileCheck_){
        if(planetModelProject_!=projectPath)clearDocumentModels();
        if(!changedDocument&&(planetModelRules_!=&rules||now>=nextFileCheck_)){
            auto recipe=studioForestOverlayRecipe(doc,rules);
            if(!(recipe==planetForestOverlayRecipe_)){
                planetForestOverlayRecipe_=std::move(recipe);
                planetForestOverlays_=planetPreview_?buildPlanetForestOverlays(doc,rules,body,*planetPreview_):PlanetForestOverlays{};
            }
        }
        if(planetPreview_)syncDocumentModels(doc,projectPath,rules,planetPreview_->markers);
        else clearDocumentModels();
        planetModelProject_=projectPath;planetModelRules_=&rules;
        nextFileCheck_=now+std::chrono::seconds(1);
    }
    const bool useLiveTerrain=syncLivePlanetTerrain(doc,projectPath)&&planetTerrain_.vb.valid()&&planetTerrain_.ib.valid();
    planetLiveTerrainVisible_=useLiveTerrain;
    syncPlanetForestModels(doc,projectPath);
    if(!cameraInitialized_){
        camera_.target={};camera_.distance=planetPreview_?std::max(2.f,planetPreview_->extent*3.f):16.f;
        camera_.yaw=.55f;camera_.pitch=.35f;camera_.ortho=false;tween_.active=false;cameraInitialized_=true;
    }
    stepTween();selectedId_=selectedId;selectedPivotValid_=false;selectedBoxValid_=false;
    selectedAxesValid_=false;selectedIsLight_=false;
    // Use the existing transform gizmo for authored rigid models. Yaw is
    // about the radial vertical (not arbitrary global Y); scale is along
    // actual model axes. Pitch/roll edits remain numeric-only.
    if(planetPreview_&&selected>=0&&selection_.size()<=1&&
       !locked_.count(selectedId)&&!hidden_.count(selectedId)&&
       (tool_==TransformTool::Rotate||tool_==TransformTool::Scale)){
        const auto& entity=doc.data.entities[size_t(selected)];
        const auto frame=planetPreview_->modelFrames.find(selectedId);
        const auto model=std::find_if(modelInstances_.begin(),modelInstances_.end(),
            [&](const auto& instance){return instance.id==selectedId&&instance.model;});
        if(editor::authoredEntity(entity)&&frame!=planetPreview_->modelFrames.end()&&model!=modelInstances_.end()){
            selectedPivot_=frame->second.transformPoint({});
            const auto radial=normalize(selectedPivot_);
            const auto view=viewCamera();
            float2 screen;float3 o,d;
            if(view.worldToScreen(selectedPivot_,float(width),float(height),screen)&&
               view.screenRay(screen.x,screen.y,float(width),float(height),o,d)&&
               planetPreview_->terrainHit(o,d,visiblePlanetTerrain())+.3f>=dot(selectedPivot_-o,d)){
                selectedPivotValid_=true;selectedAxesValid_=true;
                for(int i=0;i<3;++i)selectedAxes_[i]=normalize(frame->second.transformDir({i==0?1.f:0.f,i==1?1.f:0.f,i==2?1.f:0.f}));
                if(tool_==TransformTool::Rotate){
                    selectedAxes_[1]=radial;
                    auto right=normalize(cross(radial,float3{0,0,1}));
                    if(dot(right,right)<.5f)right=normalize(cross(radial,float3{1,0,0}));
                    selectedAxes_[0]=right;selectedAxes_[2]=normalize(cross(right,radial));
                }
            }
        }
    }
    device_->beginFrame(width,height);StudioFrameScope scope{*device_};
    renderThumbnail();
    const auto view=viewCamera();renderer_.beginScene(view,width,height);
    renderer_.setSceneSelectionOutline(true);renderer_.lighting=Lighting{};renderer_.lighting.fogDensity=0;
    lightingStatus_="Planet authoring preview · rigid models and coarse surface";
    if(planetProcessOverlays_.invalidPhysis)lightingStatus_+=" · invalid PHYSIS fields";
    if(planetProcessOverlays_.truncated)lightingStatus_+=" · overlay budget reached";
    if(planetProcessOverlays_.regionChartClipped)lightingStatus_+=" · EVOLVE chart clipped";
    if(planetForestOverlays_.invalid)lightingStatus_+=" · invalid authored forest";
    if(planetForestOverlays_.missingProfile)lightingStatus_+=" · missing ARBOR profile";
    if(planetForestOverlays_.rejected)lightingStatus_+=" · rejected forest placements";
    if(planetForestOverlays_.truncated)lightingStatus_+=" · forest overlay budget reached";
    const bool livePlanet=planetLiveSnapshot_.getBool("active")&&
        planetLiveSnapshot_.getStr("topology")=="planet"&&
        !planetLivePath_.empty()&&planetLivePath_==doc.path&&
        planetLiveProject_==projectPath&&planetLiveRevision_==doc.revision&&!doc.dirty;
    if(livePlanet&&(planetLiveSnapshot_["diagnosticBudget"]["trimmedLayers"].size()||planetLiveSnapshot_["diagnosticBudget"]["omittedLayers"].size()))
        lightingStatus_+=" · Live payload budget reached";
    const bool liveImprint=livePlanet&&planetLiveLayer_!="off"&&planetLiveSnapshot_["imprint"].isObject();
    const bool liveGranular=livePlanet&&planetLiveLayer_!="off"&&planetLiveSnapshot_["granular"].getBool("available");
    const bool liveGravity=livePlanet&&planetLiveSnapshot_["gravity"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["gravity"].isObject()){
        if(liveGravity)lightingStatus_+=" · effective PHYSIS gravity (cyan inward, orange outward, violet tangent, grey zero)";
        else lightingStatus_+=" · live PHYSIS gravity unavailable (budget/context)";
    }
    const bool liveAether=livePlanet&&planetLiveAetherLayer_!="off"&&planetLiveSnapshot_["aether"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["aether"].isObject()){
        lightingStatus_+=" · AETHER "+planetLiveAetherLayer_+(liveAether?" mass / duct potential":" unavailable");
        if(planetLiveSnapshot_["aether"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    const bool liveForest=planetForestLiveCurrent(doc,projectPath);
    if(liveForest){
        lightingStatus_+=useLiveTerrain?" · completed-tick terrain":" · authored terrain fallback (live unavailable)";
        if(useLiveTerrain)lightingStatus_+=" · surface grid step "+std::to_string(planetLiveTerrainGrid_.getInt("step"))+" · refined tiles "+std::to_string(planetLiveTerrainGrid_["patches"].size());
        lightingStatus_+=" · "+std::to_string(planetLiveSnapshot_["forest"]["plants"].size())+"/"+std::to_string(planetLiveSnapshot_["forest"].getInt("focusPlantCount"))+" native render candidates";
        if(planetLiveSnapshot_["forest"].getBool("cameraPrioritized"))lightingStatus_+=" · camera priority";
    }
    if(planetForestModelsPending_)lightingStatus_+=" · ARBOR prototypes loading";
    if(!planetForestRejectedModels_.empty()||!planetForestRejectedRoots_.empty()||!planetForestRejectedLogs_.empty())lightingStatus_+=" · ARBOR preview budget or upload rejection";
    if(!liveForest&&planetForestOverlays_.plants)lightingStatus_+=" · authored forest: ARBOR skeleton / root footprint / capsule recipe";
    if(livePlanet&&planetLiveSnapshot_["forest"].isObject()){
        lightingStatus_+=liveForest?" · live forest roots / actual capsule poses":" · live forest unavailable";
        if(planetLiveSnapshot_["forest"].getBool("truncated"))lightingStatus_+=" (detail budget)";
        if(planetLiveSnapshot_["forest"].getBool("stormsCapped")||planetLiveSnapshot_["forest"].getBool("windZonesCapped"))lightingStatus_+=" · wind field capped";
    }
    const bool liveSociety=livePlanet&&planetLiveSnapshot_["society"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["society"].isObject()){
        lightingStatus_+=liveSociety?" · SOCIETY knowledge / trust / triggers":" · SOCIETY unavailable";
        if(planetLiveSnapshot_["society"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    const bool liveCircuit=livePlanet&&planetLiveSnapshot_["circuit"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["circuit"].isObject()){
        lightingStatus_+=liveCircuit?" · CIRCUIT power / logic state":" · CIRCUIT unavailable";
        if(planetLiveSnapshot_["circuit"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    const bool liveCrowd=livePlanet&&planetLiveSnapshot_["crowd"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["crowd"].isObject()){
        lightingStatus_+=liveCrowd?" · CROWD reservations / native routes":" · CROWD unavailable";
        if(planetLiveSnapshot_["crowd"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    const bool liveEcho=livePlanet&&planetLiveSnapshot_["echo"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["echo"].isObject()){
        lightingStatus_+=liveEcho?" · ECHO acoustic graph / hearing":" · ECHO unavailable";
        if(planetLiveSnapshot_["echo"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    const bool liveMorph=livePlanet&&planetLiveSnapshot_["morph"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["morph"].isObject()){
        lightingStatus_+=liveMorph?" · MORPH bound node displacement":" · MORPH unavailable";
        if(planetLiveSnapshot_["morph"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    const bool liveFracture=livePlanet&&planetLiveSnapshot_["fracture"].getBool("available");
    if(livePlanet&&planetLiveSnapshot_["fracture"].isObject()){
        lightingStatus_+=liveFracture?" · FRACTURE live structural state":" · FRACTURE unavailable";
        if(planetLiveSnapshot_["fracture"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    const bool liveEvolve=livePlanet&&planetLiveLayer_!="off"&&planetLiveSnapshot_["evolve"].isObject();
    if(liveEvolve){
        lightingStatus_+=" · live EVOLVE "+planetLiveLayer_+" tick "+std::to_string(planetLiveSnapshot_["evolve"].getInt("sampleTick"));
        if(planetLiveSnapshot_["evolve"].getBool("truncated"))lightingStatus_+=" (sample capped)";
        if(planetLiveSnapshot_["evolve"].getInt("clipped")>0)lightingStatus_+=" (chart clipped)";
    }
    if(liveGranular){
        lightingStatus_+=" · GRANULAR diagnostic "+std::to_string(planetLiveSnapshot_["granular"].getInt("cellSizeCm"))+" cm";
        if(planetLiveSnapshot_["granular"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    if(liveImprint){
        lightingStatus_+=" · live IMPRINT tick "+std::to_string(planetLiveSnapshot_["imprint"].getInt("sampleTick"));
        if(planetLiveSnapshot_["imprint"].getBool("truncated"))lightingStatus_+=" (sample capped)";
    }
    if(planetPreview_&&planetTerrain_.vb.valid()&&planetTerrain_.ib.valid()){
        if(useLiveTerrain){for(const auto& face:planetLiveTerrainFaces_)renderer_.drawMesh(face,mat4{},{1,1,1,1},kDrawNoCastShadow);}
        else renderer_.drawMesh(planetTerrain_,mat4{},{1,1,1,1},kDrawNoCastShadow);
        if(liveForest)drawLivePlanetForestMeshes(doc,projectPath,width,height);
        if(!liveForest&&planetForestModelProject_==projectPath&&planetForestModelCatalog_==doc.data.worldExtra["terraweaveForest"]){
            static const std::vector<uint8_t> barkOnly{1,0};
            const bool current=forestSelection_.getStr("project")==projectPath&&forestSelection_.getStr("mapPath")==doc.path&&
                forestSelection_.getStr("expectedRevision")==std::to_string(doc.revision);
            for(const auto& plant:planetForestOverlays_.picks){
                if(!plant.markerOwner.empty()&&hidden_.count(plant.markerOwner))continue;
                const auto model=planetForestModels_.find(plant.species);
                if(model==planetForestModels_.end())continue;
                render::ModelDraw draw;draw.model=model->second.get();draw.world=plant.world;
                const bool dragging=forestDragPreview_.getStr("project")==projectPath&&forestDragPreview_.getStr("mapPath")==doc.path&&
                    forestDragPreview_.getStr("expectedRevision")==std::to_string(doc.revision)&&
                    forestDragPreview_.getInt("species")==int64_t(plant.species)&&forestDragPreview_.getInt("key")==int64_t(plant.key)&&
                    forestDragPreview_["at"].getStr("body")==planetPreview_->bodyId;
                if(dragging){
                    const auto& at=forestDragPreview_["at"];
                    const int face=int(at.getInt("face",-1));
                    const auto x=at["x"].fx(),y=at["y"].fx();
                    const int n=planetPreview_->faceSize;
                    if(face>=0&&face<6&&x>=Fixed{}&&y>=Fixed{}&&x<=Fixed::fromInt(n)&&y<=Fixed::fromInt(n)){
                        game::CubeSphere cube(n);const auto dir=cube.dirFromFace(face,x,y);
                        int canonical;Fixed ca,cb;cube.faceFromDir(dir,canonical,ca,cb);
                        Vec3 east,south;game::CubeSphere::faceTangents(canonical,dir,east,south);
                        const auto& bodyDoc=doc.data.bodies[size_t(body)];
                        const float height=StudioPlanetPreview::height(bodyDoc,face,float(x.toDouble()),float(y.toDouble()));
                        const auto position=StudioPlanetPreview::f3(dir)*(planetPreview_->radius+height);
                        const auto frame=studioForestFrame(position,StudioPlanetPreview::f3(dir),StudioPlanetPreview::f3(east));
                        const render::float3 col{plant.world.at(0,0),plant.world.at(1,0),plant.world.at(2,0)};
                        const float scale=std::sqrt(render::dot(col,col));
                        if(frame){draw.world=*frame*render::mat4::scale({scale,scale,scale});draw.outline={1.f,.8f,.2f,.025f};}
                    }
                }
                if(!plant.foliagePermille)draw.materialMask=&barkOnly;
                draw.useVertexParameters=true;draw.vertexParameters={0,0,0,0};draw.maxVertexOffsetMetres=.01f;
                if(current&&forestSelection_.getInt("species")==int64_t(plant.species)&&forestSelection_.getInt("key")==int64_t(plant.key))
                    draw.outline={1.f,.8f,.2f,.025f};
                renderer_.drawModel(draw);
            }
        }
        std::set<std::string> renderedModels;
        for(const auto& instance:modelInstances_){
            if(!instance.model)continue;
            renderedModels.insert(instance.id);
            if(hidden_.count(instance.id))continue;
            render::ModelDraw draw;draw.model=instance.model;draw.world=instance.world;
            if(drag_.axis>=0&&drag_.id==instance.id&&(drag_.tool==TransformTool::Rotate||drag_.tool==TransformTool::Scale))
                draw.world=dragPreview()*instance.world;
            if(auto custom=instanceMaterials_.find(instance.id);custom!=instanceMaterials_.end())draw.materials=&custom->second.materials;
            if(auto team=instanceTeams_.find(instance.id);team!=instanceTeams_.end())draw.team=team->second;
            if(instance.id==selectedId||selection_.count(instance.id))draw.outline={.25f,.62f,1.f,.035f};
            else if(instance.id==hover_)draw.outline={.62f,.8f,1.f,.02f};
            renderer_.drawModel(draw);
        }
        for(const auto& marker:planetPreview_->markers){
            if(hidden_.count(marker.id)||renderedModels.count(marker.id))continue;
            const auto world=mat4::translate(marker.center)*mat4::scale(marker.half*2.f);
            if(marker.id==selectedId||selection_.count(marker.id))
                renderer_.drawSelectionMesh(cube_,world,marker.color,{.25f,.62f,1.f,1.f});
            else renderer_.drawMesh(cube_,world,marker.color,kDrawNoCastShadow);
        }
        if(gizmosVisible_){
            if(!liveForest){
                const bool current=forestSelection_.getStr("project")==projectPath&&forestSelection_.getStr("mapPath")==doc.path&&
                    forestSelection_.getStr("expectedRevision")==std::to_string(doc.revision);
                const auto selectedForest=current?std::to_string(forestSelection_.getInt("species"))+":"+std::to_string(forestSelection_.getInt("key")):std::string{};
                for(const auto& line:planetForestOverlays_.lines)
                    renderer_.line(line.a,line.b,!selectedForest.empty()&&line.owner==selectedForest?render::rgba(255,225,90):line.color);
            }
            for(const auto& line:planetProcessOverlays_.lines)
                if((!liveGravity||!line.owner.empty())&&(line.owner.empty()||!hidden_.count(line.owner)))
                    renderer_.line(line.a,line.b,line.color);
            PlanetLiveLineBudget liveLineBudget;
            const size_t liveLineShare=planetLiveShare(kPlanetLiveLines,size_t(liveForest)+size_t(liveGravity)+size_t(liveAether)+size_t(liveSociety)+size_t(liveCircuit)+size_t(liveCrowd)+size_t(liveEcho)+size_t(liveMorph)+size_t(liveFracture)+size_t(liveEvolve)+size_t(liveGranular)+size_t(liveImprint));
            const auto liveLine=[&](render::float3 a,render::float3 b,uint32_t color){
                if(liveLineBudget.take())renderer_.line(a,b,color);
            };
            if(liveForest){
                liveLineBudget.beginLayer(liveLineShare);
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    if(!j.isArray()||j.size()!=3)return {};
                    const auto& a=j.items();
                    render::float3 v{float(a[0].toDouble(NAN)),float(a[1].toDouble(NAN)),float(a[2].toDouble(NAN))};
                    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))return {};
                    return v;
                };
                const auto basis=[](render::float3 up){
                    const auto a=render::normalize(render::cross(up,std::abs(up.y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                    return std::pair{a,render::normalize(render::cross(up,a))};
                };
                const auto ring=[&](render::float3 p,render::float3 a,render::float3 b,float r,uint32_t color){
                    auto prev=p+a*r;
                    for(int i=1;i<=8;++i){
                        const float t=6.28318530718f*float(i)/8.f;
                        const auto next=p+(a*std::cos(t)+b*std::sin(t))*r;
                        liveLine(prev,next,color);prev=next;
                    }
                };
                const float marker=std::clamp(planetPreview_->radius*.008f,.12f,1.f);
                size_t count=0;
                for(const auto& row:planetLiveSnapshot_["forest"]["plants"].items()){
                    if(count++>=128)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto& capsule=row["capsule"];
                    if(row.getBool("physical")&&!row.getBool("spent")&&capsule.isObject()){
                        const auto center=readVec(capsule["center"]),axis=readVec(capsule["axis"]);
                        const float r=float(capsule["radius"].toDouble()),h=float(capsule["halfHeight"].toDouble());
                        if(center&&axis&&render::dot(*axis,*axis)>.5f&&r>0&&r<=1000&&h>0&&h<=1000){
                            const auto up=render::normalize(*axis);
                            const auto [a,b]=basis(up);
                            const auto p=*center-up*h,q=*center+up*h;
                            const auto color=row.getBool("awake")?render::rgba(255,165,90):render::rgba(120,190,255);
                            ring(p,a,b,r,color);ring(q,a,b,r,color);
                            for(const auto side:{a,b}){
                                liveLine(p+side*r,q+side*r,color);liveLine(p-side*r,q-side*r,color);
                                for(int cap=0;cap<2;++cap){
                                    const auto c=cap?q:p,v=up*(cap?1.f:-1.f);
                                    auto prev=c+side*r;
                                    for(int j=1;j<=4;++j){
                                        const float t=3.14159265359f*float(j)/4.f;
                                        const auto next=c+(side*std::cos(t)+v*std::sin(t))*r;
                                        liveLine(prev,next,color);prev=next;
                                    }
                                }
                            }
                            continue;
                        }
                    }
                    const auto position=readVec(row["position"]),normal=readVec(row["up"]);
                    if(!position||!normal||render::dot(*normal,*normal)<.5f)continue;
                    const auto up=render::normalize(*normal);
                    const auto [a,b]=basis(up);
                    const auto p=*position+up*.025f;
                    const auto support=std::clamp(float(row.getInt("supportPermille"))/1000.f,0.f,1.f);
                    const bool retired=row.getBool("spent")||row.getBool("harvested");
                    const auto color=retired?render::rgba(125,125,135):
                        row.getBool("physical")?render::rgba(255,125,100):
                        row.getBool("pending")?render::rgba(255,210,90):
                        row.getBool("fallen")?render::rgba(190,115,80):
                        row.getBool("dead")?render::rgba(160,115,95):
                        render::rgba(uint8_t(230-145*support),uint8_t(90+145*support),100);
                    // Marker only, never invent a standing mesh or fallen orientation.
                    liveLine(p-a*marker,p+a*marker,color);liveLine(p-b*marker,p+b*marker,color);
                    if(!retired&&!row.getBool("fallen")){
                        liveLine(p,p+up*marker,color);
                        const float radius=float(row.getInt("rootRadiusCm"))*.01f;
                        if(radius>0&&radius<=100.f)ring(p,a,b,radius,color);
                    }
                }
            }
            if(liveGravity){
                liveLineBudget.beginLayer(liveLineShare);
                // Effective gravity replaces authored per-field arrows in this
                // view. Length is an indicative clamped display scale only.
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                for(const auto& body:planetLiveSnapshot_["gravity"]["bodies"].items()){
                    if(body.getStr("body")!=planetPreview_->bodyId)continue;
                    size_t count=0;
                    for(const auto& row:body["samples"].items()){
                        if(count++>=96)break;
                        const auto ground=readVec(row["position"]),up=readVec(row["up"]),acc=readVec(row["acceleration"]);
                        if(!ground||!up||!acc)continue;
                        const float radius=planetPreview_->radius;
                        const float lift=std::clamp(radius*.008f,.15f,2.f);
                        const float magnitude=float(row["magnitude"].toDouble());
                        const auto kind=row.getStr("kind");
                        if(kind=="zero"||magnitude<=.01f){
                            const auto axis=normalize(cross(*up,std::abs(up->y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                            const auto side=normalize(cross(*up,axis));
                            const auto center=*ground+*up*lift;
                            const float extent=std::clamp(radius*.012f,.1f,1.5f);
                            liveLine(center-axis*extent,center+axis*extent,rgba(160,174,185));
                            liveLine(center-side*extent,center+side*extent,rgba(160,174,185));
                            continue;
                        }
                        const auto direction=normalize(*acc);
                        const float len=std::clamp(magnitude*.2f,.1f,std::max(.1f,radius*.12f));
                        const float inward=std::max(0.f,-dot(direction,*up));
                        const auto start=*ground+*up*(lift+inward*len);
                        const auto end=start+direction*len;
                        const auto side=normalize(cross(direction,std::abs(direction.y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                        const auto color=kind=="inward"?rgba(80,225,255):kind=="outward"?rgba(255,170,80):rgba(205,145,255);
                        liveLine(start,end,color);
                        liveLine(end,end-direction*(len*.25f)+side*(len*.14f),color);
                        liveLine(end,end-direction*(len*.25f)-side*(len*.14f),color);
                    }
                    break;
                }
            }
            if(liveAether){
                liveLineBudget.beginLayer(liveLineShare);
                const auto& data=planetLiveSnapshot_["aether"];
                const size_t species=planetLiveAetherLayer_=="toxin"?1:planetLiveAetherLayer_=="oxygen"?2:0;
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                const auto color=species==1?rgba(150,245,85):species==2?rgba(100,205,255):rgba(230,180,245);
                const float marker=std::clamp(planetPreview_->radius*.012f,.12f,1.8f);
                size_t count=0;
                for(const auto& row:data["cells"].items()){
                    if(count++>=256)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto p=readVec(row["position"]);
                    if(!p||row["mass"].size()!=3)continue;
                    const float mass=std::max(0.f,float(row["mass"][species].toDouble()));
                    const float extent=marker*(.4f+std::clamp(float(std::log1p(mass)/std::log(1000000001.)),0.f,1.f));
                    const auto c=mass>0?color:rgba(115,125,140);
                    for(const auto axis:{render::float3{extent,0,0},render::float3{0,extent,0},render::float3{0,0,extent}})
                        liveLine(*p-axis,*p+axis,c);
                }
                count=0;
                for(const auto& row:data["ducts"].items()){
                    if(count++>=512)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto a=readVec(row["a"]),b=readVec(row["b"]);
                    if(!a||!b)continue;
                    const auto delta=*b-*a;
                    const float distance=std::sqrt(dot(delta,delta));
                    if(distance<.001f)continue;
                    const auto amount=row.getBool("potentialAvailable")&&row["potential"].size()==3?
                        row["potential"][species].integer():0;
                    liveLine(*a,*b,amount?color:rgba(105,115,130));
                    if(!amount)continue;
                    const auto direction=delta*(amount>0?1.f/distance:-1.f/distance);
                    const auto tip=(*a+*b)*.5f;
                    const auto side=normalize(cross(direction,std::abs(direction.y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                    const float len=std::min(marker,distance*.24f);
                    liveLine(tip,tip-direction*len+side*(len*.45f),color);
                    liveLine(tip,tip-direction*len-side*(len*.45f),color);
                }
            }
            if(liveSociety){
                liveLineBudget.beginLayer(liveLineShare);
                const auto& data=planetLiveSnapshot_["society"];
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                const float extent=std::clamp(planetPreview_->radius*.012f,.12f,1.5f);
                const auto marker=[&](render::float3 p,float s,uint32_t color){
                    for(const auto axis:{render::float3{s,0,0},render::float3{0,s,0},render::float3{0,0,s}})
                        liveLine(p-axis,p+axis,color);
                };
                size_t count=0;
                for(const auto& row:data["contacts"].items()){
                    if(count++>=512)break;
                    if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto a=readVec(row["a"]),b=readVec(row["b"]);if(!a||!b)continue;
                    const auto delta=*b-*a;
                    const float trust=std::clamp(float(row.getInt("trust"))/1000.f,0.f,1.f);
                    const bool eligible=row.getBool("eligibleNow");
                    const auto color=eligible?rgba(uint8_t(155-65*trust),uint8_t(180+65*trust),160):rgba(120,125,145);
                    if(!eligible){
                        liveLine(*a,*a+delta*.4f,color);liveLine(*a+delta*.6f,*b,color);
                    }else liveLine(*a,*b,color);
                    const float distance=std::sqrt(dot(delta,delta));
                    if(distance>.001f){
                        const auto dir=delta*(1.f/distance),tip=*a+delta*.65f;
                        const auto side=normalize(cross(dir,std::abs(dir.y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                        const float len=std::min(extent,distance*.2f);
                        liveLine(tip,tip-dir*len+side*(len*.45f),color);
                        liveLine(tip,tip-dir*len-side*(len*.45f),color);
                    }
                }
                for(const char* key:{"people","messages","triggers"}){
                    count=0;const std::string kind=key;
                    for(const auto& row:data[key].items()){
                        if(count++>=(kind=="people"?256u:128u))break;
                        if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                        const auto p=readVec(row["position"]);if(!p)continue;
                        uint32_t color=rgba(110,120,135);float scale=.75f;
                        if(row.getBool("enabled")){
                            if(kind=="people"&&row.getBool("knowledgeAvailable")){
                                const float topics=std::clamp(float(row.getInt("topicCount"))/64.f,0.f,1.f);
                                color=rgba(120,uint8_t(150+90*topics),245);scale=1.f+topics*.5f;
                            }else if(kind=="messages"&&row.getBool("sourceValid"))color=rgba(255,195,90);
                            else if(kind=="triggers"&&row.getBool("outputAvailable"))
                                color=row.getBool("active")?rgba(110,235,155):row.getBool("fired")?rgba(185,145,245):rgba(170,175,190);
                        }
                        marker(*p,extent*scale,color);
                        if(const auto q=readVec(row["personPosition"])){
                            const auto d=*q-*p;
                            for(int i=0;i<4;++i)liveLine(*p+d*(float(i)/4),*p+d*((float(i)+.5f)/4),color);
                        }
                    }
                }
            }
            if(liveCircuit){
                liveLineBudget.beginLayer(liveLineShare);
                const auto& data=planetLiveSnapshot_["circuit"];
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                const float extent=std::clamp(planetPreview_->radius*.012f,.12f,1.5f);
                const auto marker=[&](render::float3 p,float s,uint32_t color){
                    for(const auto axis:{render::float3{s,0,0},render::float3{0,s,0},render::float3{0,0,s}})
                        liveLine(p-axis,p+axis,color);
                };
                size_t count=0;
                for(const auto& row:data["wires"].items()){
                    if(count++>=512)break;
                    if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto a=readVec(row["a"]),b=readVec(row["b"]);if(!a||!b)continue;
                    const auto delta=*b-*a;
                    const auto color=!row.getBool("active")?rgba(105,115,130):row.getBool("bothPowered")?rgba(105,225,155):rgba(145,175,205);
                    if(!row.getBool("active")){
                        liveLine(*a,*a+delta*.4f,color);liveLine(*a+delta*.6f,*b,color);continue;
                    }
                    liveLine(*a,*b,color);
                    const float distance=std::sqrt(dot(delta,delta));
                    if(!row.getBool("bidirectional")&&distance>.001f){
                        const auto dir=delta*(1.f/distance),tip=*a+delta*.6f;
                        const auto side=normalize(cross(dir,std::abs(dir.y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                        const float len=std::min(extent,distance*.2f);
                        liveLine(tip,tip-dir*len+side*(len*.45f),color);
                        liveLine(tip,tip-dir*len-side*(len*.45f),color);
                    }
                }
                for(const char* key:{"nodes","gates","switches","devices"}){
                    count=0;const std::string kind=key;
                    for(const auto& row:data[key].items()){
                        if(count++>=(kind=="nodes"?256u:128u))break;
                        if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                        const auto p=readVec(row["position"]);if(!p)continue;
                        uint32_t color=rgba(110,120,135);
                        if(row.getBool("enabled")){
                            if(kind=="nodes"&&row.getBool("outputAvailable"))
                                color=row.getBool("signal")?rgba(250,220,95):row.getBool("powered")?rgba(100,235,155):rgba(235,115,120);
                            else if(kind=="gates")color=row.getBool("inputsValid")?rgba(185,145,245):rgba(235,115,120);
                            else if(kind=="switches")color=row.getBool("closed")?rgba(250,220,95):rgba(120,180,225);
                            else if(kind=="devices"&&row.getBool("outputAvailable")&&row.getBool("valid"))
                                color=row.getBool("active")?rgba(100,235,155):rgba(235,115,120);
                        }
                        marker(*p,extent*(kind=="nodes"?1.f:.7f),color);
                        if(kind!="nodes")for(const char* link:{"inputAPosition","inputBPosition","outputPosition","nodePosition"})
                            if(const auto q=readVec(row[link])){
                                const auto d=*q-*p;
                                for(int i=0;i<4;++i)liveLine(*p+d*(float(i)/4),*p+d*((float(i)+.5f)/4),color);
                            }
                    }
                }
            }
            if(liveCrowd){
                liveLineBudget.beginLayer(liveLineShare);
                const auto& data=planetLiveSnapshot_["crowd"];
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                const float extent=std::clamp(planetPreview_->radius*.012f,.12f,1.5f);
                const auto marker=[&](render::float3 p,float s,uint32_t color){
                    for(const auto axis:{render::float3{s,0,0},render::float3{0,s,0},render::float3{0,0,s}})
                        liveLine(p-axis,p+axis,color);
                };
                const auto lifted=[&](render::float3 p){return p+normalize(p)*extent*.4f;};
                size_t count=0;
                for(const auto& row:data["slots"].items()){
                    if(count++>=512)break;
                    if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto p=readVec(row["position"]);if(!p)continue;
                    const auto n=row.getInt("occupancy");
                    const auto color=!row.getBool("enabled")?rgba(110,120,135):n>1?rgba(255,80,90):n?rgba(255,195,80):rgba(100,210,255);
                    marker(lifted(*p),extent*.75f,color);
                }
                count=0;
                for(const auto& row:data["agents"].items()){
                    if(count++>=256)break;
                    if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto p=readVec(row["position"]);if(!p)continue;
                    const auto pos=lifted(*p);
                    const auto support=row["support"].getStr("status");
                    const auto color=!row.getBool("enabled")?rgba(110,120,135):
                        !support.empty()&&support!="supported"?rgba(255,105,100):
                        row.getBool("moving")?rgba(100,235,155):rgba(170,195,255);
                    marker(pos,extent,color);
                    if(row.getBool("reservationValid"))if(const auto dst=readVec(row["reservationPosition"])){
                        const auto d=lifted(*dst)-pos;
                        for(int i=0;i<6;++i)liveLine(pos+d*(float(i)/6),pos+d*((float(i)+.5f)/6),rgba(255,195,80));
                    }
                    if(const auto goal=readVec(row["goal"]))marker(lifted(*goal),extent*.6f,rgba(235,160,255));
                    std::optional<render::float3> previous=pos;
                    for(const auto& point:row["route"].items()){
                        const auto q=readVec(point);if(!q){previous.reset();continue;}
                        const auto next=lifted(*q);
                        if(previous)liveLine(*previous,next,rgba(110,230,160));
                        previous=next;
                    }
                }
            }
            if(liveEcho){
                liveLineBudget.beginLayer(liveLineShare);
                const auto& data=planetLiveSnapshot_["echo"];
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                const float extent=std::clamp(planetPreview_->radius*.012f,.12f,1.5f);
                const auto marker=[&](render::float3 p,float s,uint32_t color){
                    for(const auto axis:{render::float3{s,0,0},render::float3{0,s,0},render::float3{0,0,s}})
                        liveLine(p-axis,p+axis,color);
                };
                size_t count=0;
                for(const auto& row:data["portals"].items()){
                    if(count++>=512)break;
                    if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto a=readVec(row["a"]),b=readVec(row["b"]);if(!a||!b)continue;
                    const auto loss=std::clamp(float(row.getInt("loss"))/1000000.f,0.f,1.f);
                    const auto color=row.getBool("active")?rgba(uint8_t(110+145*loss),uint8_t(205-95*loss),230):rgba(105,115,130);
                    const auto delta=*b-*a;
                    if(!row.getBool("active")){
                        liveLine(*a,*a+delta*.4f,color);liveLine(*a+delta*.6f,*b,color);continue;
                    }
                    liveLine(*a,*b,color);
                    const float distance=std::sqrt(dot(delta,delta));
                    if(!row.getBool("bidirectional")&&distance>.001f){
                        const auto dir=delta*(1.f/distance);
                        const auto tip=*a+delta*.6f;
                        const auto side=normalize(cross(dir,std::abs(dir.y)<.9f?render::float3{0,1,0}:render::float3{1,0,0}));
                        const float len=std::min(extent,distance*.2f);
                        liveLine(tip,tip-dir*len+side*(len*.45f),color);
                        liveLine(tip,tip-dir*len-side*(len*.45f),color);
                    }
                }
                for(const char* key:{"zones","emitters","receivers"}){
                    count=0;
                    for(const auto& row:data[key].items()){
                        if(count++>=256)break;
                        if(!row.getBool("placed")||row.getStr("body")!=planetPreview_->bodyId)continue;
                        const auto p=readVec(row["position"]);if(!p)continue;
                        const std::string kind=key;
                        if(kind=="zones")marker(*p,extent,row.getBool("enabled")?rgba(115,185,255):rgba(110,120,135));
                        else if(kind=="emitters"){
                            const float power=std::clamp(float(std::log1p(std::max<int64_t>(0,row.getInt("intensity")))/std::log(1000001.)),0.f,1.f);
                            marker(*p,extent*(.6f+power),row.getBool("active")?rgba(255,185,90):rgba(110,120,135));
                        }else{
                            const bool valid=row.getBool("hearingAvailable")&&row.getBool("valid");
                            const bool audible=valid&&row.getBool("audible");
                            marker(*p,extent*.75f,audible?rgba(115,240,155):valid?rgba(210,120,135):rgba(110,120,135));
                            if(audible)if(const auto src=readVec(row["sourcePosition"])){
                                const auto delta=*src-*p;
                                // Dashed winner relation, never a portal path.
                                for(int i=0;i<8;++i)liveLine(*p+delta*(float(i)/8),*p+delta*((float(i)+.5f)/8),rgba(110,220,155));
                            }
                        }
                    }
                }
            }
            if(liveMorph){
                liveLineBudget.beginLayer(liveLineShare);
                const auto& data=planetLiveSnapshot_["morph"];
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                const float extent=std::clamp(planetPreview_->radius*.008f,.08f,1.f);
                size_t count=0;
                for(const auto& row:data["springs"].items()){
                    if(count++>=512)break;
                    if(!row.getBool("projected")||row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto a=readVec(row["a"]),b=readVec(row["b"]);if(!a||!b)continue;
                    const auto stiffness=std::clamp(float(row.getInt("stiffness"))/1000.f,0.f,1.f);
                    const auto color=row.getBool("enabled")?rgba(uint8_t(115+120*stiffness),120,230):rgba(105,115,130);
                    liveLine(*a,*b,color);
                }
                count=0;
                for(const auto& row:data["nodes"].items()){
                    if(count++>=256)break;
                    if(!row.getBool("projected")||row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto p=readVec(row["position"]),r=readVec(row["restPosition"]);if(!p||!r)continue;
                    const auto color=!row.getBool("enabled")?rgba(110,120,135):row.getBool("anchored")?rgba(90,220,255):rgba(110,230,155);
                    liveLine(*r,*p,rgba(255,195,85));
                    for(const auto axis:{render::float3{extent,0,0},render::float3{0,extent,0},render::float3{0,0,extent}}){
                        liveLine(*p-axis,*p+axis,color);
                        liveLine(*r-axis*.45f,*r+axis*.45f,rgba(170,175,185));
                    }
                }
            }
            if(liveFracture){
                liveLineBudget.beginLayer(liveLineShare);
                const auto& data=planetLiveSnapshot_["fracture"];
                const auto readVec=[](const Json& j)->std::optional<render::float3>{
                    const auto& xyz=j.items();if(xyz.size()!=3)return {};
                    return render::float3{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                };
                const float extent=std::clamp(planetPreview_->radius*.014f,.15f,2.f);
                size_t count=0;
                for(const auto& row:data["bonds"].items()){
                    if(count++>=512)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto a=readVec(row["a"]),b=readVec(row["b"]);if(!a||!b)continue;
                    const auto fraction=std::clamp(float(row.getInt("damage"))/float(std::max<int64_t>(1,row.getInt("strength"))),0.f,1.f);
                    const auto delta=*b-*a;
                    const bool severed=fraction>=1.f;
                    const auto color=!row.getBool("enabled")?rgba(100,110,125):severed?rgba(255,75,75):
                        !row.getBool("activeSupportEdge")?rgba(155,115,175):rgba(uint8_t(90+165*fraction),uint8_t(220-120*fraction),85);
                    if(severed||!row.getBool("activeSupportEdge")){
                        liveLine(*a,*a+delta*.42f,color);liveLine(*a+delta*.58f,*b,color);
                    }else liveLine(*a,*b,color);
                }
                count=0;
                for(const auto& row:data["members"].items()){
                    if(count++>=256)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto p=readVec(row["position"]);if(!p)continue;
                    const auto color=row.getBool("detached")?rgba(255,80,100):row.getBool("broken")?rgba(255,155,60):
                        row.getBool("anchored")?rgba(85,220,255):rgba(110,225,125);
                    for(const auto axis:{render::float3{extent,0,0},render::float3{0,extent,0},render::float3{0,0,extent}})
                        liveLine(*p-axis,*p+axis,color);
                    if(row["collapse"].getStr("delivery")=="pending"||row["natureFracture"].getStr("delivery")=="pending"){
                        const float e=extent*1.6f;
                        liveLine(*p+render::float3{-e,0,-e},*p+render::float3{e,0,e},rgba(255,230,90));
                        liveLine(*p+render::float3{-e,0,e},*p+render::float3{e,0,-e},rgba(255,230,90));
                    }
                }
            }
            if(liveEvolve){
                liveLineBudget.beginLayer(liveLineShare);
                size_t count=0;
                for(const auto& row:planetLiveSnapshot_["evolve"]["regions"].items()){
                    if(count>=64)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto u=[&](const char* key){return std::clamp(float(row.getInt(key))/1e9f,0.f,1.f);};
                    float r=u("pollution"),g=u("fertility"),b=.35f;
                    if(planetLiveLayer_=="fertility")r=.15f;
                    if(planetLiveLayer_=="pollution")g=.15f;
                    if(planetLiveLayer_=="succession"){r=.15f;g=u("succession");b=.85f;}
                    if(planetLiveLayer_=="durability"){g=u("durability");r=1-g;}
                    const auto color=rgba(uint8_t(r*255),uint8_t(g*255),uint8_t(b*255));
                    const auto& edges=row["edges"].items();
                    if(edges.size()!=4)continue;
                    for(const auto& edge:edges){
                        if(edge.size()!=17)continue;
                        std::optional<render::float3> previous;
                        for(const auto& point:edge.items()){
                            const auto& xyz=point.items();
                            if(xyz.size()!=3){previous.reset();continue;}
                            const render::float3 current{float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                            if(previous)liveLine(*previous,current,color);
                            previous=current;
                        }
                    }
                    ++count;
                }
            }
            if(liveGranular){
                liveLineBudget.beginLayer(liveLineShare);
                size_t count=0;
                for(const auto& row:planetLiveSnapshot_["granular"]["cells"].items()){
                    if(count>=512)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto& vertices=row["vertices"].items();
                    if(vertices.size()!=4)continue;
                    std::array<render::float3,4> points{};
                    bool valid=true;
                    for(size_t i=0;i<4;++i){
                        const auto& xyz=vertices[i].items();
                        if(xyz.size()!=3){valid=false;break;}
                        points[i]={float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                    }
                    if(!valid)continue;
                    const float fill=std::clamp(float(row.getInt("mass"))/float(std::max<int64_t>(1,row.getInt("capacity"))),0.f,1.f);
                    const auto color=rgba(uint8_t(150+105*fill),uint8_t(105+85*fill),uint8_t(65+45*fill));
                    for(size_t i=0;i<4;++i)liveLine(points[i],points[(i+1)%4],color);
                    ++count;
                }
            }
            if(liveImprint){
                liveLineBudget.beginLayer(liveLineShare);
                // Snapshot contains canonical, already body-local vertices.
                // Draw read-only outlines, not another physical mesh or collider.
                size_t count=0;
                for(const auto& row:planetLiveSnapshot_["imprint"]["cells"].items()){
                    if(count>=1024)break;
                    if(row.getStr("body")!=planetPreview_->bodyId)continue;
                    const auto& vertices=row["vertices"].items();
                    if(vertices.size()!=4)continue;
                    std::array<render::float3,4> points{};
                    bool valid=true;
                    for(size_t i=0;i<4;++i){
                        const auto& xyz=vertices[i].items();
                        if(xyz.size()!=3){valid=false;break;}
                        points[i]={float(xyz[0].toDouble()),float(xyz[1].toDouble()),float(xyz[2].toDouble())};
                    }
                    if(!valid)continue;
                    const auto f=row.getInt("footprint"),s=row.getInt("scent"),r=row.getInt("residue");
                    const auto color=f>=s&&f>=r?rgba(255,190,90):s>=r?rgba(105,215,255):rgba(215,130,255);
                    for(size_t i=0;i<4;++i)liveLine(points[i],points[(i+1)%4],color);
                    ++count;
                }
            }
            if(liveLineBudget.dropped)lightingStatus_+=" · Live line budget reached";
            if(const auto it=planetPreview_->modelFrames.find(selectedId);it!=planetPreview_->modelFrames.end()){
                const auto origin=it->second.transformPoint({});
                const float length=std::clamp(planetPreview_->radius*.045f,1.f,12.f);
                const uint32_t colors[3]={rgba(232,82,72),rgba(120,205,85),rgba(70,135,240)};
                for(int i=0;i<3;++i){
                    const auto direction=normalize(it->second.transformDir({i==0?1.f:0.f,i==1?1.f:0.f,i==2?1.f:0.f}));
                    renderer_.line(origin,origin+direction*length,colors[i]);
                }
            }
            std::map<std::string,const StudioDocumentProxy*> byId;
            for(const auto& marker:planetPreview_->markers)byId.emplace(marker.id,&marker);
            for(const auto& link:planetPreview_->links){
                if(hidden_.count(link.from)||hidden_.count(link.to))continue;
                const auto* a=byId.at(link.from);const auto* b=byId.at(link.to);
                const auto da=normalize(a->center),db=normalize(b->center);
                if(dot(da,db)<-.99f)continue; // ambiguous antipodal route
                auto prev=a->center;
                for(int i=1;i<=16;++i){
                    const float t=float(i)/16.f;
                    const auto dir=normalize(da*(1.f-t)+db*t);
                    const float radial=std::sqrt(dot(a->center,a->center))*(1.f-t)+
                        std::sqrt(dot(b->center,b->center))*t;
                    const auto next=dir*(radial+std::sin(t*3.14159265f)*std::min(planetPreview_->radius*.05f,3.f));
                    renderer_.line(prev,next,link.color);prev=next;
                }
            }
        }
        if(planetDragPlacement_.isObject()){
            game::CubeSphere cube(planetPreview_->faceSize);
            const auto dir=StudioPlanetPreview::f3(cube.dirFromFace(int(planetDragPlacement_.getInt("face")),
                planetDragPlacement_["x"].fx(),planetDragPlacement_["y"].fx()));
            const auto eye=view.eye();
            // Display a ray-hit preview, not a committed entity.
            const float t=planetPreview_->terrainHit(eye,normalize(dir*planetPreview_->radius-eye),visiblePlanetTerrain());
            if(std::isfinite(t)){
                const auto hit=eye+normalize(dir*planetPreview_->radius-eye)*t;
                renderer_.drawMesh(cube_,mat4::translate(hit+dir*.6f)*mat4::scale({.6f,.6f,.6f}),{.25f,.9f,.8f,1},kDrawNoCastShadow);
            }
        }
    }
    drawPlanetRegionHandles(view,width,height,doc);
    drawPlanetGravityHandles(view,width,height);
    if(tool_==TransformTool::Rotate||tool_==TransformTool::Scale)drawTransformGizmo(view,width,height);
    if(orientationGizmo_&&gizmosVisible_)drawOrientationGizmo(view,width,height);
    gfx::PassDesc pass;pass.clearColor[0]=.038f;pass.clearColor[1]=.064f;pass.clearColor[2]=.098f;pass.clearColor[3]=1;
    renderer_.render(pass,&antialiasing_);renderer_.resolvePostFx();++framesDrawn_;
}

void StudioScene::rebuildProxyDraws(const editor::MapDoc& doc) {
    std::set<std::string> withModel;
    for(const auto& instance:modelInstances_)withModel.insert(instance.id);
    proxyDraws_.clear();
    proxyDraws_.reserve(proxies_.size());
    for(size_t i=0;i<proxies_.size();++i) {
        const auto& entity=proxies_[i];
        ProxyDraw d;
        d.proxy=i;
        d.sourceIndex=studioMapEntityIndex(entity.id,doc);
        const auto* sourceEntity=d.sourceIndex>=0?&doc.data.entities[size_t(d.sourceIndex)]:nullptr;
        d.water=sourceEntity && sourceEntity->extra["components"].has("NereidWater");
        d.hasModel=withModel.count(entity.id)>0;
        d.world=mat4::translate(entity.center)*mat4::rotateY(entity.yaw)*mat4::scale(entity.half*2.0f);
        d.frame=mat4::rotateY(entity.yaw);
        if(sourceEntity && editor::authoredEntity(*sourceEntity)) {
            try {
                d.frame=editor::authoredModelMatrix(*sourceEntity);
                d.world=d.frame*mat4::translate({0,entity.half.y,0})*mat4::scale(entity.half*2.0f);
            } catch(const std::exception&) {}
        }
        if(sourceEntity && !entity.light && !d.water && !d.hasModel) {
            const Json& staticModel=sourceEntity->extra["components"]["StaticModel"];
            const bool sphere=staticModel.getStr("asset")=="builtin:sphere";
            const Json& materialRef=staticModel["materials"]["*"];
            if(sphere || materialRef.isString()) if(auto* primitive=builtinPrimitive(sphere)) {
                Json resolved=Json::object();
                if(materialRef.isString())
                    resolved=view::AppearanceAuthoringService::resolveMaterial(materialLibrary_,materialRef);
                if(!resolved["color"].isArray()){
                    Json color=Json::array();
                    for(float c:{entity.color.x,entity.color.y,entity.color.z})color.push(Json::number(double(c),3));
                    resolved.set("color",color);
                }
                d.materials={studioPbrPreviewMaterial(resolved,materialRef.isString()?materialRef.str():"Default")};
                d.primitive=primitive;
            }
        }
        proxyDraws_.push_back(std::move(d));
    }
}
uint64_t StudioScene::frameSignature(int width,int height,const editor::MapDoc& doc,
                                          bool fpsProject,const std::string& selectedId) const {
    uint64_t h=1469598103934665603ull;
    const auto bytes=[&h](const void* data,size_t size){
        const auto* p=static_cast<const unsigned char*>(data);
        for(size_t i=0;i<size;++i){h^=p[i];h*=1099511628211ull;}
    };
    const auto value=[&bytes](const auto& v){bytes(&v,sizeof(v));};
    const auto text=[&](const std::string& v){value(v.size());bytes(v.data(),v.size());};
    const auto ids=[&](const std::set<std::string>& set){value(set.size());for(const auto& id:set)text(id);};
    const auto vec=[&](const float3& v){value(v.x);value(v.y);value(v.z);};
    value(width);value(height);value(doc.revision);value(fpsProject);text(selectedId);
    vec(camera_.target);value(camera_.distance);value(camera_.pitch);value(camera_.yaw);value(camera_.fovY);
    value(camera_.ortho);value(autoIso_);value(tween_.active);
    ids(selection_);ids(hidden_);text(hover_);
    value(tool_);value(localSpace_);value(gizmosVisible_);value(snapEnabled_);value(pivotSnap_);value(orientationGizmo_);
    value(drag_.axis);value(drag_.tool);text(drag_.id);vec(drag_.moveOffset);value(drag_.value);
    value(drag_.accumulated);value(drag_.lastAngle);value(drag_.snapping);
    value(marquee_.on);value(marquee_.x0);value(marquee_.y0);value(marquee_.x1);value(marquee_.y1);
    value(wind_.revision);value(wind_.enabled);value(wind_.speed);value(wind_.directionDeg);
    value(wind_.gust);value(wind_.scale);value(wind_.debug);
    value(brushVisible_);vec(brushCenter_);value(brushRadius_);
    value(overlayShapes_.size());
    for(const auto& shape:overlayShapes_){value(shape.kind);vec(shape.a);vec(shape.b);value(shape.radius);
        value(shape.thickness);value(shape.r);value(shape.g);value(shape.bl);}
    value(lightingRevision_);value(terrainRevision_);
    value(modelInstances_.size());value(documentModels_.size());value(loadProgress_.done);
    return h;
}
Json StudioScene::windStats() const {
    Json j=Json::object();
    j.set("riggedInstances",Json::number(int64_t(riggedInstances_)));
    j.set("riggedParts",Json::number(int64_t(riggedParts_)));
    j.set("models",Json::number(int64_t(documentModels_.size())));
    return j;
}
}
