#pragma once
#ifndef __EMSCRIPTEN__
#include "Socket.h"
#include <map>

namespace aiverse::net {
class LanDiscovery {
public:
    static constexpr uint16_t port = 27017;
    struct Host { std::string label; uint64_t lastSeen = 0; };
    bool browse(const std::string& game, const std::string& broadcast = "255.255.255.255");
    bool advertise(const std::string& game, const std::string& label, uint16_t relayPort);
    void update(uint64_t nowMs);
    void close();
    const std::map<std::string, Host>& hosts() const { return hosts_; }
private:
    UdpSocket socket_;
    Address broadcast_;
    std::map<std::string, Host> hosts_;
    uint64_t gameId_ = 0, nextQuery_ = 0, query_ = 0;
    std::string label_;
    uint16_t relayPort_ = 0;
};
} // namespace aiverse::net
#endif
