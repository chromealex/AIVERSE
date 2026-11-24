#include "Native.h"
#include "GameCore.h"
#include <set>
#include <stdexcept>

namespace aiverse::game {
namespace {
std::vector<NativeDefinition>& registeredNativeModules() { static std::vector<NativeDefinition> modules; return modules; }
}
NativeRegistrar::NativeRegistrar(std::vector<NativeDefinition> definitions) {
    auto& modules = registeredNativeModules();
    for (auto& d : definitions) modules.push_back(std::move(d));
}
std::vector<NativeDefinition> compiledNativeModules() { return registeredNativeModules(); }
namespace {
void hashState(Hasher& h, const NativeState& s) {
    h.u32(uint32_t(s.values.size()));
    for (const auto& [key, value] : s.values) { h.str(key); h.i64(value); }
    h.u32(uint32_t(s.arrays.size()));
    for (const auto& [key, values] : s.arrays) {
        h.str(key); h.u32(uint32_t(values.size()));
        for (auto v : values) h.i64(v);
    }
}
}
void openNativeLua(lua_State* L, const Json& rules, const std::vector<NativeDefinition>& definitions) {
    if (!rules.has("nativeModules") || !rules["nativeModules"].isArray()) return;
    for (const auto& item : rules["nativeModules"].items()) {
        if (!item.isObject()) continue;
        const std::string id = item.getStr("id");
        for (const auto& d : definitions) if (d.id == id && d.create) {
            if (auto instance = d.create()) instance->openLua(L);
            break;
        }
    }
}
void NativeModules::load(GameCore& rts, Simulation& sim, const Json& rules,
                         const std::vector<NativeDefinition>& definitions) {
    entries_.clear();
    hooks_ = 0;
    if (!rules.has("nativeModules")) return;
    const auto& list = rules["nativeModules"];
    if (!list.isArray()) throw std::runtime_error("nativeModules: expected an array");
    std::set<std::string> ids;
    std::set<uint16_t> commands;
    for (const auto& item : list.items()) {
        const std::string id = item.getStr("id");
        if (!item.isObject() || id.empty()) throw std::runtime_error("nativeModules: expected object with id");
        if (!ids.insert(id).second) throw std::runtime_error("native module '" + id + "': duplicate id");
        const NativeDefinition* def = nullptr;
        for (const auto& d : definitions) if (d.id == id) {
            if (def) throw std::runtime_error("native module '" + id + "': duplicate factory");
            def = &d;
        }
        if (!def || !def->create) {
            std::string have;
            for (const auto& d : definitions) have += (have.empty() ? "" : ", ") + d.id;
            throw std::runtime_error("native module '" + id + "': not compiled into this build (compiled: " +
                                     (have.empty() ? std::string("none") : have) +
                                     "); rebuild the engine with -DAIVERSE_CONTENT_DIR=<project folder> so it compiles <project>/native, "
                                     "then open the project in that editor (docs/native-modules.md)");
        }
        Json config = item.has("config") ? item["config"] : Json::object();
        if (!config.isObject()) throw std::runtime_error("native module '" + id + "': config must be an object");
        // One command type per module, with param/flags for module-specific actions.
        int64_t command = item.getInt("command");
        if (item.has("command") && (!item["command"].isNumber() || command < 32768 || command > 65535 ||
                                    item["command"].fx() != Fixed::fromInt(command)))
            throw std::runtime_error("native module '" + id + "': command must be an integer in [32768, 65535]");
        if (command == 0xF100 || command == 0xF101)   // engine/net/HeroInput.h: kCmdTopDown / kCmdTopDownAbility
            throw std::runtime_error("native module '" + id + "': commands 61696 and 61697 (0xF100, 0xF101) are the top-down hero commands");
        if (command && !commands.insert(uint16_t(command)).second)
            throw std::runtime_error("native module '" + id + "': duplicate command");
        auto instance = def->create();
        if (!instance) throw std::runtime_error("native module '" + id + "': factory returned null");
        entries_.push_back({id, config.dump(-1), def->version, uint16_t(command), std::move(instance)});
    }
    if (entries_.empty()) return;
    hooks_ = 0;
    for (auto& e : entries_) hooks_ |= e.instance->hooks();
    auto& store = sim.world().setResource<NativeStore>();
    store.modules.resize(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].instance->setup(rts, sim, store.modules[i], Json::parse(entries_[i].config));
        sim.world().flush();
    }
    for (auto& e : entries_) hooks_ |= e.instance->hooks();   // read once, after every setup
    sim.addSystem("nativeModules", [this, &rts](Simulation& s) { tick(rts, s); });
}
void NativeModules::tick(GameCore& rts, Simulation& sim) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto& state = sim.world().resource<NativeStore>()->modules[i];
        entries_[i].instance->tick(rts, sim, state);
        sim.world().flush();
    }
}
void NativeModules::command(GameCore& rts, Simulation& sim, const Command& cmd) const {
    for (size_t i = 0; i < entries_.size(); ++i) if (entries_[i].command && entries_[i].command == cmd.type) {
        entries_[i].instance->command(rts, sim, sim.world().resource<NativeStore>()->modules[i], cmd);
        return;
    }
}
bool NativeModules::filterCommand(GameCore& rts, Simulation& sim, Command& cmd) const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if ((entries_[i].instance->hooks() & kNativeHookCommand) &&
            !entries_[i].instance->filterCommand(rts, sim, sim.world().resource<NativeStore>()->modules[i], cmd)) return false;
    return true;
}
void NativeModules::garrisonGrowth(GameCore& rts, Simulation& sim, Entity e, uint8_t owner, Fixed& rate, int& cap) const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].instance->hooks() & kNativeHookGarrison)
            entries_[i].instance->garrisonGrowth(rts, sim, sim.world().resource<NativeStore>()->modules[i], e, owner, rate, cap);
}
bool NativeModules::upgradeOrder(GameCore& rts, Simulation& sim, Entity e, uint8_t player, uint16_t& type, Fixed& cost, Fixed& time) const {
    for (size_t i = 0; i < entries_.size(); ++i)
        if ((entries_[i].instance->hooks() & kNativeHookUpgrade) &&
            !entries_[i].instance->upgradeOrder(rts, sim, sim.world().resource<NativeStore>()->modules[i], e, player, type, cost, time)) return false;
    return true;
}
#define AIVERSE_NATIVE_EACH(bit) \
    auto* store = sim.world().resource<NativeStore>(); \
    if (store) for (size_t i = 0; i < entries_.size() && i < store->modules.size(); ++i) if (entries_[i].instance->hooks() & (bit))
void NativeModules::unitSpeed(GameCore& rts, Simulation& sim, Entity e, Fixed& add, Fixed& mul) const {
    AIVERSE_NATIVE_EACH(kHookSpeed) entries_[i].instance->unitSpeed(rts, sim, store->modules[i], e, add, mul);
}
bool NativeModules::allowSend(GameCore& rts, Simulation& sim, Entity from, Entity to) const {
    AIVERSE_NATIVE_EACH(kHookSend) if (!entries_[i].instance->allowSend(rts, sim, store->modules[i], from, to)) return false;
    return true;
}
void NativeModules::damage(GameCore& rts, Simulation& sim, Entity target, Entity source, Fixed& dmg) const {
    AIVERSE_NATIVE_EACH(kHookDamage) if (entries_[i].instance->damage(rts, sim, store->modules[i], target, source, dmg)) return;
}
void NativeModules::unitDied(GameCore& rts, Simulation& sim, Entity e, uint8_t owner, Vec3 pos, bool silent) const {
    AIVERSE_NATIVE_EACH(kHookDeath) entries_[i].instance->unitDied(rts, sim, store->modules[i], e, owner, pos, silent);
}
void NativeModules::assault(GameCore& rts, Simulation& sim, Entity s, uint8_t attacker, uint8_t defender, int attackerLost, int defenderLost) const {
    AIVERSE_NATIVE_EACH(kHookAssault) entries_[i].instance->assault(rts, sim, store->modules[i], s, attacker, defender, attackerLost, defenderLost);
}
bool NativeModules::allowDefeat(GameCore& rts, Simulation& sim, uint8_t player) const {
    AIVERSE_NATIVE_EACH(kHookDefeat) if (!entries_[i].instance->allowDefeat(rts, sim, store->modules[i], player)) return false;
    return true;
}
void NativeModules::sceneBodies(GameCore& rts, Simulation& sim, std::vector<NativeModule::Body>& out) const {
    AIVERSE_NATIVE_EACH(kHookScene) entries_[i].instance->sceneBodies(rts, sim, store->modules[i], out);
}
#undef AIVERSE_NATIVE_EACH

void NativeModules::inspectComponents(Simulation& sim, Entity entity,
                                     Json& components, Json& schemas) const {
    World& world = sim.world();
    if (!world.alive(entity) || !components.isObject() || !schemas.isObject())
        return;
    for (const auto& entry : entries_) {
        Json reported = Json::object();
        Json declared = Json::object();
        entry.instance->inspectComponents(world, entity, reported, declared);
        if (!reported.isObject()) continue;
        for (const auto& [id, fields] : reported.members()) {
            // Keep builtin ECS descriptors authoritative. A module cannot
            // fabricate duplicate builtin components or overwrite another
            // module's already reported component.
            if (id.empty() || !fields.isObject() || components.has(id)) continue;
            components.set(id, fields);
            if (declared.isObject() && declared[id].isObject())
                schemas.set(id, declared[id]);
        }
    }
}

bool NativeModules::inspectWriteback(const std::string& componentId,
                                     const std::string& fieldId,
                                     const Json& value, const Json& currentExtra,
                                     Json& updatedExtra) const {
    if (componentId.empty() || fieldId.empty() || !currentExtra.isObject())
        return false;
    for (const auto& entry : entries_) {
        Json candidate = currentExtra;
        if (!entry.instance->inspectWriteback(componentId, fieldId, value,
                                               currentExtra, candidate))
            continue;
        if (!candidate.isObject()) return false;
        updatedExtra = std::move(candidate);
        return true;
    }
    return false;
}

void NativeModules::hash(Simulation& sim, Hasher& h) const {
    if (entries_.empty()) return; // preserve existing games' checksums
    h.str("nativeModules"); h.u32(uint32_t(entries_.size()));
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        h.str(e.id); h.u32(e.version); h.str(e.config); h.u32(e.command);
        hashState(h, sim.world().resource<NativeStore>()->modules[i]);
    }
}
bool NativeModules::serialize(Simulation& sim, Archive& a) const {
    if (entries_.empty()) return a.ok(); // preserve old save format without extensions
    uint32_t count = uint32_t(entries_.size());
    a(count);
    if (count != entries_.size()) { a.fail(); return false; }
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        std::string id = e.id, config = e.config;
        uint32_t version = e.version;
        uint16_t command = e.command;
        a(id, version, config, command);
        if (id != e.id || version != e.version || config != e.config || command != e.command) { a.fail(); return false; }
        auto& s = sim.world().resource<NativeStore>()->modules[i];
        a(s.values, s.arrays);
    }
    return a.ok();
}
} // namespace aiverse::game
