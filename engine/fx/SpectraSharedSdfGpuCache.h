// Immutable SpectraFX slot-13 payload sharing. Render-owner thread only.
// Device must outlive all leases, as for ComputeParticles.
#pragma once
#include "engine/gfx/Gfx.h"
#include "SpectraStaticSdfVolume.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::spectra {
class SharedSdfGpuPayload final {
public:
    SharedSdfGpuPayload(const SharedSdfGpuPayload&)=delete;
    SharedSdfGpuPayload& operator=(const SharedSdfGpuPayload&)=delete;
    ~SharedSdfGpuPayload(){
        if(buffer_.valid())device_.destroyBuffer(buffer_);
        if(charged_){budget_->retire(bytes(),lastFence_);budget_->collect(device_.gpuCompletedFence());}
    }
    gfx::BufferHandle buffer()const{return buffer_;}
    size_t bytes()const{return words_.size()*sizeof(uint32_t);}
    void markUsed(){
        const auto fence=device_.gpuSubmissionFence();
        lastFence_=fence?std::max(lastFence_,fence):gfx::GpuAllocationBudget::unknownFence;
    }
private:
    friend class SharedSdfGpuCache;
    SharedSdfGpuPayload(gfx::Device& device,std::shared_ptr<gfx::GpuAllocationBudget> budget,
                        const std::vector<uint32_t>& words,uint64_t hash)
        :device_(device),budget_(std::move(budget)),words_(words),hash_(hash){}
    gfx::Device& device_;
    std::shared_ptr<gfx::GpuAllocationBudget> budget_;
    std::vector<uint32_t> words_;
    uint64_t hash_=0,lastFence_=0;
    gfx::BufferHandle buffer_{};
    bool charged_=false;
};
class SharedSdfGpuCache final {
public:
    static constexpr size_t kMaxEntries=4096;
    explicit SharedSdfGpuCache(gfx::Device& device):device_(&device){}
    SharedSdfGpuCache(const SharedSdfGpuCache&)=delete;
    SharedSdfGpuCache& operator=(const SharedSdfGpuCache&)=delete;
    std::shared_ptr<SharedSdfGpuPayload> acquire(gfx::Device& device,
            const std::shared_ptr<gfx::GpuAllocationBudget>& budget,const std::vector<uint32_t>& words){
        if(&device!=device_||!budget||budget->deviceKey()!=&device||
           words.size()<9||words[4]<3||words[4]>fx::kStaticSdfMaxDimension||
           words.size()!=9+size_t(words[4])*words[4]*words[4])return {};
        if(device.acquireGpuAllocationBudget(budget)!=budget)return {};
        uint64_t hash=14695981039346656037ull;
        for(uint32_t word:words)hash=(hash^word)*1099511628211ull;
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),
                       [](const auto& e){return e.expired();}),entries_.end());
        for(const auto& weak:entries_)if(auto item=weak.lock()){
            if(item->budget_==budget&&item->hash_==hash&&item->words_==words)return item;
        }
        if(entries_.size()>=kMaxEntries)return {};
        auto item=std::shared_ptr<SharedSdfGpuPayload>(new SharedSdfGpuPayload(device,budget,words,hash));
        budget->collect(device.gpuCompletedFence());
        if(!budget->reserve(item->bytes()))return {};
        item->charged_=true;
        gfx::BufferDesc desc;
        desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;
        desc.size=item->bytes();desc.data=item->words_.data();desc.label="SpectraFX shared immutable SDF";
        item->buffer_=device.createBuffer(desc);
        if(!item->buffer_.valid())return {};
        entries_.push_back(item);return item;
    }
    size_t liveBytes()const{
        size_t bytes=0;
        for(const auto& weak:entries_)if(auto item=weak.lock())bytes+=item->bytes();
        return bytes;
    }
    size_t liveEntries()const{
        size_t n=0;for(const auto& weak:entries_)if(!weak.expired())++n;return n;
    }
private:
    gfx::Device* device_;
    std::vector<std::weak_ptr<SharedSdfGpuPayload>> entries_;
};
}
