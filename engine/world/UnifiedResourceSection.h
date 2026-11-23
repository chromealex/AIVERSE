#pragma once
#include "UnifiedCheckpointSections.h"
#include "engine/sim/PersistentEcs.h"
namespace aiverse::world {
class UnifiedResourceSection {
public:
    using State=UnifiedWorldReplay::State;
    using Registry=PersistentEcsRegistry;
    using Chunk=Registry::LoadedHash::Chunk;
    static UnifiedCheckpointSections::Adapter adapter(const Registry& registry,
        std::vector<std::string> selected,size_t budget){
        std::sort(selected.begin(),selected.end());
        return {1,[&registry,selected,budget](const State& state,std::vector<uint8_t>& out){
            if(selected.size()>Registry::kMaxSchemas||
               std::adjacent_find(selected.begin(),selected.end())!=selected.end())return false;
            std::string bytes;Archive a(&bytes);
            uint32_t count=uint32_t(selected.size());a(count);
            for(const auto& id:selected){
                Chunk c;
                if(!registry.captureSchema(state.world,id,c)||c.kind!=Registry::Kind::Resource)return false;
                if(bytes.size()>budget||c.bytes.size()>budget-bytes.size())return false;
                uint32_t len=uint32_t(c.bytes.size());
                a(c.id,c.version,len);a.block(c.bytes.data(),len);
                if(!a.ok()||bytes.size()>budget)return false;
            }
            if(!a.ok()||bytes.size()>budget)return false;
            std::vector<uint8_t> staged(bytes.begin(),bytes.end());out.swap(staged);return true;
        },[&registry,selected,budget](State& state,uint32_t version,std::span<const uint8_t> input){
            if(version!=1||input.size()>budget||input.size()<4)return false;
            std::string bytes(reinterpret_cast<const char*>(input.data()),input.size());
            Archive a(bytes);uint32_t count=0;a(count);
            if(!a.ok()||count!=selected.size()||count>Registry::kMaxSchemas)return false;
            std::vector<Chunk> chunks;
            for(uint32_t i=0;i<count;++i){
                Chunk c{};c.kind=Registry::Kind::Resource;uint32_t len=0;
                a(c.id,c.version,len);
                if(!a.ok()||c.id!=selected[i]||len>Registry::kMaxPayload||len>a.remaining())return false;
                c.bytes.resize(len);a.block(c.bytes.data(),len);
                if(!a.ok())return false;
                chunks.push_back(std::move(c));
            }
            return a.remaining()==0&&registry.restoreSchemas(state.world,chunks);
        }};
    }
};
}
