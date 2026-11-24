// Transactional image imports for immutable appearance publication.
// The GPU allocations are NOT inserted into a live Model until commit.
// Aborted allocations are retired by completion tokens or a proven GPU-idle
// fallback, never by assuming an earlier draw has finished.
#pragma once
#include "Model.h"
#include "AsyncResourceRetirementQueue.h"
#include "RenderDeviceLifetime.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse::render {

// A persistent owner, declared on the native render host (not on the
// temporary staging transaction). It must be drained while gfx::Device
// is still alive, AFTER all draw submissions were detached.
class ModelImageRollbackPool final {
public:
    using Batch=std::vector<Image>;
    using DiagnosticObserver=AsyncResourceRetirementQueue<Batch>::DiagnosticObserver;
    void setRetirementDiagnosticObserver(DiagnosticObserver observer) {
        queue_.setDiagnosticObserver(std::move(observer));
    }
    static void destroyImages(gfx::Device& device,Batch& images) {
        for(auto& image:images) {
            if(image.tex.valid() && !image.borrowedTexture)
                device.destroyTexture(image.tex);
            image.tex={};
            image.textureLease.reset();
        }
        images.clear();
    }
    void retire(gfx::Device& device,Batch&& images) noexcept {
        if(images.empty())return;
        try {
            queue_.enqueueFrom(images);
        }catch(...) {
            // Allocation failure must never destroy a potentially in-flight
            // texture. If native idle cannot be proven, leak the orphan
            // allocation until device teardown instead of a GPU use-after-free.
            ++enqueueFailures_;
            try {
                if(device.waitForResourceIdle())destroyImages(device,images);
                else ++unrecoverableBatches_;
            }catch(...) {
                // noexcept abort paths must not terminate the host. Keep GPU
                // handles orphaned rather than risk freeing in-flight work.
                ++unrecoverableBatches_;
            }
        }
    }
    bool collect(gfx::Device& device) {
        return queue_.collect(
            [&]{return device.beginResourceRetirement();},
            [&](uint64_t token) {return device.resourceRetirementComplete(token);},
            [&](uint64_t token) {
                if(device.resourceRetirementStatus(token)==
                   gfx::ResourceRetirementStatus::Complete)
                    device.endResourceRetirement(token);
                else device.abandonRetirementTokenAfterIdle(token);
            },
            [&]{return device.waitForResourceIdle();},
            [&](Batch& batch){destroyImages(device,batch);},
            [&](uint64_t token) {
                return device.resourceRetirementStatus(token)==
                       gfx::ResourceRetirementStatus::Failed;
            });
    }
    bool drain(gfx::Device& device) {
        return queue_.drain(
            [&]{return device.waitForResourceIdle();},
            [&](uint64_t token) {
                if(token)device.abandonRetirementTokenAfterIdle(token);
            },
            [&](Batch& batch){destroyImages(device,batch);});
    }
    size_t pendingImages() const noexcept {
        size_t n=0;
        queue_.forEachPending([&](const Batch& batch){n+=batch.size();});
        return n;
    }
    uint64_t enqueueFailures() const noexcept{return enqueueFailures_;}
    uint64_t unrecoverableBatches() const noexcept{return unrecoverableBatches_;}
private:
    AsyncResourceRetirementQueue<Batch> queue_;
    uint64_t enqueueFailures_=0,unrecoverableBatches_=0;
};

// Owner-thread, frame-boundary write transaction. The Stage owns all new
// Image handles until successful commit; published material image indices
// already point at reserved slots but MUST NOT be drawn until then.
class ModelImageUploadTransaction final {
public:
    static bool reusableGraphSlot(const Image& image,uint64_t epoch,const ModelImageRollbackPool& rollback)noexcept{
        return image.proceduralGraphImage&&image.proceduralGraphRetired&&!image.tex.valid()&&
            !image.virtualAlphaMask.valid()&&!image.proceduralGraphPin&&image.name.empty()&&
            !rollback.unrecoverableBatches()&&image.proceduralSlotRetirement.reusable(epoch,rollback.pendingImages()==0);
    }
    ModelImageUploadTransaction(gfx::Device& device,
                               ModelImageRollbackPool& rollback,
                               RenderDeviceLifetime::Lease lease={},bool recycleGraphSlots=false)
        :device_(device),rollback_(rollback),lease_(std::move(lease)),recycleGraphSlots_(recycleGraphSlots) {
        checkDevice();
        static_assert(std::is_nothrow_move_constructible_v<Image>&&std::is_nothrow_move_assignable_v<Image>,
                      "Image must move without throwing during final publish");
    }
    ModelImageUploadTransaction(const ModelImageUploadTransaction&)=delete;
    ModelImageUploadTransaction& operator=(const ModelImageUploadTransaction&)=delete;
    ~ModelImageUploadTransaction(){abort();}
    int stage(Model& model,Image image) {
        checkDevice();
        if(committed_)throw std::logic_error("image transaction already committed");
        if(runtimeGraphImage(image)&&(!image.proceduralGraphImage||image.proceduralGraphRetired||
            !image.proceduralGraphPin||image.textureCache||image.borrowedTexture||
            image.textureLease||!image.textureKey.empty()))
            throw std::invalid_argument("invalid exclusive procedural image staging");
        for(size_t i=0;i<model.images.size();++i)
            if(!image.name.empty()&&model.images[i].name==image.name){
                if(runtimeGraphImage(image)||runtimeGraphImage(model.images[i]))
                    throw std::invalid_argument("procedural image name collision");
                return int(i);
            }
        Entry* entry=nullptr;
        for(auto& e:entries_)if(e.model==&model){entry=&e;break;}
        if(!entry) {
            entries_.push_back({&model,model.images.size(),{}, {},model.materialRevision});
            entry=&entries_.back();
        }
        for(size_t i=0;i<entry->images.size();++i)
            if(!image.name.empty()&&entry->images[i].name==image.name){
                if(runtimeGraphImage(image)||runtimeGraphImage(entry->images[i]))
                    throw std::invalid_argument("procedural staged image name collision");
                return int(entry->indices[i]);
            }
        const size_t index=reserveIndex(*entry,image);
        // Allocate the CPU slot BEFORE allocating a GPU image, so a failed
        // vector growth cannot orphan a newly created texture.
        entry->images.reserve(entry->images.size()+1);
        entry->indices.reserve(entry->indices.size()+1);
        if(!uploadStandaloneImage(device_,image))
            throw std::runtime_error("staged material texture GPU upload failed");
        image.rgba.clear();
        image.sharedRgba.reset();
        image.compressed.reset();
        entry->images.push_back(std::move(image)); // nothrow move
        entry->indices.push_back(index);
        return int(index);
    }
    // Adopt an exclusively owned, already copied RGBA8 image. The producer
    // must hold its source buffer and texture through the real copy fence.
    // This is an ownership move, not another texture allocation or upload.
    // No borrowed/cached/virtual textures and no outstanding GPU work.
    int stageCompletedGpuImage(Model& model,Image& image,
            const RenderDeviceLifetime::Lease& producerLease,uint64_t copyFence) {
        checkDevice();
        if(committed_)throw std::logic_error("image transaction already committed");
        if(!lease_.validFor(device_)||!producerLease.validFor(device_)||
           lease_.generation()!=producerLease.generation()||
           !device_.gpuHealthy()||!copyFence||device_.gpuCompletedFence()<copyFence)
            throw std::runtime_error("GPU image copy is not retired in the transaction device generation");
        if(image.name.empty()||image.width<=0||image.height<=0||!image.tex.valid()||
           image.borrowedTexture||image.textureCache||image.textureLease||!image.textureKey.empty()||
           !image.rgba.empty()||image.sharedRgba||image.compressed||image.gpuCompressed||
           image.virtualFallbackMaxDimension!=0||image.virtualFallbackPreserveAlpha||
           image.virtualAlphaMask.valid()||image.alphaCoverageThreshold||image.alphaCoverageMipmaps)
            throw std::invalid_argument("GPU image must be an exclusively owned ordinary RGBA8 texture");
        gfx::TextureInfo info{};
        if(!device_.textureInfo(image.tex,info)||info.width!=image.width||info.height!=image.height||
           info.format!=gfx::PixelFormat::RGBA8)
            throw std::invalid_argument("GPU image metadata mismatch");
        // Existing Model images and all staged images must have distinct
        // ownership. The producer is responsible for exclusivity across
        // other live Models and external texture owners.
        auto conflict=[&](const Image& other){
            return other.tex==image.tex||other.virtualAlphaMask==image.tex;
        };
        for(const auto& other:model.images)
            if(other.name==image.name||conflict(other))
                throw std::invalid_argument("GPU image name or handle already published");
        Entry* entry=nullptr;
        for(auto& e:entries_){
            if(e.model==&model)entry=&e;
            for(const auto& other:e.images)
                if(conflict(other)||(e.model==&model&&other.name==image.name))
                    throw std::invalid_argument("GPU image name or handle already staged");
        }
        if(!entry){
            entries_.push_back({&model,model.images.size(),{}, {},model.materialRevision});
            entry=&entries_.back();
        }
        const size_t index=reserveIndex(*entry,image);
        // All potentially throwing allocations precede the transfer. Image
        // has nothrow move construction; explicitly invalidate source handle.
        entry->images.reserve(entry->images.size()+1);
        entry->indices.reserve(entry->indices.size()+1);
        entry->images.push_back(std::move(image));
        entry->indices.push_back(index);
        image.tex={};
        return int(index);
    }
    size_t pendingImages() const noexcept {
        size_t n=0;
        for(const auto& entry:entries_)n+=entry.images.size();
        return n;
    }
    struct PendingImageReference {
        Model* model=nullptr;
        size_t appendIndex=0; // reserved index; may be an existing certified slot
        const Image* staged=nullptr;
    };
    // Refer only during the outer authoring transaction's preparation.
    // References become invalid after commit()/abort() moves staged Images.
    std::vector<PendingImageReference> pendingImageReferences() const {
        std::vector<PendingImageReference> result;
        result.reserve(pendingImages());
        for(const auto& entry:entries_)
            for(size_t i=0;i<entry.images.size();++i)
                result.push_back({
                    entry.model,entry.indices[i],&entry.images[i]});
        return result;
    }
    size_t commit() {
        checkDevice();
        if(committed_)throw std::logic_error("image transaction already committed");
        // Reserve for ALL models BEFORE mutating the first Model. Any
        // bad_alloc leaves image counts and published material state intact.
        for(auto& entry:entries_) {
            if(entry.model->images.size()!=entry.original)
                throw std::logic_error("Model images changed during transaction");
            size_t end=entry.original;
            for(auto index:entry.indices){
                if(index<entry.original){
                    if(entry.model->materialRevision!=entry.revision||!lease_.validFor(device_)||!reusableGraphSlot(entry.model->images[index],lease_.generation(),rollback_))
                        throw std::logic_error("procedural image slot no longer reusable");
                }else{
                    if(index!=end)throw std::logic_error("noncontiguous image append reservation");
                    ++end;
                }
            }
            entry.model->images.reserve(end);
        }
        size_t committed=0;
        for(auto& entry:entries_) {
            for(size_t i=0;i<entry.images.size();++i) {
                const auto index=entry.indices[i];
                if(index<entry.original)entry.model->images[index]=std::move(entry.images[i]);
                else entry.model->images.push_back(std::move(entry.images[i]));
                ++committed;
            }
            if(std::any_of(entry.indices.begin(),entry.indices.end(),[&](size_t index){return index<entry.original;}))
                entry.model->markMaterialChanged();
            entry.images.clear();
        }
        committed_=true;
        entries_.clear();
        return committed;
    }
    void abort() noexcept {
        if(committed_)return;
        for(auto& entry:entries_)
            rollback_.retire(device_,std::move(entry.images));
        entries_.clear();
        committed_=true;
    }
private:
    struct Entry {
        Model* model=nullptr;
        size_t original=0;
        std::vector<Image> images;
        std::vector<size_t> indices;
        uint64_t revision=0;
    };
    size_t reserveIndex(const Entry& entry,const Image& image)const{
        if(recycleGraphSlots_&&image.proceduralGraphImage&&image.proceduralGraphPin&&lease_.validFor(device_)){
            for(size_t i=0;i<entry.original;++i)
                if(reusableGraphSlot(entry.model->images[i],lease_.generation(),rollback_)&&
                   std::find(entry.indices.begin(),entry.indices.end(),i)==entry.indices.end())return i;
        }
        size_t index=entry.original;
        for(auto reserved:entry.indices)if(reserved>=entry.original)++index;
        if(index>=size_t(std::numeric_limits<int>::max()))throw std::overflow_error("too many material images");
        return index;
    }
    void checkDevice() const {
        if(lease_.device() && !lease_.validFor(device_))
            throw std::logic_error("image upload GPU generation expired");
    }
    gfx::Device& device_;
    ModelImageRollbackPool& rollback_;
    RenderDeviceLifetime::Lease lease_;
    std::vector<Entry> entries_;
    bool committed_=false;
    bool recycleGraphSlots_=false;
};

} // namespace aiverse::render
