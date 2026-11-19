// PHYSIS Stage 26: immutable authoritative command history and replay cursor.
// Host must restore World snapshot BEFORE reconstructing a queue from journal.
// The journal is host-owned and intentionally not serialized as ECS state.
#pragma once
#include "PhysisFieldCommands.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::physis {

class FieldCommandJournal {
public:
    void append(FieldCommand command) {
        if (!commands_.empty()) {
            const auto& last = commands_.back();
            if (command.sequence <= last.sequence || command.tick < last.tick)
                throw std::invalid_argument("PHYSIS journal order violation");
        }
        // Reuse the same input validation as the queue before publishing.
        FieldCommandQueue validator;
        validator.enqueue(command);
        commands_.push_back(std::move(command));
    }

    // Rebuild pending commands strictly AFTER the snapshot's last committed
    // sequence. Do not infer this sequence from PHYSIS climate tick alone.
    FieldCommandQueue pendingAfter(std::uint64_t lastCommittedSequence) const {
        FieldCommandQueue queue;
        for (const auto& command : commands_)
            if (command.sequence > lastCommittedSequence)
                queue.enqueue(command);
        return queue;
    }

    // Rebuild only commands for a bounded replay window. The caller must
    // restore the World snapshot corresponding to the supplied checkpoint.
    FieldCommandQueue replayWindow(std::uint64_t lastCommittedSequence,
                                   std::uint64_t throughTick) const {
        FieldCommandQueue queue;
        for (const auto& command : commands_) {
            if (command.sequence <= lastCommittedSequence) continue;
            if (command.tick > throughTick) break;
            queue.enqueue(command);
        }
        return queue;
    }

    const std::vector<FieldCommand>& commands() const { return commands_; }
    std::size_t size() const { return commands_.size(); }
private:
    std::vector<FieldCommand> commands_;
};

} // namespace aiverse::physis
