// TerraWeave: bounded, sparse grass patch management and soil disturbance.
// A patch represents up to 262,144 potential blades, with no per-blade ECS
// entities, persistent arrays, or iteration during dig/erosion updates.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/world/TerraWeaveVegetationSoil.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct GrassCoverChange {
    uint64_t key=0;
    uint32_t beforeCoveragePermille=0;
    uint32_t afterCoveragePermille=0;
    uint64_t earthRevision=0;
};

class GrassCoverRegistry {
public:
    static constexpr size_t kMaxPatches=4096;
    static constexpr size_t kMaxBucketPatches=64;
    static constexpr int64_t kBucketCm=6400;

    size_t size() const {return patches_.size();}
    void exchangeSnapshotState(GrassCoverRegistry& other) noexcept {
        patches_.swap(other.patches_);
        buckets_.swap(other.buckets_);
    }
    const GrassPatch* find(uint64_t key) const {
        const auto it=patches_.find(key);
        return it==patches_.end()?nullptr:&it->second;
    }
    std::vector<GrassPatch> snapshot() const {
        std::vector<GrassPatch> result;
        result.reserve(patches_.size());
        for(const auto& [id,patch]:patches_) {
            (void)id;result.push_back(patch);
        }
        return result;
    }
    void setSnapshot(const std::vector<GrassPatch>& patches) {
        if(patches.size()>kMaxPatches)
            throw std::length_error("TerraWeave grass registry budget");
        std::map<uint64_t,GrassPatch> next;
        for(const auto& patch:patches) {
            validate(patch);
            if(!next.emplace(patch.key,patch).second)
                throw std::invalid_argument("TerraWeave duplicate grass patch");
        }
        auto buckets=buildBuckets(next);
        patches_.swap(next);
        buckets_.swap(buckets);
    }
    void upsert(GrassPatch patch) {
        validate(patch);
        auto next=patches_; // all-or-none capacity + index update
        if(!next.contains(patch.key) && next.size()>=kMaxPatches)
            throw std::length_error("TerraWeave grass registry budget");
        next.insert_or_assign(patch.key,std::move(patch));
        auto buckets=buildBuckets(next);
        patches_.swap(next);
        buckets_.swap(buckets);
    }
    bool erase(uint64_t key){
        if(!patches_.contains(key))return false;
        auto next=patches_;
        next.erase(key);
        auto buckets=buildBuckets(next);
        patches_.swap(next);
        buckets_.swap(buckets);
        return true;
    }
    // Replace sparse aggregate cover from a trusted revision-ordered
    // server delta. Does NOT iterate/generate any grass blades.
    void applyAuthoritativeCoverageChanges(
            std::span<const GrassCoverChange> changes,
            uint64_t soilRevision) {
        if(changes.size()>kMaxPatches)
            throw std::length_error("TerraWeave grass network change budget");
        std::set<uint64_t> seen;
        for(const auto& change:changes) {
            const auto it=patches_.find(change.key);
            if(!change.key || !seen.insert(change.key).second ||
               it==patches_.end() ||
               change.beforeCoveragePermille!=it->second.coveragePermille ||
               change.afterCoveragePermille>1000u ||
               change.earthRevision!=soilRevision)
                throw std::invalid_argument("TerraWeave grass network coverage mismatch");
        }
        for(const auto& change:changes)
            patches_.at(change.key).coveragePermille=
                change.afterCoveragePermille;
    }

    void setEnvironment(uint64_t key,PlantEnvironment environment){
        (void)plantAppearance(environment);
        const auto it=patches_.find(key);
        if(it==patches_.end())
            throw std::out_of_range("TerraWeave unknown grass patch");
        it->second.environment=environment;
    }

    // Sorted, bounded GPU/CPU draw packet descriptions; actual blades are
    // generated on demand by sampleGrassBlade or a graphics instancer.
    std::vector<GrassPatchDraw> visible(RectCm region) const {
        validateRect(region);
        std::vector<GrassPatchDraw> out;
        for(const auto& [id,patch]:patches_){
            (void)id;
            if(intersects(region,patch.bounds))
                out.push_back(prepareGrassPatch(patch));
        }
        return out;
    }

    // Excavation, deposition and erosion all disturb plant cover. Only the
    // changed Earthworks vertex cells are visited, not every grass blade.
    // Multiplying overlap area by severity prevents one tiny hole from
    // removing an entire 64m grass patch. Changes are stable in key order.
    std::vector<GrassCoverChange> applyEarthwork(
            const EarthworkChange& earth,int32_t spacingCm) {
        if(spacingCm<10 || spacingCm>10000)
            throw std::invalid_argument("TerraWeave grass earth spacing");
        if(earth.vertices.empty())return {};
        if(earth.vertices.size()>65536)
            throw std::length_error("TerraWeave grass earth edit budget");

        std::map<uint64_t,uint64_t> weightedDamage;
        std::set<EarthVertex> visited;
        for(const auto& edit:earth.vertices) {
            if(!visited.insert(edit.vertex).second)
                throw std::invalid_argument("TerraWeave duplicate grass earth vertex");
            const int64_t x=edit.vertex.x*int64_t(spacingCm);
            const int64_t z=edit.vertex.z*int64_t(spacingCm);
            const RectCm cell{x,z,x+spacingCm,z+spacingCm};
            validateRect(cell);
            const uint64_t diff=edit.afterCm>=edit.beforeCm ?
                uint64_t(int64_t(edit.afterCm)-edit.beforeCm) :
                uint64_t(int64_t(edit.beforeCm)-edit.afterCm);
            const uint32_t severity=uint32_t(
                std::min<uint64_t>(1000u,diff*20u));
            if(!severity)continue;
            const int64_t bx0=detail::floorDiv(cell.minX,kBucketCm);
            const int64_t bx1=detail::floorDiv(cell.maxX-1,kBucketCm);
            const int64_t bz0=detail::floorDiv(cell.minZ,kBucketCm);
            const int64_t bz1=detail::floorDiv(cell.maxZ-1,kBucketCm);
            // Each edit touches at most 3x3 grass bins (10m grid cells,
            // 64m grass bins). Dedup patch IDs across bucket borders.
            std::set<uint64_t> candidates;
            for(int64_t bz=bz0;bz<=bz1;++bz)
                for(int64_t bx=bx0;bx<=bx1;++bx)
                    if(const auto it=buckets_.find({bx,bz});
                       it!=buckets_.end())
                        candidates.insert(it->second.begin(),it->second.end());
            for(uint64_t id:candidates) {
                const GrassPatch& patch=patches_.at(id);
                const uint64_t overlap=overlapArea(cell,patch.bounds);
                if(overlap)weightedDamage[id]+=overlap*severity;
            }
        }

        // All calculations complete before modifying saved patch coverage.
        auto next=patches_;
        std::vector<GrassCoverChange> changes;
        changes.reserve(weightedDamage.size());
        for(const auto& [id,weighted]:weightedDamage) {
            auto& patch=next.at(id);
            const uint64_t area=areaCm2(patch.bounds);
            const uint32_t loss=uint32_t(std::min<uint64_t>(
                1000u,(weighted+area/2u)/area));
            const uint32_t before=patch.coveragePermille;
            const uint32_t after=uint32_t(
                uint64_t(before)*(1000u-loss)/1000u);
            if(before==after)continue;
            patch.coveragePermille=after;
            changes.push_back({id,before,after,earth.revision});
        }
        patches_.swap(next);
        return changes;
    }

private:
    using Bucket=std::pair<int64_t,int64_t>;
    static bool intersects(RectCm a,RectCm b) {
        return a.maxX>b.minX && b.maxX>a.minX &&
               a.maxZ>b.minZ && b.maxZ>a.minZ;
    }
    static uint64_t areaCm2(RectCm r) {
        return uint64_t(r.maxX-r.minX)*uint64_t(r.maxZ-r.minZ);
    }
    static uint64_t overlapArea(RectCm a,RectCm b) {
        if(!intersects(a,b))return 0;
        return uint64_t(std::min(a.maxX,b.maxX)-
                        std::max(a.minX,b.minX))*
               uint64_t(std::min(a.maxZ,b.maxZ)-
                        std::max(a.minZ,b.minZ));
    }
    static void validateRect(RectCm rect) {
        if(rect.minX>=rect.maxX || rect.minZ>=rect.maxZ ||
           rect.minX<-kWorldLimitCm || rect.minZ<-kWorldLimitCm ||
           rect.maxX>kWorldLimitCm || rect.maxZ>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave invalid grass rectangle");
    }
    static void validate(const GrassPatch& patch){
        if(!patch.key)
            throw std::invalid_argument("TerraWeave grass patch key must be nonzero");
        (void)prepareGrassPatch(patch); // also validates bounded geometry and ecology
    }
    static std::map<Bucket,std::vector<uint64_t>>
    buildBuckets(const std::map<uint64_t,GrassPatch>& patches){
        std::map<Bucket,std::vector<uint64_t>> out;
        for(const auto& [id,patch]:patches){
            const RectCm r=patch.bounds;
            const int64_t x0=detail::floorDiv(r.minX,kBucketCm);
            const int64_t x1=detail::floorDiv(r.maxX-1,kBucketCm);
            const int64_t z0=detail::floorDiv(r.minZ,kBucketCm);
            const int64_t z1=detail::floorDiv(r.maxZ-1,kBucketCm);
            for(int64_t z=z0;z<=z1;++z)
                for(int64_t x=x0;x<=x1;++x){
                    auto& v=out[{x,z}];
                    if(v.size()>=kMaxBucketPatches)
                        throw std::length_error("TerraWeave grass local patch density budget");
                    v.push_back(id);
                }
        }
        return out;
    }
    std::map<uint64_t,GrassPatch> patches_;
    std::map<Bucket,std::vector<uint64_t>> buckets_;
};

} // namespace aiverse::terraweave
