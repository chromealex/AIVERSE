// Non-blocking TCP (+ optional TLS) byte stream driven by poll() on the owning thread.
#pragma once
#ifndef __EMSCRIPTEN__
#include "Tls.h"
#include <memory>
#include <string>
#include <string_view>

namespace aiverse::net {
class TcpSocket;
}

namespace aiverse::grpc::detail {

class ByteStream {
  public:
    enum class State { Connecting, Handshaking, Open, Closed, Failed };

    // Resolves (blocking getaddrinfo) and starts a non-blocking connect. Never null: failures are
    // reported by poll() as Failed with error().
    static std::unique_ptr<ByteStream> connect(const std::string &host, uint16_t port, bool tls,
                                               const TlsConfig &cfg, const std::vector<std::string> &alpn,
                                               uint64_t now, uint32_t timeoutMs);
    // Accepted server socket (tests): tls may be null for plain TCP.
    ByteStream(std::unique_ptr<net::TcpSocket> socket, std::unique_ptr<TlsEngine> tls);
    ~ByteStream();

    State poll(uint64_t now);
    State state() const { return state_; }
    bool open() const { return state_ == State::Open; }
    void write(std::string_view bytes);
    std::string &inbox() { return inbox_; } // received plaintext; the consumer erases what it used
    bool flushed() const { return plainOut_.empty() && cipherOut_.empty(); }
    std::string alpn() const { return tls_ ? tls_->alpn() : std::string(); }
    const std::string &error() const { return error_; }
    bool peerClosed() const { return peerClosed_; }
    void close();

  private:
    ByteStream() = default;
    void fail(std::string why);
    std::unique_ptr<net::TcpSocket> socket_;
    std::unique_ptr<TlsEngine> tls_;
    State state_ = State::Connecting;
    uint64_t deadline_ = 0;
    std::string plainOut_;  // waiting for the TLS handshake (or the socket, without TLS)
    std::string cipherOut_; // TLS records waiting for the socket
    std::string inbox_;
    std::string error_;
    bool peerClosed_ = false;
};

} // namespace aiverse::grpc::detail
#endif
