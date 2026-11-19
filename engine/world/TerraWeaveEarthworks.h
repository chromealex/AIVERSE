// TerraWeave Earthworks: deterministic, bounded excavation/fill and
// shoreline terrain edits over a sparse R16-like centimetre overlay.
// The native scene owner applies changes to render meshes, physics and navmesh
// after this pure CPU operation; no ECS/renderer mutation happens here.
#pragma once
#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct EarthVertex {
    int64_t x=0,z=0; // integer grid index, NOT world centimetres
    constexpr auto operator<=>(const EarthVertex&)const=default;
};
struct EarthVertexEdit {
    EarthVertex vertex{};
    int32_t beforeCm=0,afterCm=0;
    constexpr bool operator==(const EarthVertexEdit&)const=default;
};
struct EarthworkChange {
    uint64_t revision=0;
    uint64_t movedVolumeCm3=0;
    RectCm dirty{};
    std::vector<EarthVertexEdit> vertices;
};
// Persistent sparse overlay. Base terrain recipe/asset identity is supplied
// separately by the game and MUST match before rehydrating this snapshot.
struct EarthworksSnapshot {
    int32_t spacingCm=0,bedrockCm=0;
    uint64_t revision=0;
    std::vector<std::pair<EarthVertex,int32_t>> heights;
};

enum class EarthworkMode:uint8_t { Dig=0, Fill=1 };
struct EarthworkBrush {
    int64_t centerXcm=0,centerZcm=0;
    int32_t radiusCm=200; // 1..50,000cm
    int32_t depthCm=100;  // 1..20,000cm at brush center
    EarthworkMode mode=EarthworkMode::Dig;
};

class EarthworksLayer {
public:
    using BaseSampler=std::function<int32_t(int64_t,int64_t)>;

    EarthworksLayer(int32_t gridSpacingCm,BaseSampler base,
                    int32_t bedrockCm=-1000000)
        : spacingCm_(gridSpacingCm),base_(std::move(base)),
          bedrockCm_(bedrockCm) {
        if(spacingCm_<10||spacingCm_>10000||!base_)
            throw std::invalid_argument("TerraWeave Earthworks grid/base");
    }
    int32_t spacingCm()const{return spacingCm_;}
    uint64_t revision()const{return revision_;}
    size_t overrideCount()const{return heights_.size();}
    int32_t bedrockCm()const{return bedrockCm_;}
    // Prevalidated swap for atomic multi-registry game-save restoration.
    // Different generator callbacks remain owned by their respective layers.
    void exchangeSnapshotState(EarthworksLayer& other) {
        if(spacingCm_!=other.spacingCm_ || bedrockCm_!=other.bedrockCm_)
            throw std::invalid_argument("TerraWeave Earthworks incompatible state swap");
        heights_.swap(other.heights_);
        std::swap(revision_,other.revision_);
    }
    EarthworksSnapshot snapshot()const {
        EarthworksSnapshot out;
        out.spacingCm=spacingCm_;
        out.bedrockCm=bedrockCm_;
        out.revision=revision_;
        out.heights.reserve(heights_.size());
        for(const auto& entry:heights_)out.heights.push_back(entry);
        return out;
    }
    // Transactional restore; rejects malformed data instead of clamping,
    // preserving the exact authoritative soil revision for ECS replay.
    void setSnapshot(const EarthworksSnapshot& snapshot){
        if(snapshot.spacingCm!=spacingCm_ ||
           snapshot.bedrockCm!=bedrockCm_ ||
           snapshot.heights.size()>1000000u)
            throw std::invalid_argument("TerraWeave Earthworks incompatible snapshot");
        std::map<EarthVertex,int32_t> next;
        for(const auto& [key,height]:snapshot.heights) {
            const auto [x,z]=worldPosition(key);
            if(height<bedrockCm_ || height==base_(x,z) ||
               !next.emplace(key,height).second)
                throw std::invalid_argument(
                    "TerraWeave Earthworks invalid/duplicate sparse override");
        }
        heights_.swap(next);
        revision_=snapshot.revision;
    }
    const std::map<EarthVertex,int32_t>& overrides()const{return heights_;}

    int32_t heightAt(EarthVertex key)const {
        const auto [x,z]=worldPosition(key);
        const auto it=heights_.find(key);
        return it==heights_.end()?base_(x,z):it->second;
    }
    int32_t heightAtWorldVertex(int64_t xCm,int64_t zCm)const {
        if(xCm%spacingCm_||zCm%spacingCm_)
            throw std::invalid_argument("TerraWeave Earthworks requires grid-aligned vertices");
        return heightAt({xCm/spacingCm_,zCm/spacingCm_});
    }
    int32_t sampleHeightCm(int64_t xCm,int64_t zCm)const {
        checkWorld(xCm,zCm);
        // Sample four grid corners. The caller should not query the last
        // grid cell at the absolute +/- world limit: there is no +1 neighbor.
        const int64_t ix=detail::floorDiv(xCm,spacingCm_);
        const int64_t iz=detail::floorDiv(zCm,spacingCm_);
        const int64_t fx=xCm-ix*spacingCm_;
        const int64_t fz=zCm-iz*spacingCm_;
        const int64_t w=spacingCm_;
        const int64_t a=heightAt({ix,iz});
        const int64_t b=heightAt({ix+1,iz});
        const int64_t c=heightAt({ix,iz+1});
        const int64_t d=heightAt({ix+1,iz+1});
        const int64_t sum=a*(w-fx)*(w-fz)+b*fx*(w-fz)+
                          c*(w-fx)*fz+d*fx*fz;
        const int64_t divisor=w*w;
        return static_cast<int32_t>((sum>=0 ?
            sum+divisor/2:sum-divisor/2)/divisor);
    }

    // One vertex edit, useful for NEREID erosion output. Surface updates
    // are snapshots/dirty rectangles consumed by the host next frame.
    EarthworkChange setHeight(EarthVertex key,int32_t nextHeightCm) {
        const auto [x,z]=worldPosition(key);
        if(nextHeightCm<bedrockCm_)
            throw std::out_of_range("TerraWeave Earthworks bedrock");
        EarthworkChange change;
        change.dirty={x,z,x+spacingCm_,z+spacingCm_};
        const int32_t old=heightAt(key);
        if(old==nextHeightCm)return change;
        change.vertices.push_back({key,old,nextHeightCm});
        return commit(std::move(change));
    }

    // Guaranteed all-or-none for an entire stroke. Recompute a final
    // candidate set before mutation; exceptions never publish half a hole.
    EarthworkChange sculpt(EarthworkBrush brush) {
        if(brush.radiusCm<1||brush.radiusCm>50000||
           brush.depthCm<1||brush.depthCm>20000||
           (brush.mode!=EarthworkMode::Dig &&
            brush.mode!=EarthworkMode::Fill))
            throw std::invalid_argument("TerraWeave Earthworks brush");
        checkWorld(brush.centerXcm,brush.centerZcm);
        const int64_t r=brush.radiusCm;
        if(brush.centerXcm-r<-kWorldLimitCm+spacingCm_||
           brush.centerZcm-r<-kWorldLimitCm+spacingCm_||
           brush.centerXcm+r>kWorldLimitCm-spacingCm_||
           brush.centerZcm+r>kWorldLimitCm-spacingCm_)
            throw std::out_of_range("TerraWeave Earthworks brush extent");
        const int64_t x0=detail::floorDiv(brush.centerXcm-r,spacingCm_);
        const int64_t x1=detail::floorDiv(brush.centerXcm+r,spacingCm_);
        const int64_t z0=detail::floorDiv(brush.centerZcm-r,spacingCm_);
        const int64_t z1=detail::floorDiv(brush.centerZcm+r,spacingCm_);
        const int64_t count=(x1-x0+1)*(z1-z0+1);
        if(count>65536)
            throw std::length_error("TerraWeave Earthworks stroke budget");
        EarthworkChange change;
        change.dirty={x0*spacingCm_,z0*spacingCm_,
                      (x1+1)*spacingCm_,(z1+1)*spacingCm_};
        change.vertices.reserve(size_t(count));
        const int64_t radiusSq=r*r;
        for(int64_t z=z0;z<=z1;++z)for(int64_t x=x0;x<=x1;++x){
            const int64_t dx=x*spacingCm_-brush.centerXcm;
            const int64_t dz=z*spacingCm_-brush.centerZcm;
            const int64_t d2=dx*dx+dz*dz;
            if(d2>radiusSq)continue;
            const int64_t attenuation=(radiusSq-d2)*1000/radiusSq;
            const int64_t amount=std::max<int64_t>(
                1,int64_t(brush.depthCm)*attenuation/1000);
            const EarthVertex key{x,z};
            const int64_t before=heightAt(key);
            const int64_t requested=brush.mode==EarthworkMode::Dig ?
                std::max<int64_t>(bedrockCm_,before-amount):
                before+amount;
            if(requested>std::numeric_limits<int32_t>::max())
                throw std::out_of_range("TerraWeave Earthworks height overflow");
            if(before==requested)continue;
            change.vertices.push_back({key,int32_t(before),int32_t(requested)});
        }
        if(change.vertices.empty())return change;
        return commit(std::move(change));
    }

    // Replay/undo without any GPU or ECS side effects. Reject conflicting
    // revisions before applying the edit to avoid incorrect multiplayer state.
    EarthworkChange applyEdits(const std::vector<EarthVertexEdit>& edits,
                               uint64_t expectedRevision) {
        if(expectedRevision!=revision_ || edits.size()>65536)
            throw std::invalid_argument("TerraWeave Earthworks stale/big edits");
        EarthworkChange change;
        change.vertices=edits;
        if(edits.empty())return change;
        bool first=true;
        for(const auto& e:edits) {
            const auto [x,z]=worldPosition(e.vertex);
            if(heightAt(e.vertex)!=e.beforeCm ||
               e.afterCm<bedrockCm_)
                throw std::invalid_argument("TerraWeave Earthworks replay conflict");
            if(first){change.dirty={x,z,x+spacingCm_,z+spacingCm_};first=false;}
            else{
                change.dirty.minX=std::min(change.dirty.minX,x);
                change.dirty.minZ=std::min(change.dirty.minZ,z);
                change.dirty.maxX=std::max(change.dirty.maxX,x+spacingCm_);
                change.dirty.maxZ=std::max(change.dirty.maxZ,z+spacingCm_);
            }
        }
        return commit(std::move(change));
    }

private:
    static void checkWorld(int64_t x,int64_t z){
        if(x<-kWorldLimitCm||x>kWorldLimitCm||
           z<-kWorldLimitCm||z>kWorldLimitCm)
            throw std::out_of_range("TerraWeave Earthworks world extent");
    }
    std::pair<int64_t,int64_t> worldPosition(EarthVertex key)const {
        if(key.x<(-kWorldLimitCm+spacingCm_)/spacingCm_||
           key.x>(kWorldLimitCm-spacingCm_)/spacingCm_||
           key.z<(-kWorldLimitCm+spacingCm_)/spacingCm_||
           key.z>(kWorldLimitCm-spacingCm_)/spacingCm_)
            throw std::out_of_range("TerraWeave Earthworks vertex extent");
        return {key.x*spacingCm_,key.z*spacingCm_};
    }
    EarthworkChange commit(EarthworkChange change) {
        if(change.vertices.empty())return change;
        if(heights_.size()+change.vertices.size()>1000000u)
            throw std::length_error("TerraWeave Earthworks sparse edit budget");
        // Copy/swap is a transaction: on allocation failure the old terrain
        // and undo/revision remain intact. Native host persists the returned
        // changes for explicit replay, save or undo.
        auto next=heights_;
        for(const auto& e:change.vertices) {
            const auto [x,z]=worldPosition(e.vertex);
            const int32_t base=base_(x,z);
            if(e.afterCm==base)next.erase(e.vertex);
            else next[e.vertex]=e.afterCm;
            const uint64_t diff=uint64_t(e.afterCm>e.beforeCm ?
                int64_t(e.afterCm)-e.beforeCm:
                int64_t(e.beforeCm)-e.afterCm);
            const uint64_t volume=diff*uint64_t(spacingCm_)*spacingCm_;
            if(change.movedVolumeCm3>std::numeric_limits<uint64_t>::max()-volume)
                throw std::overflow_error("TerraWeave Earthworks volume");
            change.movedVolumeCm3+=volume;
        }
        heights_.swap(next);
        change.revision=++revision_;
        return change;
    }

    int32_t spacingCm_;
    BaseSampler base_;
    int32_t bedrockCm_;
    uint64_t revision_=0;
    std::map<EarthVertex,int32_t> heights_;
};

} // namespace aiverse::terraweave
