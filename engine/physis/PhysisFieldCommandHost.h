// PHYSIS Stage 27: optional authoritative Simulation bridge.
// Install during GameModule::setup after installTrackedFromMap, before setup seals.
// A host must persist/replicate the external immutable journal separately.
#pragma once
#include "PhysisFieldJournal.h"
#include "PhysisFieldRuntime.h"
#include "engine/sim/Simulation.h"
#include <memory>
#include <stdexcept>

namespace aiverse::physis {

struct FieldCommandCheckpoint {
    std::uint64_t lastSequence = 0;
};

inline void io(Archive& a, FieldCommandCheckpoint& value) {
    a(value.lastSequence);
}

class FieldCommandHost final {
public:
    // Called only by authoritative host, never directly by presentation UI.
    void submit(FieldCommand command) {
        // Validate and stage both journal and queue before publishing either.
        auto journal = journal_;
        auto queue = queue_;
        journal.append(command);
        queue.enqueue(std::move(command));
        journal_ = std::move(journal);
        queue_ = std::move(queue);
    }

    // Host-owned durable journal must be reloaded before replaying a restored
    // World; otherwise restoring a snapshot with pending commands is incomplete.
    void restoreFromWorld(const World& world) {
        const auto* checkpoint = world.resourceConst<FieldCommandCheckpoint>();
        if (!checkpoint)
            throw std::logic_error("PHYSIS command checkpoint missing");
        auto next = journal_.pendingAfter(checkpoint->lastSequence);
        queue_ = std::move(next);
    }

    std::size_t apply(Simulation& sim) {
        // Simulation::step invokes systems with tick() equal to the current tick.
        const auto count = queue_.applyAtTick(sim.world(), sim.tick());
        if (count) {
            auto* checkpoint = sim.world().resource<FieldCommandCheckpoint>();
            if (!checkpoint)
                throw std::logic_error("PHYSIS command checkpoint missing");
            checkpoint->lastSequence = queue_.lastAppliedSequence();
        }
        return count;
    }

    const FieldCommandJournal& journal() const { return journal_; }
    std::size_t pendingCount() const { return queue_.size(); }

private:
    FieldCommandJournal journal_;
    FieldCommandQueue queue_;
};

// Return a handle owned by the caller. Simulation callbacks capture it safely.
// Must be called once, after installTrackedFromMap, during GameModule::setup.
inline std::shared_ptr<FieldCommandHost> installFieldCommandHost(Simulation& sim) {
    if (sim.world().resourceConst<FieldRuntimeState>())
        throw std::logic_error("PHYSIS canonical editable runtime already installed; use its Simulation command route");
    if (sim.world().resourceConst<FieldCommandCheckpoint>())
        throw std::logic_error("PHYSIS field command host already installed");
    if (!sim.world().resourceConst<RuntimeFieldRegistry>())
        throw std::logic_error("PHYSIS tracked fields must be installed first");
    sim.world().setResource<FieldCommandCheckpoint>();
    sim.registerPersistentResource<FieldCommandCheckpoint>(
        "physis.field_command_checkpoint", 1);
    auto host = std::make_shared<FieldCommandHost>();
    sim.insertSystem(0, "physis.field_commands",
        [host](Simulation& current) { host->apply(current); });
    sim.addStateRestoredHook([host](Simulation& current) {
        host->restoreFromWorld(current.world());
    });
    return host;
}

} // namespace aiverse::physis
