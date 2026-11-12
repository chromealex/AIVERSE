// In-process gRPC test server: HTTP/2 (h2c or TLS+ALPN) or gRPC-Web + cs-grpc-bridge-v1 WebSocket
// (plain or TLS). Single-threaded: tests alternate Channel::poll() and MockServer::poll().
#pragma once
#include "engine/grpc/Grpc.h"
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aiverse::grpc::test {

struct ServerCall {
    enum class Event { Start, Message, HalfClose, Cancel };
    uint64_t id = 0;
    std::string path;
    Metadata headers;
    std::vector<std::string> received;
    bool halfClosed = false;
    bool cancelled = false;
    bool finished = false;
    // Responses (buffered; flushed by MockServer::poll).
    void sendHeaders(Metadata md = {});
    void send(std::string message);
    void finish(StatusCode code, std::string message = {}, Metadata trailers = {});
    // finish() without sendHeaders()/send() becomes a trailers-only response.

    struct Out {
        bool headersQueued = false, headersSent = false;
        Metadata headers;
        std::vector<std::string> messages;
        bool finishQueued = false;
        Status status;
    } out;
};

class MockServer {
  public:
    enum class Mode { Http2, GrpcWeb };
    // Handler: called for every event of a call; respond through ServerCall at any time later.
    using Handler = std::function<void(ServerCall &, ServerCall::Event)>;

    MockServer(Mode mode, bool tls);
    ~MockServer();
    bool ok() const { return ok_; }
    const std::string &error() const { return error_; }
    uint16_t port() const;
    std::string url() const; // "http(s)://127.0.0.1:port" or "https://localhost:port"

    void handle(const std::string &path, Handler h) { handlers_[path] = std::move(h); }
    // Convenience: respond to the single request message; nullopt = never respond.
    using UnaryFn = std::function<std::optional<std::pair<Status, std::string>>(const Metadata &, const std::string &)>;
    void unary(const std::string &path, UnaryFn fn);

    void poll();
    void dropConnections(); // abrupt TCP close of every connection
    size_t connectionsAccepted() const { return accepted_; }
    size_t callsStarted() const { return started_; }
    std::vector<std::shared_ptr<ServerCall>> &calls() { return calls_; }
    static const char *testCaPem();

    struct Conn;

  private:
    void dispatch(ServerCall &c, ServerCall::Event e);
    friend struct Conn;
    Mode mode_;
    bool tls_;
    bool ok_ = false;
    std::string error_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::map<std::string, Handler> handlers_;
    std::vector<std::shared_ptr<ServerCall>> calls_;
    size_t accepted_ = 0;
    size_t started_ = 0;
    uint64_t nextId_ = 1;
};

} // namespace aiverse::grpc::test
