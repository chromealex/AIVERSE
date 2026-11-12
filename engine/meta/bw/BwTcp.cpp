#ifndef __EMSCRIPTEN__
#include "BwTcp.h"

#include <cstdlib>

namespace aiverse::meta::bw {

std::vector<HostPort> parseHostPorts(const std::vector<std::string>& hosts) {
    std::vector<HostPort> out;
    for (auto h : hosts) {
        const size_t scheme = h.find("://");
        if (scheme != std::string::npos) h = h.substr(scheme + 3);
        const size_t c = h.rfind(':');
        if (c == std::string::npos || c + 1 >= h.size()) continue;
        const int port = std::atoi(h.c_str() + c + 1);
        if (port > 0 && port < 65536) out.push_back({h.substr(0, c), port});
    }
    return out;
}

// ---------------------------------------------------------------- TcpLink

bool TcpLink::connect(const HostPort& to, uint32_t timeoutMs, uint64_t now) {
    close();
    error_.clear();
    in_.clear();
    out_.clear();
    net::Address addr;
    if (!net::Address::resolve(to.host, uint16_t(to.port), addr, true)) { error_ = "cannot resolve " + to.host; state_ = State::Closed; return false; }
    s_ = std::make_unique<net::TcpSocket>();
    if (!s_->connect(addr)) { error_ = "cannot connect to " + to.host; s_.reset(); state_ = State::Closed; return false; }
    state_ = State::Connecting;
    deadline_ = now + timeoutMs;
    return true;
}

void TcpLink::close() {
    if (s_) s_->close();
    s_.reset();
    if (state_ != State::Idle) state_ = State::Closed;
}

void TcpLink::update(uint64_t now) {
    if (!s_) return;
    if (state_ == State::Connecting) {
        const int c = s_->pollConnected();
        if (c < 0) { error_ = "connection refused"; close(); return; }
        if (c == 0) {
            if (now >= deadline_) { error_ = "connect timeout"; close(); }
            return;
        }
        state_ = State::Connected;
    }
    while (!out_.empty()) {
        const int n = s_->send(out_.data(), out_.size());
        if (n < 0) { error_ = "send failed"; close(); return; }
        if (n == 0) break;
        out_.erase(out_.begin(), out_.begin() + n);
    }
    uint8_t buf[16384];
    for (;;) {
        const int n = s_->recv(buf, sizeof buf);
        if (n < 0) { error_ = "connection closed"; close(); return; }
        if (n == 0) break;
        in_.insert(in_.end(), buf, buf + n);
    }
}

// ---------------------------------------------------------------- HostProbe

HostProbe::HostProbe(Kind kind, std::vector<HostPort> hosts, std::string token, uint64_t now, uint32_t connectTimeoutMs,
                     uint32_t answerTimeoutMs)
    : kind_(kind), hosts_(std::move(hosts)), token_(std::move(token)), tries_(hosts_.size()), answerMs_(answerTimeoutMs) {
    for (size_t i = 0; i < hosts_.size(); ++i)
        if (!tries_[i].link.connect(hosts_[i], connectTimeoutMs, now)) tries_[i].dead = true;
    done_ = hosts_.empty();
}

void HostProbe::update(uint64_t now) {
    if (done_) return;
    bool alive = false;
    for (size_t i = 0; i < tries_.size() && !done_; ++i) {
        Try& t = tries_[i];
        if (t.dead) continue;
        t.link.update(now);
        if (t.link.state() == TcpLink::State::Closed) { t.dead = true; continue; }
        alive = true;
        if (t.link.state() != TcpLink::State::Connected) continue;
        if (!t.sent) {
            t.sent = true;
            t.connectedAt = now;
            if (kind_ == Kind::Game) {
                if (!token_.empty()) t.link.send(encodeFrame({"Ping", 0, token_}));   // TcpConnector("…", "Ping").MsgConnect()
            } else {
                t.link.send(encodeChatPacket(2, std::string(1, '\0')));               // NetClient.SendMsg(2, {0})
            }
            t.link.update(now);
        }
        Bytes in = t.link.takeReceived();
        bool answered = false;
        if (kind_ == Kind::Game) {
            t.frames.feed(in);
            Frame f;
            while (t.frames.next(f)) {
                if (f.func == "onPing") { answered = true; break; }
                t.dead = true;   // any other answer fails this host (TcpConnector check)
                break;
            }
        } else {
            t.chat.feed(in);
            ChatPacket p;
            answered = t.chat.next(p);
        }
        if (answered) { winner_ = int(i); done_ = true; break; }
        if (now - t.connectedAt > answerMs_) t.dead = true;
        if (t.dead) t.link.close();
    }
    if (!alive && !done_) done_ = true;
    if (done_) for (auto& t : tries_) t.link.close();
}

// ---------------------------------------------------------------- MatchConnection

void MatchConnection::open(uint64_t now) {
    if (cfg_.hosts.empty()) return;
    if (!link_.connect(cfg_.hosts[size_t(host_) % cfg_.hosts.size()], cfg_.connectTimeoutMs, now)) retryAt_ = now + cfg_.reconnectDelayMs;
}

void MatchConnection::start(uint64_t now) {
    started_ = true;
    stopped_ = false;
    host_ = 0;
    open(now);
}

void MatchConnection::stop() {
    stopped_ = true;
    link_.close();
}

void MatchConnection::update(uint64_t now) {
    if (!started_ || stopped_) return;
    const auto before = link_.state();
    link_.update(now);
    const auto st = link_.state();
    if (st == TcpLink::State::Connected && before == TcpLink::State::Connecting) {
        session_.connect(cfg_.token);                         // MsgConnect right after the connect
        if (everConnected_ && roomId_) session_.reconnect(roomId_, lastTick_);
        everConnected_ = wasConnected_ = true;
        lastPing_ = now;
    }
    if (st == TcpLink::State::Connected) {
        session_.feed(link_.takeReceived());
        if (cfg_.pingIntervalMs && now - lastPing_ >= cfg_.pingIntervalMs) { lastPing_ = now; session_.ping(int64_t(now), tick_); }
        Bytes out = session_.takeOutgoing();
        if (!out.empty()) { link_.send(out); link_.update(now); }
        return;
    }
    if (st != TcpLink::State::Closed) return;
    if (wasConnected_) {
        wasConnected_ = false;
        session_.feed(encodeFrame({"Int.onDisconnect", 0, link_.error()}));   // TcpConnector.SendIntDisconnect
        if (!roomId_ || reconnects_ >= cfg_.maxReconnects) { stopped_ = true; return; }
        ++reconnects_;
        retryAt_ = now + cfg_.reconnectDelayMs;                               // OnReconnect: 0.5 s, same host group
        host_ = 0;
        return;
    }
    if (!retryAt_) {   // connect failed: next host of the group (HostGroup.Request)
        ++host_;
        if (size_t(host_) >= cfg_.hosts.size()) {
            session_.feed(encodeFrame({"Int.onDisconnect", 0, link_.error()}));
            stopped_ = true;
            return;
        }
        open(now);
        return;
    }
    if (now >= retryAt_) { retryAt_ = 0; open(now); }
}

// ---------------------------------------------------------------- ChatConnection

void ChatConnection::start(uint64_t now) {
    stopped_ = false;
    if (!hosts_.empty()) link_.connect(hosts_[size_t(host_) % hosts_.size()], 10000, now);
}

void ChatConnection::send(ChatType type, const std::string& json) {
    if (connected()) link_.send(encodeChatPacket(uint16_t(type), json));
}

void bindChat(BwBackend& backend, ChatConnection& chat) {
    backend.chatSend = [&chat](uint16_t type, const std::string& json) {
        if (!chat.connected()) return false;
        chat.send(ChatType(type), json);
        return true;
    };
    chat.onPacket = [&backend](const ChatPacket& p) { backend.onChatPacket(p.type, p.json); };
}

void ChatConnection::update(uint64_t now) {
    if (stopped_ || hosts_.empty()) return;
    const auto before = link_.state();
    link_.update(now);
    if (link_.state() == TcpLink::State::Connected) {
        if (before == TcpLink::State::Connecting) { link_.send(encodeChatAuth(token_)); wasConnected_ = true; }
        reader_.feed(link_.takeReceived());
        ChatPacket p;
        while (reader_.next(p)) {
            if (onPacket) onPacket(p);
            else packets_.push_back(std::move(p));
        }
        link_.update(now);
        return;
    }
    if (link_.state() != TcpLink::State::Closed) return;
    if (!retryAt_) {
        if (!wasConnected_) host_ = (host_ + 1) % int(hosts_.size());   // failed connect: next host
        wasConnected_ = false;
        reader_ = ChatReader();
        retryAt_ = now + 1000;
    } else if (now >= retryAt_) {
        retryAt_ = 0;
        link_.connect(hosts_[size_t(host_)], 10000, now);
    }
}

} // namespace aiverse::meta::bw
#endif
