#pragma once
#include <cstdint>
#include <string>

namespace aiverse::studio {
// The source MapDoc owns historyEpoch. A render snapshot has its own MapDoc
// history and must carry the identity of the document it was copied from.
struct StudioSceneDocument {
    std::string project,path,historyEpoch;
    uint64_t epoch=0,revision=0;
    bool operator==(const StudioSceneDocument&) const = default;
};
}
