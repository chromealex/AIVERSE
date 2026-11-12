// Datagram path under the secure battle transport: a real UDP socket, or the simulated mobile
// network (SimNet.h) so loss / latency / jitter / NAT rebinding / app suspend run in tests on a
// virtual clock. Native builds only.
#pragma once
#ifndef __EMSCRIPTEN__
#include <cstddef>
#include <cstdint>
#include <memory>
#include "Socket.h"

namespace aiverse::net {

class DatagramPort {
public:
    virtual ~DatagramPort() = default;
    virtual bool sendTo(const Address& to, const uint8_t* data, size_t n) = 0;
    virtual int recvFrom(Address& from, uint8_t* buf, size_t cap) = 0;   // -1 = nothing pending
    virtual uint16_t localPort() const = 0;
};

// Real UDP. port 0 = ephemeral; family 0 = dual stack. nullptr when the socket cannot be opened.
std::unique_ptr<DatagramPort> openUdpPort(uint16_t port = 0, int family = 0, bool loopbackOnly = false);

} // namespace aiverse::net
#endif
