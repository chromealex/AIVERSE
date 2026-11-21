// AIVERSE immutable/shared ECS component storage.
// Opt-in adapters over the production deterministic Sparse Set. No ECS core
// changes, no pointer handles in serialized state, and no mutable shared data.
#pragma once

#include "engine/core/Archive.h"
#include "engine/core/ECS.h"
#include "engine/core/Hash.h"
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::storage {

// Each entity stores just four bytes of payload, rather than a copy of T.
// Slot 0 is invalid. IDs are local to one World SharedStore or StaticTable.
template<class T> struct SharedRef {
    uint32_t slot = 0;
    constexpr bool valid() const { return slot != 0; }
    constexpr bool operator==(const SharedRef&) const = default;
};
template<class T> struct StaticRef {
    uint32_t slot = 0;
    constexpr bool valid() const { return slot != 0; }
    constexpr bool operator==(const StaticRef&) const = default;
};
template<class T> void io(Archive& a, SharedRef<T>& r) { a(r.slot); }
template<class T> void io(Archive& a, StaticRef<T>& r) { a(r.slot); }

// Deterministic canonical value encoding: no std::hash / pointer addresses,
// memcpy of padding, locale, nonportable unordered_map iteration, or floats
// in gameplay. A project config T must provide io(Archive&,T&) via ADL.
template<class T> struct SharedValueCodec {
    static uint64_t hash(const T& value) {
        Hasher h;
        T copy=value;
        Archive a(h);
        io(a, copy);
        if(!a.ok()) throw std::runtime_error("shared component serialization failed");
        return h.value();
    }
    static bool equal(const T& lhs, const T& rhs) {
        std::string aBytes, bBytes;
        T left=lhs, right=rhs;
        Archive a(&aBytes), b(&bBytes);
        io(a,left);
        io(b,right);
        if(!a.ok() || !b.ok())
            throw std::runtime_error("shared component comparison failed");
        return aBytes == bBytes;
    }
};

// Shared values are INTERNED by contents; the same 128-byte config is held
// only once for any number of Entities. Mutable per-entity data stays in
// ordinary components. A value update creates/points to a different slot.
// Uses shared_ptr<const T> so World deep/COW snapshots share IMMUTABLE
// payloads. Metadata is copied if a snapshot detaches on intern().
template<class T> class SharedStore {
public:
    static constexpr size_t kMaxUnique = 1'000'000;

    SharedRef<T> intern(T value) {
        const uint64_t fingerprint=SharedValueCodec<T>::hash(value);
        auto found=byHash_.find(fingerprint);
        if(found != byHash_.end()) {
            for(uint32_t slot:found->second)
                if(SharedValueCodec<T>::equal(*values_[slot-1],value))
                    return {slot};
        }
        if(values_.size() >= kMaxUnique ||
           values_.size() >= std::numeric_limits<uint32_t>::max())
            throw std::length_error("shared component unique-value limit exceeded");
        auto immutable=std::make_shared<const T>(std::move(value));
        const uint32_t id=uint32_t(values_.size()+1);
        auto [entry,created]=byHash_.try_emplace(fingerprint);
        (void)created;
        try {
            values_.push_back(std::move(immutable));
            try { entry->second.push_back(id); }
            catch(...) { values_.pop_back(); throw; }
        } catch(...) {
            if(entry->second.empty()) byHash_.erase(entry);
            throw;
        }
        return {id};
    }

    const T* tryGet(SharedRef<T> id) const noexcept {
        return id.slot && id.slot <= values_.size()
            ? values_[id.slot-1].get() : nullptr;
    }
    const T& get(SharedRef<T> id) const {
        const T* value=tryGet(id);
        if(!value) throw std::out_of_range("invalid shared component reference");
        return *value;
    }
    size_t uniqueCount() const noexcept { return values_.size(); }
    // Counts unique T payloads only; excludes allocations/hash-index overhead.
    size_t logicalPayloadBytes() const noexcept { return values_.size()*sizeof(T); }

    // Does not reclaim unused slots automatically: snapshots can still refer
    // to them. A future generation-safe, tick-barrier compaction API may
    // rebuild refs and store together, with explicit save/rollback tests.
private:
    std::vector<std::shared_ptr<const T>> values_;
    std::map<uint64_t,std::vector<uint32_t>> byHash_;
    template<class U> friend void io(Archive&, SharedStore<U>&);
};

template<class T> void io(Archive& a, SharedStore<T>& store) {
    uint32_t n=uint32_t(store.values_.size());
    io(a,n);
    if(!a.ok() || n>SharedStore<T>::kMaxUnique) {a.fail();return;}
    if(!a.writing()) store=SharedStore<T>{};
    for(uint32_t i=0;i<n && a.ok();++i) {
        T item=a.writing() ? *store.values_[i] : T{};
        io(a,item);
        if(!a.ok()) break;
        if(!a.writing()) {
            // Valid saves have unique entries in stable intern order.
            try {
                const auto id=store.intern(std::move(item));
                if(id.slot!=i+1) a.fail();
            } catch(...) {a.fail();}
        }
    }
}

// Writer-side canonical operation. World owns the SharedStore as a resource:
// it is snapshotted, hashed and versioned when registered for persistence.
template<class T>
SharedRef<T> setShared(World& world, Entity entity, T value) {
    if(!world.alive(entity)) return {};
    auto* store=world.resource<SharedStore<T>>();
    if(!store) store=&world.setResource<SharedStore<T>>();
    SharedRef<T> ref=store->intern(std::move(value));
    world.add<SharedRef<T>>(entity,ref);
    return ref;
}
template<class T>
const T* readShared(const World& world, Entity entity) noexcept {
    const auto* ref=world.tryGetConst<SharedRef<T>>(entity);
    const auto* store=world.resourceConst<SharedStore<T>>();
    return (ref && store) ? store->tryGet(*ref) : nullptr;
}
template<class T,class Fn>
bool updateShared(World& world, Entity entity, Fn&& transform) {
    const T* old=readShared<T>(world,entity);
    if(!old) return false;
    T value=*old;
    transform(value);
    return setShared<T>(world,entity,std::move(value)).valid();
}
// Readable variant for save loading: names the first offending entity/slot.
template<class T> bool validateSharedRefs(const World& world, std::string& error) {
    const auto* store=world.resourceConst<SharedStore<T>>();
    const size_t size=store?store->uniqueCount():0;
    bool valid=true;
    world.eachExistingConst<SharedRef<T>>([&](Entity e,const SharedRef<T>& ref) {
        if(!valid)return;
        if(!world.alive(e) || !ref.valid() || ref.slot>size) {
            valid=false;
            error="shared ref of entity "+std::to_string(e.index)+":"+
                  std::to_string(e.gen)+" has slot "+std::to_string(ref.slot)+
                  " but the shared store holds "+std::to_string(size)+" values";
        }
    });
    return valid;
}
template<class T> bool validateShared(const World& world) {
    const auto* store=world.resourceConst<SharedStore<T>>();
    bool valid=true;
    world.eachExistingConst<SharedRef<T>>([&](Entity e,const SharedRef<T>& ref) {
        if(!world.alive(e) || !store || !store->tryGet(ref)) valid=false;
    });
    return valid;
}
// Optional deterministic reclamation at a structural tick barrier.
// Rebuilds in ascending old-slot order, remaps only the authoritative
// SharedRef<T> components, and leaves prior World snapshots unchanged.
// No active jobs, external cached SharedRef IDs, or raw read views may survive
// this call. It is NOT safe during each() or while snapshots are written.
template<class T>
size_t compactShared(World& world) {
    const auto* old=world.resourceConst<SharedStore<T>>();
    if(!old) return 0;
    const size_t count=old->uniqueCount();
    std::vector<uint8_t> used(count+1,0);
    bool good=true;
    world.eachExistingConst<SharedRef<T>>([&](Entity e,const SharedRef<T>& ref) {
        if(!world.alive(e) || !old->tryGet(ref))good=false;
        else used[ref.slot]=1;
    });
    if(!good) throw std::logic_error("cannot compact invalid shared references");
    SharedStore<T> next;
    std::vector<uint32_t> remap(count+1,0);
    for(size_t oldId=1;oldId<=count;++oldId)
        if(used[oldId]) {
            const auto created=next.intern(old->get(SharedRef<T>{uint32_t(oldId)}));
            remap[oldId]=created.slot;
        }
    const size_t removed=count-next.uniqueCount();
    if(!removed)return 0;
    // Detach snapshots before touching the pool, while old handles still
    // refer to the old store. All remapping below writes trivially.
    world.prepareExistingWrite<SharedRef<T>>();
    world.setResource<SharedStore<T>>(std::move(next));
    world.eachExisting<SharedRef<T>>([&](Entity,SharedRef<T>& ref) {
        ref.slot=remap[ref.slot];
    });
    return removed;
}

// Hot-loop API: resolve the shared registry once, then perform exactly one
// indexed immutable lookup per entity (no hashing or per-entity allocations).
template<class T> class SharedReadView {
public:
    explicit SharedReadView(const World& world)
        : store_(world.resourceConst<SharedStore<T>>()) {}
    const T* get(SharedRef<T> ref) const noexcept {
        return store_ ? store_->tryGet(ref) : nullptr;
    }
    bool valid() const noexcept {return store_!=nullptr;}
private:
    const SharedStore<T>* store_;
};

// Static components are content-defined, immutable for their WHOLE lifetime.
// Builder is the only mutable interface, and seal() produces a const table.
// The table is held OUTSIDE the ECS World and can be shared across multiple
// Simulations, editor-preview Worlds and rollback snapshots.
template<class T> class StaticTableBuilder;
template<class T> class StaticTable {
public:
    const T* tryGet(StaticRef<T> ref) const noexcept {
        return ref.slot && ref.slot<=values_.size()
            ? &values_[ref.slot-1] : nullptr;
    }
    const T& get(StaticRef<T> ref) const {
        const auto* value=tryGet(ref);
        if(!value) throw std::out_of_range("invalid static component reference");
        return *value;
    }
    size_t uniqueCount() const noexcept {return values_.size();}
    uint64_t fingerprint() const noexcept {return fingerprint_;}
    size_t logicalPayloadBytes() const noexcept {return values_.size()*sizeof(T);}
private:
    explicit StaticTable(std::vector<T> values)
        : values_(std::move(values)) {
        Hasher h;
        h.str("aiverse.ecs.static.v1");
        h.u32(uint32_t(values_.size()));
        for(const auto& value:values_) {
            T copy=value;
            Archive a(h);
            io(a,copy);
            if(!a.ok())
                throw std::runtime_error("invalid static configuration serialization");
        }
        fingerprint_=h.value();
    }
    std::vector<T> values_; // contiguous, read-only after construction
    uint64_t fingerprint_=0;
    friend class StaticTableBuilder<T>;
};

template<class T> class StaticTableBuilder {
public:
    static constexpr size_t kMaxUnique=1'000'000;
    StaticRef<T> define(T value) {
        if(sealed_) throw std::logic_error("static table already sealed");
        uint64_t hash=SharedValueCodec<T>::hash(value);
        auto found=byHash_.find(hash);
        if(found!=byHash_.end())
            for(auto slot:found->second)
                if(SharedValueCodec<T>::equal(values_[slot-1],value))
                    return {slot};
        if(values_.size() >= kMaxUnique ||
           values_.size() >= std::numeric_limits<uint32_t>::max())
            throw std::length_error("static component table full");
        uint32_t slot=uint32_t(values_.size()+1);
        auto [entry,created]=byHash_.try_emplace(hash);
        (void)created;
        try {
            values_.push_back(std::move(value));
            try {entry->second.push_back(slot);}
            catch(...){values_.pop_back();throw;}
        } catch(...) {
            if(entry->second.empty())byHash_.erase(entry);
            throw;
        }
        return {slot};
    }
    std::shared_ptr<const StaticTable<T>> seal() {
        if(sealed_) throw std::logic_error("static table already sealed");
        auto result=std::shared_ptr<const StaticTable<T>>(
            new StaticTable<T>(std::move(values_)));
        sealed_=true;
        values_.clear();
        byHash_.clear();
        return result;
    }
private:
    bool sealed_=false;
    std::vector<T> values_;
    std::map<uint64_t,std::vector<uint32_t>> byHash_;
};

// Authoritative stamp saved with the ECS World, NOT the immutable table
// contents. A cold load must reattach exactly the same content table
// (by content fingerprint/asset revision) before simulating.
template<class T> struct StaticTableBinding {
    uint64_t fingerprint=0;
};
template<class T> void io(Archive& a, StaticTableBinding<T>& binding) {
    a(binding.fingerprint);
}
template<class T> bool bindStaticTable(World& world,const StaticTable<T>& table) {
    const auto* previous=world.resourceConst<StaticTableBinding<T>>();
    if(previous && previous->fingerprint!=table.fingerprint())return false;
    if(!previous)world.setResource<StaticTableBinding<T>>(
        StaticTableBinding<T>{table.fingerprint()});
    return true;
}
template<class T>
bool setStatic(World& world,Entity entity,StaticRef<T> ref,
               const StaticTable<T>& table) {
    if(!world.alive(entity) || world.has<StaticRef<T>>(entity) ||
       !table.tryGet(ref))return false; // Bind once, never writable in-place.
    const auto* binding=world.resourceConst<StaticTableBinding<T>>();
    if(!binding || binding->fingerprint!=table.fingerprint())return false;
    world.add<StaticRef<T>>(entity,ref);
    return true;
}
template<class T>
const T* readStatic(const World& world,Entity entity,
                    const StaticTable<T>& table) noexcept {
    const auto* binding=world.resourceConst<StaticTableBinding<T>>();
    if(!binding || binding->fingerprint!=table.fingerprint())return nullptr;
    const auto* ref=world.tryGetConst<StaticRef<T>>(entity);
    return ref ? table.tryGet(*ref) : nullptr;
}
template<class T>
bool validateStatic(const World& world,const StaticTable<T>& table) {
    const auto* binding=world.resourceConst<StaticTableBinding<T>>();
    if(!binding || binding->fingerprint!=table.fingerprint())return false;
    bool valid=true;
    world.eachExistingConst<StaticRef<T>>([&](Entity entity,const StaticRef<T>& ref){
        if(!world.alive(entity) || !table.tryGet(ref))valid=false;
    });
    return valid;
}
template<class T> class StaticReadView {
public:
    StaticReadView(const World& world,const StaticTable<T>& table) {
        const auto* binding=world.resourceConst<StaticTableBinding<T>>();
        if(binding && binding->fingerprint==table.fingerprint()) table_=&table;
    }
    const T* get(StaticRef<T> ref) const noexcept {
        return table_ ? table_->tryGet(ref) : nullptr;
    }
    bool valid() const noexcept {return table_!=nullptr;}
private:
    const StaticTable<T>* table_=nullptr;
};

static_assert(sizeof(SharedRef<uint64_t>)==sizeof(uint32_t));
static_assert(sizeof(StaticRef<uint64_t>)==sizeof(uint32_t));

} // namespace aiverse::storage
