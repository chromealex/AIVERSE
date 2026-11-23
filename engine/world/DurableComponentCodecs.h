#pragma once
#include "PersistentDeferredReference.h"
#include <functional>
#include <map>
#include <string>
namespace aiverse::world {
// Durable adapters intentionally separate from rollback byte representation.
// Stable schema IDs must match the owning native ECS registration.
class DurableComponentCodecs {
public:
    struct Chunk {std::string schema;uint32_t version=0;RegionEntityId owner=0;std::string bytes;};
    enum class Operation : uint8_t { Upsert=1, Remove=2 };
    struct Record {Operation operation=Operation::Upsert;Chunk chunk;};
    using Context=PersistentEntityReferences;
    static constexpr size_t MaxBytes=16u*1024u*1024u;
    using Writer=std::function<bool(Archive&,const World&,Entity,const Context&)>;
    using Reader=std::function<bool(Archive&,World&,Entity,const Context&)>;
private:
    using Remover=std::function<bool(World&,Entity)>;
    struct Codec {uint32_t version;Writer write;std::map<uint32_t,Reader> readers;Remover remove;std::function<bool(const World&,Entity)> present;};
    std::map<std::string,Codec> codecs_;
    std::vector<std::function<bool(World&,const RegionEntityLedger&)>> rebuilders_;
    std::vector<std::function<bool(const World&,const RegionEntityLedger&)>> captureValidators_;
public:
    bool add(std::string id,uint32_t version,Writer writer,Reader reader){
        if(id.empty()||id.size()>128||!version||!writer||!reader)return false;
        Codec codec{version,std::move(writer),{}, {}, {}};
        codec.readers.emplace(version,std::move(reader));
        return codecs_.emplace(std::move(id),std::move(codec)).second;
    }
    // Opt-in only: T's Archive codec must not contain raw runtime Entity
    // handles. Use PersistentDeferredReference or an explicit contextual codec.
    template<class T> bool addTyped(std::string id,uint32_t version=1){
        return addContextual<T>(std::move(id),version,
            [](Archive& a,T& value,const World&,const Context&){io(a,value);return a.ok();});
    }
    // The same field codec runs in both directions. Reference-bearing types
    // must translate handles through the supplied stable identity context.
    template<class T,class Fn> bool addContextual(std::string id,uint32_t version,Fn transfer){
        const auto key=id;
        if(!add(std::move(id),version,
            [transfer](Archive& a,const World& world,Entity entity,const Context& refs){
                const T* source=world.tryGetConst<T>(entity);
                if(!source)return false;
                T value=*source;return transfer(a,value,world,refs)&&a.ok();
            },
            [transfer](Archive& a,World& world,Entity entity,const Context& refs){
                T value{};
                if(!transfer(a,value,world,refs)||!a.ok()||a.remaining()!=0)return false;
                if(world.has<T>(entity))world.remove<T>(entity);
                world.add<T>(entity,std::move(value));return true;
            }))return false;
        codecs_.at(key).present=[](const World& w,Entity e){return w.has<T>(e);};
        codecs_.at(key).remove=[](World& w,Entity e){if(w.has<T>(e))w.remove<T>(e);return true;};
        return true;
    }
    // Rebuild host-derived indices only after the entire component stream,
    // including all referenced entities, has been restored privately.
    bool addCaptureValidator(std::function<bool(const World&,const RegionEntityLedger&)> fn){
        if(!fn)return false;
        captureValidators_.push_back(std::move(fn));return true;
    }
    bool addRebuilder(std::function<bool(World&,const RegionEntityLedger&)> fn){
        if(!fn)return false;
        rebuilders_.push_back(std::move(fn));return true;
    }
    bool rebuildDerived(World& world,const RegionEntityLedger& ledger)const{
        for(const auto& fn:rebuilders_)if(!fn(world,ledger))return false;
        return true;
    }
    template<class T,class Fn> bool addTypedMigration(const std::string& id,uint32_t version,Fn reader){
        return addMigration(id,version,[reader=std::move(reader)](Archive& a,World& world,Entity entity,const Context& refs){
            T value{};
            if(!reader(a,value,refs)||!a.ok()||a.remaining()!=0)return false;
            if(world.has<T>(entity))world.remove<T>(entity);
            world.add<T>(entity,std::move(value));return true;
        });
    }
    bool addMigration(const std::string& id,uint32_t version,Reader reader){
        auto it=codecs_.find(id);
        if(it==codecs_.end()||!version||version>=it->second.version||!reader)return false;
        return it->second.readers.emplace(version,std::move(reader)).second;
    }
    bool capture(const World& world,const Context& refs,RegionEntityId owner,
        const std::string& schema,Chunk& out)const{
        auto it=codecs_.find(schema);Entity entity;
        if(!owner||it==codecs_.end()||!refs.resolve(world,owner,entity))return false;
        Chunk staged{schema,it->second.version,owner,{}};
        Archive a(&staged.bytes);a.formatVersion=staged.version;
        if(!it->second.write(a,world,entity,refs)||!a.ok()||staged.bytes.size()>MaxBytes)return false;
        out=std::move(staged);return true;
    }
    // Caller supplies an offline staged World and matching reference index.
    // Reader may mutate staged state on failure; caller must discard it.
    bool restoreStaged(World& staged,const Context& refs,const Chunk& chunk)const{
        auto it=codecs_.find(chunk.schema);Entity entity;
        if(!chunk.owner||chunk.bytes.size()>MaxBytes||it==codecs_.end()||
           !refs.resolve(staged,chunk.owner,entity))return false;
        auto reader=it->second.readers.find(chunk.version);
        if(reader==it->second.readers.end())return false;
        Archive a(chunk.bytes);a.formatVersion=chunk.version;
        return reader->second(a,staged,entity,refs)&&a.ok()&&a.remaining()==0;
    }
    bool setPresence(const std::string& schema,std::function<bool(const World&,Entity)> present){
        auto it=codecs_.find(schema);if(it==codecs_.end()||!present)return false;
        it->second.present=std::move(present);return true;
    }
    bool captureAll(const World& world,const RegionEntityLedger& ledger,
        size_t budget,std::vector<Record>& out)const{
        for(const auto& validate:captureValidators_)if(!validate(world,ledger))return false;
        Context refs;if(!refs.rebuild(world,ledger))return false;
        for(const auto& [id,codec]:codecs_)if(!codec.present)return false;
        std::vector<Record> staged;size_t used=0;
        for(auto owner:ledger.liveIds()){
            Entity e;if(!refs.resolve(world,owner,e))continue;
            for(const auto& [id,codec]:codecs_){
                if(!codec.present(world,e))continue;
                Chunk c;if(!capture(world,refs,owner,id,c))return false;
                size_t cost=19+c.schema.size()+c.bytes.size();
                if(cost>budget-used)return false;
                used+=cost;staged.push_back({Operation::Upsert,std::move(c)});
            }
        }
        out.swap(staged);return true;
    }
    bool setRemover(const std::string& schema,std::function<bool(World&,Entity)> remove){
        auto it=codecs_.find(schema);
        if(it==codecs_.end()||!remove)return false;
        it->second.remove=std::move(remove);return true;
    }
    // Ordered transaction, including repeated keys. Host serializes access.
    // Codecs must not mutate identity, allocator or external state.
    bool restoreBatch(World& world,const RegionEntityLedger& ledger,
        const std::vector<Record>& records)const{
        constexpr size_t maxRecords=1000000,maxTotal=128u*1024u*1024u;
        if(records.size()>maxRecords)return false;
        size_t total=0;
        for(const auto& r:records){
            if(r.chunk.bytes.size()>MaxBytes||r.chunk.bytes.size()>maxTotal-total)return false;
            total+=r.chunk.bytes.size();
        }
        World staged=world;
        Context refs;if(!refs.rebuild(staged,ledger))return false;
        for(const auto& r:records){
            if(r.operation==Operation::Upsert){
                if(!restoreStaged(staged,refs,r.chunk))return false;
            }else if(r.operation==Operation::Remove){
                const auto& c=r.chunk;Entity e;
                auto it=codecs_.find(c.schema);
                if(!c.owner||!c.bytes.empty()||it==codecs_.end()||
                   c.version!=it->second.version||!it->second.remove||
                   !refs.resolve(staged,c.owner,e)||!it->second.remove(staged,e))return false;
            }else return false;
        }
        if(!rebuildDerived(staged,ledger))return false;
        Context checked;if(!checked.rebuild(staged,ledger))return false;
        world=std::move(staged);return true;
    }

};
}
