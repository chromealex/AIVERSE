// Native owner-thread access to a resident procedural graph image.
// Combines existing reader, image pin and device epoch contracts; does not
// own the Model or GPU allocation, submit work, or introduce a retirement queue.
#pragma once
#include "TextureGraphMaterialBinding.h"
#include "../ModelResourceCache.h"

namespace aiverse::render::texture_graph {
class NativeGraphImageRead final {
public:
    NativeGraphImageRead()=default;
    NativeGraphImageRead(const NativeGraphImageRead&)=delete;
    NativeGraphImageRead& operator=(const NativeGraphImageRead&)=delete;
    NativeGraphImageRead(NativeGraphImageRead&&)=default;
    NativeGraphImageRead& operator=(NativeGraphImageRead&&)=default;

    // Call on the bound owner thread. Acquire from the actual owning cache,
    // never from an arbitrary borrowed Model. The scope blocks cache eviction,
    // replacement and host graph sweeps that check activeModelReaders().
    static NativeGraphImageRead acquire(const ModelResourceCache& cache,
            const Model* model,const GraphImageReference& reference,
            gfx::Device& device,const RenderDeviceLifetime::Lease& lease){
        if(!lease.validFor(device)||!device.gpuHealthy()||
           !cache.boundToResourceDevice(device)||
           cache.resourceDeviceGeneration()!=lease.generation())return {};
        auto reader=cache.acquireModelRead(model);
        if(!reader)return {};
        auto image=acquirePublishedGraphImage(*reader.get(),reference);
        if(!image)return {};
        NativeGraphImageRead result;
        result.reader_=std::move(reader);
        result.image_=std::move(image);
        result.deviceLease_=lease;
        return result;
    }

    // BORROWED for synchronous submission while this scope remains live.
    // Do not cache the raw handle, use after reset/move, or cross threads.
    // GPU completion and device shutdown remain the host's responsibility.
    gfx::TextureHandle texture(gfx::Device& device)const{
        if(!reader_||!deviceLease_.validFor(device)||!device.gpuHealthy()||!image_)return {};
        return image_.texture;
    }
    void reset()noexcept{
        image_={};
        reader_.reset();
        deviceLease_={};
    }
private:
    ModelReadLease reader_;
    PublishedGraphImageAccess image_;
    RenderDeviceLifetime::Lease deviceLease_;
};
} // namespace aiverse::render::texture_graph
