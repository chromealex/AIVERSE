// A small presentation-owner lifetime interlock for borrowed RHI atlases.
// A Renderer3D keeps raw pointer(s) to the texture owner during an opt-in GI
// frame. The owner must not be shutdown/recreated or switch GPU->CPU while
// any renderer might still read its old atlas. The caller MUST pair its
// successful bind with controller.unbind(renderer) after the render pass.
// No renderer internals or genre/ECS dependency; no background threads.
#pragma once
#include <cstddef>
#include <set>

namespace aiverse::render {
class GiBorrowedPresentation {
public:
    bool acquire(const void* renderer) const {
        if(!renderer)return false;
        readers_.insert(renderer);
        return true;
    }
    void release(const void* renderer) const {
        readers_.erase(renderer);
    }
    bool canRetire() const {return readers_.empty();}
    size_t readers() const {return readers_.size();}
private:
    // Owner presentation operations are confined to the RHI owner thread.
    // Mutable allows GiLightingController::bind/unbind to remain const.
    mutable std::set<const void*> readers_;
};
} // namespace aiverse::render
