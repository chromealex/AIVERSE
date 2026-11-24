// Statically linked game extensions. Mutable state lives in World for rollback.
#pragma once
#include "../sim/Simulation.h"

struct lua_State;

namespace aiverse::game {
class GameCore;

// Each module owns one namespace. Use Fixed::raw / fromRaw for fixed-point values,
// Entity::packed / fromPacked for entity references. No raw pointers in state.
struct NativeState {
    std::map<std::string, int64_t> values;
    std::map<std::string, std::vector<int64_t>> arrays;
};
struct NativeStore { std::vector<NativeState> modules; };

class NativeModule {
public:
    virtual ~NativeModule() = default;
    // Instance members are immutable configuration only; keep persistent data in state.
    virtual void setup(GameCore&, Simulation&, NativeState&, const Json& config) const = 0;
    virtual void tick(GameCore&, Simulation&, NativeState&) const {}
    virtual void command(GameCore&, Simulation&, NativeState&, const Command&) const {}
    // Lua functions of the module for the simulation's scripts: called on a temporary instance (from the
    // factory) right after the engine libraries are installed in the sim Lua state and before any script of
    // the game runs, for each module listed in rules.json "nativeModules", in that order (headless, client,
    // server and editor alike: every sim Lua state). Register globals here (e.g. a library table with
    // luaL_newlib + lua_setglobal). The functions must not keep pointers to the instance, to the world or to
    // the simulation: they are pure helpers over their Lua arguments (deterministic; script determinism
    // rules apply, docs/native-modules.md). Errors inside them use luaL_error (no C++ object with a
    // destructor alive at that point).
    virtual void openLua(lua_State*) const {}
    // Editor-only observation hook: expose REAL components owned by this
    // module, keyed by their actual C++/ECS component IDs. The engine asks
    // this only for loaded modules in the current Simulation. Do not mutate
    // World or keep pointers to the editor's preview.
    // components: { "MyComponent": { "field": value } }
    // schemas: { "MyComponent": { "title": "...", "fields":
    //             { "field": { "title": "...", "kind": "number" } } } }
    // Modules must only report a component when World::has<T>(entity) is true.
    virtual void inspectComponents(World&, Entity, Json& /*components*/,
                                   Json& /*schemas*/) const {}
    // Opt-in persisted Inspector editing for native ECS component fields.
    // The project module validates the requested field and returns a new
    // authoring-extra JSON object. The editor commits it with MapDoc Undo.
    // Return false unless the changed authoring data really recreates the
    // same ECS field on the next Simulation::setup. No runtime World writes.
    virtual bool inspectWriteback(const std::string& /*componentId*/,
                                  const std::string& /*fieldId*/,
                                  const Json& /*value*/,
                                  const Json& /*currentExtra*/,
                                  Json& /*updatedExtra*/) const {
        return false;
    }
    // ---- optional hooks into built-in mechanics: declare the implemented ones in hooks() (NativeHook bits); a
    // module without hooks costs nothing. All of them run inside the simulation: deterministic, state in NativeState.
    virtual uint32_t hooks() const { return 0; }
    // kNativeHookCommand: every built-in command (type < 32768) of a living player before it executes. Return false
    // to drop it; the command may be changed (another target, param ...). Runs in nativeModules order.
    virtual bool filterCommand(GameCore&, Simulation&, NativeState&, Command&) const { return true; }
    // kNativeHookGarrison: growth of a garrison this tick. rate multiplies the growth (neutral structures come in
    // with 0: they do not grow unless a module raises it), cap is the garrison maximum (change it to override).
    virtual void garrisonGrowth(GameCore&, Simulation&, NativeState&, Entity, uint8_t /*owner*/, Fixed& /*rate*/, int& /*cap*/) const {}
    // kNativeHookUpgrade: an upgrade order that passed the built-in checks, before it is paid. type may be replaced
    // by another structure type; cost multiplies resource and garrison costs, time the build time. False refuses it.
    virtual bool upgradeOrder(GameCore&, Simulation&, NativeState&, Entity, uint8_t /*player*/, uint16_t& /*type*/, Fixed& /*cost*/, Fixed& /*time*/) const { return true; }

    // kHookSpeed: extra add / multiplier of a unit's Speed stat (called from GameCore::stat: read-only, cheap).
    virtual void unitSpeed(GameCore&, Simulation&, const NativeState&, Entity, Fixed& /*add*/, Fixed& /*mul*/) const {}
    // kHookSend: false refuses sending units from one garrison to another (commands, effects and bots ask canSend).
    virtual bool allowSend(GameCore&, Simulation&, const NativeState&, Entity /*from*/, Entity /*to*/) const { return true; }
    // kHookDamage: before damage is applied; may change it (0 = nothing happens). True: later modules are not asked.
    virtual bool damage(GameCore&, Simulation&, NativeState&, Entity /*target*/, Entity /*source*/, Fixed& /*dmg*/) const { return false; }
    // kHookDeath: a unit is being removed (silent: without death effects, e.g. it entered a garrison).
    virtual void unitDied(GameCore&, Simulation&, NativeState&, Entity, uint8_t /*owner*/, Vec3 /*pos*/, bool /*silent*/) const {}
    // kHookAssault: a unit of `attacker` hit garrison `structure` of `defender`: units lost on both sides.
    virtual void assault(GameCore&, Simulation&, NativeState&, Entity /*structure*/, uint8_t /*attacker*/, uint8_t /*defender*/,
                         int /*attackerLost*/, int /*defenderLost*/) const {}
    // kHookDefeat: false keeps a player in the match when match modes / victory scripts would defeat it.
    virtual bool allowDefeat(GameCore&, Simulation&, const NativeState&, uint8_t /*player*/) const { return true; }
    // kHookScene: extra objects for the view, drawn as unit types (presentation: must not change anything).
    struct Body { uint16_t type = 0; Vec3 pos{}, facing{}; bool moving = false; };
    virtual void sceneBodies(GameCore&, Simulation&, const NativeState&, std::vector<Body>&) const {}
};
// hook bits: one mask for every hook (values are part of no file format: modules use the names)
enum NativeHook : uint32_t { kNativeHookCommand = 1, kNativeHookGarrison = 2, kNativeHookUpgrade = 4,
                             kHookSpeed = 8, kHookSend = 16, kHookDamage = 32, kHookDeath = 64, kHookAssault = 128,
                             kHookDefeat = 256, kHookScene = 512 };
using NativeFactory = std::unique_ptr<NativeModule> (*)();
struct NativeDefinition {
    std::string id;
    uint32_t version;
    NativeFactory create;
};
// Native modules compiled into this executable. The generated NativeRegistry.cpp (part of AIVERSE::native_modules,
// project code above the engine) registers them through one NativeRegistrar; a binary without modules has none.
struct NativeRegistrar { explicit NativeRegistrar(std::vector<NativeDefinition> definitions); };
std::vector<NativeDefinition> compiledNativeModules();

// Calls openLua of the modules listed in rules["nativeModules"] that are compiled in (unknown ids are left to
// NativeModules::load, which rejects them).
void openNativeLua(lua_State* L, const Json& rules, const std::vector<NativeDefinition>& definitions);

class NativeModules {
public:
    void load(GameCore&, Simulation&, const Json& rules,
              const std::vector<NativeDefinition>& definitions);
    void tick(GameCore&, Simulation&) const;
    void command(GameCore&, Simulation&, const Command&) const;
    uint32_t hooks() const { return hooks_; }   // union of the loaded modules' NativeHook bits
    bool filterCommand(GameCore&, Simulation&, Command&) const;
    void garrisonGrowth(GameCore&, Simulation&, Entity, uint8_t owner, Fixed& rate, int& cap) const;
    bool upgradeOrder(GameCore&, Simulation&, Entity, uint8_t player, uint16_t& type, Fixed& cost, Fixed& time) const;
    void hash(Simulation&, Hasher&) const;
    bool serialize(Simulation&, Archive&) const;
    // hooks (no-ops unless a loaded module declares the bit)
    bool has(uint32_t hook) const { return (hooks_ & hook) != 0; }
    void unitSpeed(GameCore&, Simulation&, Entity, Fixed& add, Fixed& mul) const;
    bool allowSend(GameCore&, Simulation&, Entity from, Entity to) const;
    void damage(GameCore&, Simulation&, Entity target, Entity source, Fixed& dmg) const;
    void unitDied(GameCore&, Simulation&, Entity, uint8_t owner, Vec3 pos, bool silent) const;
    void assault(GameCore&, Simulation&, Entity structure, uint8_t attacker, uint8_t defender, int attackerLost, int defenderLost) const;
    bool allowDefeat(GameCore&, Simulation&, uint8_t player) const;
    void sceneBodies(GameCore&, Simulation&, std::vector<NativeModule::Body>&) const;
    void inspectComponents(Simulation&, Entity, Json& components, Json& schemas) const;
    bool inspectWriteback(const std::string& componentId,
                          const std::string& fieldId,
                          const Json& value, const Json& currentExtra,
                          Json& updatedExtra) const;
private:
    uint32_t hooks_ = 0;
    struct Entry {
        std::string id, config;
        uint32_t version;
        uint16_t command;
        std::unique_ptr<NativeModule> instance;
    };
    std::vector<Entry> entries_;
};
} // namespace aiverse::game
