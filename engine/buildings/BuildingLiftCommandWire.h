#pragma once
// Bounded command codec for the EXISTING authenticated game transport.
// Client -> server request ONLY. This codec does not authenticate a peer,
// open sockets, assign ECS identity or apply state. Server must pass the
// authenticated connection identity to BuildingLiftAuthority::execute.
// Protocol deliberately separate from server->client lift state snapshots.
#include "engine/buildings/BuildingLiftCommands.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aiverse::building {
namespace buildingLiftCommandWire {
constexpr uint32_t kMagic=0x444D434Cu; // LCMD little-endian
constexpr uint16_t kVersion=1;
constexpr size_t kBytes=4+2+2+8+8+8+4+4+1+3;
inline void u16(std::vector<uint8_t>& out,uint16_t v){
    for(int i=0;i<2;++i)out.push_back(uint8_t(v>>(8*i)));
}
inline void u32(std::vector<uint8_t>& out,uint32_t v){
    for(int i=0;i<4;++i)out.push_back(uint8_t(v>>(8*i)));
}
inline void u64(std::vector<uint8_t>& out,uint64_t v){
    for(int i=0;i<8;++i)out.push_back(uint8_t(v>>(8*i)));
}
struct Reader {
    std::span<const uint8_t> bytes;
    size_t at=0;
    uint64_t read(unsigned count) {
        uint64_t value=0;
        for(unsigned i=0;i<count;++i)value|=uint64_t(bytes[at++])<<(i*8);
        return value;
    }
};
} // namespace buildingLiftCommandWire

inline bool validBuildingLiftCommand(const BuildingLiftCommand& command){
    return command.sceneId!=0&&command.actorId!=0&&command.sequence!=0&&
        command.kind<=BuildingLiftCommandKind::Exit&&command.floor<8;
}
inline bool encodeBuildingLiftCommand(const BuildingLiftCommand& command,
                                      std::vector<uint8_t>& out,
                                      std::string* error=nullptr){
    if(!validBuildingLiftCommand(command)){
        if(error)*error="Invalid building lift client command";
        return false;
    }
    using namespace buildingLiftCommandWire;
    std::vector<uint8_t> data;
    data.reserve(kBytes);
    u32(data,kMagic);u16(data,kVersion);u16(data,0);
    u64(data,command.sceneId);u64(data,command.actorId);
    u64(data,command.sequence);u32(data,command.liftFeature);
    u32(data,command.floor);
    data.push_back(uint8_t(command.kind));
    data.push_back(0);data.push_back(0);data.push_back(0);
    out=std::move(data);
    if(error)error->clear();
    return true;
}
inline bool decodeBuildingLiftCommand(std::span<const uint8_t> data,
                                      BuildingLiftCommand& out,
                                      std::string* error=nullptr){
    const auto fail=[&](const char* message){
        if(error)*error=message;
        return false;
    };
    using namespace buildingLiftCommandWire;
    if(data.size()!=kBytes)return fail("Building lift command packet has invalid length");
    Reader reader{data};
    const auto magic=uint32_t(reader.read(4));
    const auto version=uint16_t(reader.read(2));
    const auto reserved=uint16_t(reader.read(2));
    if(magic!=kMagic||version!=kVersion||reserved!=0)
        return fail("Unsupported building lift command header");
    BuildingLiftCommand candidate;
    candidate.sceneId=reader.read(8);
    candidate.actorId=reader.read(8);
    candidate.sequence=reader.read(8);
    candidate.liftFeature=uint32_t(reader.read(4));
    candidate.floor=uint32_t(reader.read(4));
    candidate.kind=BuildingLiftCommandKind(reader.read(1));
    for(int i=0;i<3;++i)if(reader.read(1)!=0)
        return fail("Building lift command contains nonzero reserved bytes");
    if(!validBuildingLiftCommand(candidate))
        return fail("Building lift command has invalid fields");
    out=candidate;
    if(error)error->clear();
    return true;
}
} // namespace aiverse::building
