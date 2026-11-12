// Small HTTP/1.1 message parser and WebSocket (RFC 6455) frame codec for the gRPC-Web transport
// and the in-process test server.
#pragma once
#include "Grpc.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace aiverse::grpc::http1 {

struct Message {
    // Response: status; request: method + target.
    int status = 0;
    std::string reason, method, target;
    Metadata headers; // lower-case keys
    std::string body;
    const std::string *header(std::string_view k) const { return findMetadata(headers, k); }
};

class Parser {
  public:
    enum class Result { NeedMore, Head, Complete, Error };
    explicit Parser(bool response) : response_(response) {}
    // Consumes bytes from inbox. Head = head parsed and the message is an upgrade (101) or the
    // caller asked headOnly; Complete = full message (body de-chunked).
    Result feed(std::string &inbox, bool headOnly = false);
    // Response bodies without length end at connection close.
    Result finishOnClose();
    Message &message() { return msg_; }
    const std::string &error() const { return error_; }
    bool keepAlive() const { return keepAlive_; }

  private:
    Result fail(std::string e) {
        error_ = std::move(e);
        return Result::Error;
    }
    bool response_;
    enum class Stage { Head, Body, Chunked, Done } stage_ = Stage::Head;
    Message msg_;
    int64_t remaining_ = -1; // -1 = until close (responses)
    bool keepAlive_ = true;
    std::string error_;
};

} // namespace aiverse::grpc::http1

namespace aiverse::grpc::ws {

enum Opcode : uint8_t { Continuation = 0, Text = 1, Binary = 2, Close = 8, Ping = 9, Pong = 10 };

struct Frame {
    uint8_t opcode = 0;
    bool fin = true;
    std::string payload;
};

void writeFrame(std::string &out, uint8_t opcode, std::string_view payload, bool masked);
// 1 = frame read, 0 = need more bytes, -1 = protocol error.
int readFrame(std::string &inbox, Frame &f, bool expectMasked, size_t maxPayload = 16u << 20);
std::string randomKey(); // Sec-WebSocket-Key
std::string acceptFor(const std::string &key);

} // namespace aiverse::grpc::ws
