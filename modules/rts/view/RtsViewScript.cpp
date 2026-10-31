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
const char* eventName(uint16_t t) {
    switch (t) {
        case kEvDied: return "unit_died";
        case kEvSpawned: return "spawned";
        case kEvBuildDone: return "built";
        case kEvTrainDone: return "trained";
        case kEvResearched: return "researched";
        case kEvCaptured: return "captured";
        case kEvPlayerDefeated: return "player_defeated";
        case kEvAbility: return "ability";
        case kEvHit: return "damage";
        case kEvLevelUp: return "level_up";
        case kEvUpgraded: return "upgraded";
        case kEvMessage: return "message";
        default: return nullptr;
    }
}
struct Collector { std::vector<SimEvent> events; bool enabled = true; };
} // namespace

struct ViewScripts::Impl {
    ViewScripts& self;
    Vm vm;
    std::map<std::string, std::vector<int>> handlers;   // event -> function refs (LUA_NOREF: disabled)
    std::vector<int> frame;
    std::vector<ViewRequest> requests;
    std::shared_ptr<Collector> collector;
    uint8_t player = 0;
    ViewTargeting targeting;
    std::shared_ptr<render::PaintLayers> paint = std::make_shared<render::PaintLayers>();
    explicit Impl(ViewScripts& s) : self(s) {}

    void logLine(bool error, const std::string& t) {
        if (self.log_.size() > 300) self.log_.erase(self.log_.begin());
        self.log_.push_back({error, vm.sim ? vm.sim->tick() : 0, t});
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
    size_t pixels=p.textureId?0:size_t(p.width)*p.height;
    for(auto& [id,l]:store.layers)if(!l.textureId)pixels+=size_t(l.width)*l.height;
    if(pixels>8*1024*1024)return fail(vm,"paint layers exceed 8 million pixels");
    const int id=store.nextId++;store.layers[id]=std::move(p);
    lua_newtable(L);lua_pushinteger(L,id);lua_setfield(L,-2,"id");
    lua_pushcfunction(L,bind<vPaintStroke>);lua_setfield(L,-2,"stroke");
    lua_pushcfunction(L,bind<vPaintOpacity>);lua_setfield(L,-2,"set_opacity");
    lua_pushcfunction(L,bind<vPaintCopy>);lua_setfield(L,-2,"copy_from");
    lua_pushcfunction(L,bind<vPaintClear>);lua_setfield(L,-2,"clear");
    lua_pushcfunction(L,bind<vPaintDestroy>);lua_setfield(L,-2,"destroy");return 1;
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
int vPlayer(lua_State* L, Vm& vm) { lua_pushinteger(L, viewOf(vm).player); return 1; }
} // namespace

ViewScripts::ViewScripts() : impl_(std::make_unique<Impl>(*this)) {}
ViewScripts::~ViewScripts() { detach(); }
std::shared_ptr<render::PaintLayers> ViewScripts::paintLayers() const { return impl_->paint; }

void ViewScripts::setTargeting(const ViewTargeting& target) { impl_->targeting=target; }

bool ViewScripts::active() const { return impl_->vm.L != nullptr; }

bool ViewScripts::load(Simulation& sim, const Json& rules, const Json& map, uint8_t localPlayer) {
    Impl& im = *impl_;
    detach();
    im.vm.close();
    im.handlers.clear(); im.frame.clear(); im.requests.clear();
    log_.clear();
    im.paint=std::make_shared<render::PaintLayers>();im.targeting={};
    const auto files = scriptFiles(rules, map, true);
    if (files.empty() || sim.module().name() != "rts") return false;
    Vm& vm = im.vm;
    vm.owner = &im;
    vm.module = static_cast<RtsModule*>(&sim.module());
    vm.sim = &sim;
    vm.log = [&im](bool e, const std::string& t) { im.logLine(e, t); };
    if (!vm.open(Mode::View, size_t(64) << 20)) return false;
    vm.budget = 5000000;
    im.player = localPlayer;
    for (auto& [p, t] : rules["$scriptSources"].members()) vm.sources[p] = t.str();
    openGameApi(vm);
    static const luaL_Reg funcs[] = {{"on", bind<vOn>}, {"on_frame", bind<vOnFrame>}, {"notice", bind<vNotice>},
                                     {"render_texture", bind<vRenderTexture>}, {"paint_layer", bind<vPaintLayer>}, {"effect", bind<vEffect>}, {"sound", bind<vSound>}, {"player", bind<vPlayer>}, {"targeting", bind<vTargeting>}, {nullptr, nullptr}};
    luaL_newlib(vm.L, funcs);
    lua_setglobal(vm.L, "view");
    for (auto& f : files) {
        auto it = vm.sources.find(f);
        if (it == vm.sources.end()) { im.logLine(true, f + ": view script not loaded"); continue; }
        std::string err;
        vm.used = 0; vm.exceeded = false;
        if (!loadChunk(vm, f, it->second, err) || !protectedCall(vm, 0, 0, &err)) im.logLine(true, err);
    }
    return true;
}

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
    im.vm.sim = &sim;
    im.vm.used = 0;
    im.vm.exceeded = false;
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

} // namespace rts::game
