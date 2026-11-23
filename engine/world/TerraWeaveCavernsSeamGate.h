#pragma once
// Stage 16: source-revision-aware face certification and bounded regeneration.
// A host must regenerate affected chunks from the SAME immutable density
// snapshot, then publish certified groups at its graphics/physics barrier.
#include "engine/world/TerraWeaveCavernsLodPolicy.h"
#include "engine/world/TerraWeaveCavernsShards.h"
#include <cstdint>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace aiverse::terraweave::caverns {
struct CaveCertifiedChunk {
    CaveMesh mesh{};
    uint32_t resolution=0;
    int32_t spacingCm=0;
    uint64_t sourceRevision=0;
    uint64_t renderRevision=0;
};
struct CaveRebuildRequest {
    ChunkKey3 key{};
    uint32_t resolution=0;
    int32_t spacingCm=0;
    uint64_t sourceRevision=0;
    uint64_t requestSerial=0;
};
struct CaveSeamGateStats {
    size_t certifiedFaces=0;
    size_t fallbackFaces=0;
    size_t pendingChunks=0;
};
struct CaveFaceKey {
    ChunkKey3 negative{},positive{};
    constexpr bool operator==(const CaveFaceKey&)const=default;
};
struct CaveFaceKeyLess {
    bool operator()(const CaveFaceKey& a,const CaveFaceKey& b)const {
        const KeyLess less{};
        return less(a.negative,b.negative)||
            (!less(b.negative,a.negative)&&less(a.positive,b.positive));
    }
};
class CaveSeamPublicationGate {
public:
    explicit CaveSeamPublicationGate(size_t maxChunks=512):maxChunks_(maxChunks) {
        if(maxChunks==0||maxChunks>8192)
            throw std::invalid_argument("Caverns seam gate capacity");
    }
    // Source and visual revisions are never inferred from mesh topology.
    // A changed chunk invalidates all its neighboring certifications.
    void upsert(CaveCertifiedChunk chunk) {
        if(!chunk.sourceRevision||!chunk.renderRevision||
           !chunk.resolution||chunk.resolution>64||chunk.spacingCm<=0)
            throw std::invalid_argument("Caverns seam gate chunk");
        if(!chunks_.contains(chunk.mesh.key)&&chunks_.size()>=maxChunks_)
            throw std::length_error("Caverns seam gate full");
        const auto key=chunk.mesh.key;
        const auto it=chunks_.find(key);
        if(it!=chunks_.end()&&it->second.sourceRevision==chunk.sourceRevision&&
           it->second.renderRevision==chunk.renderRevision&&
           it->second.resolution==chunk.resolution&&
           it->second.spacingCm==chunk.spacingCm)
            return;
        chunks_.insert_or_assign(key,std::move(chunk));
        pending_.erase(key);
        invalidate(key);
    }
    bool erase(ChunkKey3 key) {
        if(!chunks_.erase(key))return false;
        pending_.erase(key);invalidate(key);
        return true;
    }
    // Process all loaded face neighbors at a stable owner-thread snapshot.
    // Unsupported mixed contours request the finest common lattice;
    // NO mismatched pair may be published until both rebuilt sources arrive.
    CaveSeamGateStats analyze() {
        certificates_.clear();fallbackFaces_=0;
        std::map<ChunkKey3,CaveRebuildRequest,KeyLess> desired;
        constexpr ChunkKey3 directions[]={{1,0,0},{0,1,0},{0,0,1}};
        for(const auto& [key,first]:chunks_) {
            for(const auto d:directions){
                const ChunkKey3 next{key.x+d.x,key.y+d.y,key.z+d.z};
                const auto it=chunks_.find(next);
                if(it==chunks_.end())continue;
                const auto& second=it->second;
                const auto decision=resolveCaveFaceLod({
                    &first.mesh,first.resolution,first.spacingCm,
                    &second.mesh,second.resolution,second.spacingCm});
                const CaveFaceKey face{key,next};
                // Stage37 GPU shard publication applies certified collinear
                // subdivisions to the presentation mesh of BOTH participating
                // shards. Only truly divergent contours require regeneration.
                // Game collision always uses the unsimplified source mesh.
                if(decision.decision==CaveFaceDecision::RegenerateAtCommonLod) {
                    ++fallbackFaces_;
                    const uint64_t span=int64_t(first.resolution)*first.spacingCm;
                    const int32_t spacing=decision.commonSpacingCm;
                    if(span%spacing||span/spacing>64)
                        throw std::invalid_argument("Caverns fallback LOD budget");
                    for(const auto& [k,c]:std::array<std::pair<ChunkKey3,const CaveCertifiedChunk*>,2>{{
                        {key,&first},{next,&second}}}){
                        if(c->spacingCm==spacing&&c->resolution==uint32_t(span/spacing))
                            continue; // already fine: wait for its neighbor rebuild
                        auto [target,inserted]=desired.try_emplace(k,CaveRebuildRequest{
                            k,uint32_t(span/spacing),spacing,c->sourceRevision,0});
                        if(!inserted&&target->second.spacingCm>spacing){
                            target->second.spacingCm=spacing;
                            target->second.resolution=uint32_t(span/spacing);
                        }
                    }
                }else{
                    certificates_[face]={key,next,first.renderRevision,
                        second.renderRevision,decision.decision,
                        decision.commonResolution,decision.commonSpacingCm};
                }
            }
        }
        // Same source revision and same target: keep the existing request.
        // Otherwise generate a distinct serial, invalidating older workers.
        for(const auto& [key,request]:desired) {
            auto it=pending_.find(key);
            if(it!=pending_.end()&&it->second.resolution==request.resolution&&
               it->second.spacingCm==request.spacingCm&&
               it->second.sourceRevision==request.sourceRevision)continue;
            if(serial_==UINT64_MAX)throw std::overflow_error("Caverns seam serial");
            auto fresh=request;fresh.requestSerial=++serial_;
            pending_.insert_or_assign(key,fresh);
        }
        for(auto it=pending_.begin();it!=pending_.end();) {
            if(!desired.contains(it->first))it=pending_.erase(it);
            else ++it;
        }
        return {certificates_.size(),fallbackFaces_,pending_.size()};
    }
    std::vector<CaveRebuildRequest> requests(size_t budget=8)const{
        if(!budget||budget>maxChunks_)throw std::invalid_argument("Caverns seam request budget");
        std::vector<CaveRebuildRequest> out;
        for(const auto& [key,request]:pending_) {
            (void)key;
            if(out.size()==budget)break;
            out.push_back(request);
        }
        return out;
    }
    // Only accept completed rebuilt meshes for a current, outstanding job.
    // Analyze again afterwards: a source must never be exposed based only
    // on the completion of one side of the seam.
    bool accept(const CaveRebuildRequest& request,CaveCertifiedChunk rebuilt){
        auto it=pending_.find(request.key);
        if(it==pending_.end()||it->second.requestSerial!=request.requestSerial||
           it->second.sourceRevision!=request.sourceRevision||
           rebuilt.mesh.key!=request.key||
           rebuilt.sourceRevision!=request.sourceRevision||
           rebuilt.resolution!=request.resolution||
           rebuilt.spacingCm!=request.spacingCm||
           !rebuilt.renderRevision)return false;
        pending_.erase(it);
        invalidate(request.key);
        chunks_.insert_or_assign(request.key,std::move(rebuilt));
        return true;
    }
    // A chunk cannot enter STRATA/Renderer3D publication if ANY loaded face
    // is uncertified or a regeneration is still pending.
    bool canPublish(ChunkKey3 key,uint64_t renderRevision)const{
        const auto it=chunks_.find(key);
        if(it==chunks_.end()||it->second.renderRevision!=renderRevision||
           pending_.contains(key))return false;
        constexpr ChunkKey3 offsets[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for(const auto d:offsets){
            const ChunkKey3 other{key.x+d.x,key.y+d.y,key.z+d.z};
            auto jt=chunks_.find(other);
            if(jt==chunks_.end())continue;
            const CaveFaceKey face=KeyLess{}(other,key)?
                CaveFaceKey{other,key}:CaveFaceKey{key,other};
            auto ct=certificates_.find(face);
            if(ct==certificates_.end())return false;
            const auto& cert=ct->second;
            const uint64_t first=chunks_.at(face.negative).renderRevision;
            const uint64_t second=chunks_.at(face.positive).renderRevision;
            if(!caveFaceLeaseCurrent(cert,first,second))return false;
        }
        return true;
    }
private:
    void invalidate(ChunkKey3 key) {
        for(auto it=certificates_.begin();it!=certificates_.end();) {
            if(it->first.negative==key||it->first.positive==key)
                it=certificates_.erase(it);
            else ++it;
        }
    }
    size_t maxChunks_;
    uint64_t serial_=0;
    size_t fallbackFaces_=0;
    std::map<ChunkKey3,CaveCertifiedChunk,KeyLess> chunks_;
    std::map<ChunkKey3,CaveRebuildRequest,KeyLess> pending_;
    std::map<CaveFaceKey,CaveFaceLease,CaveFaceKeyLess> certificates_;
};
} // namespace aiverse::terraweave::caverns
