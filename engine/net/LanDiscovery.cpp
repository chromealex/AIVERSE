#include "LanDiscovery.h"
#ifndef __EMSCRIPTEN__
#include "Protocol.h"
#include "../core/Hash.h"
#include <algorithm>

namespace aiverse::net {
namespace {
constexpr uint32_t magic = 0x4e4c5641;
constexpr size_t querySize = 96;
uint64_t gameKey(const std::string& game) { Hasher hash; hash.str(game); return hash.value(); }
ByteWriter header(uint8_t kind, uint64_t game, uint64_t query) {
    ByteWriter packet;
    packet.u32(magic); packet.u32(kProtocolVersion); packet.u8(kind); packet.u64(game); packet.u64(query);
    return packet;
}
}

void LanDiscovery::close() {
    socket_.close(); hosts_.clear(); relayPort_ = 0; nextQuery_ = 0; query_ = 0;
}

bool LanDiscovery::browse(const std::string& game, const std::string& broadcast) {
    close();
    gameId_ = gameKey(game);
    return Address::resolve(broadcast,port,broadcast_,true) && socket_.openBroadcast(0);
}

bool LanDiscovery::advertise(const std::string& game, const std::string& label, uint16_t relayPort) {
    close();
    if (!relayPort || !socket_.openBroadcast(port)) return false;
    gameId_ = gameKey(game); label_ = label.substr(0,60); relayPort_ = relayPort;
    return true;
}

void LanDiscovery::update(uint64_t now) {
    if (!socket_.isOpen()) return;
    if (!relayPort_ && now >= nextQuery_) {
        ++query_;
        auto packet = header(1,gameId_,query_);
        packet.buf.resize(querySize);
        socket_.sendBroadcast(broadcast_,packet.buf.data(),packet.buf.size());
        nextQuery_ = now + 1500;
    }
    for (int i=0; i<32; ++i) {
        uint8_t bytes[querySize+1];
        Address sender;
        const auto n = socket_.recvFrom(sender,bytes,sizeof(bytes));
        if (n < 0) break;
        if (size_t(n) > querySize) continue;
        ByteReader reader(bytes,size_t(n));
        if (reader.u32()!=magic || reader.u32()!=kProtocolVersion) continue;
        const auto kind = reader.u8();
        if (reader.u64()!=gameId_) continue;
        const auto token = reader.u64();
        if (!reader.ok()) continue;
        if (relayPort_ && kind==1 && size_t(n)==querySize) {
            auto reply = header(2,gameId_,token);
            reply.u16(relayPort_); reply.str(label_);
            socket_.sendTo(sender,reply.buf.data(),reply.buf.size());
        } else if (!relayPort_ && kind==2 && token==query_) {
            const auto portNumber = reader.u16();
            const auto label = reader.str();
            if (!reader.ok() || !reader.atEnd() || !portNumber || label.size()>60) continue;
            const auto source = sender.str();
            const auto split = source.rfind(':');
            if (split==std::string::npos) continue;
            const auto address = source.substr(0,split+1)+std::to_string(portNumber);
            if (hosts_.size()<128 || hosts_.count(address)) hosts_[address] = {label,now};
        }
    }
    for (auto it=hosts_.begin(); it!=hosts_.end();) {
        if (now-it->second.lastSeen>5000) it=hosts_.erase(it);
        else ++it;
    }
}
} // namespace aiverse::net
#endif
