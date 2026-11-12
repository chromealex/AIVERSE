// Minimal HTTP/2 connection state machine (RFC 9113) for gRPC: framing, SETTINGS, flow control,
// HPACK, PING, RST_STREAM, GOAWAY. No server push, no priorities. Used by the client transport
// and the in-process test server (role = server).
#pragma once
#include "Hpack.h"
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace aiverse::grpc::h2 {

enum ErrorCode : uint32_t {
    NoError = 0, ProtocolError = 1, InternalError = 2, FlowControlError = 3, SettingsTimeout = 4,
    StreamClosed = 5, FrameSizeError = 6, RefusedStream = 7, Cancel = 8, CompressionError = 9,
    ConnectError = 10, EnhanceYourCalm = 11, InadequateSecurity = 12, Http11Required = 13,
};

class Connection {
  public:
    struct Handler {
        virtual ~Handler() = default;
        virtual void onHeaders(uint32_t sid, hpack::HeaderList headers, bool endStream) = 0;
        virtual void onData(uint32_t sid, std::string_view data, bool endStream) = 0;
        virtual void onReset(uint32_t sid, uint32_t code) = 0;
        virtual void onGoaway(uint32_t lastSid, uint32_t code, const std::string &debug) = 0;
    };

    Connection(bool client, Handler *handler);
    // Client: connection preface + SETTINGS; server: SETTINGS. Appends to out.
    void start(std::string &out);
    // Consumes complete frames from inbox. False = connection error (error() says why; a GOAWAY
    // was already appended to out).
    bool receive(std::string &inbox, std::string &out);

    uint32_t newStreamId(); // client: 1, 3, 5, ...
    bool canOpenStream() const { return openStreams_ < peerMaxStreams_; }
    void sendHeaders(uint32_t sid, const hpack::HeaderList &headers, bool endStream, std::string &out);
    // Queues DATA; flow-controlled. Pending bytes are written as windows open (receive / flush).
    void sendData(uint32_t sid, std::string_view data, bool endStream, std::string &out);
    void reset(uint32_t sid, uint32_t code, std::string &out);
    void goaway(uint32_t code, const std::string &debug, std::string &out);
    void ping(std::string &out);
    void flush(std::string &out);
    bool streamPending(uint32_t sid) const; // queued DATA not yet sent
    const std::string &error() const { return error_; }
    bool goawayReceived() const { return goawayReceived_; }

  private:
    struct Stream {
        int64_t sendWindow = 65535;
        uint32_t recvConsumed = 0;
        std::string pending;
        bool endQueued = false;
        bool localClosed = false;
        bool remoteClosed = false;
    };
    bool fail(uint32_t code, std::string why, std::string &out);
    void frame(std::string &out, uint8_t type, uint8_t flags, uint32_t sid, std::string_view payload) const;
    void windowUpdate(std::string &out, uint32_t sid, uint32_t inc) const;
    bool onFrame(uint8_t type, uint8_t flags, uint32_t sid, std::string_view p, std::string &out);
    bool onSettings(uint8_t flags, std::string_view p, std::string &out);
    void closeHalf(uint32_t sid, bool local);
    Stream *find(uint32_t sid);

    bool client_;
    Handler *handler_;
    bool prefaceSeen_;
    hpack::Decoder decoder_;
    hpack::Encoder encoder_;
    std::map<uint32_t, Stream> streams_;
    uint32_t nextSid_;
    uint32_t lastPeerSid_ = 0;
    uint32_t openStreams_ = 0;
    uint32_t peerMaxStreams_ = 100;
    uint32_t peerMaxFrame_ = 16384;
    int64_t peerInitialWindow_ = 65535;
    int64_t connSendWindow_ = 65535;
    uint32_t connRecvConsumed_ = 0;
    uint32_t headerSid_ = 0; // HEADERS awaiting CONTINUATION
    bool headerEndStream_ = false;
    std::string headerBlock_;
    bool goawayReceived_ = false;
    std::string error_;
};

constexpr uint32_t kOurInitialWindow = 1u << 20; // per stream
constexpr uint32_t kOurConnWindow = 16u << 20;

} // namespace aiverse::grpc::h2
