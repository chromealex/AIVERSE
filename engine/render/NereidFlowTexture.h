#pragma once
#include "NereidFlowBinding.h"
#include "GpuResourceRetirement.h"
#include "HdrTexture.h"
#include "engine/world/NereidFlowMap.h"

namespace aiverse::render {
struct NereidFlowTextureVersion {
    gfx::TextureHandle texture{};
    double minX=0,minZ=0,worldWidth=0,worldDepth=0;
    uint32_t width=0,height=0;
    uint64_t sourceRevision=0,meshRevision=0;
    size_t bytes=0;
    std::vector<uint16_t> halfTexels; // exact equality, never hash-only reuse
    bool resolvedTransfer=false;
    std::shared_ptr<std::atomic<bool>> lifetime=std::make_shared<std::atomic<bool>>(true);
    NereidFlowBinding binding(double originX,double originZ)const {
        if(!texture.valid())return {};
        return {texture,{float(minX-originX),float(minZ-originZ),
            float(1/worldWidth),float(1/worldDepth)},lifetime};
    }
};

// Explicit graphics-owner lifecycle. CPU replay leases AND completed GPU
// fences must retire before ordinary replacement destroys a texture.
class NereidFlowTextureCache {
public:
    static constexpr size_t kMaxVersions=256;
    static constexpr size_t kMaxBytes=32u*1024u*1024u;
    NereidFlowTextureCache(){versions_.reserve(kMaxVersions);}
    NereidFlowTextureCache(const NereidFlowTextureCache&)=delete;
    NereidFlowTextureCache& operator=(const NereidFlowTextureCache&)=delete;
    void collect(gfx::Device& device) {
        for(auto it=versions_.begin();it!=versions_.end();) {
            auto& version=*it;
            if(version.use_count()==1&&version->lifetime.use_count()==1) {
                version->lifetime->store(false,std::memory_order_release);
                retirement_.retire(device,version->texture,version->bytes);
                it=versions_.erase(it);
            }else ++it;
        }
        retirement_.collect(device);
    }
    size_t knownBytes(const gfx::Device& device)const {
        size_t total=size_t(retirement_.stats(device).knownPendingBytes);
        for(const auto& version:versions_)total+=version->bytes;
        return total;
    }
    std::shared_ptr<const NereidFlowTextureVersion> upload(gfx::Device& device,
            const world::NereidFlowMap& map,uint64_t sourceRevision,uint64_t meshRevision,
            bool resolvedTransfer=false,
            const std::shared_ptr<const NereidFlowTextureVersion>& reuse={},bool* created=nullptr) {
        if(created)*created=false;
        collect(device);
        if(map.empty()||map.texels.size()>kMaxBytes/8)return {};
        const size_t bytes=map.texels.size()*8;
        std::vector<uint16_t> half;half.reserve(map.texels.size()*4);
        for(const auto& cell:map.texels)for(float value:{cell.x,cell.y,cell.z,cell.w}) {
            if(!std::isfinite(value)||std::abs(value)>65504)
                throw std::invalid_argument("NEREID flow-map half range");
            // HDR's helper encodes nonnegative radiance. Preserve signed flow.
            half.push_back(uint16_t(hdr_detail::toHalf(std::abs(value))|
                (std::signbit(value)?0x8000u:0u)));
        }
        if(reuse&&reuse->texture.valid()&&reuse->lifetime->load()&&
           reuse->width==map.width&&reuse->height==map.height&&
           reuse->resolvedTransfer==resolvedTransfer&&reuse->minX==map.minX&&reuse->minZ==map.minZ&&
           reuse->worldWidth==map.width*map.cellMetres&&reuse->worldDepth==map.height*map.cellMetres&&
           reuse->halfTexels==half)return reuse;
        const auto pending=retirement_.stats(device);
        if(versions_.size()+pending.textures>=kMaxVersions||
           knownBytes(device)>kMaxBytes||bytes>kMaxBytes-knownBytes(device))return {};
        auto version=std::make_shared<NereidFlowTextureVersion>();
        version->width=map.width;version->height=map.height;
        version->minX=map.minX;version->minZ=map.minZ;
        version->worldWidth=map.width*map.cellMetres;
        version->worldDepth=map.height*map.cellMetres;
        version->sourceRevision=sourceRevision;version->meshRevision=meshRevision;version->bytes=bytes;
        version->resolvedTransfer=resolvedTransfer;version->halfTexels=std::move(half);
        gfx::TextureDesc desc;desc.width=int(map.width);desc.height=int(map.height);
        desc.format=gfx::PixelFormat::RGBA16F;desc.filter=gfx::Filter::Linear;
        desc.data=version->halfTexels.data();desc.label="NEREID local surface current";
        version->texture=device.createTexture(desc);
        if(!version->texture.valid())return {};
        versions_.push_back(version);if(created)*created=true;return version;
    }
    bool shutdown(gfx::Device& device) {
        // No new CPU capture replay may borrow these versions after shutdown.
        for(auto& version:versions_) {
            version->lifetime->store(false,std::memory_order_release);
            retirement_.retire(device,version->texture,version->bytes);
        }
        versions_.clear();
        if(retirement_.stats(device).textures==0)return true;
        if(device.waitForResourceIdle()) {
            retirement_.drainAfterGpuIdle(device);return true;
        }
        retirement_.abandonDevice(device);return false;
    }
private:
    std::vector<std::shared_ptr<NereidFlowTextureVersion>> versions_;
    GpuResourceRetirement retirement_;
};
}
