// Deterministic, schema-versioned native ECS persistence.
// Register component/resource types and optional explicit legacy value readers
// during Simulation::setup. No reflection, transient pointer serialization or
// implicit field migrations: stable IDs and field-wise Archive IO are required.
#pragma once
#include "../core/Archive.h"
#include "../core/Hash.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse {
class PersistentEcsRegistry {
public:
    enum class Kind : uint8_t { Component=1, Resource=2 };
    static constexpr uint32_t kMaxPayload=32u*1024u*1024u;
    static constexpr uint32_t kMaxEntities=1000000;
    static constexpr uint32_t kMaxSchemas=1024;

    // The original (pre-upgrade) byte stream is hashed independently of
    // current schema versions. This lets Simulation verify the ORIGINAL save
    // checksum even if a registered component is explicitly migrated.
    struct LoadedHash {
        struct Chunk {std::string id;Kind kind;uint32_t version;std::string bytes;};
        std::vector<Chunk> chunks;
        bool migrated=false;
        void hash(Hasher& h)const {
            h.str("aiverse.native.ecs.v1");
            h.u32(uint32_t(chunks.size()));
            for(const Chunk& c:chunks) {
                h.str(c.id);h.u32(uint32_t(c.kind));h.u32(c.version);
                h.bytes(c.bytes.data(),c.bytes.size());
            }
        }
    };

    template<class T> void registerComponent(World& world,std::string id,uint32_t version=1) {
        static_assert(std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>);
        insert({std::move(id),Kind::Component,version,detail::componentTypeId<T>(),
            [](Archive& a,const World& w) {
                const size_t count=w.countExisting<T>();
                if(count>kMaxEntities){a.fail();return;}
                uint32_t n=uint32_t(count);io(a,n);
                w.eachExistingConst<T>([&](Entity entity,const T& source) {
                    Entity key=entity;T item=source;
                    io(a,key);io(a,item);
                });
            },
            [](Archive& a,World& w,const Decoder* migrate) {
                uint32_t n=0;io(a,n);
                if(!a.ok() || n>kMaxEntities){a.fail();return;}
                auto& pool=w.pool<T>();pool.clear();
                for(uint32_t i=0;i<n && a.ok();++i) {
                    Entity entity;T item{};
                    io(a,entity);
                    if(!a.ok())return;
                    if(migrate)(*migrate)(a,&item);
                    else io(a,item);
                    if(!a.ok() || !w.alive(entity) || pool.has(entity.index)){a.fail();return;}
                    pool.emplace(entity,std::move(item));
                }
            },{}
        });
        world.pool<T>(); // registered empty pools are part of RTS pool accounting
    }

    template<class T> void registerResource(std::string id,uint32_t version=1) {
        static_assert(std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>);
        insert({std::move(id),Kind::Resource,version,detail::componentTypeId<T>(),
            [](Archive& a,const World& w) {
                const T* source=w.resourceConst<T>();
                uint8_t present=source?1:0;io(a,present);
                if(source){T item=*source;io(a,item);}
            },
            [](Archive& a,World& w,const Decoder* migrate) {
                uint8_t present=0;io(a,present);
                if(!a.ok() || present>1){a.fail();return;}
                if(!present){w.eraseResource<T>();return;}
                T item{};
                if(migrate)(*migrate)(a,&item);
                else io(a,item);
                if(a.ok())w.setResource<T>(std::move(item));
            },{}
        });
    }

    // Explicit world-aware resource codec, e.g. stable references. Transfer
    // is a stateless function; native handle-based codecs remain unchanged.
    template<class T,bool (*Transfer)(Archive&,T&,const World&)>
    void registerContextualResource(std::string id,uint32_t version=1){
        static_assert(std::is_copy_constructible_v<T> && std::is_default_constructible_v<T>);
        insert({std::move(id),Kind::Resource,version,detail::componentTypeId<T>(),
            [](Archive& a,const World& w){
                const auto* source=w.resourceConst<T>();
                uint8_t present=source?1:0;a(present);
                if(source){T value=*source;if(!Transfer(a,value,w))a.fail();}
            },
            [](Archive& a,World& w,const Decoder* migrate){
                uint8_t present=0;a(present);
                if(!a.ok()||present>1||migrate){a.fail();return;}
                if(!present){w.eraseResource<T>();return;}
                T value{};
                if(!Transfer(a,value,w)){a.fail();return;}
                if(a.ok())w.setResource<T>(std::move(value));
            },{}});
    }

    // Decode an OLD value, then explicitly transform it into the current T.
    // The new schema's normal io(Archive&,T&) always handles writes.
    // Versions are exact: readers for unregistered older versions are rejected.
    template<class T,class Fn>
    void registerComponentMigration(const std::string& id,uint32_t fromVersion,Fn&& reader) {
        registerMigration<T>(id,Kind::Component,fromVersion,std::forward<Fn>(reader));
    }
    template<class T,class Fn>
    void registerResourceMigration(const std::string& id,uint32_t fromVersion,Fn&& reader) {
        registerMigration<T>(id,Kind::Resource,fromVersion,std::forward<Fn>(reader));
    }

    // Reuse native schema codecs for long-lived storage without changing
    // the existing snapshot format. Selection is by stable registered ID.
    struct SchemaInfo {std::string id;Kind kind;uint32_t version;};
    std::vector<SchemaInfo> schemas()const {
        std::vector<SchemaInfo> out;out.reserve(entries_.size());
        for(const auto& e:entries_)out.push_back({e.id,e.kind,e.version});
        return out;
    }
    bool captureSchema(const World& world,const std::string& id,LoadedHash::Chunk& out)const {
        auto it=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& e){return e.id==id;});
        if(it==entries_.end())return false;
        LoadedHash::Chunk chunk{it->id,it->kind,it->version,{}};
        Archive a(&chunk.bytes);a.formatVersion=it->version;it->write(a,world);
        if(!a.ok()||chunk.bytes.size()>kMaxPayload)return false;
        out=std::move(chunk);return true;
    }
    // Entity allocator/liveness must already be restored by the host.
    // Selected schemas replace their entire pools/resources; others stay intact.
    bool restoreSchemas(World& world,const std::vector<LoadedHash::Chunk>& chunks)const {
        if(chunks.size()>kMaxSchemas)return false;
        World staged=world;
        std::string previous;
        for(const auto& chunk:chunks){
            if(chunk.id.empty()||chunk.id<=previous||chunk.bytes.size()>kMaxPayload)return false;
            previous=chunk.id;
            auto it=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& e){return e.id==chunk.id;});
            if(it==entries_.end()||it->kind!=chunk.kind)return false;
            const Decoder* migration=nullptr;
            if(chunk.version!=it->version){
                auto m=it->migrations.find(chunk.version);
                if(!chunk.version||chunk.version>=it->version||m==it->migrations.end())return false;
                migration=&m->second;
            }
            Archive a(chunk.bytes);a.formatVersion=chunk.version;it->read(a,staged,migration);
            if(!a.ok()||a.remaining()!=0)return false;
        }
        for(const auto& [name,validate]:validators_){
            (void)name;std::string reason;if(!validate(staged,reason))return false;
        }
        world=std::move(staged);return true;
    }
    size_t componentCount()const {
        return size_t(std::count_if(entries_.begin(),entries_.end(),
            [](const Entry& e){return e.kind==Kind::Component;}));
    }
    bool empty()const{return entries_.empty();}
    void seal(){sealed_=true;}

    // Cross-schema consistency check run on the fully decoded staged World
    // before a load is committed (e.g. SharedRef slots vs SharedStore size).
    // Returning false rejects the load with "persistent ECS: <name>: <error>".
    // Registered during setup only; has no effect on save bytes or hashes.
    using LoadValidator=std::function<bool(const World&,std::string&)>;
    void addLoadValidator(std::string name,LoadValidator validator) {
        if(sealed_)throw std::logic_error("persistent ECS validators register during setup");
        if(!validator)throw std::invalid_argument("empty persistent ECS validator");
        validators_.push_back({std::move(name),std::move(validator)});
    }

    bool serialize(Archive& a,World& world,std::string* err=nullptr,
                   LoadedHash* loaded=nullptr)const {
        auto fail=[&](const std::string& reason) {
            a.fail();if(err)*err="persistent ECS: "+reason;return false;
        };
        uint32_t count=uint32_t(entries_.size());io(a,count);
        if(!a.ok() || count!=entries_.size() || count>kMaxSchemas)
            return fail("schema count differs or exceeds limit");
        World staged;
        if(!a.writing())staged=world; // do not expose partial custom chunks
        if(loaded){loaded->chunks.clear();loaded->migrated=false;}
        for(const Entry& e:entries_) {
            std::string id=e.id;
            uint8_t kind=uint8_t(e.kind);
            uint32_t version=e.version;
            io(a,id);io(a,kind);io(a,version);
            if(!a.ok() || id!=e.id || kind!=uint8_t(e.kind))
                return fail("schema ID/kind mismatch: "+e.id);
            const Decoder* migration=nullptr;
            if(!a.writing() && version!=e.version) {
                const auto it=e.migrations.find(version);
                if(version==0 || version>=e.version || it==e.migrations.end())
                    return fail("unsupported schema version: "+e.id);
                migration=&it->second;
                if(loaded)loaded->migrated=true;
            }
            std::string chunk;
            if(a.writing()){
                Archive sub(&chunk);sub.formatVersion=e.version;
                e.write(sub,world);
                if(!sub.ok() || chunk.size()>kMaxPayload)
                    return fail("invalid/oversized payload: "+e.id);
            }
            uint32_t len=uint32_t(chunk.size());io(a,len);
            if(!a.ok() || len>kMaxPayload || (!a.writing() && len>a.remaining()))
                return fail("invalid payload size: "+e.id);
            if(!a.writing())chunk.resize(len);
            a.block(chunk.data(),len);
            if(!a.ok())return fail("truncated payload: "+e.id);
            if(!a.writing()){
                Archive sub(chunk);sub.formatVersion=version;
                e.read(sub,staged,migration);
                if(!sub.ok() || sub.remaining()!=0)return fail("invalid payload: "+e.id);
                if(loaded)loaded->chunks.push_back({e.id,e.kind,version,std::move(chunk)});
            }
        }
        if(!a.writing()) {
            for(const auto& [name,validate]:validators_) {
                std::string reason;
                if(!validate(staged,reason))
                    return fail(name+": "+(reason.empty()?"invalid state":reason));
            }
            world=std::move(staged);
        }
        return a.ok();
    }

    void hash(const World& world,Hasher& h)const {
        if(entries_.empty())return; // exact legacy checksum compatibility
        h.str("aiverse.native.ecs.v1");
        h.u32(uint32_t(entries_.size()));
        for(const Entry& e:entries_) {
            h.str(e.id);h.u32(uint32_t(e.kind));h.u32(e.version);
            Archive a(h);a.formatVersion=e.version;
            e.write(a,world);
            if(!a.ok())throw std::runtime_error("persistent ECS hash failed: "+e.id);
        }
    }
private:
    using Decoder=std::function<void(Archive&,void*)>;
    struct Entry {
        std::string id;Kind kind;uint32_t version,typeId;
        void (*write)(Archive&,const World&);
        void (*read)(Archive&,World&,const Decoder*);
        std::map<uint32_t,Decoder> migrations;
    };

    template<class T,class Fn>
    void registerMigration(const std::string& id,Kind kind,uint32_t from,Fn&& fn) {
        if(sealed_)throw std::logic_error("persistent ECS migration must register during setup");
        auto it=std::find_if(entries_.begin(),entries_.end(),
            [&](const Entry& e){return e.id==id;});
        if(it==entries_.end() || it->kind!=kind || it->typeId!=detail::componentTypeId<T>())
            throw std::invalid_argument("native ECS migration type/ID not registered: "+id);
        if(from==0 || from>=it->version)
            throw std::invalid_argument("native ECS migration must target a strictly older version");
        Decoder decoder=[fn=std::forward<Fn>(fn)](Archive& a,void* out) mutable {
            fn(a,*static_cast<T*>(out));
        };
        if(!it->migrations.emplace(from,std::move(decoder)).second)
            throw std::invalid_argument("duplicate native ECS migration version: "+id);
    }
    void insert(Entry e){
        if(sealed_)throw std::logic_error("persistent ECS registration only during setup");
        if(e.id.empty() || e.id.size()>128 || !e.version || entries_.size()>=kMaxSchemas)
            throw std::invalid_argument("invalid persistent ECS ID/version/count");
        for(char c:e.id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'||c==':'))
            throw std::invalid_argument("persistent ECS ID must be stable ASCII");
        for(const Entry& old:entries_){
            if(old.id==e.id)throw std::invalid_argument("duplicate persistent ECS ID: "+e.id);
            if(old.typeId==e.typeId && old.kind==e.kind)
                throw std::invalid_argument("persistent ECS C++ type registered more than once");
        }
        auto at=std::lower_bound(entries_.begin(),entries_.end(),e.id,
            [](const Entry& lhs,const std::string& rhs){return lhs.id<rhs;});
        entries_.insert(at,std::move(e));
    }
    std::vector<Entry> entries_;
    std::vector<std::pair<std::string,LoadValidator>> validators_;
    bool sealed_=false;
};
} // namespace aiverse
