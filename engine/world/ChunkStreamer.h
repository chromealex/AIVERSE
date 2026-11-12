#pragma once
#include "SpatialWorld.h"
#include <span>

namespace aiverse::world {
struct StreamObserver { Cell cell; int radius=2; };
struct StreamLimits {
    size_t residentBytes=64*1024*1024;
    size_t residentChunks=512;
    uint32_t retryUpdates=30;
};
struct StreamStatus {
    size_t wanted=0, resident=0, reservedBytes=0, pending=0, budgetBlocked=0;
    size_t failed=0, loads=0, unloads=0;
    bool ready() const { return pending==0 && budgetBlocked==0; }
};
// Host-side resource residency; never decides deterministic simulation state.
// Each catalog cost is a conservative reservation for ALL resources owned by a chunk.
// load must publish atomically within that reservation; false must leave nothing allocated.
// unload must release all owned resources. Callbacks must not throw or re-enter this object.
class ChunkStreamer {
public:
    explicit ChunkStreamer(StreamLimits limits={}):limits_(limits) {}
    bool add(Cell cell,size_t reservedBytes);
    void failed(Cell cell);
    StreamStatus update(std::span<const StreamObserver> observers,std::span<const Cell> pinned,
                        size_t loadBudget,const Streamer::Load& load,const Streamer::Unload& unload);
    const std::set<Cell>& loaded() const { return resident_; }
    const StreamStatus& status() const { return status_; }
private:
    struct Entry { size_t bytes; uint64_t retryAt=0; bool failed=false; };
    StreamLimits limits_;
    std::map<Cell,Entry> catalog_;
    std::set<Cell> resident_;
    uint64_t update_=0;
    StreamStatus status_;
};
}
