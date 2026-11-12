// View scripts: presentation-only Lua VM (see RtsViewScript.h).
#include "RtsViewScript.h"
#include "engine/platform/Platform.h"
#include "engine/render/AnimationAsset.h"
#include <map>
#include <cmath>
#include <algorithm>
#include "../RtsModule.h"
#include "../RtsScriptInternal.h"

namespace aiverse::game {
using namespace script;

namespace {
const char* eventName(uint16_t t) { return simEventName(t); }   // every GameEvent (RtsScript.cpp table)
struct Collector { std::vector<SimEvent> events; bool enabled = true; };
} // namespace

struct AnimationLuaContext { script::Vm* vm=nullptr; size_t registrations=0; bool executing=false; };
struct ViewScripts::Impl {
    std::function<bool(const std::string&,std::string&)> animationRead;
    std::shared_ptr<AnimationLuaContext> animationContext=std::make_shared<AnimationLuaContext>();
    ~Impl() { animationContext->vm=nullptr; }
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
    float dragDx = 0, dragDy = 0;     // view.set_map_drag_offset (window points)
    ViewCamera camera;
    std::string screen = "playing";   // host screen state (view.screen()): playing | paused | over
    ViewCameraRequest cameraRequest{};
    bool cameraRequested = false;
    std::vector<ViewDrawBatch> draws;
    size_t drawVerts = 0;
    std::vector<ViewGroundBatch> groundDraws;
    size_t groundVerts = 0;
    std::shared_ptr<render::PaintLayers> paint = std::make_shared<render::PaintLayers>();
    std::shared_ptr<EntityFxStore> fx = std::make_shared<EntityFxStore>();   // view.entity_fx (EntityFx.h)
    ScriptSound* sound = nullptr;   // the `sound` table's host
    std::function<int(lua_State*)> uiCall;   // view.ui_call's host (the client's UI Lua); empty = no UI state
    explicit Impl(ViewScripts& s) : self(s) {}

    void logLine(bool error, const std::string& t) {
        if (self.log_.size() > 300) self.log_.erase(self.log_.begin());
        self.log_.push_back({error, vm.sim ? vm.sim->tick() : 0, t});
        if (self.onLog) self.onLog(self.log_.back());
    }
    // Per-handler cost (ViewScripts::profile) by registry ref; a disabled handler keeps its row.
    std::vector<ViewScriptProfile::Handler> stats;
    std::map<int, size_t> statOf;
    int64_t frameCost = 0, frames = 0, frameTotal = 0, frameMax = 0;   // per update(): every handler call
    ViewScriptProfile::Handler& statFor(int ref) {
        auto it = statOf.find(ref);
        if (it != statOf.end()) return stats[it->second];
        ViewScriptProfile::Handler h;
        lua_rawgeti(vm.L, LUA_REGISTRYINDEX, ref);
        lua_Debug ar{};
        if (lua_getinfo(vm.L, ">S", &ar)) h.name = std::string(ar.short_src) + ":" + std::to_string(ar.linedefined);
        statOf[ref] = stats.size();
        stats.push_back(std::move(h));
        return stats.back();
    }
    // Every handler call gets the whole budget. A call over budget is aborted and skipped (logged); only a handler
    // over budget kOverrunLimit calls in a row (an endless loop, not a spike) or failing with an error is disabled.
    static constexpr int kOverrunLimit = 10;
    // result: when set, receives the truthiness of the handler's first return value (false on an error)
    void call(int& ref, int nargs, bool* result = nullptr) {
        ViewScriptProfile::Handler& st = statFor(ref);
        lua_rawgeti(vm.L, LUA_REGISTRYINDEX, ref);
        lua_insert(vm.L, -nargs - 1);
        vm.used = 0; vm.exceeded = false;
        std::string err;
        const bool ok = protectedCall(vm, nargs, result ? 1 : 0, &err);
        if (result) { *result = ok && lua_toboolean(vm.L, -1); if (ok) lua_pop(vm.L, 1); }
        const int64_t cost = instructionsUsed(vm);
        ++st.calls; st.total += cost; st.max = std::max(st.max, cost); st.last += cost; frameCost += cost;
        if (ok) { st.streak = 0; return; }
        if (vm.exceeded && ++st.streak < kOverrunLimit) {
            if (++st.overruns <= 20) logLine(true, err + " [view handler skipped once]");
            return;
        }
        if (vm.exceeded) ++st.overruns;
        logLine(true, err + (vm.exceeded ? " [view handler disabled: over budget " + std::to_string(kOverrunLimit) + " calls in a row]"
                                         : std::string(" [view handler disabled]")));
        st.disabled = true;
        statOf.erase(ref);
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
        // ground = {scale, strength, flipHeight, relief (0 meadow, 1 / 2 snow / dune ripples), team = {min, max}, context = {min, max}, coast = {sea, drop, beach,
        // shallow, earthNear, earthFar, foam}}: meadow / coast surface (render::PaintLayer, docs § Categorical cell layers)
        lua_getfield(L,1,"ground");
        if(lua_istable(L,-1)){
            const int gi=lua_gettop(L);p.ground=true;
            lua_getfield(L,gi,"linear");p.groundLinear=lua_toboolean(L,-1);lua_pop(L,1);
            p.groundScale=numberField(L,gi,"scale",8);p.groundStrength=numberField(L,gi,"strength",.12f);p.flipHeight=numberField(L,gi,"flipHeight",0);
            if(!std::isfinite(p.groundScale)||p.groundScale<=0||!std::isfinite(p.groundStrength)||!std::isfinite(p.flipHeight)){lua_pop(L,1);return fail(vm,"invalid paint ground");}
            p.groundStrength=std::clamp(p.groundStrength,0.f,1.f);
            {const float relief=numberField(L,gi,"relief",0);if(!(relief==0||relief==1||relief==2)){lua_pop(L,1);return fail(vm,"paint ground relief must be 0, 1 or 2");}p.groundRelief=int(relief);}
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
    lua_pushboolean(L,t.touch);lua_setfield(L,-2,"touch");
    lua_pushnumber(L,t.sx);lua_setfield(L,-2,"sx");
    lua_pushnumber(L,t.sy);lua_setfield(L,-2,"sy");
    return 1;
}
int vSetMapDragOffset(lua_State* L, Vm& vm) {
    const double dx=toDouble(L,1), dy=toDouble(L,2);
    if(!std::isfinite(dx)||!std::isfinite(dy)) return fail(vm,"view.set_map_drag_offset(dx, dy): finite window points");
    auto& v=viewOf(vm);
    v.dragDx=float(std::clamp(dx,-4096.0,4096.0));v.dragDy=float(std::clamp(dy,-4096.0,4096.0));
    return 0;
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
    if (!lua_istable(L, 1)) return fail(vm, "view.draw(verts [, {image =, layer =, transform =}])");
    std::string image; int layer = 0;
    double tf[6] = {1, 0, 0, 1, 0, 0};   // transform {xx, xy, yx, yy, ox, oy}: x' = ox + x*xx + y*yx, y' = oy + x*xy + y*yy
    bool transformed = false;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "image"); if (lua_isstring(L, -1)) image = lua_tostring(L, -1); lua_pop(L, 1);
        lua_getfield(L, 2, "layer"); if (lua_isnumber(L, -1)) layer = int(lua_tointeger(L, -1)); lua_pop(L, 1);
        lua_getfield(L, 2, "transform");
        if (lua_istable(L, -1)) {
            for (int i = 0; i < 6; ++i) {
                lua_rawgeti(L, -1, i + 1);
                const bool ok = lua_isnumber(L, -1); tf[i] = lua_tonumber(L, -1); lua_pop(L, 1);
                if (!ok || !std::isfinite(tf[i])) { lua_pop(L, 1); return fail(vm, "view.draw: transform needs six numbers {xx, xy, yx, yy, ox, oy}"); }
            }
            transformed = true;
        }
        lua_pop(L, 1);
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
        if (transformed) {   // in binary64 and in this order, like the same sum written in Lua (no fused multiply-add)
            const double x = num(b), y = num(b + 1);
            volatile double ax = x * tf[0], ay = x * tf[1];
            volatile double sx = tf[4] + ax, sy = tf[5] + ay;
            volatile double bx = y * tf[2], by = y * tf[3];
            w.x = float(sx + bx); w.y = float(sy + by);
        } else { w.x = float(num(b)); w.y = float(num(b + 1)); }
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
// view.entity_fx(h, {scale =, lift =, pitch =, roll =, opacity =, tint =, team_mix =, team_glow =, shadow =, shadow_scale =, time =, ease =})
// sets an entity's presentation overrides (EntityFx.h); fields left out keep their target. view.entity_fx(h, nil)
// removes them (drawn unchanged at once); view.entity_fx(h) returns the values drawn now, or nil.
// Atomic parameter patch; only presentation state, shared with client and editor views.
#include "RtsAnimationLua.inc"
int vAnimation(lua_State* L, Vm& vm) {
    if(viewOf(vm).animationContext->executing)return fail(vm,"animation controls cannot be changed inside a trait");
    Entity entity;
    if (!argEntity(L, 1, entity) || !entity.valid()) return fail(vm, "view.animation: entity handle expected");
    auto& entries = viewOf(vm).fx->animations;
    const auto key = entity.packed();
    if (lua_isnil(L, 2)) { entries.erase(key); return 0; }
    if (!vm.sim || !vm.sim->world().alive(entity)) return fail(vm, "view.animation: entity is not alive");
    if (!lua_istable(L, 2)) return fail(vm, "view.animation: parameter table expected");
    auto it = entries.find(key);
    if (it == entries.end() && entries.size() >= 65536) return fail(vm, "view.animation: entity limit");
    EntityAnimationEntry next = it == entries.end() ? EntityAnimationEntry{} : it->second;
    lua_pushnil(L);
    while (lua_next(L, 2)) {
        if (lua_type(L, -2) != LUA_TSTRING) { lua_pop(L, 2); return fail(vm, "view.animation: string parameter expected"); }
        const std::string name = lua_tostring(L, -2);
        bool ok = false;
        if (lua_istable(L, -1)) {
            render::float3 v;
            float* components[] = {&v.x, &v.y, &v.z};
            ok = lua_rawlen(L, -1) == 3;
            for (int i = 0; i < 3; ++i) {
                lua_rawgeti(L, -1, i + 1);
                ok = ok && lua_isnumber(L, -1);
                *components[i] = float(toDouble(L, -1)); lua_pop(L, 1);
            }
            ok = ok && next.parameters.set(name, v);
        } else if (lua_isboolean(L, -1)) ok = next.parameters.set(name, float(lua_toboolean(L, -1)));
        else if (lua_isnumber(L, -1)) ok = next.parameters.set(name, float(toDouble(L, -1)));
        lua_pop(L, 1);
        if (!ok) { lua_pop(L, 1); return fail(vm, "view.animation: invalid parameter or limit exceeded"); }
    }
    entries[key] = std::move(next);
    return 0;
}
int vEntityFx(lua_State* L, Vm& vm) {
    auto& im = viewOf(vm);
    Entity e;
    if (!argEntity(L, 1, e) || !e.valid()) return fail(vm, "view.entity_fx(h [, fields | nil]): entity handle expected");
    const uint64_t key = e.packed();
    using V = EntityFxValues;
    if (lua_gettop(L) < 2) {   // read
        V cur;
        if (!im.fx->lookup(key, cur)) { lua_pushnil(L); return 1; }
        lua_createtable(L, 0, 8);
        auto vec = [&](const char* k, int at, int n) {
            lua_createtable(L, n, 0);
            for (int i = 0; i < n; ++i) { lua_pushnumber(L, cur.v[at + i]); lua_rawseti(L, -2, i + 1); }
            lua_setfield(L, -2, k);
        };
        vec("scale", V::kScale, 3); vec("tint", V::kTint, 3); vec("team_mix", V::kTeamMix, 4); vec("shadow_scale", V::kShadowScale, 3);
        lua_pushnumber(L, cur.v[V::kOpacity]); lua_setfield(L, -2, "opacity");
        lua_pushnumber(L, cur.v[V::kTeamGlow]); lua_setfield(L, -2, "team_glow");
        lua_pushnumber(L, cur.v[V::kShadow]); lua_setfield(L, -2, "shadow");
        lua_pushnumber(L, cur.v[V::kLift]); lua_setfield(L, -2, "lift");
        lua_pushnumber(L, cur.v[V::kPitch]); lua_setfield(L, -2, "pitch");
        lua_pushnumber(L, cur.v[V::kRoll]); lua_setfield(L, -2, "roll");
        if (cur.v[V::kCorpseAge] >= 0) { lua_pushnumber(L, cur.v[V::kCorpseAge]); lua_setfield(L, -2, "corpse_age"); }
        return 1;
    }
    if (lua_isnil(L, 2)) { im.fx->entries.erase(key); return 0; }
    if (!lua_istable(L, 2)) return fail(vm, "view.entity_fx(h, fields | nil): table expected");
    auto it = im.fx->entries.find(key);
    EntityFxEntry n;
    if (it != im.fx->entries.end()) { n.to = it->second.to; n.from = it->second.current(); n.shadowScaleOwn = it->second.shadowScaleOwn; }
    V target = n.to;
    std::string err;
    // a number or {x, y, z} (scale: one number = uniform); colours {r, g, b [, a]}
    auto field = [&](const char* k, int at, int n3, bool numberFills, float lo, float hi) -> bool {
        lua_getfield(L, 2, k);
        bool set = false;
        if (lua_isnumber(L, -1) && (numberFills || n3 == 1)) {
            const float v = float(lua_tonumber(L, -1));
            if (!std::isfinite(v)) err = std::string("view.entity_fx: non-finite ") + k;
            for (int i = 0; i < n3; ++i) target.v[at + i] = std::clamp(v, lo, hi);
            set = true;
        } else if (lua_istable(L, -1) && n3 > 1) {
            for (int i = 0; i < n3; ++i) {
                lua_rawgeti(L, -1, i + 1);
                if (!lua_isnumber(L, -1)) { if (!(n3 == 4 && i == 3)) err = std::string("view.entity_fx: ") + k + " needs " + std::to_string(n3) + " numbers"; lua_pop(L, 1); continue; }
                const float v = float(lua_tonumber(L, -1)); lua_pop(L, 1);
                if (!std::isfinite(v)) err = std::string("view.entity_fx: non-finite ") + k;
                target.v[at + i] = std::clamp(v, lo, hi);
            }
            set = true;
        } else if (!lua_isnil(L, -1)) err = std::string("view.entity_fx: bad ") + k;
        lua_pop(L, 1);
        return set;
    };
    field("scale", V::kScale, 3, true, 0.0f, 1000.0f);
    field("opacity", V::kOpacity, 1, true, 0.0f, 1.0f);
    field("tint", V::kTint, 3, false, 0.0f, 64.0f);
    if (field("team_mix", V::kTeamMix, 4, false, 0.0f, 64.0f)) target.v[V::kTeamMix + 3] = std::clamp(target.v[V::kTeamMix + 3], 0.0f, 1.0f);
    field("team_glow", V::kTeamGlow, 1, true, 0.0f, 64.0f);
    field("shadow", V::kShadow, 1, true, 0.0f, 1.0f);
    field("lift", V::kLift, 1, true, -1000.0f, 1000.0f);
    field("pitch", V::kPitch, 1, true, -360.0f, 360.0f);
    field("roll", V::kRoll, 1, true, -360.0f, 360.0f);
    {   // corpse_age = seconds since the death on the script's clock (false: the view's own clock again)
        lua_getfield(L, 2, "corpse_age");
        const bool own = lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1);
        lua_pop(L, 1);
        if (own) target.v[V::kCorpseAge] = -1;
        else field("corpse_age", V::kCorpseAge, 1, true, 0.0f, 1e6f);
    }
    bool ownShadowScale = false;
    {
        lua_getfield(L, 2, "shadow_scale");
        const bool follow = lua_type(L, -1) == LUA_TBOOLEAN && !lua_toboolean(L, -1);   // shadow_scale = false: follow scale again
        lua_pop(L, 1);
        if (follow) n.shadowScaleOwn = false;
        else if (field("shadow_scale", V::kShadowScale, 3, true, 0.0f, 1000.0f)) ownShadowScale = true;
    }
    float time = 0;
    lua_getfield(L, 2, "time"); if (lua_isnumber(L, -1)) time = float(lua_tonumber(L, -1)); lua_pop(L, 1);
    if (!std::isfinite(time) || time < 0) err = "view.entity_fx: time must be >= 0";
    uint8_t ease = 0;
    lua_getfield(L, 2, "ease");
    if (lua_isstring(L, -1)) { const std::string m = lua_tostring(L, -1); if (m == "smooth") ease = 1; else if (m != "linear") err = "view.entity_fx: ease is \"linear\" or \"smooth\""; }
    lua_pop(L, 1);
    if (!err.empty()) return fail(vm, err);   // nothing changes
    if (ownShadowScale) {
        // the eased-from shadow size continues from what was drawn (it followed the scale until now)
        if (!n.shadowScaleOwn) for (int a = 0; a < 3; ++a) n.from.v[V::kShadowScale + a] = n.from.v[V::kScale + a];
        n.shadowScaleOwn = true;
    }
    n.to = target;
    n.time = time; n.age = 0; n.ease = ease;
    if (time <= 0) n.from = target;
    if (it == im.fx->entries.end()) {
        if (im.fx->entries.size() >= 65536) return fail(vm, "view.entity_fx: more than 65536 entities");
        im.fx->entries.emplace(key, n);
    } else it->second = n;
    return 0;
}
int vPlayer(lua_State* L, Vm& vm) { lua_pushinteger(L, viewOf(vm).player); return 1; }
// view.screen(): what the host shows over the match ("playing", "paused" = the pause screen / modal, "over" = the match
// has ended); presentation only (a paused match stops ticking, its view keeps running)
int vScreen(lua_State* L, Vm& vm) { lua_pushstring(L, viewOf(vm).screen.c_str()); return 1; }
// view.ui_call(name, ...) -> ok, results...: a synchronous call of the UI Lua's global function `name` (dotted) with the
// arguments copied (nil, booleans, numbers, strings, tables); false, message when there is no UI state or the call fails
int vUiCall(lua_State* L, Vm& vm) {
    luaL_checkstring(L, 1);
    auto& f = viewOf(vm).uiCall;
    if (!f) { lua_pushboolean(L, 0); lua_pushstring(L, "no UI Lua state"); return 2; }
    return f(L);
}

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
std::shared_ptr<EntityFxStore> ViewScripts::entityFx() const { return impl_->fx; }

void ViewScripts::setTargeting(const ViewTargeting& target) { impl_->targeting=target; }
void ViewScripts::mapDragOffset(float& dx, float& dy) const { dx=impl_->dragDx; dy=impl_->dragDy; }
void ViewScripts::setMapDragOffset(float dx, float dy) { impl_->dragDx=dx; impl_->dragDy=dy; }
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
    im.animationContext->vm=nullptr;
    im.animationContext=std::make_shared<AnimationLuaContext>();
    im.vm.close();
    im.handlers.clear(); im.frame.clear(); im.requests.clear(); im.commands.clear();
    im.ui.clear(); im.uiLast.clear(); im.uiHandlers.clear(); im.stats.clear(); im.statOf.clear(); im.frameCost = im.frames = im.frameTotal = im.frameMax = 0; im.stateRef = LUA_NOREF; im.stateTick = UINT32_MAX;
    log_.clear();
    im.paint=std::make_shared<render::PaintLayers>();im.fx=std::make_shared<EntityFxStore>();im.targeting={};im.dragDx=im.dragDy=0;im.cameraRequested=false;
    const auto files = scriptFiles(rules, map, true);
    if (files.empty() || sim.module().name() != "rts") return false;
    Vm& vm = im.vm;
    vm.owner = &im;
    vm.module = static_cast<RtsModule*>(&sim.module());
    vm.sim = &sim; vm.spatialOk = false;
    vm.log = [&im](bool e, const std::string& t) { im.logLine(e, t); };
    if (!vm.open(Mode::View, size_t(64) << 20)) return false;
    vm.budget = 5000000;
    im.animationContext->vm=&vm;
    im.player = localPlayer;
    for (auto& [p, t] : rules["$scriptSources"].members()) vm.sources[p] = t.str();
    openGameApi(vm);
    static const luaL_Reg funcs[] = {{"on", bind<vOn>}, {"on_frame", bind<vOnFrame>}, {"notice", bind<vNotice>},
                                     {"effect", bind<vEffect>}, {"sound", bind<vSound>}, {"player", bind<vPlayer>}, {"screen", bind<vScreen>},
                                     {"send", bind<vSend>}, {"state", bind<vState>}, {"ui_call", bind<vUiCall>},
                                     {"render_texture", bind<vRenderTexture>}, {"paint_layer", bind<vPaintLayer>}, {"targeting", bind<vTargeting>}, {"set_map_drag_offset", bind<vSetMapDragOffset>},
                                     {"camera", bind<vCamera>}, {"set_camera", bind<vSetCamera>},
                                     {"to_screen", bind<vToScreen>}, {"draw", bind<vDraw>},
                                     {"draw_ground", bind<vDrawGround>}, {"entity_fx", bind<vEntityFx>}, {"animation", bind<vAnimation>}, {"animation_trait", bind<vAnimationTrait>},
                                     {"animation_load", bind<vAnimationLoad>}, {"animation_define", bind<vAnimationDefine>}, {"animation_use", bind<vAnimationUse>}, {"animation_status", bind<vAnimationStatus>}, {nullptr, nullptr}};
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

void ViewScripts::setContentReader(std::function<bool(const std::string&,std::string&)> reader) { impl_->animationRead=std::move(reader); }
void ViewScripts::setSound(ScriptSound* sound) { impl_->sound = sound; }
void ViewScripts::setUiCall(std::function<int(lua_State*)> call) { impl_->uiCall = std::move(call); }

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
    im.frameCost = 0;
    for (auto& h : im.stats) h.last = 0;
    im.draws.clear(); im.drawVerts = 0; im.groundDraws.clear(); im.groundVerts = 0;   // view.draw / draw_ground: one frame
    // view.entity_fx: eases advance in frame time; entities gone from the simulation keep theirs for a corpse a while
    im.fx->advance(dt);
    for (auto it = im.fx->animations.begin(); it != im.fx->animations.end();) {
        if (sim.world().alive(Entity::fromPacked(it->first))) it->second.goneFor = -1;
        else if ((it->second.goneFor = std::max(0.0f, it->second.goneFor) + dt) > EntityFxEntry::kKeepDead) {
            it = im.fx->animations.erase(it); continue;
        }
        ++it;
    }
    for (auto it = im.fx->entries.begin(); it != im.fx->entries.end();) {
        const Entity e = Entity::fromPacked(it->first);
        if (sim.world().alive(e)) it->second.goneFor = -1;
        else if ((it->second.goneFor = std::max(0.0f, it->second.goneFor) + dt) > EntityFxEntry::kKeepDead) { it = im.fx->entries.erase(it); continue; }
        ++it;
    }
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
    ++im.frames; im.frameTotal += im.frameCost; im.frameMax = std::max(im.frameMax, im.frameCost);
}

ViewScriptProfile ViewScripts::profile() const {
    const Impl& im = *impl_;
    ViewScriptProfile p;
    p.budget = im.vm.budget;
    p.frames = im.frames; p.frameTotal = im.frameTotal; p.frameMax = im.frameMax; p.frameLast = im.frameCost;
    p.handlers = im.stats;
    return p;
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

bool ViewScripts::uiEvent(Simulation& sim, const std::string& name, const Json& args) {
    Impl& im = *impl_;
    if (!im.vm.L) return false;
    auto it = im.uiHandlers.find(name);
    if (it == im.uiHandlers.end()) { im.logLine(true, "ui event '" + name + "': no ui.on handler"); return false; }
    im.vm.sim = &sim; im.vm.spatialOk = false;
    im.vm.used = 0; im.vm.exceeded = false;
    bool any = false;
    for (int& ref : it->second) {
        if (ref == LUA_NOREF) continue;
        pushUiJson(im.vm.L, args);
        bool r = false;
        im.call(ref, 1, &r);
        any = any || r;
    }
    return any;
}

bool ViewScripts::hasUiHandler(const std::string& name) const {
    const Impl& im = *impl_;
    if (!im.vm.L) return false;
    auto it = im.uiHandlers.find(name);
    if (it == im.uiHandlers.end()) return false;
    for (int ref : it->second) if (ref != LUA_NOREF) return true;
    return false;
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

} // namespace aiverse::game
