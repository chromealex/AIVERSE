// World tree: every object of a game world, read generically from the simulation's ECS, for the
// Hierarchy / Inspector («Детали») windows and the world_* MCP operations (docs/editor-world-tree.md).
//
// The rows come from a Simulation the editor owns or observes: the throwaway preview of the open map
// (edit mode: the map's setup + scripts + C++ modules + a few ticks) or the running test (play mode).
// Reading never changes the simulation (only ScriptEngine::flush, the observation step every checksum
// does anyway, writes the Lua entity tables into the world). No UI and no rendering in here.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "MapDoc.h"
#include "engine/data/Json.h"
#include "engine/sim/Simulation.h"
#include "modules/rts/RtsModule.h"

namespace rts::editor {

// Component types the inspector knows (one per ECS pool of the RTS module). `id` is the C++ name.
struct WorldComponent { const char* id; const char* title; };
const std::vector<WorldComponent>& worldComponents();
int findWorldComponent(const std::string& idOrTitle);   // case-insensitive id or title; -1
// Creates every described pool in `w` and returns how many were described (tests compare it with the
// pools a save state touches, so a new component type without a description is noticed).
size_t touchWorldComponentPools(World& w);

enum class WorldKind : uint8_t { Unit, Structure, Hero, Resource, Projectile, Effect, Other, Count };
const char* worldKindId(WorldKind k);      // "unit", "structure", ...
const char* worldKindTitle(WorldKind k);   // Russian, plural: "Юниты", "Здания", ...

// Where an object came from.
enum class WorldOrigin : uint8_t { Map, Setup, Runtime };   // map document entity / created by the game at start / later
const char* worldOriginTitle(WorldOrigin o);

// game.json "editor": {"hierarchy": {...}} (docs/game-format.md § Editor: world tree).
struct WorldConfig {
    int previewTicks = 2;                         // ticks the edit-mode preview runs after setup (0..600)
    std::string labelKey;                         // script data key shown as the row name (game.data(h)[key])
    std::vector<std::string> categoryTags;        // category = the first of these tags a type has (else the kind)
    struct Grouping { std::string title; std::vector<std::string> by; };
    std::vector<Grouping> groupings;              // extra grouping modes of the game
    std::vector<std::string> errors;              // readable problems of the block
};
WorldConfig parseWorldConfig(const Json& manifest);

// World tree ids ("ref"): packed entity + 1, so 0 always means "none" (entity 0 of generation 0 packs to 0).
inline uint64_t worldRef(Entity e) { return e.valid() ? e.packed() + 1 : 0; }
inline Entity worldEntity(uint64_t ref) { return ref ? Entity::fromPacked(ref - 1) : kNullEntity; }

struct WorldRow {
    Entity e;                 // entity in the world it was read from
    uint64_t ref = 0;         // worldRef(e)
    int type = -1;            // rules type index (-1: no Unit component)
    int owner = 0;            // map convention: 0 neutral, 1..N players, -1 hostile creeps
    WorldKind kind = WorldKind::Other;
    WorldOrigin origin = WorldOrigin::Runtime;
    int doc = -1;             // index of the map document entity it was spawned for (-1: none)
    bool hasPos = false;
    float x = 0, y = 0, z = 0;
    float hp = 0, maxHp = 0;
    uint16_t flags = 0;       // game::UnitFlags
    uint64_t parent = 0;      // worldRef of the container / source (transport, shooter, caster, summoner); 0 none
    const char* parentRel = "";   // Russian relation of the parent ("в транспорте", "выпущен", ...)
    int region = -1;
    std::vector<uint8_t> comps;   // indices into worldComponents()
    std::string label;        // game label (WorldConfig::labelKey) or ""
    std::string category;     // category name (tags / kind)
    std::map<std::string, std::string> data;   // top-level scalar script data (game.data) as text, for grouping / search
};

struct WorldModel {
    std::vector<WorldRow> rows;           // entity index order
    std::map<uint64_t, size_t> byRef;     // ref -> row
    uint32_t tick = 0;
    uint64_t serial = 0;                  // bumps on every rebuild
    std::vector<std::string> regionNames;
    std::vector<std::string> playerNames; // index = owner slot (0 neutral); from the map / sim
    const WorldRow* find(uint64_t ref) const { auto it = byRef.find(ref); return it == byRef.end() ? nullptr : &rows[it->second]; }
    const WorldRow* findDoc(int doc) const { for (auto& r : rows) if (r.doc == doc) return &r; return nullptr; }
};

// Map document entity i -> worldRef of the entity spawned for it (0: none). Call right after Simulation::setup:
// map entities are spawned in document order (after the faction start units); type, owner and (flat maps)
// position must agree.
std::vector<uint64_t> linkMapEntities(Simulation& sim, game::RtsModule& mod, const MapData& doc);

// Rebuilds `out` from the world. links: doc index -> packed (linkMapEntities); setupRefs: sorted packed
// refs of the entities that existed when the world was first seen (origin Setup), may be empty.
void readWorld(Simulation& sim, game::RtsModule& mod, const WorldConfig& cfg, const std::vector<uint64_t>& links,
               const std::vector<uint64_t>& setupRefs, const MapData* doc, WorldModel& out);

// Everything about one object: {"components": {id: {...}}, "stats": {...}, "script": {...}, "rules": {...}}.
// Entity references inside are {"$entity": packed, "label": "..."}.
Json inspectEntity(Simulation& sim, game::RtsModule& mod, Entity e, const Json& rulesJson);

// Lua entity data / state blob (ScriptData::blob) as JSON; fx values become numbers.
Json decodeScriptBlob(const std::string& blob);

// Grouping key of a row: "owner", "type", "kind", "category", "origin", "region", "parent", "data:<key>",
// "tag" (first tag), "component" (first component; the Hierarchy lists every component instead).
std::string worldGroupValue(const WorldModel& m, const game::Rules& rules, const WorldRow& r, const std::string& key);
// Search: tokens are AND-ed; prefixes t: type, p: owner, c: component, r: region, d: script data,
// # / id: entity or map id; a plain token matches any of them and the row name.
bool worldRowMatches(const WorldModel& m, const game::Rules& rules, const MapData* doc, const WorldRow& r, const std::string& query);
std::string worldRowName(const WorldModel& m, const game::Rules& rules, const MapData* doc, const WorldRow& r);

// The edit-mode preview: the open map set up in a throwaway simulation (seed 1) and run a few ticks.
class WorldPreview {
public:
    // Rebuilds the preview. Never throws: the error text is kept (error()).
    bool build(const Json& simRules, const Json& simMap, const MapData& doc, const WorldConfig& cfg);
    void clear() { sim_.reset(); links_.clear(); }
    Simulation* sim() const { return sim_.get(); }
    game::RtsModule* module();
    const std::vector<uint64_t>& links() const { return links_; }
    const std::vector<uint64_t>& setupRefs() const { return setupRefs_; }
    const std::string& error() const { return error_; }
    int scriptErrors() const { return scriptErrors_; }
    double buildMs() const { return ms_; }

private:
    std::unique_ptr<Simulation> sim_;
    std::vector<uint64_t> links_, setupRefs_;
    std::string error_;
    int scriptErrors_ = 0;
    double ms_ = 0;
};

} // namespace rts::editor
