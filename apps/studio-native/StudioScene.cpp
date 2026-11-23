#include "apps/editor/AuthoringWater.h"
#include "apps/studio-native/StudioScene.h"
#include "apps/studio-native/StudioFont6x11.h"
#include "engine/render/CameraPng.h"
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
void StudioScene::setTransformTool(TransformTool tool) { tool_ = tool; drag_ = TransformDrag{}; }
void StudioScene::clearTransformPreview() { drag_ = TransformDrag{}; }
void StudioScene::releasePreviewModel(render::Model& model) {
    // Retire CPU packets/layouts before the Model and its geometry disappear.
    renderer_.invalidateModelCachesFor(&model);
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
std::unique_ptr<render::Model> StudioScene::loadContentModel(const std::string& projectPath,const std::string& asset) {
    namespace fs=std::filesystem;
    if(asset.rfind("content/",0)!=0 || asset.size()>512 || projectPath.empty())return nullptr;
    const fs::path root=fs::path(projectPath)/"content";
    // Same containment as the Scene's model loader: no "..", no symlinks, content/ only.
    auto read=[root](const std::string& name,std::string& out)->bool {
        if(name.rfind("content/",0)!=0 || name.size()>512)return false;
        const fs::path rel(name.substr(8));
        if(rel.empty()||rel.is_absolute())return false;
        fs::path file=root;
        for(const auto& part:rel){ if(part.empty()||part=="."||part=="..")return false; file/=part;
            std::error_code ec;const auto st=fs::symlink_status(file,ec);if(ec||fs::is_symlink(st))return false; }
        std::error_code ec;
        if(!fs::is_regular_file(file,ec))return false;
        const auto bytes=fs::file_size(file,ec);
        if(ec||bytes>256ull*1024ull*1024ull)return false;
        std::ifstream input(file,std::ios::binary);if(!input)return false;
        out.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
        return bool(input)||input.eof();
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
                try { const Json parsed=Json::parse(text)["materials"]; if(parsed.isObject())materialLibrary_=parsed; }
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
            const render::mat4 placement=editor::authoredEntity(entity) ? editor::authoredModelMatrix(entity) :
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
                        view::AppearanceAuthoringService::applyMaterial(cached.materials[i],materialLibrary_,layer);
            }
            overridden.insert(proxy.id);
        }
        modelInstances_.push_back({proxy.id,found->second.get(),
           editor::authoredEntity(entity)?editor::authoredModelMatrix(entity):
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

StudioScene::~StudioScene() {
    if (device_) {
        device_->waitForResourceIdle();
        spectraPreview_.stop(*device_); // before gfx Device and renderer are destroyed
        clearDocumentModels();
        lightingRuntime_.shutdown(*device_,renderer_);
        if(lightingModel_) {
            renderer_.invalidateModelCachesFor(lightingModel_.get());
            releaseModel(*device_,*lightingModel_);lightingModel_.reset();
        }
        post_.shutdown();
        (void)facialGpu_.clear(*device_,renderer_);
        if(studioCyclorama_.vb.valid())device_->destroyBuffer(studioCyclorama_.vb);
        if(studioCyclorama_.ib.valid())device_->destroyBuffer(studioCyclorama_.ib);
        if (cube_.vb.valid()) device_->destroyBuffer(cube_.vb);
        if (cube_.ib.valid()) device_->destroyBuffer(cube_.ib);
        if (materialSphere_) {
            releasePreviewModel(*materialSphere_);
            materialSphere_.reset();
        }
        for (auto* primitive:{&builtinCube_,&builtinSphere_})
            if (*primitive) { releasePreviewModel(**primitive); primitive->reset(); }
        if (terrain_.vb.valid()) device_->destroyBuffer(terrain_.vb);
        if (terrain_.ib.valid()) device_->destroyBuffer(terrain_.ib);
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
    if (materialMode_ || facialMode_ || !cameraInitialized_ ||
        !viewCamera().screenRay(x,y,width,height,origin,direction)) return {};
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
bool StudioScene::groundPoint(float x,float y,float width,float height,float3& out) const {
    float3 o,d;
    if(!cameraInitialized_||!viewCamera().screenRay(x,y,width,height,o,d)||std::fabs(d.y)<1e-5f)return false;
    const float t=-o.y/d.y; if(!(t>0)||!std::isfinite(t))return false;
    out=o+d*t; return true;
}
void StudioScene::frameAll(const editor::MapDoc& map,bool fpsProject) {
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
    for(int i=0;i<=N;++i){
        const float a=float(i)/N*6.2831853f;
        const float3 p{brushCenter_.x+std::cos(a)*brushRadius_,brushCenter_.y+0.05f,brushCenter_.z+std::sin(a)*brushRadius_};
        float2 s;const bool ok=view.worldToScreen(p,W,H,s);
        if(ok&&prevOk)o.segment(prev.x,prev.y,s.x,s.y,w,rgba(120,200,255,220));
        prev=s;prevOk=ok;
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
    const bool local=(tool_==TransformTool::Scale||localSpace_)&&selectedAxesValid_;
    for(int a=0;a<3;++a)out[a]=local?selectedAxes_[a]:float3{a==0?1.f:0.f,a==1?1.f:0.f,a==2?1.f:0.f};
}
int StudioScene::transformGizmoHit(float x,float y,float width,float height) const {
    if(materialMode_||!gizmosVisible_||tool_==TransformTool::View||!selectedPivotValid_||width<4||height<4)return -1;
    if(selectedIsLight_&&tool_!=TransformTool::Move)return -1;
    const auto view=viewCamera();
    float3 axes[3];handleAxes(axes);
    const auto h=axisHandles(view,selectedPivot_,axes,width,height);
    if(!h.valid)return -1;
    int best=-1;float bestD=h.pick;
    // Plane squares (all tools): Move = drag in the plane, Scale = both plane axes,
    // Rotate = turn about the plane's normal (same as that axis's ring).
    for(int n=0;n<3;++n){
        float2 q[4];
        if(planeQuad(view,selectedPivot_,axes,n,h.worldLength,width,height,q)&&insideQuad(q,x,y))
            return tool_==TransformTool::Rotate?n:4+n;
    }
    if(tool_==TransformTool::Rotate) {
        for(int a=0;a<3;++a){
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
    return true;
}
void StudioScene::updateTransformDrag(float x,float y,float width,float height,bool invertSnap) {
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
    if(drag_.axis<0)return std::nullopt;
    TransformCommit c;c.id=drag_.id;c.tool=drag_.tool;c.axis=drag_.axis;
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
// Real Scene-device render target placed inside the SpectraFX Graph dock.
// This is deliberately a *mode* on the existing render-thread-owned
// CAMetalLayer/Vulkan surface, NOT a second graphics context or HTML frame.
// The map is not submitted while FX authoring is active.
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
    if(spectraPreview_.active()) {
        spectraPreview_.prepare(fx::SpectraFxView::fromCamera(
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
    renderer_.render(pass,&antialiasing_);
    if(spectraPreview_.active())spectraPreview_.draw();
    renderer_.resolvePostFx();
    ++framesDrawn_;
    // Record the state actually used by this dock frame, before submission.
    Json provenance=spectraPreviewCommand(Json::parse(R"({"action":"status"})"),spectraProject_);
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
    if(spectraPreview_.active() && spectraProject_!=projectRoot)
        spectraPreview_.stop(*device_);
    facialReady_=false;
    facialError_.clear();
    if (!device_ || framebufferWidth < 2 || framebufferHeight < 2) return;
    if(spectraDockMode_) {
        // A dedicated VFX authoring viewport, not the authored map.
        frameSpectraDock(framebufferWidth,framebufferHeight);
        return;
    }
    if(antialiasing_.pendingTargetRetirements())antialiasing_.collectRetiredTargets();
    if (renderer_.pendingProgramRetirements()) renderer_.collectRetiredPrograms();
    if (renderer_.pendingTargetRetirements()) renderer_.collectRetiredTargets();
    if (renderer_.hasPendingModelPipelineJobs()) renderer_.pollPendingModelPipelineJobs();
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
        if (materialRequested) {
            if (!materialSphere_) {
                // Same primitive->Model->GPU path used by AIVERSE's HDR material gallery.
                render::MeshBuilder sphere;
                sphere.color(render::rgba(255,255,255));
                sphere.sphere({0,0,0},1.0f,32,48);
                render::Material base;
                base.vertexColors=false;
                materialSphere_ = render::modelFromParts("studio/pbr-preview",
                    {{"PBR sphere",&sphere,base,render::mat4{}}});
                if (!materialSphere_) throw std::runtime_error("Material preview geometry missing");
                materialSphere_->batching.autoLod=false;
                render::prepareModelUpload(*materialSphere_);
                if (!render::uploadModel(*device_,*materialSphere_))
                    throw std::runtime_error("Material preview GPU upload failed");
            }
            materialSphere_->materials.at(0)=studioPbrPreviewMaterial(previewFactors,previewName);
            materialSphere_->markMaterialChanged(); // invalidate renderer-owned material signature
        }
    }
    if (materialMode_ != materialRequested) {
        materialMode_=materialRequested;
        lightingRevision_=std::numeric_limits<uint64_t>::max();
        cameraInitialized_=false;
    }
    if (materialMode_) {
        lightingStatus_="Material preview · GI: off · Direct: standard";
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
        renderer_.beginScene(viewCamera(),framebufferWidth,framebufferHeight);
        renderer_.clearDynamicGiOverlay();
        renderer_.lighting=render::Lighting{};
        renderer_.lighting.sunIntensity=1.2f;
        renderer_.lighting.skyColor={.42f,.51f,.66f};
        renderer_.lighting.groundColor={.27f,.27f,.31f};
        renderer_.lighting.fogDensity=0;
        renderer_.lights.add({1.5f,2.4f,1.6f},6,{1,.9f,.78f},2.5f);
        if (materialSphere_) {
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
        renderer_.render(previewPass,&antialiasing_);
        renderer_.resolvePostFx();

        return;
    }
    const auto& map = doc.data;
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
    const bool showTerrain=showTerrain_;
    snapPivots_.clear();
    // Same pivot as the Move gizmo: bottom centre (lights: centre).
    for (const auto& proxy:proxies) if (!hidden_.count(proxy.id))
        snapPivots_.push_back({proxy.id,proxy.light?proxy.center:float3{proxy.center.x,proxy.boundsMin.y,proxy.center.z}});

    if (terrainRevision_ != doc.revision || terrainLevelHeight_ != levelHeight || (showTerrain && !terrain_.vb.valid())) {
        // A document edit already makes the lighting stale (debounced below);
        // force an immediate rebuild only for the other terrain causes.
        if (terrainRevision_ == doc.revision) lightingRevision_=std::numeric_limits<uint64_t>::max();
        terrainLevelHeight_ = levelHeight;
        MeshBuilder source;
        Mesh replacement;
        if(showTerrain){const auto stats=buildTerrainPreview(map,source,levelHeight);if(stats.quads)replacement=source.upload(*device_);}
        if (terrain_.vb.valid()) device_->destroyBuffer(terrain_.vb);
        if (terrain_.ib.valid()) device_->destroyBuffer(terrain_.ib);
        terrain_ = replacement;
        terrainRevision_ = doc.revision;
    }
    const Json& settings=lightingSettings_;
    const bool useLighting=useLighting_;
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
        if(useLighting || fpsProject) {
            std::set<std::string> modelIds;
            for(const auto& instance:modelInstances_)modelIds.insert(instance.id);
            lightingModel_=studioLightingPreview(doc,fpsProject,levelHeight,modelIds);
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
        if(fpsProject && lightingModel_) {
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
    {
        // Redraw only when something visible changed (like an editor viewport with
        // Realtime off), plus settle frames for temporal effects and a 1 s heartbeat.
        constexpr int kSettleFrames=30;
        const uint64_t signature=frameSignature(framebufferWidth,framebufferHeight,doc,fpsProject,selectedId);
        // Pending Assets thumbnails render inside a drawn frame (one per frame).
        const bool animated=continuousRedraw_ || tween_.active || modelsPending_ || !thumbJobs_.empty() ||
            lightingRevision_!=doc.revision || nereid_.configuredSurfaces()>0 ||
            (wind_.enabled && riggedInstances_>0) || day.clouds>0 || lodDiagnostics_ ||
            (lightingRuntime_.indirect && !lightingRuntime_.indirect->converged) ||
            !thumbJobs_.empty() || spectraPreview_.needsFrame(); // native FX playback forces draw
        if(animated || signature!=lastFrameSignature_) settleFrames_=kSettleFrames;
        lastFrameSignature_=signature;
        if(settleFrames_>0) --settleFrames_;
        else if(now-lastDrawn_<std::chrono::seconds(1)) { ++framesSkipped_; return; }
        lastDrawn_=now; ++framesDrawn_;
    }
    // proxies/syncDocumentModels ran above: lighting needs modelInstances_ first.
    device_->beginFrame(framebufferWidth, framebufferHeight);
    StudioFrameScope scope{*device_};
    if(spectraPreview_.active()) {
        // Device-safe point: GPU simulation update before any encoder/pass.
        const float aspect=float(framebufferWidth)/float(framebufferHeight);
        const auto viewProj=view.proj(aspect,device_->info().clipDepthZeroToOne)*view.view();
        spectraPreview_.prepare(fx::SpectraFxView::fromCamera(
            view,viewProj,framebufferWidth,framebufferHeight),camera_.target);
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
    if(!lightingRuntime_.direct || !lightingRuntime_.direct->enabled)
        for(const auto& light:activeLights)renderer_.lights.add(light);
    lightingRuntime_.update(*device_,renderer_,camera_.target);
    if(lightingRuntime_.indirect || lightingRuntime_.direct)lightingStatus_=lightingRuntime_.status();

    // Real map dimensions and positions from the active native Workspace.
    // Temporary primitive preview (NOT legacy editor terrain, animation or materials).
    const float w = float(std::max(map.w, 1));
    const float h = float(std::max(map.h, 1));
    if(lightingModel_) {
        // Shared lit preview geometry (terrain + document proxies) owned by the lighting runtime.
        ModelDraw draw;draw.model=lightingModel_.get();renderer_.drawModel(draw);
    }
    if(showTerrain){
    if(lightingModel_) {
        // Terrain is already part of the lit preview model.
    } else if (terrain_.vb.valid() && terrain_.ib.valid()) {
        renderer_.drawMesh(terrain_, mat4{}, {1,1,1,1}, kDrawNoCastShadow);
    } else {
        // Unsupported planet maps or empty terrain: basic plane only.
        renderer_.drawMesh(cube_,
                           mat4::translate({w*.5f,-0.08f,h*.5f}) * mat4::scale({w,.12f,h}),
                           {0.18f,0.26f,0.33f,1}, kDrawNoCastShadow);
    }
    const int xStep = std::max(1, map.w / 48);
    const int zStep = std::max(1, map.h / 48);
    for (int x=0; !fpsProject && x<=map.w; x+=xStep)
        renderer_.line({float(x),.01f,0},{float(x),.01f,h},rgba(55,84,109,120));
    for (int z=0; !fpsProject && z<=map.h; z+=zStep)
        renderer_.line({0,.01f,float(z)},{w,.01f,float(z)},rgba(55,84,109,120));

    } // external/Terra surface suppresses the default plane and grid

    selectedId_=selectedId;
    selectedPivotValid_=false;
    selectedBoxValid_=false;
    for(auto& cached:proxyDraws_) {
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
    riggedInstances_=0; riggedParts_=0;
    const double windSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-windClock_).count();
    for(const auto& instance:modelInstances_) {
        if(hidden_.count(instance.id))continue;
        render::ModelDraw draw;
        draw.model=instance.model;
        draw.world=instance.world;
        if(!drag_.id.empty() && instance.id==drag_.id) draw.world=dragPreview()*instance.world;
        if(auto custom=instanceMaterials_.find(instance.id);custom!=instanceMaterials_.end())
            draw.materials=&custom->second.materials;
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
    nereid_.draw(renderer_);
    // Editor gizmos for authored lights; actual light contribution is passed through
    // Renderer3D::lights, not faked with an overlay screenshot.
    for(const auto& light:activeLights) {
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
    renderer_.render(pass,useLighting?lightingRuntime_.pipeline(post_):static_cast<PostProcess*>(&post_));
    // Renderer3D keeps the HDR scene pass open; GPU/CPU FX draw here,
    // before PRISMA resolves it and before the frame/pass are retired.
    if(spectraPreview_.active())spectraPreview_.draw();
    renderer_.resolvePrisma();
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
