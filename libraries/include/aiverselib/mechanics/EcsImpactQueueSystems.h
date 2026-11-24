// Deterministic ECS impact request/commit barrier for any game genre.
// Different motion/targeting systems enqueue impacts, while the host supplies
// armor, shields, faction, status and collision/spatial-query rules.
// No RTS/FPS module dependency; no secondary health or projectile owner.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsOrderedImpactSystems.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::mechanics {

struct QueuedProjectileImpact {
    uint64_t sequence=0;
    OrderedProjectileImpact impact{};
};

// Rollback-owned, bounded world resource. Sequence numbers and pending
// requests must be included in authoritative snapshots/checksums.
struct OrderedImpactQueue {
    static constexpr std::size_t kMaximumPending=8192;
    uint64_t nextSequence=1;
    std::vector<QueuedProjectileImpact> pending;
};


// Explicit endian-independent field serializers; no raw-memory snapshots.
inline void io(Archive& a,OrderedProjectileImpact& value) {
    a(value.projectile,value.source,value.target,value.point,
      value.damage,value.splashRadius);
}
inline void io(Archive& a,QueuedProjectileImpact& value) {
    a(value.sequence,value.impact);
}
inline bool validOrderedImpactQueue(const OrderedImpactQueue& queue) {
    if(queue.nextSequence==0 ||
       queue.pending.size()>OrderedImpactQueue::kMaximumPending)return false;
    uint64_t previous=0;
    for(const auto& request:queue.pending) {
        if(request.sequence<=previous ||
           request.sequence>=queue.nextSequence)return false;
        previous=request.sequence;
    }
    return true;
}
inline void io(Archive& archive,OrderedImpactQueue& queue) {
    uint32_t format=1;
    archive(format);
    if(!archive.ok() || format!=1){archive.fail();return;}
    archive(queue.nextSequence);
    // 72 bytes for each canonical (sequence, three Entity refs, Vec3, two Fixed).
    const uint32_t count=archive.count(queue.pending.size(),72);
    if(!archive.ok() || count>OrderedImpactQueue::kMaximumPending) {
        archive.fail();return;
    }
    if(!archive.writing())queue.pending.resize(count);
    for(auto& request:queue.pending) {
        archive(request);
        if(!archive.ok())return;
    }
    if(!validOrderedImpactQueue(queue))archive.fail();
}

// Transactional load: a malformed stream leaves the previous queue intact.
inline bool loadOrderedImpactQueue(const std::string& bytes,
                                   OrderedImpactQueue& destination) {
    if(bytes.size()>1024*1024)return false;
    Archive archive(bytes);
    OrderedImpactQueue candidate;
    archive(candidate);
    if(!archive.ok() || archive.remaining()!=0 ||
       !validOrderedImpactQueue(candidate))return false;
    destination=std::move(candidate);
    return true;
}
inline std::string saveOrderedImpactQueue(const OrderedImpactQueue& queue) {
    if(!validOrderedImpactQueue(queue))return {};
    OrderedImpactQueue copy=queue;
    std::string bytes;
    Archive archive(&bytes);
    archive(copy);
    return archive.ok()?bytes:std::string{};
}
inline void hashOrderedImpactQueue(Hasher& hash,
                                   const OrderedImpactQueue& queue) {
    hash.u64(queue.nextSequence);
    hash.u32(uint32_t(queue.pending.size()));
    for(const auto& request:queue.pending) {
        hash.u64(request.sequence);
        hash.u64(request.impact.projectile.packed());
        hash.u64(request.impact.source.packed());
        hash.u64(request.impact.target.packed());
        hash.v3(request.impact.point);
        hash.fx(request.impact.damage);
        hash.fx(request.impact.splashRadius);
    }
}

// Submit after validated weapon/collision rules, in a deterministic order.
// Failure does not consume a sequence ID or append a partial request.
inline uint64_t enqueueOrderedImpact(World& world,
                                     OrderedProjectileImpact impact) {
    auto* queue=world.resource<OrderedImpactQueue>();
    if(!queue || queue->pending.size()>=OrderedImpactQueue::kMaximumPending ||
       queue->nextSequence==0 ||
       queue->nextSequence==std::numeric_limits<uint64_t>::max())
        return 0;
    const uint64_t sequence=queue->nextSequence++;
    queue->pending.push_back({sequence,std::move(impact)});
    return sequence;
}

// Signature of callbacks:
//   DirectHit  (Simulation&, const QueuedProjectileImpact&, Entity target)
//   AfterDirect(Simulation&, const QueuedProjectileImpact&, Entity target)
//   QueryArea  (Simulation&, const QueuedProjectileImpact&, Vec3, Fixed,
//               visitor(Entity))
//   CanSplash  (Simulation&, const QueuedProjectileImpact&, Entity) -> bool
//   SplashHit  (Simulation&, const QueuedProjectileImpact&, Entity)
//
// The queue is swapped at the barrier. Requests created by callbacks are
// processed at the NEXT invocation, never recursively in the same pass.
// A caller whose legacy mechanics require immediate per-projectile impacts
// must register/execute a compatible barrier for each relevant stage; blindly
// batching all hits at end-of-tick can change shield, kill and trigger order.
template<class DirectHit,class AfterDirect,class QueryArea,
         class CanSplash,class SplashHit>
void addOrderedImpactResolutionSystem(Simulation& sim,std::string name,
                                      DirectHit directHit,
                                      AfterDirect afterDirect,
                                      QueryArea queryArea,
                                      CanSplash canSplash,
                                      SplashHit splashHit) {
    sim.addSystem(std::move(name),
        [directHit=std::move(directHit),
         afterDirect=std::move(afterDirect),
         queryArea=std::move(queryArea),
         canSplash=std::move(canSplash),
         splashHit=std::move(splashHit)](Simulation& current) mutable {
            auto* queue=current.world().resource<OrderedImpactQueue>();
            if(!queue || queue->pending.empty())return;
            std::vector<QueuedProjectileImpact> batch;
            batch.swap(queue->pending);
            for(const auto& request:batch)
                resolveOrderedProjectileImpact(
                    request.impact,
                    [&](Entity target){directHit(current,request,target);},
                    [&](Entity target){afterDirect(current,request,target);},
                    [&](Vec3 point,Fixed radius,auto&& visit) {
                        queryArea(current,request,point,radius,
                                  std::forward<decltype(visit)>(visit));
                    },
                    [&](Entity target)->bool {
                        return canSplash(current,request,target);
                    },
                    [&](Entity target){splashHit(current,request,target);});
        });
}

} // namespace aiverse::lib::mechanics
