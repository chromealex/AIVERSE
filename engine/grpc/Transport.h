// Internal transport interface shared by the HTTP/2, gRPC-Web/WebSocket and browser transports.
#pragma once
#include "Grpc.h"
#include <memory>
#include <string>
#include <string_view>

namespace aiverse::grpc::detail {

// Receives the events of one RPC. Called only from inside Transport::poll().
struct StreamSink {
    virtual ~StreamSink() = default;
    virtual void onHeaders(Metadata headers) = 0;
    virtual void onMessage(std::string payload) = 0; // one de-framed message
    virtual void onClose(Status status) = 0;         // final event (trailers in status)
};

class TransportStream {
  public:
    virtual ~TransportStream() = default; // destruction cancels and detaches the sink
    virtual void send(std::string_view message) = 0; // unframed message payload
    virtual void halfClose() = 0;
};

struct StreamRequest {
    std::string path;    // "/pkg.Service/Method" (target path prefix already applied)
    Metadata headers;    // custom metadata, authorization, grpc-timeout
    bool unary = false;  // exactly one request message followed by half-close
};

class Transport {
  public:
    virtual ~Transport() = default;
    virtual std::unique_ptr<TransportStream> open(const StreamRequest &req, StreamSink *sink) = 0;
    virtual void poll(uint64_t nowMs) = 0;
    virtual std::string lastError() const = 0;
};

std::unique_ptr<Transport> makeHttp2Transport(const ChannelConfig &cfg, const Target &target);
std::unique_ptr<Transport> makeGrpcWebTransport(const ChannelConfig &cfg, const Target &target);

// --- gRPC message framing (shared by all transports and the test server) ---
constexpr size_t kMaxMessageBytes = 16u << 20;

inline void frameMessage(std::string &out, std::string_view msg, uint8_t flags = 0) {
    out.push_back(char(flags));
    const uint32_t n = uint32_t(msg.size());
    out.push_back(char(n >> 24));
    out.push_back(char(n >> 16));
    out.push_back(char(n >> 8));
    out.push_back(char(n));
    out.append(msg.data(), msg.size());
}

// Incremental de-framer. Flag bit 0x80 marks a gRPC-Web headers/trailers block.
class Deframer {
  public:
    enum class Result { NeedMore, Message, Block, Error };
    void feed(std::string_view bytes) { buf_.append(bytes.data(), bytes.size()); }
    Result next(std::string &out, std::string *error) {
        if (buf_.size() - pos_ < 5) {
            compact();
            return Result::NeedMore;
        }
        const auto *p = reinterpret_cast<const uint8_t *>(buf_.data() + pos_);
        const uint32_t n = (uint32_t(p[1]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 8) | p[4];
        if (n > kMaxMessageBytes) {
            if (error)
                *error = "message larger than " + std::to_string(kMaxMessageBytes) + " bytes";
            return Result::Error;
        }
        if (buf_.size() - pos_ - 5 < n) {
            compact();
            return Result::NeedMore;
        }
        const uint8_t flags = p[0];
        out.assign(buf_.data() + pos_ + 5, n);
        pos_ += 5 + size_t(n);
        if (flags & 0x80)
            return Result::Block;
        if (flags & 0x01) {
            if (error)
                *error = "compressed messages are not supported (we advertise identity only)";
            return Result::Error;
        }
        return Result::Message;
    }
    size_t buffered() const { return buf_.size() - pos_; }

  private:
    void compact() {
        if (pos_ > 0) {
            buf_.erase(0, pos_);
            pos_ = 0;
        }
    }
    std::string buf_;
    size_t pos_ = 0;
};

// "k: v\r\n" blocks (gRPC-Web trailers, bridge headers).
Metadata parseHeaderBlock(std::string_view block);
void writeHeaderBlock(const Metadata &md, std::string &out);
// grpc-status / grpc-message from trailers (or headers in a trailers-only reply).
// False when grpc-status is missing.
bool statusFromMetadata(const Metadata &md, Status &out);
// Values of "-bin" keys are base64 on the wire; these convert in place.
void decodeBinaryValues(Metadata &md);
std::string wireValue(const std::string &key, const std::string &value);
bool isReservedHeader(std::string_view key);

} // namespace aiverse::grpc::detail
