# Native ECS Inspector extension registration

The editor Inspector reads components from the **actual ECS World**, not from map-authoring `DocEntity` placeholders.

## Existing reader

`apps/editor/core/WorldInspect.cpp` calls `inspectEntity()`, which queries `World::has<T>` for built-in ECS pools and serializes the fields under `{"components": {"ComponentId": {...}}}`. The common `DetailsPanel` draws one card for each **present** component. `DocEntity` metadata, when relevant, appears separately.

## Preferred integration: loaded NativeModule hook

AIVERSE native modules are **statically compiled** into the project-specific
editor (see `docs/native-modules.md`). There is no dynamic DLL/dylib loading
or hot reload. The editor therefore reads native component data through the
`NativeModule` instance already loaded into the current Simulation.

Override the optional `NativeModule::inspectComponents(World&, Entity, Json&
components, Json& schemas) const` method in a project's module. Example
(the project must declare its actual `VehiclePhysics` type):

```cpp
void MyNativeModule::inspectComponents(aiverse::World& world,
                                       aiverse::Entity entity,
                                       aiverse::Json& components,
                                       aiverse::Json& schemas) const {
    const auto* vehicle = world.tryGet<VehiclePhysics>(entity);
    if (!vehicle) return; // no component: no Inspector card

    aiverse::Json values = aiverse::Json::object();
    values.set("speed", aiverse::Json::number(vehicle->speed.toDouble(), 3));
    components.set("VehiclePhysics", values);

    aiverse::Json fields = aiverse::Json::object();
    aiverse::Json speed = aiverse::Json::object();
    speed.set("title", aiverse::Json::string("Speed"));
    speed.set("kind", aiverse::Json::string("number"));
    fields.set("speed", speed);
    aiverse::Json schema = aiverse::Json::object();
    schema.set("title", aiverse::Json::string("Vehicle Physics"));
    schema.set("fields", fields);
    schemas.set("VehiclePhysics", schema);
}
```

This is a **read-only inspector hook**. Only report real ECS components
whose pools are present; the engine does not create components or edit the
runtime world on your behalf. The loaded module's hooks are automatically
queried by `WorldInspect::inspectEntity`, scoped to that Simulation, so no
global unregister action is needed when switching projects. Built-in
component entries retain priority if IDs collide.

**Important:** Existing native modules do not automatically implement this
hook. Each owning module must supply its actual component schema/read callback.
This does not yet provide generic arbitrary native ECS reflection or
project-specific persistence/Undo.

## Alternative: global editor-side registration (advanced)

The `registerWorldInspectorExtension` API below remains available for
components compiled into the editor host directly. Unlike the preferred
`NativeModule` hook, its registration is global and requires synchronization
when callbacks are replaced/unregistered.

## Opt-in reader registration for additional C++ ECS components

A native C++ module **linked into the editor process** can register an inspector reader after its component type is available and before the editor begins inspecting objects:

```cpp
#include "apps/editor/core/WorldInspect.h"

void registerVehicleInspector() {
    using namespace aiverse;
    using namespace aiverse::editor;

    registerWorldInspectorExtension({
        "VehiclePhysics",
        "Vehicle Physics",
        {{"speed", "Speed", WorldFieldKind::Number},
         {"engineOn", "Engine On", WorldFieldKind::Boolean}},
        [](const World& w, Entity e) {
            return w.has<VehiclePhysics>(e);
        },
        [](World& w, Entity e) {
            // The registry calls read only for a live entity that has this pool.
            const auto& v = *w.tryGet<VehiclePhysics>(e);
            Json fields = Json::object();
            fields.set("speed", Json::number(double(v.speed), 3));
            fields.set("engineOn", Json::fromBool(v.engineOn));
            return fields;
        }
    });
}
```

`VehiclePhysics` above is **an example component type**, not an AIVERSE built-in. Its declaration and actual numeric conversions must be supplied by the project; do not copy this example into the engine as an imaginary component. For Fixed fields use the correct `toDouble()` conversion instead of a raw cast.

**Registration contract**

- `id` must be nonempty and unique. Built-in ECS component IDs cannot be replaced.
- `has` performs a real `World::has<T>()` check. `read` returns only the real component's current fields as a JSON object.
- Field IDs must be unique and map to keys returned by `read`. Unknown fields still display with the default JSON renderer; supported registered field kinds are used for labels/formatting.
- Readers are copied into a thread-protected snapshot before inspection, allowing safe registration changes. **Module unloading requires an explicit synchronization barrier**: unregister the extension and ensure in-flight snapshots/reads are complete before unloading its code. Unregister alone does not wait for callbacks in flight.
- This is a **C++ in-process registration API**, not yet an exported stable C ABI for separately loaded native game plugins. Connecting existing native-module lifecycle hooks remains future work.
- Read callbacks run against the editor's current World; they must be observational, even though `World::tryGet<T>` currently requires non-const `World&`.
- Current editor Hierarchy and world index bitsets are still based on the fixed RTS descriptor list; these extensions add real components to Inspector snapshots but do **not** automatically add new Hierarchy component filters.
- An unavailable or uncompiled native module cannot expose its runtime component pools.

## Editing and persistence

The presence of a field in an ECS snapshot does **not** imply that editing is safe. The preview can be regenerated from project data. The current `EditorEcsWriteback.h` explicitly allows only `Transform.pos` (authored RTS X/Z) and `Owner.player` (authored RTS owner), persisted back to the map. All other fields remain read-only until their owning module supplies a validated writeback/persistence strategy and undo/redo.

The built-in RTS `Transform` stores `Vec3 pos`, `Vec3 prevPos`, and `Fixed yaw`, **not** a generic Euler rotation or Scale. Do not invent Rotation/Scale values if the registered component does not have them.

## Verification checklist

1. Build AiverseEditor on macOS with the extension registered in-process.
2. Confirm an entity with the native ECS pool shows exactly one correctly titled component card and its real fields.
3. Confirm an entity without that pool shows **no** card, and built-in component cards remain unchanged.
4. Confirm duplicate IDs are rejected and unregister/re-register works when no inspector reads are in flight.
5. Inspect multi-selection, filtered fields, and the unavailable native-module case.
6. Keep native fields read-only unless explicit persisted setter and Undo tests exist.

**Current validation status:** API source edits were reviewed; no successful macOS build, runtime extension test or visual inspection has been recorded for this stage.


## Immutable Shared/Static prefab configurations (opt-in)

The `feature/ecs-static-prefab-scene-integration` branch adds a
read-only, genre-independent Inspector adapter:

`engine/scene/PrefabStaticInspector.h`

A native module that already implements `inspectComponents(World&,
Entity,Json& components,Json& schemas) const` can use
`PrefabStaticInspectorRegistry` to show **all registered static
configuration domains** through one hook:

```cpp
// During native module initialization (one-time):
aiverse::scene::PrefabStaticInspectorRegistry inspectors;
inspectors.add<WeaponConfig>(
    weaponCatalog, "StaticWeapon", "Weapon config",
    [](const WeaponConfig& value) {
        Json out=Json::object();
        out.set("damage",Json::number(int64_t(value.damage)));
        return out;
    });
inspectors.add<AIConfig>(
    aiCatalog, "StaticBrain", "AI config",
    [](const AIConfig& value) {
        Json out=Json::object();
        out.set("aggression",Json::number(int64_t(value.aggression)));
        return out;
    });
inspectors.seal();

// In this module's existing inspectComponents callback:
inspectors.append(world,entity,components,schemas);
```

Each reader fetches the **actual** `StaticRef<T>` from its
World and dereferences its matching sealed
`PrefabStaticCatalog<T>`. The generated schema marks every exposed
field with `editable:false`. The Inspector may show two or more
distinct immutable domains for the same entity without copying their
configuration into the World or into the editor document.

Editing a configuration is an **authoring operation**, not a runtime
component write. The new `engine/scene/PrefabStaticAuthoring.h`
strictly parses versioned JSON prefab configurations using typed
project decoder hooks and seals a new catalog. Reimport/revision
migration must occur at a safe host barrier and preserve save/network
content identity.

The editor's existing `EditorEcsWriteback.h` rules stay unchanged;
no generic static config runtime writeback is enabled. This helper is
opt-in per native GameModule because the editor cannot reflect
arbitrary C++ config types automatically. Native contract test:
`ecs_static_scene_editor_contract_test` (Stage 6 source exists but
its native run is pending).

## Local paused runtime debugging (S4)

Studio Runtime has a separate temporary debug path for owner-approved Unit HP and finite configured Energy. It reuses the gameplay UnitVitals setters also used by Lua and is not the persisted Inspector writeback API. The host checks the paused local session, expected project/epoch/tick and exact previous fixed-point value. The adapter refuses unsupported, dead, dormant or off-map entities and values outside current bounds. No generic native component writes are inferred from inspector schemas. Debug edits are outside the deterministic command log, mark the local session as modified and are discarded when the session stops/restarts; do not use a command-only replay to reproduce a modified session. Native module runtime mutation adapters require a separate opt-in contract. No network or remote MCP write route is added.
