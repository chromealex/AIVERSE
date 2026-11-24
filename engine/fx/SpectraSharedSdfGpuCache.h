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
// One GPU page owns storage and its full canonical ledger reservation.
// Ranges remain immutable while leased; released ranges are quarantined until
// the last use/upload fence is completed. All calls are render-owner only.
class SharedSdfGpuPage final {
public:
    struct Range {uint32_t first=0,count=0;uint64_t fence=0;};
    SharedSdfGpuPage(gfx::Device& device,std::shared_ptr<gfx::GpuAllocationBudget> budget,uint32_t words)
        :device_(device),budget_(std::move(budget)),capacity_(words){}
    ~SharedSdfGpuPage(){
        if(buffer_.valid())device_.destroyBuffer(buffer_);
        if(charged_){budget_->retire(bytes(),lastFence_);budget_->collect(device_.gpuCompletedFence());}
    }
    SharedSdfGpuPage(const SharedSdfGpuPage&)=delete;
    SharedSdfGpuPage& operator=(const SharedSdfGpuPage&)=delete;
    bool initialize(){
        budget_->collect(device_.gpuCompletedFence());
        if(!budget_->reserve(bytes()))return false;
        charged_=true;
        gfx::BufferDesc desc;desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;
        desc.size=bytes();desc.label="SpectraFX packed SDF atlas page";
        buffer_=device_.createBuffer(desc);
        if(!buffer_.valid())return false;
        free_.push_back({0,capacity_,0});return true;
    }
    size_t bytes()const{return size_t(capacity_)*sizeof(uint32_t);}
    gfx::BufferHandle buffer()const{return buffer_;}
    bool sameBudget(const std::shared_ptr<gfx::GpuAllocationBudget>& budget)const{return budget_==budget;}
    bool claim(uint32_t words,uint32_t& first){
        collect();
        for(size_t i=0;i<free_.size();++i){
            auto& r=free_[i];
            if(r.count<words)continue;
            first=r.first;r.first+=words;r.count-=words;
            if(!r.count)free_.erase(free_.begin()+i);
            return true;
        }
        return false;
    }
    void mark(uint64_t fence){lastFence_=std::max(lastFence_,fence);}
    void release(uint32_t first,uint32_t count,uint64_t fence){
        mark(fence);
        if(!fence||(fence!=gfx::GpuAllocationBudget::unknownFence&&fence<=device_.gpuCompletedFence())){
            free_.push_back({first,count,0});mergeFree();
        }else pending_.push_back({first,count,fence});
    }
private:
    void collect(){
        const auto done=device_.gpuCompletedFence();
        if(!done)return;
        bool changed=false;
        for(size_t i=0;i<pending_.size();){
            const auto r=pending_[i];
            if(r.fence!=gfx::GpuAllocationBudget::unknownFence&&r.fence<=done){
                free_.push_back({r.first,r.count,0});
                pending_[i]=pending_.back();pending_.pop_back();changed=true;
            }else ++i;
        }
        if(changed)mergeFree();
    }
    void mergeFree(){
        std::sort(free_.begin(),free_.end(),[](Range a,Range b){return a.first<b.first;});
        size_t n=0;
        for(auto r:free_){
            if(n&&free_[n-1].first+free_[n-1].count==r.first)free_[n-1].count+=r.count;
            else free_[n++]=r;
        }
        free_.resize(n);
    }
    gfx::Device& device_;
    std::shared_ptr<gfx::GpuAllocationBudget> budget_;
    uint32_t capacity_=0;
    gfx::BufferHandle buffer_{};
    uint64_t lastFence_=0;
    bool charged_=false;
    std::vector<Range> free_,pending_;
};
class SharedSdfGpuPayload final {
public:
    SharedSdfGpuPayload(const SharedSdfGpuPayload&)=delete;
    SharedSdfGpuPayload& operator=(const SharedSdfGpuPayload&)=delete;
    ~SharedSdfGpuPayload(){
        if(page_){page_->release(offset_,uint32_t(words_.size()),lastFence_);return;}
        if(buffer_.valid())device_.destroyBuffer(buffer_);
        if(charged_){
            // Vulkan may queue the initial upload before beginFrame. Without
            // a later verified submission fence, do not credit those bytes.
            const auto fence=uploadUntracked_&&!lastFence_?gfx::GpuAllocationBudget::unknownFence:lastFence_;
            budget_->retire(bytes(),fence);budget_->collect(device_.gpuCompletedFence());
        }
    }
    gfx::BufferHandle buffer()const{return page_?page_->buffer():buffer_;}
    size_t bytes()const{return words_.size()*sizeof(uint32_t);}
    uint32_t offsetWords()const{return offset_;}
    bool packed()const{return bool(page_);}
    bool ensureUploaded(){
        if(!page_||uploaded_)return true;
        const auto fence=device_.gpuSubmissionFence();
        if(!fence||!device_.canUpdateSharedBuffers()||!device_.gpuHealthy())return false;
        // Even a rejected upload is conservatively quarantined through this frame.
        markUsed();
        if(!device_.updateStorageBufferChecked(page_->buffer(),words_.data(),bytes(),size_t(offset_)*sizeof(uint32_t)))return false;
        uploaded_=true;return true;
    }
    void markUsed(){
        const auto fence=device_.gpuSubmissionFence();
        lastFence_=fence?std::max(lastFence_,fence):gfx::GpuAllocationBudget::unknownFence;
        if(page_)page_->mark(lastFence_);
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
    bool charged_=false,uploadUntracked_=false,uploaded_=false;
    std::shared_ptr<SharedSdfGpuPage> page_;
    uint32_t offset_=0;
};
class SharedSdfGpuCache final {
public:
    static constexpr size_t kMaxEntries=4096;
    static constexpr size_t kMaxPageBytes=32ull*1024*1024;
    static constexpr size_t kMaxAtlasBytes=128ull*1024*1024;
    static constexpr size_t kMaxPages=16;
    explicit SharedSdfGpuCache(gfx::Device& device,bool packedPages=false):device_(&device),packedPages_(packedPages){}
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
        if(entries_.size()>=kMaxEntries||!device.canUpdateSharedBuffers())return {};
        auto item=std::shared_ptr<SharedSdfGpuPayload>(new SharedSdfGpuPayload(device,budget,words,hash));
        if(packedPages_&&allocatePacked(device,budget,*item)){
            entries_.push_back(item);return item;
        }
        budget->collect(device.gpuCompletedFence());
        if(!budget->reserve(item->bytes()))return {};
        item->charged_=true;
        gfx::BufferDesc desc;
        desc.type=gfx::BufferType::Storage;desc.usage=gfx::Usage::Immutable;
        desc.size=item->bytes();desc.data=item->words_.data();desc.label="SpectraFX shared immutable SDF";
        item->buffer_=device.createBuffer(desc);
        if(!item->buffer_.valid())return {};
        if(device.info().backend==gfx::Backend::Vulkan){
            item->lastFence_=device.gpuSubmissionFence();
            item->uploadUntracked_=item->lastFence_==0;
        }
        entries_.push_back(item);return item;
    }
    size_t livePayloadBytes()const{
        size_t bytes=0;
        for(const auto& weak:entries_)if(auto item=weak.lock())bytes+=item->bytes();
        return bytes;
    }
    size_t atlasBytes()const{
        size_t bytes=0;
        for(const auto& weak:pages_)if(auto page=weak.lock())bytes+=page->bytes();
        return bytes;
    }
    size_t atlasPages()const{
        size_t n=0;for(const auto& weak:pages_)if(!weak.expired())++n;return n;
    }
    size_t liveBytes()const{
        size_t bytes=atlasBytes();
        for(const auto& weak:entries_)if(auto item=weak.lock();item&&!item->packed())bytes+=item->bytes();
        return bytes;
    }
    size_t liveEntries()const{
        size_t n=0;for(const auto& weak:entries_)if(!weak.expired())++n;return n;
    }
private:
    bool allocatePacked(gfx::Device& device,const std::shared_ptr<gfx::GpuAllocationBudget>& budget,
                        SharedSdfGpuPayload& item){
        const uint32_t words=uint32_t(item.words_.size());
        pages_.erase(std::remove_if(pages_.begin(),pages_.end(),[](const auto& p){return p.expired();}),pages_.end());
        for(const auto& weak:pages_)if(auto page=weak.lock()){
            if(page->sameBudget(budget)&&page->claim(words,item.offset_)){
                item.page_=std::move(page);return true;
            }
        }
        // Leave room for a second similarly sized volume. The largest page
        // contains at most 2^23 words; offset+1 is exactly representable in float.
        size_t capacity=1024*1024;
        while(capacity<2*item.bytes()&&capacity<kMaxPageBytes)capacity*=2;
        if(capacity<item.bytes()||pages_.size()>=kMaxPages||
           capacity>kMaxAtlasBytes-atlasBytes())return false;
        auto page=std::make_shared<SharedSdfGpuPage>(device,budget,uint32_t(capacity/sizeof(uint32_t)));
        if(!page->initialize()||!page->claim(words,item.offset_))return false;
        pages_.push_back(page);item.page_=std::move(page);return true;
    }
    gfx::Device* device_;
    bool packedPages_=false;
    std::vector<std::weak_ptr<SharedSdfGpuPayload>> entries_;
    std::vector<std::weak_ptr<SharedSdfGpuPage>> pages_;
};
}
