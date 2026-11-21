// Shared GUI/MCP authoring; never mutates the live simulation.
#include "Workspace.h"
#include "apps/editor/PhysisAuthoring.h"
#include <limits>
#include <stdexcept>

namespace aiverse::editor {
namespace {
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
void registerPhysisOps(Workspace& ws) {
    auto add = [&ws](const char* name, const char* description, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, description, Json::parse(schema), std::move(fn), false});
    };
    auto* w = &ws;
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
