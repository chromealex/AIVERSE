// Single in-flight GPU frame token. Zero never proves retirement.
#pragma once
#include <cstdint>

namespace aiverse::render::texture_graph::gpu {
class StepFence {
    std::uint64_t pending_=0;
public:
    bool busy()const{return pending_!=0;}
    bool submit(std::uint64_t token){
        if(!token||busy())return false;
        pending_=token;return true;
    }
    bool retire(std::uint64_t completed){
        if(!busy()||!completed||completed<pending_)return false;
        pending_=0;return true;
    }
    // Device loss or independently proven shutdown-idle only.
    void abandon(){pending_=0;}
};
} // namespace aiverse::render::texture_graph::gpu
