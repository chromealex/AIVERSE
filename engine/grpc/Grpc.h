// gRPC client for engine hosts (meta backends, matchmaking). Not for simulation code.
//
// Threading: a Channel and everything created from it belong to one owning thread. All
// callbacks run inside Channel::poll() on that thread; nothing is ever invoked from another
// thread or from inside a call that starts/cancels an RPC. The channel creates no threads on
// desktop/web; the Android TLS backend uses Java SSLEngine through JNI on the owning thread.
//
// Transports (ChannelConfig::protocol):
//   Http2   - standard gRPC over HTTP/2 (h2 via TLS ALPN, or h2c prior knowledge for http://).
//   GrpcWeb - gRPC-Web binary over HTTP/1.1 for unary calls and the GrpcWebSocketBridge
//             protocol (WebSocket sub-protocol "cs-grpc-bridge-v1", Cysharp) for streams.
//             This is what Unity clients using GrpcWebSocketBridgeHandler speak, and the only
//             protocol available in the web build (browser fetch + WebSocket).
// See docs/grpc.md.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::grpc {

enum class StatusCode : int {
    Ok = 0, Cancelled = 1, Unknown = 2, InvalidArgument = 3, DeadlineExceeded = 4, NotFound = 5,
    AlreadyExists = 6, PermissionDenied = 7, ResourceExhausted = 8, FailedPrecondition = 9, Aborted = 10,
    OutOfRange = 11, Unimplemented = 12, Internal = 13, Unavailable = 14, DataLoss = 15, Unauthenticated = 16,
};
const char *statusName(StatusCode c);

// Lower-case keys. Keys ending in "-bin" carry raw bytes (base64 on the wire).
using Metadata = std::vector<std::pair<std::string, std::string>>;
const std::string *findMetadata(const Metadata &md, std::string_view key);

struct Status {
    StatusCode code = StatusCode::Ok;
    std::string message;
    Metadata trailers;
    bool ok() const { return code == StatusCode::Ok; }
    std::string str() const; // "UNAVAILABLE: connection refused"
};

enum class MethodKind { Unary, ClientStream, ServerStream, Bidi };
struct MethodInfo {
    const char *path; // "/package.Service/Method"
    MethodKind kind;
};

struct RetryPolicy {
    int maxAttempts = 1; // 1 = no retries (Drone Fall's Unity client never retries)
    uint32_t initialBackoffMs = 200;
    uint32_t maxBackoffMs = 5000;
    uint32_t multiplierPercent = 200;
    std::vector<StatusCode> retryOn{StatusCode::Unavailable};
};

// Serializes unary calls like Drone Fall's CallsThrottle interceptor: at most `limit` unary RPCs
// run at once (FIFO), shared by every channel holding the same gate. Streams are not gated.
// A call cancelled while it waits never starts and completes with CANCELLED.
class CallGate {
  public:
    explicit CallGate(int limit = 1) : limit_(limit) {}
    int limit() const { return limit_; }
    int running() const { return running_; }
    size_t waiting() const { return waiting_.size(); }

  private:
    friend class Channel;
    int limit_;
    int running_ = 0;
    uint64_t nextTicket_ = 1;
    std::set<uint64_t> waiting_; // FIFO across every channel sharing the gate
};

struct CallOptions {
    uint32_t timeoutMs = 0; // 0 = no deadline. Counted from the call, including queue time.
    Metadata metadata;
    bool throttle = true; // unary only: go through the channel's CallGate
    bool retry = true;    // unary only: apply ChannelConfig::retry
};

struct TlsConfig {
    bool verifyPeer = true;
    std::string caPem;      // extra trusted roots (PEM); empty = system store only
    std::string serverName; // SNI / hostname check override; empty = target host
};

enum class Protocol { Http2, GrpcWeb };

struct ChannelConfig {
    std::string target; // "https://host[:port]" or "http://host[:port]"; a path prefix is kept
    Protocol protocol = Protocol::Http2;
    bool webSocketForUnary = false; // GrpcWeb: run unary calls over the WebSocket bridge too
    TlsConfig tls;
    // Evaluated when each attempt starts; a non-empty token adds "authorization: Bearer <token>".
    std::function<std::string()> authToken;
    std::shared_ptr<CallGate> gate; // null = no unary throttling
    RetryPolicy retry;
    uint32_t connectTimeoutMs = 10000;
    std::string userAgent = "aiverse-grpc/1.0";
    std::function<uint64_t()> nowMs; // monotonic milliseconds; default steady_clock
};

namespace detail {
struct CallCore;
class Transport;
} // namespace detail

// Handle to an RPC. Copyable; cancel() is safe after completion or channel destruction.
class Call {
  public:
    Call() = default;
    explicit Call(std::weak_ptr<detail::CallCore> core) : core_(std::move(core)) {}
    void cancel();
    bool active() const;
    uint64_t id() const;

  private:
    std::weak_ptr<detail::CallCore> core_;
};

struct RawStreamHandlers {
    std::function<void(const Metadata &)> onHeaders;
    // Serialized message; return false when it cannot be parsed (the call ends with INTERNAL).
    std::function<bool(std::string)> onMessage;
    std::function<void(const Status &)> onFinish; // exactly once
};

class Channel {
  public:
    explicit Channel(ChannelConfig config);
    ~Channel(); // cancels everything silently: no callbacks after destruction
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;

    // Drives connections, timers and delivers callbacks. Call every frame on the owning thread.
    void poll();
    // Cancels all calls; their callbacks run (CANCELLED) on the next poll().
    void cancelAll();
    size_t activeCalls() const;
    const ChannelConfig &config() const { return config_; }
    std::string lastTransportError() const;

    // Byte-level API used by generated stubs.
    Call unary(const MethodInfo &m, std::string request, CallOptions options,
               std::function<void(const Status &, std::string)> done);
    std::shared_ptr<detail::CallCore> stream(const MethodInfo &m, CallOptions options, RawStreamHandlers handlers);
    // Streams: queue an outgoing message / half-close. False once the call has finished.
    bool streamWrite(const std::shared_ptr<detail::CallCore> &core, std::string message);
    void streamWritesDone(const std::shared_ptr<detail::CallCore> &core);

  private:
    friend class Call;
    void start(const std::shared_ptr<detail::CallCore> &c, uint64_t now);
    void finish(const std::shared_ptr<detail::CallCore> &c, Status s);
    void abort(const std::shared_ptr<detail::CallCore> &c, Status s);
    void releaseGate(detail::CallCore &c);
    bool gateAdmits(detail::CallCore &c);
    uint64_t now() const;
    Metadata requestHeaders(const detail::CallCore &c, uint64_t now) const;

    ChannelConfig config_;
    std::unique_ptr<detail::Transport> transport_;
    std::vector<std::shared_ptr<detail::CallCore>> calls_;
    std::vector<std::function<void()>> ready_; // deferred user callbacks
    uint64_t nextId_ = 1;
    bool polling_ = false;
};

template <class Resp> using UnaryDone = std::function<void(const Status &, Resp &&)>;

template <class Resp>
Call unaryCall(Channel &ch, const MethodInfo &m, std::string request, UnaryDone<Resp> done, CallOptions options) {
    return ch.unary(m, std::move(request), std::move(options),
                    [done = std::move(done)](const Status &s, std::string bytes) {
                        Resp resp{};
                        if (s.ok() && !resp.parse(bytes)) {
                            Status bad{StatusCode::Internal, "failed to parse response message", s.trailers};
                            if (done)
                                done(bad, Resp{});
                            return;
                        }
                        if (done)
                            done(s, std::move(resp));
                    });
}

template <class Resp> struct StreamHandlers {
    std::function<void(const Metadata &)> onHeaders;
    std::function<void(Resp &&)> onMessage;
    std::function<void(const Status &)> onFinish;
};

template <class Req, class Resp> class Stream {
  public:
    Stream(Channel &ch, std::shared_ptr<detail::CallCore> core) : ch_(&ch), core_(std::move(core)) {}
    bool write(const Req &m) { return ch_->streamWrite(core_, m.serialize()); }
    void writesDone() { ch_->streamWritesDone(core_); }
    void cancel() { call().cancel(); }
    bool active() const { return call().active(); }
    Call call() const { return Call(core_); }

  private:
    Channel *ch_;
    std::shared_ptr<detail::CallCore> core_;
};

template <class Req, class Resp>
std::shared_ptr<Stream<Req, Resp>> openStream(Channel &ch, const MethodInfo &m, StreamHandlers<Resp> h,
                                              CallOptions options) {
    RawStreamHandlers raw;
    raw.onHeaders = std::move(h.onHeaders);
    raw.onFinish = std::move(h.onFinish);
    raw.onMessage = [onMessage = std::move(h.onMessage)](std::string bytes) {
        Resp resp{};
        if (!resp.parse(bytes))
            return false; // the channel aborts the call with INTERNAL
        if (onMessage)
            onMessage(std::move(resp));
        return true;
    };
    return std::make_shared<Stream<Req, Resp>>(ch, ch.stream(m, std::move(options), std::move(raw)));
}

// Splits "https://host:port/prefix" into parts. False on a malformed target.
struct Target {
    bool tls = true;
    std::string host;
    uint16_t port = 443;
    std::string pathPrefix; // without trailing '/'
    std::string authority() const;
};
bool parseTarget(std::string_view url, Target &out);

// grpc-timeout header value for a remaining time ("150m", "20S").
std::string encodeTimeout(uint64_t ms);
// Maps an HTTP status of a failed gRPC exchange (spec: http-grpc-status-mapping.md).
StatusCode statusFromHttp(int httpStatus);
// grpc-message percent-encoding.
std::string percentEncode(std::string_view s);
std::string percentDecode(std::string_view s);
std::string base64Encode(std::string_view bytes);
bool base64Decode(std::string_view text, std::string &out);

} // namespace aiverse::grpc
