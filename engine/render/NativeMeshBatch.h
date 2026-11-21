// Generic render-owner-thread batch transaction for native GPU meshes.
// Used by GroundFX and other multi-camera presentation owners. All replacement
// buffers remain unpublished until publish(), and any failure releases only
// never-submitted GPU allocations. Old published buffers use ordered RHI
// retirement tokens; no frame-age heuristic is sufficient.
#pragma once
#include "Renderer3D.h"
#include "NativePresentationRetirement.h"
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

class NativeMeshBatch final {
public:
    NativeMeshBatch(gfx::Device& device,NativePresentationRetirement& retirement)
        :device_(device),retirement_(retirement){}
    NativeMeshBatch(const NativeMeshBatch&)=delete;
    NativeMeshBatch& operator=(const NativeMeshBatch&)=delete;
    ~NativeMeshBatch() noexcept {discard();}

    void stage(Mesh& published,const MeshBuilder& builder) {
        if(published_)throw std::logic_error("NativeMeshBatch: already published");
        const size_t footprint=builder.empty()?0:
            builder.vertexCount()*sizeof(MeshVertex)+
            builder.indices().size()*sizeof(uint32_t);
        Mesh replacement{};
        if(!builder.empty())replacement=builder.upload(device_);
        try {
            for(auto& entry:pending_)
                if(entry.target==&published) {
                    destroyPrivate(entry.replacement);
                    entry.replacement=replacement;
                    entry.bytes=footprint;
                    recalculateBytes();
                    return;
                }
            pending_.push_back({&published,replacement,footprint});
            recalculateBytes();
        }catch(...) {
            destroyPrivate(replacement);
            throw;
        }
    }
    size_t stagedBytes() const noexcept {return bytes_;}
    size_t stagedMeshes() const noexcept {return pending_.size();}
    void publish() noexcept {
        if(published_)return;
        for(auto& entry:pending_) {
            retirement_.retireBuffer(device_,entry.target->vb);
            retirement_.retireBuffer(device_,entry.target->ib);
            *entry.target=std::exchange(entry.replacement,Mesh{});
        }
        pending_.clear();
        bytes_=0;
        published_=true;
    }
    // Optional explicit rollback; destructor also performs this.
    void discard() noexcept {
        for(auto& entry:pending_)destroyPrivate(entry.replacement);
        pending_.clear();
        bytes_=0;
    }
private:
    struct Pending {Mesh* target; Mesh replacement; size_t bytes;};
    void destroyPrivate(Mesh& mesh) noexcept {
        // These buffers have never been bound to any submitted draw.
        if(mesh.vb.valid())device_.destroyBuffer(mesh.vb);
        if(mesh.ib.valid())device_.destroyBuffer(mesh.ib);
        mesh={};
    }
    void recalculateBytes() noexcept {
        bytes_=0;
        for(const auto& entry:pending_)
            bytes_+=entry.bytes;
    }
    gfx::Device& device_;
    NativePresentationRetirement& retirement_;
    std::vector<Pending> pending_;
    size_t bytes_=0;
    bool published_=false;
};

} // namespace aiverse::render
