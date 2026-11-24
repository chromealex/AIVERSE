// Native-only GPU texture transport and staging. No independent resource
// owner: the caller's existing graphics owner retains candidate/source
// handles through the copy fence and retires any unadopted images.
#pragma once
#include "TextureGraphMaterialBinding.h"
#include "TextureGraphGpuTextureBudget.h"
#include <span>

namespace aiverse::render::texture_graph {
inline std::string gpuGraphImageName(std::string_view generation,std::string_view output){
    require(!generation.empty()&&generation.size()<=128,"invalid material generation");
    for(const char c:generation)
        require((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-',
                "invalid material generation character");
    require(materialSlot(output)!=nullptr,"nonbindable texture graph output");
    return "#texture-graph/"+std::string(generation)+"/"+std::string(output);
}
// Shared admission estimate for plan inspection and native request execution.
inline gpu::TextureRequestBudget graphGpuTextureBudget(const Program& program,bool mipmaps,size_t materializedLimit,bool fenceOnly=false){
    if(program.size<8||program.size>1024)return {};
    size_t count=0;
    for(const auto& [name,node]:program.outputs){(void)node;if(materialSlot(name))++count;}
    gfx::TextureDesc desc;desc.width=desc.height=program.size;desc.format=gfx::PixelFormat::RGBA8;
    desc.mipmaps=mipmaps;desc.mipLevels=mipmaps?0:1;
    return gpu::textureRequestBudget(count,gfx::estimateTextureFootprint(desc),
        fenceOnly?0:gfx::rgba8CopyLayout(program.size,program.size).bytes,materializedLimit,
        gpu::maxTextureRequestBytes,fenceOnly);
}
// Stable storage owned by the existing native graph request. No implicit
// destruction on a CPU deadline: retirement is the host's responsibility.
struct GraphGpuTextureCopy {
    Image image;
    RenderDeviceLifetime::Lease lease;
    uint64_t fence=0;
    bool encoded=false;
    GraphGpuTextureCopy()=default;
    GraphGpuTextureCopy(const GraphGpuTextureCopy&)=delete;
    GraphGpuTextureCopy& operator=(const GraphGpuTextureCopy&)=delete;
};
// Enqueue in the current graphics frame, after the padded RGBA8 pack pass.
// A false return after fence assignment can still own a destination handle.
// On ALL failures retain source/destination through fence, then use the
// existing image rollback/retirement owner. Never publish from encoded alone.
inline bool queueGraphGpuTextureCopy(gfx::Device& dev,const RenderDeviceLifetime::Lease& lease,
        const Program& program,std::string_view generation,std::string_view output,
        gfx::BufferHandle source,const gfx::BufferTextureCopy2D& copy,GraphGpuTextureCopy& candidate){
    require(!candidate.fence&&!candidate.image.tex.valid()&&!candidate.encoded,
        "GPU texture copy candidate already used");
    require(program.size>=8&&program.size<=1024&&program.outputs.contains(std::string(output))&&
        copy.width==program.size&&copy.height==program.size,"GPU texture graph output or extent mismatch");
    auto name=gpuGraphImageName(generation,output);
    if(!lease.validFor(dev)||!dev.gpuHealthy()||!dev.supportsStorageBufferTextureCopy()||!source.valid())
        return false;
    gfx::BufferInfo info{};
    if(!dev.bufferInfo(source,info)||!gfx::validBufferTextureCopy2D(copy,info.logicalBytes,program.size,program.size))
        return false;
    const auto token=dev.gpuSubmissionFence();
    if(!token||token<=dev.gpuCompletedFence())return false;
    Image image;image.name=std::move(name);image.width=image.height=program.size;
    image.proceduralGraphImage=true;
    // Match the Model uploader: RGBA8 with shader-side colour decoding;
    // hardware sRGB would double-decode some existing material inputs.
    // Optional full GPU mip chain. No alpha-coverage/VT conversion.
    gfx::TextureDesc desc;
    desc.width=image.width;desc.height=image.height;desc.format=gfx::PixelFormat::RGBA8;
    desc.filter=gfx::Filter::Linear;desc.repeat=true;
    desc.mipmaps=copy.generateMipmaps;desc.mipLevels=copy.generateMipmaps?0:1;
    candidate.image=std::move(image);candidate.lease=lease;candidate.fence=token;
    desc.label=candidate.image.name.c_str();
    candidate.image.tex=dev.createTexture(desc);
    if(!candidate.image.tex.valid())return false;
    candidate.encoded=dev.copyStorageBufferToTexture2D(source,candidate.image.tex,copy);
    return candidate.encoded;
}
inline bool graphGpuTextureCopyComplete(const GraphGpuTextureCopy& candidate,gfx::Device& dev){
    return candidate.encoded&&candidate.image.tex.valid()&&candidate.lease.validFor(dev)&&
        dev.gpuHealthy()&&candidate.fence&&dev.gpuCompletedFence()>=candidate.fence;
}
// Bind the complete ordered set of bindable outputs into the existing
// transaction. The native graph owner must verify actual GPU output
// acceptance and immutable request identity before calling. This function
// checks ownership/copy fences, not shader numerical parity.
// No live Model mutation until commit; abort the outer transaction on error.
inline Material stageCompletedGraphGpuTextures(ModelImageUploadTransaction& transaction,
        Model& model,const Material& base,const Program& program,std::string_view generation,
        std::span<GraphGpuTextureCopy* const> candidates,gfx::Device& dev,
        bool acceptedByGraphOwner){
    require(acceptedByGraphOwner,"GPU graph output not accepted by native owner");
    require(program.size>=8&&program.size<=1024&&candidates.size()<=6,
        "invalid GPU material texture set");
    std::vector<MaterialSlot> slots;slots.reserve(6);
    size_t index=0;
    for(const auto& [output,node]:program.outputs){
        (void)node;
        const auto slot=materialSlot(output);
        if(!slot)continue;
        require(index<candidates.size()&&candidates[index]!=nullptr,"incomplete GPU texture set");
        const auto& candidate=*candidates[index];
        require(graphGpuTextureCopyComplete(candidate,dev)&&
            candidate.image.name==gpuGraphImageName(generation,output)&&
            candidate.image.width==program.size&&candidate.image.height==program.size,
            "GPU texture set identity or copy fence mismatch");
        for(size_t previous=0;previous<index;++previous)
            require(candidates[previous]->image.tex!=candidate.image.tex,"duplicate GPU texture ownership");
        slots.push_back(slot);++index;
    }
    require(index!=0&&index==candidates.size(),"invalid GPU texture set order or count");
    Material result=base;
    for(size_t i=0;i<candidates.size();++i){
        auto& candidate=*candidates[i];
        result.*slots[i]=transaction.stageCompletedGpuImage(model,candidate.image,candidate.lease,candidate.fence);
        candidate.encoded=false;candidate.fence=0;candidate.lease={};
    }
    return result;
}
} // namespace aiverse::render::texture_graph
