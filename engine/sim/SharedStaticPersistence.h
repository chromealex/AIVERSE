// Versioned persistence helpers for interned shared / sealed static ECS
// components. Call during GameModule::setup, before Simulation registry seals.
#pragma once
#include "SharedStaticComponents.h"
#include "engine/sim/Simulation.h"
#include <string>
#include <utility>

namespace aiverse::storage {

template<class T>
void registerSharedPersistence(Simulation& sim,std::string stableTypeId,
                               uint32_t version=1) {
    // Shared values are saved once in the authoritative World resource.
    // Entity refs are only 4 bytes. A separate store schema avoids
    // duplicating payload in every serialized component.
    sim.registerPersistentComponent<SharedRef<T>>(stableTypeId+".shared_ref",version);
    sim.registerPersistentResource<SharedStore<T>>(stableTypeId+".shared_store",version);
    // Reject a save whose refs point outside the loaded store with a
    // readable error instead of later out_of_range / null reads.
    sim.addPersistentLoadValidator(stableTypeId+".shared_ref",
        [](const World& world,std::string& error) {
            return validateSharedRefs<T>(world,error);
        });
}

template<class T>
void registerStaticPersistence(Simulation& sim,std::string stableTypeId,
                               uint32_t version=1) {
    // Immutable table bytes live in content assets outside snapshots.
    // The binding fingerprint and each entity's ID are in the checksum/save.
    sim.registerPersistentComponent<StaticRef<T>>(stableTypeId+".static_ref",version);
    sim.registerPersistentResource<StaticTableBinding<T>>(
        stableTypeId+".static_binding",version);
}

} // namespace aiverse::storage
