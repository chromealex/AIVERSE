// Shared GUI/MCP authoring; never mutates the live simulation.
#include "Workspace.h"
#include "PhysisDiagnosticFrame.h"
#include "apps/editor/PhysisDiagnosticDemo.h"
#include "engine/physis/PhysisDebugPalette.h"
#include "apps/editor/PhysisAuthoring.h"
#include <limits>
#include <stdexcept>

namespace aiverse::editor {
namespace {
struct DiagnosticDemo {
    uint64_t projectLifetime = 0;
    std::unique_ptr<World> world;
};

int diagnosticInteger(const Json& p, const char* key, int fallback, int low, int high) {
    if (!p.has(key)) return fallback;
    const Json& v = p[key];
    const auto n = v.integer(int64_t(low) - 1);
    if (!v.isNumber() || v.numberLiteral().find_first_of(".eE") != std::string::npos || n < low || n > high)
        throw std::invalid_argument(std::string("Invalid diagnostic ") + key);
    return static_cast<int>(n);
}

using namespace physis;
DiagnosticRequest diagnosticArea(const Json& params) {
    DiagnosticRequest req;
    req.layer = static_cast<DiagnosticLayer>(diagnosticInteger(params,"layer",0,0,13));
    req.width = req.height = diagnosticInteger(params,"size",32,8,64);
    req.stride = diagnosticInteger(params,"stride",1,1,8);
    req.originX = diagnosticInteger(params,"centerX",0,-1000000,1000000) - req.width/2;
    req.originZ = diagnosticInteger(params,"centerZ",0,-1000000,1000000) - req.height/2;
    return req;
}

Json readCell(const World& world, int x, int z) {
    Json result = Json::object();
    result.set("active",Json::fromBool(true));
    result.set("tick",Json::number(int64_t(world.resourceConst<State>()->tick)));
    NatureFieldSample sample;
    const bool available = samplePhysisCell(world,x,z,sample);
    result.set("available", Json::fromBool(available));
    if (available) {
        auto vector = [](Vec3 v) {
            Json out = Json::array();
            for (auto n : {v.x,v.y,v.z}) out.push(Json::number(n.toDouble(),6));
            return out;
        };
        result.set("wind",vector(sample.windVelocity));
        result.set("gravity",vector(sample.gravityAcceleration));
    }
    return result;
}

Json readFrame(const World& world, const DiagnosticRequest& req, DebugPalette palette) {
    Json result = Json::object();
    result.set("active",Json::fromBool(true));
    DiagnosticRaster raster;
    const bool ok = diagnosticNeeds3DField(req.layer)
        ? sampleDiagnosticRasterIndexed(FieldSpatialIndex(world),req,raster)
        : sampleDiagnosticRaster(world,req,raster);
    if (!ok) throw std::runtime_error("PHYSIS could not sample the diagnostic area");
    result.set("tick",Json::number(int64_t(raster.tick)));
    result.set("width",Json::number(int64_t(raster.width)));
    result.set("height",Json::number(int64_t(raster.height)));
    result.set("originX",Json::number(int64_t(raster.originX)));
    result.set("originZ",Json::number(int64_t(raster.originZ)));
    result.set("stride",Json::number(int64_t(raster.stride)));
    result.set("available",Json::number(int64_t(raster.sampleCount)));
    Json colors = Json::array(), values = Json::array(), rings = Json::array();
    for (size_t i = 0; i < raster.valid.size(); ++i) {
        const auto c = diagnosticColor(raster,i,palette);
        colors.push(Json::number(int64_t((c.r << 16) | (c.g << 8) | c.b)));
        values.push(raster.valid[i] ? Json::number(Fixed::fromRaw(raster.rawValues[i]).toDouble(),6) : Json{});
    }
    const auto* state = world.resourceConst<State>();
    auto ring = [&](const char* kind, Vec3 center, Fixed radius) {
        const double metres = state->settings.cellSizeMeters;
        Json row = Json::object();
        row.set("kind",Json::string(kind));
        row.set("x",Json::number((center.x.toDouble()/metres-raster.originX)/raster.stride));
        row.set("z",Json::number((center.z.toDouble()/metres-raster.originZ)/raster.stride));
        row.set("radius",Json::number(radius.toDouble()/(metres*raster.stride)));
        rings.push(std::move(row));
    };
    world.eachExistingConst<WindZone>([&](Entity,const WindZone& z){ring("Wind",z.center,z.radiusMeters);});
    world.eachExistingConst<StormEvent>([&](Entity,const StormEvent& s){
        if (stormActive(s,state->tick)) ring(s.kind==StormKind::Tornado?"Tornado":"Hurricane",s.center,s.outerRadiusMeters);
    });
    world.eachExistingConst<GravityField>([&](Entity,const GravityField& f){if(f.enabled) ring("Gravity",f.center,f.radiusMeters);});
    result.set("colors",std::move(colors));
    result.set("values",std::move(values));
    result.set("rings",std::move(rings));
    return result;
}

Json inspectDemo(DiagnosticDemo& demo, uint64_t lifetime, const Json& params) {
    using namespace physis;
    const auto action = params["action"].str();
    if (action != "reset" && action != "view" && action != "step" && action != "step60" && action != "probe")
        throw std::invalid_argument("Expected reset, view, step, step60 or probe");
    const auto req = diagnosticArea(params);
    const auto palette = static_cast<DebugPalette>(diagnosticInteger(params,"palette",0,0,2));
    if (demo.projectLifetime != lifetime) {
        demo.world.reset();
        demo.projectLifetime = lifetime;
    }
    if (action == "probe" && (!params.has("cellX") || !params.has("cellZ")))
        throw std::invalid_argument("A probe requires cellX and cellZ");
    if (action == "reset") demo.world = makePhysisDiagnosticDemo();
    Json result = Json::object();
    result.set("active",Json::fromBool(bool(demo.world)));
    if (!demo.world) return result;
    if (action == "probe") {
        const int x = diagnosticInteger(params,"cellX",0,INT32_MIN,INT32_MAX);
        const int z = diagnosticInteger(params,"cellZ",0,INT32_MIN,INT32_MAX);
        return readCell(*demo.world,x,z);
    }
    const int ticks = action == "step" ? 1 : action == "step60" ? 60 : 0;
    for (int i = 0; i < ticks; ++i)
        advanceEnvironmentWithStormsAndDisasters(*demo.world, FloodMode::ExternalAuthority);
    return readFrame(*demo.world,req,palette);
}

void validateConfig(const Json& config) {
    if (config.isNull()) return; // explicit removal
    std::string error;
    if (!physis_authoring::validate(config, &error)) throw std::invalid_argument(error);
    const auto& mode = config["floodMode"];
    if (!mode.isNull() && !mode.isString())
        throw std::invalid_argument("PHYSIS floodMode must be a string");
}
const Json& requiredConfig(const Json& p) {
    if (!p.has("config")) throw std::invalid_argument("config is required; null explicitly removes PHYSIS");
    return p["config"];
}
Json snapshot(const Workspace& w) {
    Json r = Json::object();
    r.set("ok", Json::fromBool(true));
    r.set("config", physis_authoring::read(w.map));
    r.set("revision", Json::number(int64_t(w.map.revision)));
    r.set("liveApplied", Json::fromBool(false));
    return r;
}
}
Json readPhysisGameFrame(const World* world, const Json& params) {
    const auto area = diagnosticArea(params);
    const auto palette = static_cast<DebugPalette>(diagnosticInteger(params,"palette",0,0,2));
    if (params.has("cellX") != params.has("cellZ"))
        throw std::invalid_argument("Supply both cell coordinates");
    if (!world || !world->resourceConst<State>()) {
        Json out = Json::object();
        out.set("active",Json::fromBool(false));
        return out;
    }
    auto frame = readFrame(*world,area,palette);
    if (params.has("cellX")) {
        const int x = diagnosticInteger(params,"cellX",0,INT32_MIN,INT32_MAX);
        const int z = diagnosticInteger(params,"cellZ",0,INT32_MIN,INT32_MAX);
        frame.set("cell",readCell(*world,x,z));
    }
    return frame;
}
void registerPhysisOps(Workspace& ws) {
    auto add = [&ws](const char* name, const char* description, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, description, Json::parse(schema), std::move(fn), false});
    };
    auto* w = &ws;
    auto demo = std::make_shared<DiagnosticDemo>();
    add("physis_demo", "Reset, step or sample the private PHYSIS diagnostic world. Returns palette colors, measurements and volume outlines.", R"({"properties":{"action":{"type":"string","enum":["view","reset","step","step60","probe"]},"cellX":{"type":"integer"},"cellZ":{"type":"integer"},"layer":{"type":"integer","minimum":0,"maximum":13},"palette":{"type":"integer","minimum":0,"maximum":2},"stride":{"type":"integer","minimum":1,"maximum":8},"size":{"type":"integer","minimum":8,"maximum":64},"centerX":{"type":"integer","minimum":-1000000,"maximum":1000000},"centerZ":{"type":"integer","minimum":-1000000,"maximum":1000000}},"required":["action"],"additionalProperties":false})",
        [w,demo](const Json& p){ return inspectDemo(*demo,w->projectLifetime(),p); });
    add("physis_get", "Read the open map's authored PHYSIS config and document revision. Includes a default config for new maps. Not live simulation state.",
        R"({"properties":{},"additionalProperties":false})", [w](const Json&) {
            auto r = snapshot(*w);
            r.set("defaultConfig", physis_authoring::defaultConfig());
            return r;
        });
    add("physis_validate", "Validate a complete PHYSIS authoring config without changing the map. config:null validates explicit removal. Does not install or tick a simulation.",
        R"({"properties":{"config":{"type":["object","null"]}},"required":["config"],"additionalProperties":false})", [](const Json& p) {
            validateConfig(requiredConfig(p));
            Json r=Json::object(); r.set("ok",Json::fromBool(true)); return r;
        });
    add("physis_set", "Atomically replace the open map's PHYSIS config with Undo/Redo. Read physis_get first and supply its revision as expectedRevision; stale edits fail. config:null removes the block. Does not save files or apply live/remote simulation changes.",
        R"({"properties":{"config":{"type":["object","null"]},"expectedRevision":{"type":"integer","minimum":0}},"required":["config","expectedRevision"],"additionalProperties":false})", [w](const Json& p) {
            const auto& config = requiredConfig(p);
            const auto& expected = p["expectedRevision"];
            if (!expected.isNumber() || expected.numberLiteral().find_first_of(".eE") != std::string::npos ||
                expected.integer(-1)<0 || uint64_t(expected.integer(-1)) != uint64_t(w->map.revision))
                throw std::invalid_argument("PHYSIS document revision mismatch; read physis_get again");
            validateConfig(config);
            const auto before = w->map.revision;
            std::string error;
            if (!physis_authoring::apply(w->map, config, &error)) throw std::invalid_argument(error);
            auto r=snapshot(*w);
            r.set("changed",Json::fromBool(before != w->map.revision));
            if (before != w->map.revision) {
                Change c; c.kind=Change::Map; c.text="PHYSIS configuration updated"; w->note(c);
                r.set("summary",Json::string(c.text));
            }
            return r;
        });
}
}
