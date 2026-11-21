// Transfer skinned vertex influences to authored LOD surface samples.
// All edits staged; existing Model and morph vertex IDs remain unchanged.
#pragma once
#include "MorphLodMapping.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace aiverse::vivid {
class SkinWeightLodTransfer final {
public:
    struct Stats {std::size_t transferred=0;};
    static Stats apply(render::Model& model,std::size_t sourceMesh,
                       std::size_t targetMesh,const MorphLodMapping& mapping,
                       std::span<const std::uint32_t> sourceIndices) {
        if(model.vb.valid()||model.ib.valid()||model.geometryPoolPage ||
           model.geometry||sourceMesh>=model.meshes.size() ||
           targetMesh>=model.meshes.size()||sourceMesh==targetMesh)
            throw std::invalid_argument("VIVID skin LOD: requires distinct pre-upload CPU meshes");
        mapping.validate();
        if(mapping.targetVertexIds.empty() ||
           mapping.samples.size()!=mapping.targetVertexIds.size() ||
           sourceIndices.size()!=std::size_t(mapping.sourceTriangles)*3)
            throw std::invalid_argument("VIVID skin LOD: invalid correspondence");
        // A mesh must have one unambiguous skinned owner node.
        auto owner=[&](std::size_t mesh) {
            int found=-1;
            for(std::size_t n=0;n<model.nodes.size();++n)
                if(model.nodes[n].mesh==int(mesh)) {
                    if(found>=0 || model.nodes[n].skin<0)
                        throw std::invalid_argument("VIVID skin LOD: ambiguous/unskinned mesh owner");
                    found=int(n);
                }
            if(found<0)throw std::invalid_argument("VIVID skin LOD: no mesh owner");
            return std::size_t(found);
        };
        const auto srcOwner=owner(sourceMesh),dstOwner=owner(targetMesh);
        const auto srcSkinId=std::size_t(model.nodes[srcOwner].skin);
        const auto dstSkinId=std::size_t(model.nodes[dstOwner].skin);
        if(srcSkinId>=model.skins.size() || dstSkinId>=model.skins.size())
            throw std::invalid_argument("VIVID skin LOD: invalid skin owner");
        const auto& srcSkin=model.skins[srcSkinId];
        const auto& dstSkin=model.skins[dstSkinId];
        auto paletteFor=[&](std::size_t mesh) {
            std::map<std::uint32_t,std::vector<std::uint16_t>> lookup;
            for(const auto& prim:model.meshes[mesh].primitives) {
                if(prim.jointPalette.empty())
                    throw std::invalid_argument("VIVID skin LOD: rigid primitive in skinned mesh");
                const std::size_t first=prim.firstIndex;
                if(first>model.indices.size()||
                   prim.indexCount>model.indices.size()-first)
                    throw std::invalid_argument("VIVID skin LOD: primitive range invalid");
                for(std::size_t k=first;k<first+prim.indexCount;++k) {
                    const auto id=model.indices[k];
                    if(id>=model.vertices.size())
                        throw std::invalid_argument("VIVID skin LOD: primitive vertex invalid");
                    auto [it,inserted]=lookup.emplace(id,prim.jointPalette);
                    if(!inserted && it->second!=prim.jointPalette)
                        throw std::invalid_argument("VIVID skin LOD: shared seam palettes conflict");
                }
            }
            return lookup;
        };
        const auto srcPalettes=paletteFor(sourceMesh);
        const auto dstPalettes=paletteFor(targetMesh);
        if(mapping.sourceVertices>model.vertices.size() ||
           dstPalettes.size()!=mapping.targetVertexIds.size())
            throw std::invalid_argument("VIVID skin LOD: source/target vertex count mismatch");
        std::vector<render::float3> sourcePositions;
        sourcePositions.reserve(mapping.sourceVertices);
        for(std::size_t i=0;i<mapping.sourceVertices;++i)
            sourcePositions.push_back(model.vertices[i].pos);
        if(MorphLodMapping::fingerprint(sourcePositions,sourceIndices)!=
           mapping.sourceFingerprint)
            throw std::invalid_argument("VIVID skin LOD: source geometry fingerprint mismatch");
        for(const auto& [id,palette]:dstPalettes) {
            (void)palette;
            if(!std::binary_search(mapping.targetVertexIds.begin(),
                                   mapping.targetVertexIds.end(),id))
                throw std::invalid_argument("VIVID skin LOD: target vertex set mismatch");
        }
        std::vector<std::pair<std::uint32_t,render::ModelVertex>> staged;
        staged.reserve(mapping.samples.size());
        for(std::size_t i=0;i<mapping.samples.size();++i) {
            const auto dst=mapping.targetVertexIds[i];
            const auto dstPalette=dstPalettes.find(dst);
            if(dstPalette==dstPalettes.end())
                throw std::invalid_argument("VIVID skin LOD: target not on target mesh");
            const auto& sample=mapping.samples[i];
            if(sample.triangle>=sourceIndices.size()/3)
                throw std::invalid_argument("VIVID skin LOD: correspondence triangle invalid");
            const std::size_t base=std::size_t(sample.triangle)*3;
            const std::array<std::uint32_t,3> ids{
                sourceIndices[base],sourceIndices[base+1],sourceIndices[base+2]};
            const std::array<float,3> bary{
                sample.bary.x,sample.bary.y,sample.bary.z};
            std::map<int,double> nodeWeights;
            for(std::size_t corner=0;corner<3;++corner) {
                const auto src=ids[corner];
                const auto pal=srcPalettes.find(src);
                if(src>=mapping.sourceVertices || pal==srcPalettes.end())
                    throw std::invalid_argument("VIVID skin LOD: source not in authored mesh");
                const auto& vertex=model.vertices[src];
                for(int j=0;j<4;++j) {
                    if(!vertex.weights[j])continue;
                    const auto slot=vertex.joints[j];
                    if(slot>=pal->second.size() ||
                       pal->second[slot]>=srcSkin.joints.size())
                        throw std::invalid_argument("VIVID skin LOD: source palette invalid");
                    const auto node=srcSkin.joints[pal->second[slot]];
                    if(node<0||std::size_t(node)>=model.nodes.size())
                        throw std::invalid_argument("VIVID skin LOD: source joint invalid");
                    nodeWeights[node]+=double(vertex.weights[j])*
                        double(bary[corner])/255.0;
                }
            }
            std::vector<std::pair<int,double>> converted;
            for(const auto& [joint,value]:nodeWeights) {
                if(value<=0)continue;
                int slot=-1;
                for(std::size_t k=0;k<dstPalette->second.size();++k) {
                    const auto idx=dstPalette->second[k];
                    if(idx>=dstSkin.joints.size())
                        throw std::invalid_argument("VIVID skin LOD: destination palette invalid");
                    if(dstSkin.joints[idx]==joint) {
                        if(k>255 || slot>=0)
                            throw std::invalid_argument("VIVID skin LOD: duplicate destination joint");
                        slot=int(k);
                    }
                }
                if(slot<0)
                    throw std::invalid_argument("VIVID skin LOD: LOD palette missing source joint");
                converted.push_back({slot,value});
            }
            std::sort(converted.begin(),converted.end(),[](const auto& a,const auto& b){
                return a.second!=b.second?a.second>b.second:a.first<b.first;
            });
            if(converted.size()>4)converted.resize(4);
            double sum=0;for(const auto& p:converted)sum+=p.second;
            if(!(sum>0)||!std::isfinite(sum))
                throw std::invalid_argument("VIVID skin LOD: no valid weights");
            struct Quant{int slot;int count;double rem;};
            std::vector<Quant> q;
            int allocated=0;
            for(const auto& [slot,value]:converted) {
                const double scaled=value*255.0/sum;
                const int bytes=int(std::floor(scaled));
                q.push_back({slot,bytes,scaled-bytes});
                allocated+=bytes;
            }
            while(allocated<255) {
                auto best=std::max_element(q.begin(),q.end(),[](const Quant& a,const Quant& b){
                    return a.rem!=b.rem?a.rem<b.rem:a.slot>b.slot;
                });
                ++best->count;best->rem=-1;++allocated;
            }
            render::ModelVertex updated=model.vertices[dst];
            for(int k=0;k<4;++k){updated.joints[k]=0;updated.weights[k]=0;}
            for(std::size_t k=0;k<q.size();++k){
                updated.joints[k]=std::uint8_t(q[k].slot);
                updated.weights[k]=std::uint8_t(q[k].count);
            }
            staged.push_back({dst,updated});
        }
        for(const auto& [id,v]:staged)model.vertices[id]=v;
        return {staged.size()};
    }
};
} // namespace aiverse::vivid
