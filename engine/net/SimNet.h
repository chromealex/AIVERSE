// Simulated network for transport tests and load runs: mobile-style loss (independent + bursts),
// one-way latency, jitter (which also reorders), duplicates, NAT rebinding and "app suspended"
// blackouts, all on a virtual clock and a seeded RNG so every run is reproducible.
#pragma once
#ifndef __EMSCRIPTEN__
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Datagram.h"
#include "../core/Random.h"

namespace aiverse::net {

// Applies to every datagram leaving or entering an endpoint (the phone's radio link). A packet
// between two endpoints goes through both profiles; give the server a clean profile.
struct LinkProfile {
    uint32_t lossPercent = 0;       // independent loss per datagram
    uint32_t burstPercent = 0;      // chance per datagram to start a loss burst (radio fade, handover)
    uint32_t burstLength = 0;       // datagrams lost in a row once a burst starts
    uint32_t latencyMs = 0;         // one way (RTT between a phone and a clean server = 2 x latency)
    uint32_t jitterMs = 0;          // extra uniform 0..jitter delay per datagram (reorders packets)
    uint32_t duplicatePercent = 0;
};

// Named presets for tests, docs and the MCP op: "lan", "wifi", "4g", "3g", "bad" (20% loss, 150 ms RTT).
LinkProfile linkPreset(const std::string& name, bool* found = nullptr);

struct SimNetStats { uint64_t sent = 0, dropped = 0, delivered = 0, duplicated = 0, bytes = 0; };

class SimNetwork {
public:
    explicit SimNetwork(uint64_t seed = 1) { rng_.reseed(seed); }
    // ip is a dotted IPv4 literal ("10.0.0.2"); the port is the endpoint's address.
    std::unique_ptr<DatagramPort> open(const std::string& ip, uint16_t port, LinkProfile profile = {});
    void setTime(uint64_t nowMs) { now_ = nowMs; }
    uint64_t time() const { return now_; }
    void setProfile(const Address& endpoint, LinkProfile p);
    // NAT rebinding / Wi-Fi -> LTE handover: the endpoint now appears from a new address (in-flight
    // packets to the old one are lost).
    void rebind(const Address& endpoint, const std::string& ip, uint16_t port);
    // App in the background: everything to and from the endpoint is dropped.
    void setSuspended(const Address& endpoint, bool suspended);
    const SimNetStats& stats() const { return stats_; }
    static Address address(const std::string& ip, uint16_t port);

    struct Endpoint;
private:
    friend class SimPort;
    bool lose(Endpoint& e);
    void send(Endpoint& from, const Address& to, const uint8_t* data, size_t n);
    int recv(Endpoint& at, Address& from, uint8_t* buf, size_t cap);
    Endpoint* find(const Address& a);

    std::vector<std::shared_ptr<Endpoint>> endpoints_;
    Random rng_;
    uint64_t now_ = 0, order_ = 0;
    SimNetStats stats_;
};

} // namespace aiverse::net
#endif
