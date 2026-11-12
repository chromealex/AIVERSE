#pragma once
#include "ChunkStreamer.h"
#include <memory>
#include <string>

namespace aiverse::world {
// CPU preparation owns no GPU or live collision objects. Publication/retirement run on caller thread.
class AsyncChunkStream {
public:
    struct Resource { virtual ~Resource()=default; };
    using Payload=std::shared_ptr<Resource>;
    using Prepare=std::function<Payload(Cell)>;
    using Publish=std::function<bool(Cell,const Payload&)>;
    struct Status {
        StreamStatus selection;
        size_t ready=0, preparing=0, stagingBytes=0, discarded=0;
        std::string error;
        bool complete()const{return selection.ready()&&ready==selection.resident;}
    };
    AsyncChunkStream(StreamLimits limits,size_t stagingBytes,size_t maxJobs,Prepare prepare);
    ~AsyncChunkStream();
    AsyncChunkStream(const AsyncChunkStream&)=delete;
    AsyncChunkStream& operator=(const AsyncChunkStream&)=delete;
    bool add(Cell,size_t reservation);
    Status update(std::span<const StreamObserver>,std::span<const Cell> pins,size_t requests,size_t publications,
                  const Publish&,const Streamer::Unload&,size_t publicationBytes=SIZE_MAX);
    bool ready(Cell)const;
    const Status& status()const;
    // Stop/join before destroying reader contexts; release published objects on their owner thread.
    void shutdown(const Streamer::Unload&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
