#ifndef __EMSCRIPTEN__
#include "SimNet.h"
#include <algorithm>
#include <queue>

namespace aiverse::net {

namespace {
struct Packet {
    uint64_t at, order;
    Address from;
    std::vector<uint8_t> data;
    bool operator<(const Packet& o) const { return at != o.at ? at > o.at : order > o.order; } // min-heap
};
} // namespace

struct SimNetwork::Endpoint {
    Address addr;
    std::priority_queue<Packet> queue;
    LinkProfile profile;
    uint32_t burstLeft = 0;
    bool suspended = false, closed = false;
};

class SimPort final : public DatagramPort {
public:
    SimPort(SimNetwork& net, std::shared_ptr<SimNetwork::Endpoint> e) : net_(net), e_(std::move(e)) {}
    ~SimPort() override { e_->closed = true; }
    bool sendTo(const Address& to, const uint8_t* data, size_t n) override { net_.send(*e_, to, data, n); return true; }
    int recvFrom(Address& from, uint8_t* buf, size_t cap) override { return net_.recv(*e_, from, buf, cap); }
    uint16_t localPort() const override { return port_; }
    uint16_t port_ = 0;
private:
    SimNetwork& net_;
    std::shared_ptr<SimNetwork::Endpoint> e_;
};

LinkProfile linkPreset(const std::string& name, bool* found) {
    LinkProfile p;
    bool ok = true;
    if (name == "lan") { p.latencyMs = 1; }
    else if (name == "wifi") { p.latencyMs = 20; p.jitterMs = 10; p.lossPercent = 1; }
    else if (name == "4g") { p.latencyMs = 40; p.jitterMs = 30; p.lossPercent = 3; p.burstPercent = 1; p.burstLength = 4; }
    else if (name == "3g") { p.latencyMs = 75; p.jitterMs = 60; p.lossPercent = 10; p.burstPercent = 1; p.burstLength = 6; }
    else if (name == "bad") { p.latencyMs = 75; p.jitterMs = 40; p.lossPercent = 20; p.burstPercent = 1; p.burstLength = 5; p.duplicatePercent = 1; }
    else ok = false;
    if (found) *found = ok;
    return p;
}

Address SimNetwork::address(const std::string& ip, uint16_t port) {
    Address a;
    Address::resolve(ip, port, a, true);
    return a;
}

std::unique_ptr<DatagramPort> SimNetwork::open(const std::string& ip, uint16_t port, LinkProfile profile) {
    auto e = std::make_shared<Endpoint>();
    e->addr = address(ip, port);
    e->profile = profile;
    endpoints_.push_back(e);
    auto p = std::make_unique<SimPort>(*this, e);
    p->port_ = port;
    return p;
}

SimNetwork::Endpoint* SimNetwork::find(const Address& a) {
    for (auto& e : endpoints_) if (!e->closed && e->addr == a) return e.get();
    return nullptr;
}

void SimNetwork::setProfile(const Address& endpoint, LinkProfile p) { if (auto* e = find(endpoint)) e->profile = p; }
void SimNetwork::rebind(const Address& endpoint, const std::string& ip, uint16_t port) {
    if (auto* e = find(endpoint)) { e->addr = address(ip, port); e->queue = {}; }
}
void SimNetwork::setSuspended(const Address& endpoint, bool s) { if (auto* e = find(endpoint)) e->suspended = s; }

bool SimNetwork::lose(Endpoint& e) {
    if (e.suspended) return true;
    if (e.burstLeft) { --e.burstLeft; return true; }
    if (e.profile.burstPercent && rng_.below(1000) < e.profile.burstPercent * 10) { e.burstLeft = e.profile.burstLength; return true; }
    return e.profile.lossPercent && rng_.below(100) < e.profile.lossPercent;
}

void SimNetwork::send(Endpoint& from, const Address& to, const uint8_t* data, size_t n) {
    ++stats_.sent;
    stats_.bytes += n;
    Endpoint* dst = find(to);
    if (lose(from) || !dst || lose(*dst)) { ++stats_.dropped; return; }
    auto delay = [&](const LinkProfile& p) { return uint64_t(p.latencyMs) + (p.jitterMs ? rng_.below(p.jitterMs + 1) : 0); };
    const uint64_t at = now_ + delay(from.profile) + delay(dst->profile);
    std::vector<uint8_t> bytes(data, data + n);
    const uint32_t dup = from.profile.duplicatePercent + dst->profile.duplicatePercent;
    if (dup && rng_.below(100) < dup) { ++stats_.duplicated; dst->queue.push({at + 1 + rng_.below(20), order_++, from.addr, bytes}); }
    dst->queue.push({at, order_++, from.addr, std::move(bytes)});
}

int SimNetwork::recv(Endpoint& at, Address& from, uint8_t* buf, size_t cap) {
    while (!at.queue.empty() && at.queue.top().at <= now_) {
        Packet p = at.queue.top();
        at.queue.pop();
        if (at.suspended) { ++stats_.dropped; continue; }
        ++stats_.delivered;
        from = p.from;
        const size_t n = std::min(cap, p.data.size());
        std::copy(p.data.begin(), p.data.begin() + long(n), buf);
        return int(n);
    }
    return -1;
}

} // namespace aiverse::net
#endif
