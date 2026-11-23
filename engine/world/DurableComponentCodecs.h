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
    using Context=PersistentEntityReferences;
    static constexpr size_t MaxBytes=16u*1024u*1024u;
    using Writer=std::function<bool(Archive&,const World&,Entity,const Context&)>;
    using Reader=std::function<bool(Archive&,World&,Entity,const Context&)>;
private:
    struct Codec {uint32_t version;Writer write;std::map<uint32_t,Reader> readers;};
    std::map<std::string,Codec> codecs_;
public:
    bool add(std::string id,uint32_t version,Writer writer,Reader reader){
        if(id.empty()||id.size()>128||!version||!writer||!reader)return false;
        Codec codec{version,std::move(writer),{}};
        codec.readers.emplace(version,std::move(reader));
        return codecs_.emplace(std::move(id),std::move(codec)).second;
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
};
}
