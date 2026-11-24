// Procedural graph pixels -> existing staged model image/material publication.
// Presentation only. No second resource owner, no implicit live mutation.
#pragma once
#include "TextureGraph.h"
#include "../ModelImageUploadTransaction.h"
#include <string_view>

namespace aiverse::render::texture_graph {
using MaterialSlot = int Material::*;
inline MaterialSlot materialSlot(std::string_view output) {
    if(output=="color")return &Material::baseTex;
    if(output=="normal")return &Material::normalTex;
    if(output=="metallicRoughness")return &Material::mrTex;
    if(output=="ao")return &Material::occlusionTex;
    if(output=="emissive")return &Material::emissiveTex;
    if(output=="mask")return &Material::teamTex;
    return nullptr; // height and standalone roughness/metallic are not PBR slots.
}
struct MaterialImagePlan {
    struct Entry { MaterialSlot slot; Image image; };
    std::vector<Entry> entries;
};
// Images follow Program::outputs' ordered map. Caller provides a unique,
// immutable publication generation within the target model. No filesystem
// path or shared texture-cache identity is constructed from this token.
inline MaterialImagePlan prepareMaterialImages(const Program& program,
        const std::vector<Image>& images,std::string_view generation) {
    require(!generation.empty()&&generation.size()<=128,"invalid material generation");
    for(const char c:generation)
        require((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-',
                "invalid material generation character");
    require(program.size>=8&&program.size<=1024,"invalid material texture size");
    require(!program.outputs.empty()&&program.outputs.size()<=9&&images.size()==program.outputs.size(),
            "incomplete material texture set");
    MaterialImagePlan plan;
    plan.entries.reserve(6);
    size_t index=0;
    const size_t bytes=size_t(program.size)*size_t(program.size)*4u;
    for(const auto& [name,node]:program.outputs){
        (void)node;
        const auto& src=images[index++];
        (void)outputKind(name);
        require(src.width==program.size&&src.height==program.size&&src.rgba.size()==bytes,
                "invalid material texture pixels");
        if(const auto slot=materialSlot(name)){
            Image img;
            img.name="#texture-graph/"+std::string(generation)+"/"+name;
            img.width=src.width;img.height=src.height;img.rgba=src.rgba;
            img.proceduralGraphImage=true;
            // Model image uploader and material shader own sampling/color
            // semantics. Do not reinterpret data maps or manufacture sRGB metadata.
            plan.entries.push_back({slot,std::move(img)});
        }
    }
    require(!plan.entries.empty(),"texture set contains no bindable material maps");
    return plan;
}
// Use only in an existing owner-thread appearance transaction, for a prepared
// model or candidate material. Commit images before publishing returned Material;
// on ANY failure abort the entire outer transaction. Retire superseded models
// through their existing owner. No live model replacement or GPU zero-copy here.
inline Material stageMaterialImages(ModelImageUploadTransaction& transaction,
        Model& model,const Material& base,MaterialImagePlan plan){
    // stage() normally deduplicates by name. A procedural generation must
    // never silently borrow pixels from an earlier or pending publication.
    const auto pending=transaction.pendingImageReferences();
    for(const auto& entry:plan.entries){
        for(const auto& image:model.images)
            require(image.name!=entry.image.name,"material generation already exists");
        for(const auto& image:pending)
            require(image.model!=&model||image.staged->name!=entry.image.name,
                    "material generation already staged");
    }
    Material result=base;
    for(auto& entry:plan.entries)
        result.*(entry.slot)=transaction.stage(model,std::move(entry.image));
    return result;
}
} // namespace aiverse::render::texture_graph
