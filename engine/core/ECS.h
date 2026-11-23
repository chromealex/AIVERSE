// Minimal, deterministic sparse-set ECS.
//
// Determinism rules:
//  * iteration order = dense order of the first component in the query; it depends only on
//    the sequence of add/remove calls, which is itself deterministic in a lockstep simulation;
//  * entity ids are recycled LIFO with a generation counter;
//  * no pointers/addresses ever influence logic (no unordered_map keyed by pointer, etc.).
//
// Structural changes while iterating: adding components of OTHER types and creating entities is
// fine; destroying entities or removing components must go through destroyLater()/flush().
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include <memory>
#include <cassert>
#include <tuple>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace aiverse {

struct Entity {
    uint32_t index = UINT32_MAX;
    uint32_t gen = 0;
    constexpr bool valid() const { return index != UINT32_MAX; }
    constexpr bool operator==(const Entity&) const = default;
    constexpr uint64_t packed() const { return (uint64_t(gen) << 32) | index; }
    static constexpr Entity fromPacked(uint64_t p) { return {uint32_t(p & 0xffffffffu), uint32_t(p >> 32)}; }
};
inline constexpr Entity kNullEntity{};

namespace detail {
// Atomic: tools build simulations on worker threads (the editor's world preview), and two types first used
// at the same time on two threads must not get the same id (each type's own id is a thread-safe static).
inline uint32_t nextComponentTypeId() { static std::atomic<uint32_t> counter{0}; return counter.fetch_add(1, std::memory_order_relaxed); }
template <class T> uint32_t componentTypeId() { static const uint32_t id = nextComponentTypeId(); return id; }
} // namespace detail

class IPool {
public:
    virtual ~IPool() = default;
    virtual void remove(uint32_t index) = 0;
    virtual bool has(uint32_t index) const = 0;
    virtual void clear() = 0;
    // Snapshots for rollback netcode: deep copy that reuses this pool's memory when possible.
    virtual std::unique_ptr<IPool> clone() const = 0;
    virtual void copyFrom(const IPool& other) = 0;
    virtual void setPaged(bool enabled) = 0;
    virtual void prepareParallelWrite() = 0;
    virtual size_t sharedPageCount() const = 0;
};

template <class T>
class Pool final : public IPool {
public:
    static constexpr uint32_t kNone=UINT32_MAX;
    static constexpr size_t kCowPageElements=256;

    // Contiguous Sparse Set is unchanged in default (non-COW) mode. Opt-in COW
    // uses shared metadata and 256-element pages for component payloads.
    void setPaged(bool enabled) override {
        if(enabled==paged_)return;
        if(enabled) {
            // Build the new representation first. Failed allocations or
            // throwing component copies must leave the original pool intact.
            auto sparse=std::make_shared<std::vector<uint32_t>>(sparse_);
            auto dense=std::make_shared<std::vector<Entity>>(dense_);
            std::vector<std::shared_ptr<std::vector<T>>> pages;
            pages.reserve((data_.size()+kCowPageElements-1)/kCowPageElements);
            for(size_t i=0;i<data_.size();++i) {
                if(i%kCowPageElements==0) {
                    auto page=std::make_shared<std::vector<T>>();
                    page->reserve(kCowPageElements);
                    pages.push_back(std::move(page));
                }
                pages.back()->push_back(data_[i]);
            }
            sparseCow_=std::move(sparse);
            denseCow_=std::move(dense);
            pages_=std::move(pages);
            sparse_.clear();dense_.clear();data_.clear();
        } else {
            auto sparse=*sparseCow_;
            auto dense=*denseCow_;
            std::vector<T> data;
            data.reserve(dense.size());
            for(const auto& page:pages_)
                for(const auto& value:*page)data.push_back(value);
            sparse_=std::move(sparse);
            dense_=std::move(dense);
            data_=std::move(data);
            sparseCow_.reset();
            denseCow_.reset();
            pages_.clear();
        }
        paged_=enabled;
    }
    void prepareParallelWrite() override {
        if(paged_)for(auto& page:pages_)ensureUnique(page);
    }
    size_t sharedPageCount() const override {
        size_t result=0;
        if(paged_)for(const auto& page:pages_)if((page.use_count() != 1))++result;
        return result;
    }

    template <class... Args>
    T& emplace(Entity e,Args&&... args) {
        if(!paged_) {
            if(e.index>=sparse_.size())sparse_.resize(e.index+1,kNone);
            if(sparse_[e.index]!=kNone) {
                data_[sparse_[e.index]]=T{std::forward<Args>(args)...};
                return data_[sparse_[e.index]];
            }
            // Commit sparse membership only after both dense columns append.
            // A throwing component constructor/move must not leave a phantom
            // Entity in the pool, even in the legacy contiguous mode.
            const size_t pos=dense_.size();
            data_.push_back(T{std::forward<Args>(args)...});
            try {dense_.push_back(e);}
            catch(...) {data_.pop_back();throw;}
            sparse_[e.index]=static_cast<uint32_t>(pos);
            return data_.back();
        }
        // Replacing an existing component only dirties its payload page.
        // Do not clone the giant sparse index on ordinary component writes.
        if(e.index<sparseCow_->size() && (*sparseCow_)[e.index]!=kNone) {
            T& current=dataAt((*sparseCow_)[e.index]);
            current=T{std::forward<Args>(args)...};
            return current;
        }
        ensureUnique(sparseCow_);
        if(e.index>=sparseCow_->size())sparseCow_->resize(e.index+1,kNone);
        ensureUnique(denseCow_);
        const size_t pos=denseCow_->size();
        const bool newPage=(pos%kCowPageElements)==0;
        if(newPage) {
            auto page=std::make_shared<std::vector<T>>();
            page->reserve(kCowPageElements);
            pages_.push_back(std::move(page));
        } else ensureUnique(pages_.back());
        try {
            pages_.back()->push_back(T{std::forward<Args>(args)...});
        } catch(...) {
            if(newPage)pages_.pop_back();
            throw;
        }
        try {
            denseCow_->push_back(e);
        } catch(...) {
            pages_.back()->pop_back();
            if(newPage)pages_.pop_back();
            throw;
        }
        (*sparseCow_)[e.index]=static_cast<uint32_t>(pos);
        return pages_.back()->back();
    }

    void remove(uint32_t index) override {
        if(!has(index))return;
        if(!paged_) {
            uint32_t pos=sparse_[index];
            uint32_t last=static_cast<uint32_t>(dense_.size()-1);
            if(pos!=last) {
                dense_[pos]=dense_[last];
                data_[pos]=std::move(data_[last]);
                sparse_[dense_[pos].index]=pos;
            }
            dense_.pop_back();data_.pop_back();
            sparse_[index]=kNone;
            return;
        }
        ensureUnique(sparseCow_);
        ensureUnique(denseCow_);
        const size_t pos=(*sparseCow_)[index];
        const size_t last=denseCow_->size()-1;
        if(pos!=last) {
            dataAt(pos)=std::move(dataAt(last));
            (*denseCow_)[pos]=(*denseCow_)[last];
            (*sparseCow_)[(*denseCow_)[pos].index]=static_cast<uint32_t>(pos);
        }
        ensureUnique(pages_[last/kCowPageElements]);
        pages_[last/kCowPageElements]->pop_back();
        if(pages_.back()->empty())pages_.pop_back();
        denseCow_->pop_back();
        (*sparseCow_)[index]=kNone;
    }

    bool has(uint32_t index) const override {
        const auto& sparse=paged_?*sparseCow_:sparse_;
        return index<sparse.size()&&sparse[index]!=kNone;
    }
    void clear() override {
        if(paged_) {
            sparseCow_=std::make_shared<std::vector<uint32_t>>();
            denseCow_=std::make_shared<std::vector<Entity>>();
            pages_.clear();
        } else {sparse_.clear();dense_.clear();data_.clear();}
    }
    std::unique_ptr<IPool> clone() const override {
        return std::make_unique<Pool<T>>(*this);
    }
    void copyFrom(const IPool& other) override {
        *this=static_cast<const Pool<T>&>(other);
    }
    T* tryGet(uint32_t index) {
        if(!has(index))return nullptr;
        return &dataAt(indexOf(index));
    }
    const T* tryGet(uint32_t index) const {
        if(!has(index))return nullptr;
        return &dataAt(indexOf(index));
    }
    T& get(uint32_t index) {
        assert(has(index));return dataAt(indexOf(index));
    }
    const T& get(uint32_t index) const {
        assert(has(index));return dataAt(indexOf(index));
    }
    size_t size() const {return paged_?denseCow_->size():dense_.size();}
    Entity entityAt(size_t i) const {
        return paged_?(*denseCow_)[i]:dense_[i];
    }
    T& dataAt(size_t i) {
        if(!paged_)return data_[i];
        auto& page=pages_[i/kCowPageElements];
        ensureUnique(page);
        return (*page)[i%kCowPageElements];
    }
    const T& dataAt(size_t i) const {
        if(!paged_)return data_[i];
        return (*pages_[i/kCowPageElements])[i%kCowPageElements];
    }
private:
    template<class V> static void ensureUnique(std::shared_ptr<V>& value) {
        if((value.use_count() != 1))value=std::make_shared<V>(*value);
    }
    size_t indexOf(uint32_t index) const {
        return paged_?(*sparseCow_)[index]:sparse_[index];
    }
    bool paged_=false;
    std::vector<uint32_t> sparse_;
    std::vector<Entity> dense_;
    std::vector<T> data_;
    std::shared_ptr<std::vector<uint32_t>> sparseCow_;
    std::shared_ptr<std::vector<Entity>> denseCow_;
    std::vector<std::shared_ptr<std::vector<T>>> pages_;
};

class World {
public:
    World() = default;
    World(World&&) = default;
    World& operator=(World&&) = default;
    World(const World& o) { copyFrom(o); }
    World& operator=(const World& o) { if (this != &o) copyFrom(o); return *this; }

    // Deep copy of every pool and resource (rollback snapshots). Pools and resources that already
    // exist here are assigned in place so a ring of snapshots stops allocating after warm-up.
    // Opt-in pool-level copy-on-write snapshots. The switch must be made at
    // tick boundaries; outstanding mutable references must not survive it.
    // Copies share untouched pools/resources, and detach on mutable access.
    void enableCowSnapshots(bool enable=true) {
        if(enable==cowSnapshots_)return;
        // Transactional mode conversion: construct ALL replacement pools before
        // exposing any of them. If a component copy throws, the original World
        // (and all snapshots) remain in their old mode and are still usable.
        std::vector<std::shared_ptr<IPool>> converted;
        converted.reserve(pools_.size());
        for(const auto& p:pools_) {
            if(!p) {converted.emplace_back();continue;}
            auto next=std::shared_ptr<IPool>(p->clone());
            next->setPaged(enable);
            converted.push_back(std::move(next));
        }
        std::vector<std::shared_ptr<void>> detached;
        if(!enable) {
            // Test ownership BEFORE staging a second reference: otherwise
            // copying resources_ into a temporary makes every entry look
            // shared, needlessly deep-copying immutable/unique resources.
            detached.reserve(resources_.size());
            for(size_t i=0;i<resources_.size();++i) {
                const auto& resource=resources_[i];
                if(resource && (resource.use_count() != 1))
                    detached.push_back(resourceOps_[i].make(resource.get()));
                else detached.push_back(resource);
            }
        }
        pools_.swap(converted);
        if(!enable)resources_.swap(detached);
        cowSnapshots_=enable;
    }
    bool cowSnapshotsEnabled() const {return cowSnapshots_;}

    void copyFrom(const World& o) {
        if(o.cowSnapshots_) {
            pools_=o.pools_;
            resources_=o.resources_;
            resourceOps_=o.resourceOps_;
        } else {
            pools_.resize(o.pools_.size());
            for (size_t i=0;i<o.pools_.size();++i) {
                if(!o.pools_[i])pools_[i].reset();
                else if(pools_[i] && (pools_[i].use_count() == 1))pools_[i]->copyFrom(*o.pools_[i]);
                else pools_[i]=std::shared_ptr<IPool>(o.pools_[i]->clone());
            }
            resources_.resize(o.resources_.size());
            resourceOps_.resize(o.resourceOps_.size());
            for (size_t i=0;i<o.resources_.size();++i) {
                const ResourceOps& op=o.resourceOps_[i];
                if(!o.resources_[i])resources_[i].reset();
                else if(resources_[i] && (resources_[i].use_count() == 1) && resourceOps_[i].assign==op.assign)
                    op.assign(resources_[i].get(),o.resources_[i].get());
                else resources_[i]=op.make(o.resources_[i].get());
                resourceOps_[i]=op;
            }
        }
        trackedChanges_=o.trackedChanges_;
        cowSnapshots_=o.cowSnapshots_;
        gens_=o.gens_;alive_=o.alive_;free_=o.free_;pendingDestroy_=o.pendingDestroy_;
    }

    // Opt-in conservative tracking of mutable World API access. Direct pool
    // and parallel writers require explicit notification at their barrier.
    template<class T> void trackComponentChanges() {
        trackedChanges_.try_emplace(detail::componentTypeId<T>());
    }
    template<class T> void notifyComponentChange(Entity e) {
        recordComponentChange(detail::componentTypeId<T>(),e);
    }
    // Worker-owned journals: no World mutation until the scheduler barrier.
    struct ComponentChangeBatch {
        static constexpr size_t kMaxEntries=65536;
        std::set<uint64_t> entities;
        bool overflow=false;
        void record(Entity e) {
            if(!e.valid() || overflow)return;
            if(entities.size()>=kMaxEntries && !entities.contains(e.packed())) {
                overflow=true; entities.clear(); return;
            }
            entities.insert(e.packed());
        }
    };
    template<class T> void mergeComponentChanges(const ComponentChangeBatch& batch) {
        auto it=trackedChanges_.find(detail::componentTypeId<T>());
        if(it==trackedChanges_.end())return;
        if(batch.overflow) {it->second.overflow=true;it->second.entities.clear();return;}
        for(auto id:batch.entities)it->second.record(Entity::fromPacked(id));
    }
    template<class T> ComponentChangeBatch drainComponentChanges() {
        auto it=trackedChanges_.find(detail::componentTypeId<T>());
        if(it==trackedChanges_.end())return {};
        ComponentChangeBatch result;
        std::swap(result,it->second);
        return result;
    }
    // Legacy consumer must not silently mistake overflow for no changes.
    template<class T> std::set<uint64_t> takeComponentChanges() {
        auto it=trackedChanges_.find(detail::componentTypeId<T>());
        if(it==trackedChanges_.end())return {};
        if(it->second.overflow)throw std::overflow_error("ECS change journal requires full scan");
        return drainComponentChanges<T>().entities;
    }

    Entity create() {
        if (!free_.empty()) {
            uint32_t idx = free_.back(); free_.pop_back();
            alive_[idx] = 1;
            return {idx, gens_[idx]};
        }
        uint32_t idx = static_cast<uint32_t>(gens_.size());
        gens_.push_back(0); alive_.push_back(1);
        return {idx, 0};
    }

    bool alive(Entity e) const { return e.valid() && e.index < gens_.size() && alive_[e.index] && gens_[e.index] == e.gen; }

    void destroy(Entity e) {
        if (!alive(e)) return;
        for(const auto& [id,changes]:trackedChanges_) {
            if(id<pools_.size() && pools_[id] && pools_[id]->has(e.index))
                recordComponentChange(id,e);
        }
        for (auto& p : pools_) if (p && p->has(e.index)) {
            // Unrelated pools remain shared; only pools containing this Entity
            // need a structural detach before swap-remove.
            if (cowSnapshots_ && (p.use_count() != 1)) p=std::shared_ptr<IPool>(p->clone());
            p->remove(e.index);
        }
        alive_[e.index] = 0;
        ++gens_[e.index];
        free_.push_back(e.index);
    }
    void destroyLater(Entity e) { pendingDestroy_.push_back(e); }
    void flush() {
        for (size_t i = 0; i < pendingDestroy_.size(); ++i) destroy(pendingDestroy_[i]);
        pendingDestroy_.clear();
    }

    template <class T, class... Args> T& add(Entity e, Args&&... args) {
        assert(alive(e));
        notifyComponentChange<T>(e);
        return pool<T>().emplace(e, std::forward<Args>(args)...);
    }
    template <class T> void remove(Entity e) { if (has<T>(e)) { notifyComponentChange<T>(e); pool<T>().remove(e.index); } }
    template <class T> bool has(Entity e) const {
        auto* p = poolIfExists<T>();
        return alive(e) && p && p->has(e.index);
    }
    template <class T> T* tryGet(Entity e) {
        if (!alive(e)) return nullptr;
        auto* p = poolIfExists<T>();
        if(p && p->has(e.index))notifyComponentChange<T>(e);
        return p ? p->tryGet(e.index) : nullptr;
    }
    // Never detaches a shared pool. Safe for declared read-only jobs if no
    // structural changes run concurrently.
    template <class T> const T* tryGetConst(Entity e) const {
        if(!alive(e))return nullptr;
        const auto* p=poolIfExists<T>();
        return p?p->tryGet(e.index):nullptr;
    }
    template <class T> T& get(Entity e) { assert(alive(e)); notifyComponentChange<T>(e); return pool<T>().get(e.index); }

    // Iterate entities having all components. fn(Entity, A&, B&, ...)
    template <class First, class... Rest, class Fn>
    void each(Fn&& fn) {
        auto& first = pool<First>();
        const size_t n = first.size(); // snapshot: entities created during iteration are skipped
        for (size_t i = 0; i < n && i < first.size(); ++i) {
            Entity e = first.entityAt(i);
            if constexpr (sizeof...(Rest) == 0) {
                notifyComponentChange<First>(e);
                fn(e, first.dataAt(i));
            } else {
                if ((pool<Rest>().has(e.index) && ...)) {
                    notifyComponentChange<First>(e);
                    (notifyComponentChange<Rest>(e), ...);
                    fn(e, first.dataAt(i), pool<Rest>().get(e.index)...);
                }
            }
        }
    }
    // No implicit pool registration. Suitable for read-only/query scopes in
    // opt-in jobs once all structural writes have been stopped.
    // Unlike each<T>(), absent component types do not mutate the pool registry.
    template <class T, class Fn>
    void eachExisting(Fn&& fn) {
        auto* p = poolIfExists<T>();
        if (!p) return;
        const size_t n = p->size();
        for (size_t i = 0; i < n && i < p->size(); ++i) {
            notifyComponentChange<T>(p->entityAt(i));
            fn(p->entityAt(i), p->dataAt(i));
        }
    }

    // Stable range iteration for already-registered component pools. No pool
    // creation and no structural writes permitted until all lanes complete.
    template <class T, class Fn>
    void eachExistingRange(size_t begin,size_t end,Fn&& fn) {
        auto* p=poolIfExists<T>();
        if(!p)return;
        const size_t n=p->size();
        if(end>n)end=n;
        for(size_t i=begin;i<end;++i)
            fn(p->entityAt(i),p->dataAt(i));
    }
    template <class T> size_t countExisting() const {
        auto* p=poolIfExists<T>();
        return p?p->size():0;
    }

    template <class T, class Fn>
    void eachExistingConst(Fn&& fn) const {
        const auto* p=poolIfExists<T>();
        if(!p)return;
        const size_t n=p->size();
        for(size_t i=0;i<n;++i)
            fn(p->entityAt(i),p->dataAt(i));
    }

    // Resolve COW once on the caller before launching disjoint write lanes.
    // A null result means the type was never registered; no registry mutation.
    // Scheduler barrier: prepare all declared writable component pools
    // before launching worker lanes. No new pools are registered here.
    void prepareExistingWriteIds(const std::vector<uint32_t>& typeIds) {
        for(uint32_t id:typeIds) {
            if(id>=pools_.size() || !pools_[id])continue;
            auto& pool=pools_[id];
            if(cowSnapshots_ && (pool.use_count() != 1))
                pool=std::shared_ptr<IPool>(pool->clone());
            pool->prepareParallelWrite();
        }
    }

    template <class T> Pool<T>* prepareExistingWrite() {
        auto* pool=poolIfExists<T>();
        if(pool)pool->prepareParallelWrite();
        return pool;
    }

    template <class T> size_t count() { return countExisting<T>(); }

    // Hot: called for every component access. The common case (pool exists) stays inline; creating a
    // pool is the rare out-of-line path.
    template <class T> [[gnu::always_inline]] inline Pool<T>& pool() {
        const uint32_t id = detail::componentTypeId<T>();
        if (id < pools_.size()) [[likely]] if (IPool* p = pools_[id].get()) [[likely]] {
            if (cowSnapshots_ && (pools_[id].use_count() != 1))
                pools_[id]=std::shared_ptr<IPool>(p->clone());
            return *static_cast<Pool<T>*>(pools_[id].get());
        }
        return makePool<T>(id);
    }
    template <class T> [[gnu::noinline]] Pool<T>& makePool(uint32_t id) {
        if (id >= pools_.size()) pools_.resize(id + 1);
        if (!pools_[id]) {
            auto fresh=std::make_shared<Pool<T>>();
            if(cowSnapshots_)fresh->setPaged(true);
            pools_[id]=std::move(fresh);
        }
        else if(cowSnapshots_ && (pools_[id].use_count() != 1))
            pools_[id]=std::shared_ptr<IPool>(pools_[id]->clone());
        return *static_cast<Pool<T>*>(pools_[id].get());
    }

    // Singleton "resources" (game config, map graph, etc.) stored outside the ECS pools.
    template <class T, class... Args> T& setResource(Args&&... args) {
        uint32_t id = detail::componentTypeId<T>();
        if (id >= resources_.size()) { resources_.resize(id + 1); resourceOps_.resize(id + 1); }
        resources_[id] = std::make_shared<T>(std::forward<Args>(args)...);
        resourceOps_[id] = {
            [](const void* src) -> std::shared_ptr<void> { return std::make_shared<T>(*static_cast<const T*>(src)); },
            [](void* dst, const void* src) { *static_cast<T*>(dst) = *static_cast<const T*>(src); }};
        return *static_cast<T*>(resources_[id].get());
    }
    template <class T> T* resource() {
        uint32_t id = detail::componentTypeId<T>();
        if(id>=resources_.size() || !resources_[id])return nullptr;
        if(cowSnapshots_ && (resources_[id].use_count() != 1))
            resources_[id]=resourceOps_[id].make(resources_[id].get());
        return static_cast<T*>(resources_[id].get());
    }
    template <class T> const T* resourceConst() const {
        uint32_t id=detail::componentTypeId<T>();
        return id<resources_.size()&&resources_[id]
            ? static_cast<const T*>(resources_[id].get()):nullptr;
    }

    template<class T> void eraseResource() {
        uint32_t id=detail::componentTypeId<T>();
        if(id<resources_.size())resources_[id].reset();
    }
    // Save states (Simulation::serialize): the entity allocator, restored exactly so ids and recycling match.
    struct EntityState { std::vector<uint32_t> gens; std::vector<uint8_t> alive; std::vector<uint32_t> free; std::vector<Entity> pendingDestroy; };
    EntityState entityState() const { return {gens_, alive_, free_, pendingDestroy_}; }
    size_t poolCount() const { size_t n = 0; for (auto& p : pools_) if (p) ++n; return n; }
    // Diagnostic counters: pools/resources shared with snapshots rather than copied.
    size_t sharedPoolCount() const {
        size_t count=0;
        for(const auto& p:pools_)if(p && (p.use_count() != 1))++count;
        return count;
    }
    size_t sharedCowPageCount() const {
        size_t count=0;
        for(const auto& p:pools_)if(p)count+=p->sharedPageCount();
        return count;
    }
    size_t sharedResourceCount() const {
        size_t count=0;
        for(const auto& resource:resources_)if(resource && (resource.use_count() != 1))++count;
        return count;
    }

    // Validate the entity allocator BEFORE applying untrusted disk state.
    // Dead slots are exactly the unique free-list entries; deferred destroys
    // may contain stale handles because flush() deliberately ignores them.
    static bool validEntityState(const EntityState& s) {
        constexpr size_t kMaxSavedEntities = 16u * 1024u * 1024u;
        const size_t n=s.gens.size();
        if(n>kMaxSavedEntities || s.alive.size()!=n || s.free.size()>n)return false;
        size_t live=0;
        for(uint8_t a:s.alive) {
            if(a>1)return false;
            live+=a;
        }
        if(live+s.free.size()!=n)return false;
        std::vector<uint8_t> seen(n,0);
        for(uint32_t index:s.free) {
            if(index>=n || s.alive[index] || seen[index])return false;
            seen[index]=1;
        }
        return true;
    }
    void restoreEntityState(EntityState s) { gens_ = std::move(s.gens); alive_ = std::move(s.alive); free_ = std::move(s.free); pendingDestroy_ = std::move(s.pendingDestroy); }

    void clear() {
        trackedChanges_.clear();
        pools_.clear(); resources_.clear(); resourceOps_.clear(); gens_.clear(); alive_.clear(); free_.clear(); pendingDestroy_.clear();
    }

private:
    template <class T> const Pool<T>* poolIfExists() const {
        uint32_t id = detail::componentTypeId<T>();
        return id < pools_.size() && pools_[id] ? static_cast<const Pool<T>*>(pools_[id].get()) : nullptr;
    }
    template <class T> Pool<T>* poolIfExists() {
        uint32_t id=detail::componentTypeId<T>();
        if(id>=pools_.size() || !pools_[id])return nullptr;
        if(cowSnapshots_ && (pools_[id].use_count() != 1))
            pools_[id]=std::shared_ptr<IPool>(pools_[id]->clone());
        return static_cast<Pool<T>*>(pools_[id].get());
    }

    void recordComponentChange(uint32_t id,Entity e) {
        if(trackedChanges_.empty() || !e.valid())return;
        auto it=trackedChanges_.find(id);
        if(it!=trackedChanges_.end())it->second.record(e);
    }
    std::map<uint32_t,ComponentChangeBatch> trackedChanges_;
    std::vector<std::shared_ptr<IPool>> pools_;
    bool cowSnapshots_=false;
    std::vector<std::shared_ptr<void>> resources_;
    struct ResourceOps {
        std::shared_ptr<void> (*make)(const void*) = nullptr;
        void (*assign)(void*, const void*) = nullptr;
    };
    std::vector<ResourceOps> resourceOps_;
    std::vector<uint32_t> gens_;
    std::vector<uint8_t> alive_;
    std::vector<uint32_t> free_;
    std::vector<Entity> pendingDestroy_;
};

} // namespace aiverse
