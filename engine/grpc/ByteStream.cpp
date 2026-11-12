#ifndef __EMSCRIPTEN__
#include "ByteStream.h"
#include "engine/net/Socket.h"

namespace aiverse::grpc::detail {

std::unique_ptr<ByteStream> ByteStream::connect(const std::string &host, uint16_t port, bool tls,
                                                const TlsConfig &cfg, const std::vector<std::string> &alpn,
                                                uint64_t now, uint32_t timeoutMs) {
    std::unique_ptr<ByteStream> s(new ByteStream());
    s->deadline_ = timeoutMs ? now + timeoutMs : 0;
    net::Address addr;
    // IPv4 first: NAT64 networks still resolve to IPv6 only, dual-stack hosts avoid broken v6 routes.
    if (!net::Address::resolve(host, port, addr, true)) {
        s->fail("cannot resolve host '" + host + "'");
        return s;
    }
    if (tls) {
        std::string err;
        s->tls_ = createTlsClient(cfg, cfg.serverName.empty() ? host : cfg.serverName, alpn, &err);
        if (!s->tls_) {
            s->fail("TLS unavailable: " + err);
            return s;
        }
    }
    s->socket_ = std::make_unique<net::TcpSocket>();
    if (!s->socket_->connect(addr))
        s->fail("connect to " + addr.str() + " failed");
    return s;
}

ByteStream::ByteStream(std::unique_ptr<net::TcpSocket> socket, std::unique_ptr<TlsEngine> tls)
    : socket_(std::move(socket)), tls_(std::move(tls)) {
    state_ = tls_ ? State::Handshaking : State::Open;
}

ByteStream::~ByteStream() = default;

void ByteStream::fail(std::string why) {
    if (state_ == State::Failed || state_ == State::Closed)
        return;
    error_ = std::move(why);
    state_ = State::Failed;
    if (socket_)
        socket_->close();
}

void ByteStream::close() {
    if (socket_)
        socket_->close();
    if (state_ != State::Failed)
        state_ = State::Closed;
}

void ByteStream::write(std::string_view bytes) { plainOut_.append(bytes.data(), bytes.size()); }

ByteStream::State ByteStream::poll(uint64_t now) {
    if (state_ == State::Failed || state_ == State::Closed)
        return state_;
    if (state_ == State::Connecting) {
        const int r = socket_->pollConnected();
        if (r < 0) {
            fail("connection refused or unreachable");
            return state_;
        }
        if (r == 0) {
            if (deadline_ && now >= deadline_)
                fail("connect timed out");
            return state_;
        }
        state_ = tls_ ? State::Handshaking : State::Open;
    }
    // Read everything available.
    uint8_t buf[16384];
    for (;;) {
        const int n = socket_->recv(buf, sizeof buf);
        if (n > 0) {
            if (tls_)
                tls_->putCipher(buf, size_t(n));
            else
                inbox_.append(reinterpret_cast<const char *>(buf), size_t(n));
            continue;
        }
        if (n < 0)
            peerClosed_ = true;
        break;
    }
    if (tls_) {
        if (state_ == State::Open && !plainOut_.empty()) {
            tls_->writePlain(reinterpret_cast<const uint8_t *>(plainOut_.data()), plainOut_.size());
            plainOut_.clear();
        }
        const TlsEngine::State ts = tls_->step();
        tls_->readPlain(inbox_);
        tls_->takeCipher(cipherOut_);
        if (ts == TlsEngine::State::Failed) {
            fail("TLS: " + tls_->error());
            return state_;
        }
        if (ts == TlsEngine::State::Closed)
            peerClosed_ = true;
        if (state_ == State::Handshaking) {
            if (ts == TlsEngine::State::Open) {
                state_ = State::Open;
                if (!plainOut_.empty()) {
                    tls_->writePlain(reinterpret_cast<const uint8_t *>(plainOut_.data()), plainOut_.size());
                    plainOut_.clear();
                    tls_->step();
                    tls_->takeCipher(cipherOut_);
                }
            } else if (deadline_ && now >= deadline_) {
                fail("TLS handshake timed out");
                return state_;
            }
        }
    }
    std::string &out = tls_ ? cipherOut_ : plainOut_;
    while (!out.empty()) {
        const int n = socket_->send(reinterpret_cast<const uint8_t *>(out.data()), out.size());
        if (n > 0) {
            out.erase(0, size_t(n));
            continue;
        }
        if (n < 0) {
            peerClosed_ = true;
            out.clear();
        }
        break;
    }
    if (peerClosed_ && state_ == State::Handshaking)
        fail(tls_ && !tls_->error().empty() ? "TLS: " + tls_->error() : "connection closed during the TLS handshake");
    return state_;
}

} // namespace aiverse::grpc::detail
#endif
