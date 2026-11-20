// CPU-owned per-slot validity mask for a GPU-written scrolling GI atlas.
// One R8 texel per PHYSICAL probe slot; shader uses nearest texelFetch/read.
// Dirty and in-flight slots remain zero until a real GPU fence has completed
// and the clipmap scheduler has accepted the matching revision.
#pragma once
#include "GiClipmapVolume.h"
#include "../gfx/Gfx.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

class GiClipmapValidityTexture {
public:
    static constexpr int kWidth=128;  // same physical 128 probes per L1 atlas row
    ~GiClipmapValidityTexture(){release();}
    GiClipmapValidityTexture()=default;
    GiClipmapValidityTexture(const GiClipmapValidityTexture&)=delete;
    GiClipmapValidityTexture& operator=(const GiClipmapValidityTexture&)=delete;

    bool initialize(gfx::Device& device,size_t probeCount) {
        release();
        if(!probeCount || probeCount>20000 || !device.supports(gfx::PixelFormat::R8))
            return false;
        const int rows=int((probeCount+kWidth-1)/kWidth);
        const std::vector<uint8_t> zeroes(size_t(kWidth)*size_t(rows),0);
        gfx::TextureDesc desc;
        desc.width=kWidth;
        desc.height=rows;
        desc.format=gfx::PixelFormat::R8;
        desc.filter=gfx::Filter::Nearest;
        desc.label="GI physical probe validity";
        desc.data=zeroes.data();
        auto result=device.createTexture(desc);
        if(!result.valid())return false;
        device_=&device;
        texture_=result;
        state_.assign(probeCount,0);
        validCount_=0;
        return true;
    }

    void release(){
        if(device_ && texture_.valid())device_->destroyTexture(texture_);
        texture_={};
        device_=nullptr;
        state_.clear();
        validCount_=0;
    }

    bool ready() const{return device_ && texture_.valid();}
    gfx::TextureHandle texture() const{return texture_;}
    const gfx::Device* device() const{return device_;}
    size_t size() const{return state_.size();}
    size_t validCount() const{return validCount_;}
    bool valid(uint32_t id) const{return state_.at(id)!=0;}

    struct UploadStats {size_t changed=0,regions=0,bytes=0;};

    // Call after polling GPU callbacks and after any local invalidation or
    // scrolling, but BEFORE shading. Updates only differing physical R8 bytes.
    UploadStats sync(const GiClipmapVolume& volume) {
        if(!ready() || volume.size()!=state_.size())
            throw std::logic_error("GI validity texture/volume mismatch");
        UploadStats stats;
        validCount_=0;
        for(size_t i=0;i<state_.size();++i)
            validCount_+=size_t(volume.valid(uint32_t(i)));
        size_t i=0;
        while(i<state_.size()) {
            const uint8_t desired=volume.valid(uint32_t(i))?255u:0u;
            if(state_[i]==desired){++i;continue;}
            const size_t begin=i, row=begin/kWidth;
            std::vector<uint8_t> values;
            while(i<state_.size() && i/kWidth==row) {
                const uint8_t next=volume.valid(uint32_t(i))?255u:0u;
                if(next==state_[i])break;
                values.push_back(next);
                ++i;
            }
            if(values.empty())throw std::logic_error("GI validity no-progress");
            device_->updateTexture(texture_,int(begin%kWidth),int(row),
                                   int(values.size()),1,values.data(),
                                   int(values.size()));
            for(size_t offset=0;offset<values.size();++offset)
                state_[begin+offset]=values[offset];
            stats.changed+=values.size();
            stats.bytes+=values.size();
            ++stats.regions;
        }
        return stats;
    }

private:
    gfx::Device* device_=nullptr;
    gfx::TextureHandle texture_{};
    std::vector<uint8_t> state_;
    size_t validCount_=0;
};

} // namespace aiverse::render
