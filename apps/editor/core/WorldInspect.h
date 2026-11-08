// World tree: every object of a game world, read generically from the simulation's ECS, for the
// Hierarchy / Inspector («Детали») windows and the world_* MCP operations (docs/editor-world-tree.md).
//
// The rows come from a Simulation the editor owns or observes: the throwaway preview of the open map
// (edit mode: the map's setup + scripts + C++ modules + a few ticks) or the running test (play mode).
// Reading never changes the simulation (only ScriptEngine::flush, the observation step every checksum
// does anyway, writes the Lua entity tables into the world). No UI and no rendering in here.
#pragma once
#include <atomic>
#include <bitset>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "MapDoc.h"
#include "engine/data/Json.h"
#include "engine/sim/Simulation.h"
#include "modules/rts/RtsModule.h"

// Background preview builds need threads: everywhere but a web build without pthreads.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define RTS_PREVIEW_THREAD 0
#else
#define RTS_PREVIEW_THREAD 1
#endif

namespace rts::editor {

// Component types the inspector knows (one per ECS pool of the RTS module). `id` is the C++ name.
constexpr size_t kMaxWorldComponents = 128;   // WorldRow::comps bits (tests check the described pools fit)
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
    std::bitset<kMaxWorldComponents> comps;   // indices into worldComponents()
    std::string label;        // game label (WorldConfig::labelKey) or ""
    std::string category;     // category name (tags / kind)
    std::map<std::string, std::string> data;   // top-level scalar script data (game.data) as text, for grouping / search
    // (only when read with WorldReadOptions::scriptData)
    template <class Fn> void eachComp(Fn&& fn) const { for (size_t c = 0; c < comps.size(); ++c) if (comps[c]) fn(int(c)); }
    int firstComp() const { for (size_t c = 0; c < comps.size(); ++c) if (comps[c]) return int(c); return -1; }
};

struct WorldModel {
    std::vector<WorldRow> rows;           // entity index order
    std::vector<int32_t> byIndex;         // entity index -> row (-1)
    std::vector<int32_t> byDoc;           // map document entity -> row (-1)
    uint32_t tick = 0;
    uint64_t serial = 0;                  // bumps on every rebuild
    bool scriptData = false;              // rows carry script data / labels (WorldReadOptions::scriptData)
    std::vector<std::string> regionNames;
    std::vector<std::string> playerNames; // index = owner slot (0 neutral); from the map / sim
    const WorldRow* find(uint64_t ref) const {
        if (!ref) return nullptr;
        const uint32_t i = worldEntity(ref).index;
        if (i >= byIndex.size() || byIndex[i] < 0) return nullptr;
        const WorldRow& r = rows[size_t(byIndex[i])];
        return r.ref == ref ? &r : nullptr;
    }
    const WorldRow* findDoc(int doc) const { return doc >= 0 && size_t(doc) < byDoc.size() && byDoc[size_t(doc)] >= 0 ? &rows[size_t(byDoc[size_t(doc)])] : nullptr; }
};

// What a read fills. The row fields (id, owner, type, kind, category, position, hp, parent, origin, components)
// always; script data (game.data scalars + the configured label) only when asked: decoding every object's Lua
// blob is the expensive part of a read, and the blobs are only current after GameModule::prepareSnapshot.
struct WorldReadOptions { bool scriptData = true; };

// Map document entity i -> worldRef of the entity spawned for it (0: none). Call right after Simulation::setup:
// map entities are spawned in document order (after the faction start units); type, owner and (flat maps)
// position must agree.
std::vector<uint64_t> linkMapEntities(Simulation& sim, game::RtsModule& mod, const MapData& doc);

// Rebuilds `out` from the world. links: doc index -> packed (linkMapEntities); setupRefs: sorted packed
// refs of the entities that existed when the world was first seen (origin Setup), may be empty.
// Rows are reused (their buffers survive), so reading the same world again allocates little.
void readWorld(Simulation& sim, game::RtsModule& mod, const WorldConfig& cfg, const std::vector<uint64_t>& links,
               const std::vector<uint64_t>& setupRefs, const MapData* doc, WorldModel& out, const WorldReadOptions& opt = {});
// Script data of a row from a ScriptData blob (game.data scalars + the configured label).
void setWorldRowData(const WorldConfig& cfg, const std::string& blob, WorldRow& r);
// One object's row (false: not alive); origin / doc are left as they are (the caller knows them).
bool readWorldRow(Simulation& sim, game::RtsModule& mod, const WorldConfig& cfg, Entity e, WorldRow& r, const WorldReadOptions& opt = {});

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
// build() runs on the calling thread; start() runs the same build on a worker thread where threads are
// available (not on the web build) and poll() adopts the result on the owner's thread. The worker only
// touches what it was given (copies) and its own Simulation; the replaced simulation is also destroyed
// there (a Lua state of a big game takes tens of ms to free), so the editor never waits for a setup.
class WorldPreview {
public:
    // Makes the simulation inputs (rules with embedded scripts, the map JSON); runs on the worker, so it must
    // only use what it captured by value. Throws std::exception with a readable message.
    using Prepare = std::function<void(Json& simRules, Json& simMap)>;

    WorldPreview();
    WorldPreview(const WorldPreview&) = delete;
    WorldPreview& operator=(const WorldPreview&) = delete;
    ~WorldPreview();
    // Rebuilds the preview now. Never throws: the error text is kept (error()).
    bool build(const Json& simRules, const Json& simMap, const MapData& doc, const WorldConfig& cfg);
    // Background build; a start while one runs replaces the queued request (the running one finishes and is
    // dropped). doc: the map document (its entities are linked to the spawned objects).
    void start(Prepare prepare, const MapData& doc, const WorldConfig& cfg);
    bool poll();                       // true when a background build was adopted (sim() / links() / error() changed)
    bool busy() const { return job_ != nullptr; }
    void clear() { sim_.reset(); links_.clear(); }
    Simulation* sim() const { return sim_.get(); }
    game::RtsModule* module();
    const std::vector<uint64_t>& links() const { return links_; }
    const std::vector<uint64_t>& setupRefs() const { return setupRefs_; }
    const std::string& error() const { return error_; }
    int scriptErrors() const { return scriptErrors_; }
    double buildMs() const { return ms_; }

private:
    struct Result {
        std::unique_ptr<Simulation> sim;
        std::vector<uint64_t> links, setupRefs;
        std::string error;
        int scriptErrors = 0;
        double ms = 0;
    };
    struct Request { Prepare prepare; MapData doc; WorldConfig cfg; };
    struct Job;
    static Result run(const Json* simRules, const Json* simMap, const Prepare& prepare, const MapData& doc, const WorldConfig& cfg);
    void adopt(Result&& r);

    std::unique_ptr<Simulation> sim_;
    std::vector<uint64_t> links_, setupRefs_;
    std::string error_;
    int scriptErrors_ = 0;
    double ms_ = 0;
    std::unique_ptr<Job> job_;                 // the running background build
#if RTS_PREVIEW_THREAD
    void launch(Request&& r);
    std::unique_ptr<Request> queued_;          // the newest request while one runs
    std::vector<std::unique_ptr<Simulation>> trash_;   // replaced previews: freed by the next job's thread
#else
    bool adopted_ = false;                     // no threads: start() built synchronously, poll() reports it once
#endif
};

} // namespace rts::editor
