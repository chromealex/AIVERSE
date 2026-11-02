// View scripts: presentation-only Lua VM (see RtsViewScript.h).
#include "RtsViewScript.h"
#include <map>
#include <cmath>
#include <algorithm>
#include "../RtsModule.h"
#include "../RtsScriptInternal.h"

namespace rts::game {
using namespace script;

namespace {
const char* eventName(uint16_t t) { return simEventName(t); }   // every GameEvent (RtsScript.cpp table)
struct Collector { std::vector<SimEvent> events; bool enabled = true; };
} // namespace

struct ViewScripts::Impl {
    ViewScripts& self;
    Vm vm;
    std::map<std::string, std::vector<int>> handlers;   // event -> function refs (LUA_NOREF: disabled)
    std::vector<int> frame;
    std::vector<ViewRequest> requests;
    std::vector<Command> commands;
    std::vector<UiRequest> ui;
    std::map<std::string, std::string> uiLast;          // key -> last value sent (unchanged values are not resent)
    std::map<std::string, std::vector<int>> uiHandlers;
    uint32_t stateTick = UINT32_MAX;                   // view.state(): copy of the simulation's `state`, once per tick
    uint64_t stateEpoch = 0;
    int stateRef = LUA_NOREF;
    std::shared_ptr<Collector> collector;
    uint8_t player = 0;
    ViewTargeting targeting;
    ViewCamera camera;
    std::string screen = "playing";   // host screen state (view.screen()): playing | paused | over
    ViewCameraRequest cameraRequest{};
    bool cameraRequested = false;
    std::vector<ViewDrawBatch> draws;
    size_t drawVerts = 0;
    std::vector<ViewGroundBatch> groundDraws;
    size_t groundVerts = 0;
    std::shared_ptr<render::PaintLayers> paint = std::make_shared<render::PaintLayers>();
    ScriptSound* sound = nullptr;   // the `sound` table's host
    explicit Impl(ViewScripts& s) : self(s) {}

    void logLine(bool error, const std::string& t) {
        if (self.log_.size() > 300) self.log_.erase(self.log_.begin());
        self.log_.push_back({error, vm.sim ? vm.sim->tick() : 0, t});
        if (self.onLog) self.onLog(self.log_.back());
    }
    void call(int& ref, int nargs) {
        lua_rawgeti(vm.L, LUA_REGISTRYINDEX, ref);
        lua_insert(vm.L, -nargs - 1);
        std::string err;
        if (protectedCall(vm, nargs, 0, &err)) return;
        logLine(true, err + " [view handler disabled]");
        luaL_unref(vm.L, LUA_REGISTRYINDEX, ref);
        ref = LUA_NOREF;
    }
};

namespace {
ViewScripts::Impl& viewOf(Vm& vm) { return *static_cast<ViewScripts::Impl*>(vm.owner); }

float numberField(lua_State* L, int table, const char* key, float fallback) {
    lua_getfield(L,table,key); float v=lua_isnil(L,-1)?fallback:float(toDouble(L,-1)); lua_pop(L,1); return v;
}
render::PaintLayer* layerOf(lua_State* L, Vm& vm) {
    if(!lua_istable(L,1)) return nullptr;
    lua_getfield(L,1,"id"); int id=int(lua_tointeger(L,-1)); lua_pop(L,1);
    auto& layers=viewOf(vm).paint->layers; auto it=layers.find(id);
    return it==layers.end()?nullptr:&it->second;
}
int vPaintClear(lua_State* L,Vm& vm) {
    if(auto* p=layerOf(L,vm)){p->clear=true;p->copyFrom=0;p->strokes.clear();} return 0;
}
int vPaintCopy(lua_State* L,Vm& vm) {
    auto* p=layerOf(L,vm);if(!p||p->territoryOwner||p->textureId||!lua_istable(L,2))return fail(vm,"copy_from needs two manual canvases");
    lua_getfield(L,1,"id");int dest=int(lua_tointeger(L,-1));lua_pop(L,1);
    lua_getfield(L,2,"id");int source=int(lua_tointeger(L,-1));lua_pop(L,1);
    auto& layers=viewOf(vm).paint->layers;auto it=layers.find(source);
    if(source>=dest||it==layers.end()||it->second.textureId)return fail(vm,"copy source must be an earlier-created canvas");
    p->clear=false;p->strokes.clear();p->copyFrom=source;return 0;
}
int vPaintDestroy(lua_State* L,Vm& vm) {
    if(lua_istable(L,1)){lua_getfield(L,1,"id");viewOf(vm).paint->layers.erase(int(lua_tointeger(L,-1)));lua_pop(L,1);}return 0;
}
int vPaintOpacity(lua_State* L,Vm& vm) {
    auto* p=layerOf(L,vm);if(!p)return fail(vm,"invalid paint layer");
    const float value=float(toDouble(L,2));
    if(!std::isfinite(value))return fail(vm,"non-finite opacity");
    p->opacity=std::clamp(value,0.f,1.f);return 0;
}
int vPaintStroke(lua_State* L,Vm& vm) {
    auto* p=layerOf(L,vm); if(!p||p->territoryOwner||p->textureId)return fail(vm,"stroke needs a manual canvas; paint linked textures directly");
    if(!lua_istable(L,6))return fail(vm,"layer:stroke(x0,z0,x1,z1,{radius=...,softness=...})");
    float radius=numberField(L,6,"radius",0.5f),soft=numberField(L,6,"softness",0.1f);
    float ax=float(toDouble(L,2)),az=float(toDouble(L,3)),bx=float(toDouble(L,4)),bz=float(toDouble(L,5));
    for(float v:{radius,soft,ax,az,bx,bz})if(!std::isfinite(v))return fail(vm,"non-finite paint stroke");
    if(radius<=0||radius>p->h*4||soft<0||p->strokes.size()>=8192)return fail(vm,"paint stroke limit");
    // UV distance metric matches texture aspect. Layers require equal texel density in x and z.
    p->strokes.push_back({(ax-p->x)/p->w,(az-p->z)/p->h,(bx-p->x)/p->w,(bz-p->z)/p->h,radius/p->h,soft/p->h,{1,1,1,1}});
    return 0;
}
// layer:set_cells(bytes): a string of width * height id bytes (row-major) for a categorical layer
int vPaintCells(lua_State* L,Vm& vm) {
    auto* p=layerOf(L,vm);if(!p||!p->categorical)return fail(vm,"set_cells needs a categorical layer (paint_layer{cells=...})");
    size_t n=0;const char* bytes=lua_tolstring(L,2,&n);
    if(!bytes||n!=size_t(p->cellsW)*size_t(p->cellsH))return fail(vm,"set_cells expects a string of width * height bytes");
    p->cells.assign(reinterpret_cast<const uint8_t*>(bytes),reinterpret_cast<const uint8_t*>(bytes)+n);p->cellsDirty=true;return 0;
}
// layer:set_palette{[id] = {r, g, b [, a]}} (0..1): replaces the given entries
int vPaintPalette(lua_State* L,Vm& vm) {
    auto* p=layerOf(L,vm);if(!p||!p->categorical||!lua_istable(L,2))return fail(vm,"set_palette needs a categorical layer and a table");
    lua_pushnil(L);
    while(lua_next(L,2)){
        if(lua_isinteger(L,-2)&&lua_istable(L,-1)){
            const lua_Integer id=lua_tointeger(L,-2);
            if(id<0||id>255){lua_pop(L,2);return fail(vm,"palette ids are 0..255");}
            for(int k=0;k<4;++k){lua_rawgeti(L,-1,k+1);float v=lua_isnil(L,-1)?1.f:float(toDouble(L,-1));lua_pop(L,1);
                if(!std::isfinite(v)){lua_pop(L,2);return fail(vm,"invalid palette colour");}
                p->palette[size_t(id)*4+size_t(k)]=uint8_t(std::clamp(v,0.f,1.f)*255.f+.5f);}
        }
        lua_pop(L,1);
    }
    p->paletteDirty=true;return 0;
}
int vPaintLayer(lua_State* L,Vm& vm) {
    auto& store=*viewOf(vm).paint;
    if(!lua_istable(L,1)||store.layers.size()>=16)return fail(vm,"paint_layer expects options; maximum 16 layers");
    render::PaintLayer p;
    float res=numberField(L,1,"resolution",512);
    if(!std::isfinite(res)||res<32||res>2048)return fail(vm,"paint resolution must be 32..2048");
    lua_getfield(L,1,"bounds");
    if(!lua_istable(L,-1)){lua_pop(L,1);return fail(vm,"paint bounds must be {x,z,width,height}");}
    float* bounds[]={&p.x,&p.z,&p.w,&p.h};
    for(int i=0;i<4;++i){lua_rawgeti(L,-1,i+1);*bounds[i]=float(toDouble(L,-1));lua_pop(L,1);}lua_pop(L,1);
    for(float* v:bounds)if(!std::isfinite(*v)||std::abs(*v)>100000)return fail(vm,"invalid paint bounds");
    if(p.w<=0||p.h<=0)return fail(vm,"paint bounds must have positive size");
    p.width=std::max(1,int(res*std::min(1.f,p.w/p.h)));p.height=std::max(1,int(res*std::min(1.f,p.h/p.w)));
    p.opacity=numberField(L,1,"opacity",0.45f);p.border=numberField(L,1,"border",0.06f);p.lift=numberField(L,1,"lift",0.08f);
    for(float v:{p.opacity,p.border,p.lift})if(!std::isfinite(v))return fail(vm,"invalid paint style");
    p.opacity=std::clamp(p.opacity,0.f,1.f);p.border=std::clamp(p.border,0.f,0.4f);p.lift=std::clamp(p.lift,0.f,2.f);
    lua_getfield(L,1,"color");if(lua_istable(L,-1))for(int i=0;i<3;++i){lua_rawgeti(L,-1,i+1);float v=float(toDouble(L,-1));lua_pop(L,1);if(!std::isfinite(v)){lua_pop(L,1);return fail(vm,"invalid paint color");}p.color[i]=std::clamp(v,0.f,1.f);}lua_pop(L,1);
    lua_getfield(L,1,"landOnly");if(!lua_isnil(L,-1))p.landOnly=lua_toboolean(L,-1);lua_pop(L,1);
    lua_getfield(L,1,"source");const std::string source=lua_isstring(L,-1)?lua_tostring(L,-1):"";lua_pop(L,1);
    if(!source.empty()&&source!="territory")return fail(vm,"unknown paint source");
    if(source=="territory") {float owner=numberField(L,1,"owner",0);if(!std::isfinite(owner)||owner<1||owner>255)return fail(vm,"territory layer needs owner 1..255");p.territoryOwner=int(owner);}
    lua_getfield(L,1,"cells");
    if(lua_istable(L,-1)){   // categorical raster: {width, height}
        float cw=numberField(L,lua_gettop(L),"width",0),ch=numberField(L,lua_gettop(L),"height",0);
        lua_pop(L,1);
        if(p.territoryOwner||!std::isfinite(cw)||!std::isfinite(ch)||cw<1||ch<1||cw>2048||ch>2048||cw!=std::floor(cw)||ch!=std::floor(ch))
            return fail(vm,"paint_layer cells must be {width=1..2048, height=1..2048} (not with a territory source)");
        p.categorical=true;p.cellsW=int(cw);p.cellsH=int(ch);p.width=p.cellsW;p.height=p.cellsH;
        p.cells.assign(size_t(p.cellsW)*size_t(p.cellsH),0);p.cellsDirty=true;p.paletteDirty=true;
        p.borderWidth=numberField(L,1,"borderWidth",0.55f);p.smooth=numberField(L,1,"smooth",2.0f);
        const float outside=numberField(L,1,"outside",255),density=numberField(L,1,"density",1);
        const float transparent=numberField(L,1,"transparent",-1);
        if(!std::isfinite(transparent)||transparent<-1||transparent>255||transparent!=std::floor(transparent))return fail(vm,"paint_layer transparent must be an id 0..255 (or -1)");
        p.transparent=int(transparent);
        if(!std::isfinite(density)||density<1||density>4||density!=std::floor(density)||p.cellsW*int(density)>4096||p.cellsH*int(density)>4096)return fail(vm,"paint_layer density must be 1..4 (texture side <= 4096)");
        p.density=int(density);
        if(!std::isfinite(p.borderWidth)||!std::isfinite(p.smooth)||!std::isfinite(outside))return fail(vm,"invalid cell style");
        p.borderWidth=std::clamp(p.borderWidth,0.f,2.f);p.smooth=std::clamp(p.smooth,0.75f,2.f);p.outside=std::clamp(int(outside),0,255);
        lua_getfield(L,1,"borderColor");
        if(lua_istable(L,-1))for(int i=0;i<4;++i){lua_rawgeti(L,-1,i+1);float v=lua_isnil(L,-1)?1.f:float(toDouble(L,-1));lua_pop(L,1);if(!std::isfinite(v)){lua_pop(L,1);return fail(vm,"invalid border colour");}p.borderColor[size_t(i)]=std::clamp(v,0.f,1.f);}
        lua_pop(L,1);
        // ground = {scale, strength, flipHeight, team = {min, max}, context = {min, max}, coast = {sea, drop, beach,
        // shallow, earthNear, earthFar, foam}}: meadow / coast surface (render::PaintLayer, docs § Categorical cell layers)
        lua_getfield(L,1,"ground");
        if(lua_istable(L,-1)){
            const int gi=lua_gettop(L);p.ground=true;
            lua_getfield(L,gi,"linear");p.groundLinear=lua_toboolean(L,-1);lua_pop(L,1);
            p.groundScale=numberField(L,gi,"scale",8);p.groundStrength=numberField(L,gi,"strength",.12f);p.flipHeight=numberField(L,gi,"flipHeight",0);
            if(!std::isfinite(p.groundScale)||p.groundScale<=0||!std::isfinite(p.groundStrength)||!std::isfinite(p.flipHeight)){lua_pop(L,1);return fail(vm,"invalid paint ground");}
            p.groundStrength=std::clamp(p.groundStrength,0.f,1.f);
            auto range=[&](const char* key,int& lo,int& hi)->bool{
                lua_getfield(L,gi,key);bool ok=true;
                if(lua_istable(L,-1)){lua_rawgeti(L,-1,1);lua_rawgeti(L,-2,2);const double a=toDouble(L,-2),b=toDouble(L,-1);lua_pop(L,2);
                    ok=std::isfinite(a)&&std::isfinite(b)&&a>=0&&b<=255&&a<=b;if(ok){lo=int(a);hi=int(b);}}
                lua_pop(L,1);return ok;};
            if(!range("team",p.teamMin,p.teamMax)||!range("context",p.contextMin,p.contextMax)){lua_pop(L,1);return fail(vm,"paint ground ranges must be {min, max} ids 0..255");}
            lua_getfield(L,gi,"coast");
            if(lua_istable(L,-1)){
                const int ci=lua_gettop(L);p.coast=true;
                const float sea=numberField(L,ci,"sea",255),drop=numberField(L,ci,"drop",-1);
                if(!std::isfinite(sea)||sea<0||sea>255||!std::isfinite(drop)){lua_pop(L,2);return fail(vm,"invalid paint coast");}
                p.seaId=int(sea);p.coastDrop=std::clamp(drop,-1.f,1.f);
                auto colour=[&](const char* key,std::array<float,3>& c)->bool{
                    lua_getfield(L,ci,key);bool ok=true;
                    if(lua_istable(L,-1))for(int i=0;i<3;++i){lua_rawgeti(L,-1,i+1);const double v=toDouble(L,-1);lua_pop(L,1);if(!std::isfinite(v))ok=false;else c[size_t(i)]=std::clamp(float(v),0.f,1.f);}
                    lua_pop(L,1);return ok;};
                if(!colour("beach",p.beach)||!colour("shallow",p.shallow)||!colour("earthNear",p.earthNear)||!colour("earthFar",p.earthFar)||!colour("foam",p.foam)){lua_pop(L,2);return fail(vm,"invalid paint coast colour");}
            }
            lua_pop(L,1);
        }
        lua_pop(L,1);
    } else lua_pop(L,1);
    if(p.territoryOwner){
        const auto& grid=vm.module->state(*vm.sim).grid;
        if(p.x!=0||p.z!=0||p.w!=grid.w||p.h!=grid.h)return fail(vm,"territory bounds must cover the complete map");
        p.width=grid.w;p.height=grid.h;
    }
    lua_getfield(L,1,"texture");
    if(lua_istable(L,-1)){
        if(p.territoryOwner){lua_pop(L,1);return fail(vm,"choose texture or territory source, not both");}
        lua_getfield(L,-1,"id");p.textureId=int(lua_tointeger(L,-1));lua_pop(L,1);
        if(!store.layers.count(p.textureId)){lua_pop(L,1);return fail(vm,"invalid paint texture");}
        if(store.layers.at(p.textureId).textureId){lua_pop(L,1);return fail(vm,"cannot chain paint texture references");}
    }lua_pop(L,1);
    if(p.categorical&&p.textureId)return fail(vm,"a categorical layer has its own cells (no texture)");
    size_t pixels=p.textureId?0:size_t(p.width)*p.height;
    for(auto& [id,l]:store.layers)if(!l.textureId)pixels+=size_t(l.width)*l.height;
    if(pixels>8*1024*1024)return fail(vm,"paint layers exceed 8 million pixels");
    const int id=store.nextId++;store.layers[id]=std::move(p);
    lua_newtable(L);lua_pushinteger(L,id);lua_setfield(L,-2,"id");
    lua_pushcfunction(L,bind<vPaintStroke>);lua_setfield(L,-2,"stroke");
    lua_pushcfunction(L,bind<vPaintOpacity>);lua_setfield(L,-2,"set_opacity");
    lua_pushcfunction(L,bind<vPaintCopy>);lua_setfield(L,-2,"copy_from");
    lua_pushcfunction(L,bind<vPaintClear>);lua_setfield(L,-2,"clear");
    lua_pushcfunction(L,bind<vPaintDestroy>);lua_setfield(L,-2,"destroy");
    lua_pushcfunction(L,bind<vPaintCells>);lua_setfield(L,-2,"set_cells");
    lua_pushcfunction(L,bind<vPaintPalette>);lua_setfield(L,-2,"set_palette");return 1;
}

int vRenderTexture(lua_State* L,Vm& vm) {
    if(!lua_istable(L,1))return fail(vm,"render_texture expects {width,height}");
    float w=numberField(L,1,"width",512),h=numberField(L,1,"height",512);
    if(!std::isfinite(w)||!std::isfinite(h)||w<32||h<32||w>2048||h>2048)return fail(vm,"render texture dimensions must be 32..2048");
    lua_newtable(L);
    for(int i=1;i<=4;++i){lua_pushnumber(L,i==3?w:(i==4?h:0));lua_rawseti(L,-2,i);}lua_setfield(L,1,"bounds");
    lua_pushnumber(L,std::max(w,h));lua_setfield(L,1,"resolution");
    int result=vPaintLayer(L,vm);if(result<0)return result;
    auto& store=*viewOf(vm).paint;store.layers.at(store.nextId-1).surface=false;
    return result;
}

int vOn(lua_State* L, Vm& vm) {
    const char* n = lua_tostring(L, 1);
    if (!n || !lua_isfunction(L, 2)) return fail(vm, "view.on(event, fn)");
    lua_pushvalue(L, 2);
    viewOf(vm).handlers[n].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
int vOnFrame(lua_State* L, Vm& vm) {
    if (!lua_isfunction(L, 1)) return fail(vm, "view.on_frame(fn)");
    lua_pushvalue(L, 1);
    viewOf(vm).frame.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
int vRequest(lua_State* L, Vm& vm, ViewRequest::Kind k) {
    size_t len = 0;
    const char* s = luaL_tolstring(L, 1, &len);
    ViewRequest r;
    r.kind = k;
    r.text.assign(s, len);
    lua_pop(L, 1);
    if (!lua_isnoneornil(L, 2)) { r.x = float(toDouble(L, 2)); r.z = float(toDouble(L, 3)); r.positioned = true; }
    if (k == ViewRequest::Effect) {
        if (!lua_isnoneornil(L, 4)) r.y = float(toDouble(L, 4));
        if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z))
            return fail(vm, "view.effect coordinates must be finite");
    }
    auto& q = viewOf(vm).requests;
    if (q.size() < 256) q.push_back(std::move(r));
    return 0;
}
int vNotice(lua_State* L, Vm& vm) { return vRequest(L, vm, ViewRequest::Notice); }
int vEffect(lua_State* L, Vm& vm) { return vRequest(L, vm, ViewRequest::Effect); }
int vSound(lua_State* L, Vm& vm) { return vRequest(L, vm, ViewRequest::Sound); }
int vTargeting(lua_State* L, Vm& vm) {
    const auto& t=viewOf(vm).targeting;
    lua_newtable(L);
    lua_pushstring(L,t.ability.c_str());lua_setfield(L,-2,"ability");
    lua_pushnumber(L,t.x);lua_setfield(L,-2,"x");
    lua_pushnumber(L,t.z);lua_setfield(L,-2,"z");
    lua_pushboolean(L,t.active);lua_setfield(L,-2,"active");
    lua_pushboolean(L,t.overWorld);lua_setfield(L,-2,"overWorld");
    lua_pushinteger(L,lua_Integer(t.caster));lua_setfield(L,-2,"caster");
    return 1;
}
int vCamera(lua_State* L, Vm& vm) {
    const auto& c = viewOf(vm).camera;
    lua_createtable(L, 0, 8);
    lua_pushnumber(L, c.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, c.z); lua_setfield(L, -2, "z");
    lua_pushnumber(L, c.zoom); lua_setfield(L, -2, "zoom");
    lua_pushnumber(L, c.pitch); lua_setfield(L, -2, "pitch");
    lua_pushnumber(L, c.yaw); lua_setfield(L, -2, "yaw");
    lua_pushinteger(L, c.width); lua_setfield(L, -2, "width");
    lua_pushinteger(L, c.height); lua_setfield(L, -2, "height");
    lua_pushboolean(L, c.ortho); lua_setfield(L, -2, "ortho");
    return 1;
}
// view.set_camera{x =, z =, zoom =, pitch =, yaw =}: any subset; applied by the host after this frame's scripts.
int vSetCamera(lua_State* L, Vm& vm) {
    if (!lua_istable(L, 1)) return fail(vm, "view.set_camera{x =, z =, zoom =, pitch =, yaw =}");
    auto& im = viewOf(vm);
    const float keep = std::nanf("");
    ViewCameraRequest r = im.cameraRequested ? im.cameraRequest : ViewCameraRequest{keep, keep, keep, keep, keep};
    struct F { const char* k; float* v; } fields[] = {{"x", &r.x}, {"z", &r.z}, {"zoom", &r.zoom}, {"pitch", &r.pitch}, {"yaw", &r.yaw}};
    for (auto& f : fields) {
        lua_getfield(L, 1, f.k);
        if (!lua_isnil(L, -1)) {
            const float v = float(toDouble(L, -1));
            if (!std::isfinite(v)) { lua_pop(L, 1); return fail(vm, std::string("view.set_camera: non-finite ") + f.k); }
            *f.v = v;
        }
        lua_pop(L, 1);
    }
    if (std::isfinite(r.zoom) && r.zoom <= 0) return fail(vm, "view.set_camera: zoom must be positive");
    if (std::isfinite(r.pitch)) r.pitch = std::clamp(r.pitch, 5.0f, 90.0f);
    im.cameraRequest = r;
    im.cameraRequested = true;
    // the copy view.to_screen projects with follows at once (the host applies the same request after the scripts)
    auto& v = im.camera.view;
    if (std::isfinite(r.x)) v.target.x = r.x;
    if (std::isfinite(r.z)) v.target.z = r.z;
    if (std::isfinite(r.pitch)) v.pitch = r.pitch / 57.29578f;
    if (std::isfinite(r.yaw)) v.yaw = r.yaw / 57.29578f;
    if (std::isfinite(r.zoom)) v.distance = v.ortho ? r.zoom * 0.5f / std::tan(v.fovY * 0.5f) : r.zoom;
    im.camera.zoom = std::isfinite(r.zoom) ? r.zoom : im.camera.zoom;
    if (std::isfinite(r.x)) im.camera.x = r.x;
    if (std::isfinite(r.z)) im.camera.z = r.z;
    if (std::isfinite(r.pitch)) im.camera.pitch = r.pitch;
    if (std::isfinite(r.yaw)) im.camera.yaw = r.yaw;
    return 0;
}
// view.to_screen(x, z [, y]) -> sx, sy, onScreen: window points (top-left origin), the space of view.draw.
int vToScreen(lua_State* L, Vm& vm) {
    const auto& c = viewOf(vm).camera;
    const float x = float(toDouble(L, 1)), z = float(toDouble(L, 2)), y = lua_isnoneornil(L, 3) ? 0.0f : float(toDouble(L, 3));
    render::float2 sp{0, 0};
    const bool front = c.screenW > 0 && c.screenH > 0 && std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                       c.view.worldToScreen({x, y, z}, c.screenW, c.screenH, sp);
    lua_pushnumber(L, sp.x); lua_pushnumber(L, sp.y);
    lua_pushboolean(L, front && sp.x >= 0 && sp.y >= 0 && sp.x <= c.screenW && sp.y <= c.screenH);
    return 3;
}
// view.draw(verts [, {image =, layer =}]): a triangle list in window points. Without image: x, y, rgba per vertex;
// with image: x, y, u, v, rgba (uv 0..1 over the image, 0, 0 = top left). rgba = 0xRRGGBBAA. At most 1M vertices a frame.
int vDraw(lua_State* L, Vm& vm) {
    auto& im = viewOf(vm);
    if (!lua_istable(L, 1)) return fail(vm, "view.draw(verts [, {image =, layer =}])");
    std::string image; int layer = 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "image"); if (lua_isstring(L, -1)) image = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, 2, "layer"); if (lua_isnumber(L, -1)) layer = int(lua_tointeger(L, -1)); lua_pop(L, 1);
    }
    const size_t stride = image.empty() ? 3 : 5, n = size_t(lua_rawlen(L, 1));
    if (n % (stride * 3) != 0) return fail(vm, "view.draw: vertex data is not a whole number of triangles (" + std::to_string(stride) + " numbers a vertex)");
    const size_t count = n / stride;
    if (im.drawVerts + count > (size_t(1) << 20)) return fail(vm, "view.draw: more than 1048576 vertices this frame");
    std::vector<ViewDrawVertex> out;   // validated first: a failed call adds nothing
    out.reserve(count);
    auto num = [&](size_t i) { lua_rawgeti(L, 1, lua_Integer(i)); double d = lua_tonumber(L, -1); lua_pop(L, 1); return d; };
    for (size_t v = 0; v < count; ++v) {
        const size_t b = v * stride + 1;
        ViewDrawVertex w;
        w.x = float(num(b)); w.y = float(num(b + 1));
        if (stride == 5) { w.u = float(num(b + 2)); w.v = float(num(b + 3)); }
        lua_rawgeti(L, 1, lua_Integer(b + stride - 1));
        w.rgba = uint32_t(lua_Integer(lua_isinteger(L, -1) ? lua_tointeger(L, -1) : lua_Integer(lua_tonumber(L, -1))));
        lua_pop(L, 1);
        if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.u) || !std::isfinite(w.v)) return fail(vm, "view.draw: non-finite vertex");
        out.push_back(w);
    }
    if (im.draws.empty() || im.draws.back().image != image || im.draws.back().layer != layer) {
        im.draws.push_back({});
        im.draws.back().image = image; im.draws.back().layer = layer;
    }
    auto& dst = im.draws.back().verts;
    dst.insert(dst.end(), out.begin(), out.end());
    im.drawVerts += count;
    return 0;
}
// view.draw_ground(verts [, {image =, layer =}]): world triangles, x, y, z, rgba (with image: x, y, z, u, v, rgba)
int vDrawGround(lua_State* L, Vm& vm) {
    auto& im = viewOf(vm);
    if (!lua_istable(L, 1)) return fail(vm, "view.draw_ground(verts [, {image =, layer =}])");
    std::string image; int layer = 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "image"); if (lua_isstring(L, -1)) image = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, 2, "layer"); if (lua_isnumber(L, -1)) layer = int(lua_tointeger(L, -1)); lua_pop(L, 1);
    }
    const size_t stride = image.empty() ? 4 : 6, n = size_t(lua_rawlen(L, 1));
    if (n % (stride * 3) != 0) return fail(vm, "view.draw_ground: vertex data is not a whole number of triangles (" + std::to_string(stride) + " numbers a vertex)");
    const size_t count = n / stride;
    if (im.groundVerts + count > (size_t(1) << 20)) return fail(vm, "view.draw_ground: more than 1048576 vertices this frame");
    std::vector<ViewGroundVertex> out;
    out.reserve(count);
    auto num = [&](size_t i) { lua_rawgeti(L, 1, lua_Integer(i)); double d = lua_tonumber(L, -1); lua_pop(L, 1); return d; };
    for (size_t v = 0; v < count; ++v) {
        const size_t b = v * stride + 1;
        ViewGroundVertex w;
        w.x = float(num(b)); w.y = float(num(b + 1)); w.z = float(num(b + 2));
        if (stride == 6) { w.u = float(num(b + 3)); w.v = float(num(b + 4)); }
        lua_rawgeti(L, 1, lua_Integer(b + stride - 1));
        w.rgba = uint32_t(lua_Integer(lua_isinteger(L, -1) ? lua_tointeger(L, -1) : lua_Integer(lua_tonumber(L, -1))));
        lua_pop(L, 1);
        if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z) || !std::isfinite(w.u) || !std::isfinite(w.v))
            return fail(vm, "view.draw_ground: non-finite vertex");
        out.push_back(w);
    }
    if (im.groundDraws.empty() || im.groundDraws.back().image != image || im.groundDraws.back().layer != layer) {
        im.groundDraws.push_back({});
        im.groundDraws.back().image = image; im.groundDraws.back().layer = layer;
    }
    auto& dst = im.groundDraws.back().verts;
    dst.insert(dst.end(), out.begin(), out.end());
    im.groundVerts += count;
    return 0;
}
int vPlayer(lua_State* L, Vm& vm) { lua_pushinteger(L, viewOf(vm).player); return 1; }
// view.screen(): what the host shows over the match ("playing", "paused" = the pause screen / modal, "over" = the match
// has ended); presentation only (a paused match stops ticking, its view keeps running)
int vScreen(lua_State* L, Vm& vm) { lua_pushstring(L, viewOf(vm).screen.c_str()); return 1; }

// ---- Lua <-> Json for the UI (view values: numbers, fx, strings, booleans, tables)
Json toUiJson(lua_State* L, int idx, int depth = 0) {
    idx = lua_absindex(L, idx);
    if (Fixed* f = testFx(L, idx)) return Json::number(f->toDouble(), 4);
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN: return Json::fromBool(lua_toboolean(L, idx));
    case LUA_TSTRING: { size_t n; const char* c = lua_tolstring(L, idx, &n); return Json::string(std::string(c, n)); }
    case LUA_TNUMBER: return lua_isinteger(L, idx) ? Json::number(int64_t(lua_tointeger(L, idx))) : Json::number(double(lua_tonumber(L, idx)), 4);
    case LUA_TTABLE: {
        if (depth > 16) return Json();
        const lua_Integer n = lua_Integer(lua_rawlen(L, idx));
        if (n > 0) {   // array part
            Json a = Json::array();
            for (lua_Integer i = 1; i <= n; ++i) { lua_rawgeti(L, idx, i); a.push(toUiJson(L, -1, depth + 1)); lua_pop(L, 1); }
            return a;
        }
        Json o = Json::object();   // string keys (lua_next order: the host sees a map, order doesn't matter)
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            if (lua_type(L, -2) == LUA_TSTRING) o.set(lua_tostring(L, -2), toUiJson(L, -1, depth + 1));
            lua_pop(L, 1);
        }
        return o;
    }
    default: return Json();
    }
}
void pushUiJson(lua_State* L, const Json& j, int depth = 0) {
    luaL_checkstack(L, 4, "json too deep");
    switch (j.type()) {
    case Json::Type::Bool: lua_pushboolean(L, j.boolean()); break;
    case Json::Type::String: lua_pushstring(L, j.str().c_str()); break;
    case Json::Type::Number: {
        const Fixed f = j.fx();
        if (f.raw % Fixed::fromInt(1).raw == 0) lua_pushinteger(L, lua_Integer(f.toInt())); else lua_pushnumber(L, f.toDouble());
        break;
    }
    case Json::Type::Array: {
        lua_createtable(L, int(j.items().size()), 0);
        lua_Integer k = 1;
        for (auto& v : j.items()) { pushUiJson(L, v, depth + 1); lua_rawseti(L, -2, k++); }
        break;
    }
    case Json::Type::Object:
        lua_createtable(L, 0, int(j.members().size()));
        for (auto& [k, v] : j.members()) { pushUiJson(L, v, depth + 1); lua_setfield(L, -2, k.c_str()); }
        break;
    default: lua_pushnil(L); break;
    }
}
// ui.set(key, value): a value of data model "script" ({{key}} / {{key.field}} in ui/hud documents)
int uSet(lua_State* L, Vm& vm) {
    const char* k = lua_tostring(L, 1);
    if (!k || !*k) return fail(vm, "ui.set(key, value)");
    auto& im = viewOf(vm);
    Json v = toUiJson(L, 2);
    std::string text = v.dump(0);
    auto it = im.uiLast.find(k);
    if (it != im.uiLast.end() && it->second == text) return 0;
    im.uiLast[k] = std::move(text);
    if (im.ui.size() < 4096) im.ui.push_back({UiRequest::Set, k, std::move(v)});
    return 0;
}
int uOpen(lua_State* L, Vm& vm) {
    const char* n = lua_tostring(L, 1);
    if (!n || !*n) return fail(vm, "ui.open(document [, params])");
    viewOf(vm).ui.push_back({UiRequest::Open, n, lua_isnoneornil(L, 2) ? Json::object() : toUiJson(L, 2)});
    return 0;
}
int uClose(lua_State* L, Vm& vm) {
    const char* n = lua_tostring(L, 1);
    if (!n || !*n) return fail(vm, "ui.close(document)");
    viewOf(vm).ui.push_back({UiRequest::Close, n, Json()});
    return 0;
}
int uOn(lua_State* L, Vm& vm) {
    const char* n = lua_tostring(L, 1);
    if (!n || !lua_isfunction(L, 2)) return fail(vm, "ui.on(event, fn)");
    lua_pushvalue(L, 2);
    viewOf(vm).uiHandlers[n].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
// view.state(): read-only copy of the simulation scripts' `state` (refreshed once per tick)
int vState(lua_State* L, Vm& vm) {
    auto& im = viewOf(vm);
    Simulation& sim = *vm.sim;
    auto* st = sim.world().resource<ScriptStore>();
    if (!st || !st->userScripts) { lua_newtable(L); return 1; }
    if (im.stateRef == LUA_NOREF || im.stateTick != sim.tick() || im.stateEpoch != st->serial) {
        if (ScriptEngine* se = vm.module->scripts()) se->flush(sim);   // what a checksum would write now (no effect on outcomes)
        if (st->state.empty()) lua_newtable(L); else deserialize(L, st->state);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
        if (im.stateRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, im.stateRef);
        im.stateRef = luaL_ref(L, LUA_REGISTRYINDEX);
        im.stateTick = sim.tick();
        im.stateEpoch = st->serial;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, im.stateRef);
    return 1;
}
// view.send(id, args): a rules "commands" command from the local player (args as in on_command; the
// selection goes in args.units). The simulation validates it again when it arrives.
int vSend(lua_State* L, Vm& vm) {
    const char* id = lua_tostring(L, 1);
    ScriptEngine* se = vm.module ? vm.module->scripts() : nullptr;
    const int k = id && se ? se->findCommand(id) : -1;
    if (k < 0) return fail(vm, std::string("view.send: unknown command '") + (id ? id : "?") + "'");
    Command c;
    c.type = kCmdScript;
    c.param = k;
    if (!encodeScriptArgs(L, vm, 2, se->commands()[size_t(k)], c)) return -1;
    auto& q = viewOf(vm).commands;
    if (q.size() < 64) q.push_back(std::move(c));
    lua_pushboolean(L, 1);
    return 1;
}
} // namespace

ViewScripts::ViewScripts() : impl_(std::make_unique<Impl>(*this)) {}
ViewScripts::~ViewScripts() { detach(); }
std::shared_ptr<render::PaintLayers> ViewScripts::paintLayers() const { return impl_->paint; }

void ViewScripts::setTargeting(const ViewTargeting& target) { impl_->targeting=target; }
void ViewScripts::setCamera(const ViewCamera& camera) { impl_->camera = camera; }
void ViewScripts::setScreen(const std::string& screen) { impl_->screen = screen.empty() ? "playing" : screen; }
bool ViewScripts::takeCameraRequest(ViewCameraRequest& out) {
    if (!impl_->cameraRequested) return false;
    out = impl_->cameraRequest;
    impl_->cameraRequested = false;
    return true;
}

std::vector<ViewDrawBatch> ViewScripts::takeDraws() { impl_->drawVerts = 0; return std::move(impl_->draws); }
std::vector<ViewGroundBatch> ViewScripts::takeGroundDraws() { impl_->groundVerts = 0; return std::move(impl_->groundDraws); }

bool ViewScripts::active() const { return impl_->vm.L != nullptr; }

bool ViewScripts::load(Simulation& sim, const Json& rules, const Json& map, uint8_t localPlayer) {
    Impl& im = *impl_;
    detach();
    im.vm.close();
    im.handlers.clear(); im.frame.clear(); im.requests.clear(); im.commands.clear();
    im.ui.clear(); im.uiLast.clear(); im.uiHandlers.clear(); im.stateRef = LUA_NOREF; im.stateTick = UINT32_MAX;
    log_.clear();
    im.paint=std::make_shared<render::PaintLayers>();im.targeting={};im.cameraRequested=false;
    const auto files = scriptFiles(rules, map, true);
    if (files.empty() || sim.module().name() != "rts") return false;
    Vm& vm = im.vm;
    vm.owner = &im;
    vm.module = static_cast<RtsModule*>(&sim.module());
    vm.sim = &sim; vm.spatialOk = false;
    vm.log = [&im](bool e, const std::string& t) { im.logLine(e, t); };
    if (!vm.open(Mode::View, size_t(64) << 20)) return false;
    vm.budget = 5000000;
    im.player = localPlayer;
    for (auto& [p, t] : rules["$scriptSources"].members()) vm.sources[p] = t.str();
    openGameApi(vm);
    static const luaL_Reg funcs[] = {{"on", bind<vOn>}, {"on_frame", bind<vOnFrame>}, {"notice", bind<vNotice>},
                                     {"effect", bind<vEffect>}, {"sound", bind<vSound>}, {"player", bind<vPlayer>}, {"screen", bind<vScreen>},
                                     {"send", bind<vSend>}, {"state", bind<vState>},
                                     {"render_texture", bind<vRenderTexture>}, {"paint_layer", bind<vPaintLayer>}, {"targeting", bind<vTargeting>},
                                     {"camera", bind<vCamera>}, {"set_camera", bind<vSetCamera>},
                                     {"to_screen", bind<vToScreen>}, {"draw", bind<vDraw>},
                                     {"draw_ground", bind<vDrawGround>}, {nullptr, nullptr}};
    luaL_newlib(vm.L, funcs);
    lua_setglobal(vm.L, "view");
    static const luaL_Reg uiFuncs[] = {{"set", bind<uSet>}, {"open", bind<uOpen>}, {"close", bind<uClose>}, {"on", bind<uOn>}, {nullptr, nullptr}};
    luaL_newlib(vm.L, uiFuncs);
    lua_setglobal(vm.L, "ui");
    openScriptSound(vm.L, &im.sound);
    for (auto& f : files) {
        auto it = vm.sources.find(f);
        if (it == vm.sources.end()) { im.logLine(true, f + ": view script not loaded"); continue; }
        std::string err;
        vm.used = 0; vm.exceeded = false;
        if (!loadChunk(vm, f, it->second, err) || !protectedCall(vm, 0, 0, &err)) im.logLine(true, err);
    }
    return true;
}

void ViewScripts::setSound(ScriptSound* sound) { impl_->sound = sound; }

void ViewScripts::attach(Simulation& sim) {
    detach();
    impl_->collector = std::make_shared<Collector>();
    std::weak_ptr<Collector> weak = impl_->collector;
    // runs last in every tick and only copies events out
    sim.addSystem("viewscripts.collect", [weak](Simulation& s) {
        if (auto c = weak.lock(); c && c->enabled && c->events.size() < 20000) c->events.insert(c->events.end(), s.events().begin(), s.events().end());
    });
}

void ViewScripts::detach() {
    if (impl_->collector) impl_->collector->enabled = false;
    impl_->collector.reset();
}

void ViewScripts::update(Simulation& sim, float dt) {
    Impl& im = *impl_;
    if (!im.vm.L) return;
    lua_State* L = im.vm.L;
    im.vm.sim = &sim; im.vm.spatialOk = false;
    im.vm.used = 0;
    im.vm.exceeded = false;
    im.draws.clear(); im.drawVerts = 0; im.groundDraws.clear(); im.groundVerts = 0;   // view.draw / draw_ground: one frame
    if (im.collector) {
        std::vector<SimEvent> evs;
        evs.swap(im.collector->events);
        const Rules& r = im.vm.module->rules();
        for (const SimEvent& ev : evs) {
            const char* n = eventName(ev.type);
            if (!n) continue;
            auto it = im.handlers.find(n);
            if (it == im.handlers.end()) continue;
            for (int& ref : it->second) {
                if (ref == LUA_NOREF) continue;
                lua_createtable(L, 0, 8);
                lua_pushstring(L, n); lua_setfield(L, -2, "event");
                lua_pushinteger(L, ev.player); lua_setfield(L, -2, "player");
                lua_pushinteger(L, ev.player); lua_setfield(L, -2, "owner");
                if (ev.a.valid()) { pushEntity(L, ev.a); lua_setfield(L, -2, "unit"); }
                if (ev.b.valid()) { pushEntity(L, ev.b); lua_setfield(L, -2, "other"); }
                if (ev.b.valid() && ev.type == kEvDied) { pushEntity(L, ev.b); lua_setfield(L, -2, "killer"); }
                lua_pushinteger(L, ev.value); lua_setfield(L, -2, "value");
                lua_pushnumber(L, ev.pos.x.toDouble()); lua_setfield(L, -2, "x");
                lua_pushnumber(L, ev.pos.z.toDouble()); lua_setfield(L, -2, "z");
                const bool typed = ev.type == kEvDied || ev.type == kEvSpawned || ev.type == kEvBuildDone || ev.type == kEvTrainDone || ev.type == kEvUpgraded;
                if (typed && ev.value >= 0 && size_t(ev.value) < r.types.size()) { lua_pushstring(L, r.types[size_t(ev.value)].id.c_str()); lua_setfield(L, -2, "unit_type"); }
                if (ev.type == kEvHit) { lua_pushnumber(L, Fixed::fromRaw(ev.value).toDouble()); lua_setfield(L, -2, "amount"); }
                if (ev.type == kEvMessage) {
                    auto& msgs = im.vm.module->state(sim).trig.messages;
                    if (ev.value >= 0 && size_t(ev.value) < msgs.size()) { lua_pushstring(L, msgs[size_t(ev.value)].c_str()); lua_setfield(L, -2, "text"); }
                }
                im.call(ref, 1);
            }
        }
    }
    for (int& ref : im.frame) {
        if (ref == LUA_NOREF) continue;
        lua_pushnumber(L, dt);
        im.call(ref, 1);
    }
}

std::vector<ViewRequest> ViewScripts::takeRequests() {
    std::vector<ViewRequest> out;
    out.swap(impl_->requests);
    return out;
}

std::vector<Command> ViewScripts::takeCommands() {
    std::vector<Command> out;
    out.swap(impl_->commands);
    return out;
}

std::vector<UiRequest> ViewScripts::takeUiRequests() {
    std::vector<UiRequest> out;
    out.swap(impl_->ui);
    return out;
}

void ViewScripts::uiEvent(Simulation& sim, const std::string& name, const Json& args) {
    Impl& im = *impl_;
    if (!im.vm.L) return;
    auto it = im.uiHandlers.find(name);
    if (it == im.uiHandlers.end()) { im.logLine(true, "ui event '" + name + "': no ui.on handler"); return; }
    im.vm.sim = &sim; im.vm.spatialOk = false;
    im.vm.used = 0; im.vm.exceeded = false;
    for (int& ref : it->second) {
        if (ref == LUA_NOREF) continue;
        pushUiJson(im.vm.L, args);
        im.call(ref, 1);
    }
}

bool ViewScripts::send(Simulation& sim, const std::string& id, const Json& args) {
    Impl& im = *impl_;
    if (!im.vm.L) return false;
    ScriptEngine* se = im.vm.module->scripts();
    const int k = se ? se->findCommand(id) : -1;
    if (k < 0) { im.logLine(true, "ui send: unknown command '" + id + "'"); return false; }
    im.vm.sim = &sim; im.vm.spatialOk = false;
    lua_State* L = im.vm.L;
    pushUiJson(L, args);
    Command c;
    c.type = kCmdScript;
    c.param = k;
    const bool ok = encodeScriptArgs(L, im.vm, -1, se->commands()[size_t(k)], c);
    lua_pop(L, 1);
    if (!ok) { im.logLine(true, "ui send '" + id + "': " + im.vm.err); im.vm.err.clear(); return false; }
    if (im.commands.size() < 64) im.commands.push_back(std::move(c));
    return true;
}

} // namespace rts::game
