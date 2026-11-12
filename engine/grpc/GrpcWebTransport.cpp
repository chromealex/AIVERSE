// gRPC-Web (binary) over HTTP/1.1 for unary calls + GrpcWebSocketBridge for streams, matching
// Cysharp GrpcWebSocketBridge.Client 1.4.x (what Drone Fall's Unity client uses):
//   unary : POST <path>, Content-Type: application/grpc-web, body = framed request; the response
//           body holds framed messages and a 0x80 trailer block (or grpc-status in headers).
//   stream: WebSocket upgrade with sub-protocol "cs-grpc-bridge-v1". Client sends one binary
//           message with a 0x80 block of request headers, then each framed message as a binary
//           message, and [0x80 00 00 00 00] to half-close. The server sends a 0x80 header block,
//           framed messages and a final 0x80 trailer block (grpc-status). Chunking of server
//           binary messages is arbitrary.
#ifndef __EMSCRIPTEN__
#include "ByteStream.h"
#include "Http1.h"
#include "Transport.h"
#include <algorithm>

namespace aiverse::grpc::detail {
namespace {

constexpr const char *kBridgeProtocol = "cs-grpc-bridge-v1";

struct WebCall {
    StreamSink *sink = nullptr;
    StreamRequest req;
    bool ws = false;
    bool closed = false;
    // unary
    std::string body;
    bool halfClose = false;
    bool sent = false;
    http1::Parser parser{true};
    // websocket
    enum class WsStage { Connecting, Upgrading, Open } stage = WsStage::Connecting;
    std::string wsKey;
    std::string outbox; // binary messages, already WS-framed, waiting for the upgrade
    bool halfCloseSent = false;
    bool headersSeen = false;
    std::string fragment;
    Deframer deframer;
    std::unique_ptr<ByteStream> conn;
};

class WebTransport;

class WebStream final : public TransportStream {
  public:
    WebStream(WebTransport *t, std::shared_ptr<WebCall> c) : t_(t), c_(std::move(c)) {}
    ~WebStream() override;
    void send(std::string_view message) override;
    void halfClose() override;

  private:
    WebTransport *t_;
    std::shared_ptr<WebCall> c_;
};

class WebTransport final : public Transport {
  public:
    WebTransport(const ChannelConfig &cfg, const Target &t) : cfg_(cfg), target_(t) {}

    std::unique_ptr<TransportStream> open(const StreamRequest &req, StreamSink *sink) override {
        auto c = std::make_shared<WebCall>();
        c->sink = sink;
        c->req = req;
        c->ws = !req.unary || cfg_.webSocketForUnary;
        calls_.push_back(c);
        return std::make_unique<WebStream>(this, c);
    }

    void poll(uint64_t now) override {
        now_ = now;
        auto calls = calls_;
        for (auto &c : calls)
            if (!c->closed)
                c->ws ? pollWs(*c) : pollUnary(*c);
        calls_.erase(std::remove_if(calls_.begin(), calls_.end(), [](const auto &c) { return c->closed; }),
                     calls_.end());
        // Idle keep-alive connections: drop the ones the server closed.
        for (auto &s : idle_)
            s->poll(now);
        idle_.erase(std::remove_if(idle_.begin(), idle_.end(),
                                   [](const auto &s) { return !s->open() || s->peerClosed(); }),
                    idle_.end());
        closing_.erase(std::remove_if(closing_.begin(), closing_.end(),
                                      [&](const auto &s) { return s->poll(now) != ByteStream::State::Open || s->flushed(); }),
                       closing_.end());
    }

    std::string lastError() const override { return lastError_; }

    void send(WebCall &c, std::string_view msg) {
        if (c.closed)
            return;
        if (!c.ws) {
            frameMessage(c.body, msg);
            return;
        }
        std::string framed;
        frameMessage(framed, msg);
        ws::writeFrame(c.outbox, ws::Binary, framed, true);
    }
    void halfClose(WebCall &c) {
        if (c.closed)
            return;
        c.halfClose = true;
        if (c.ws && !c.halfCloseSent) {
            static const char kEnd[5] = {char(0x80), 0, 0, 0, 0};
            ws::writeFrame(c.outbox, ws::Binary, std::string_view(kEnd, 5), true);
            c.halfCloseSent = true;
        }
    }
    void cancel(const std::shared_ptr<WebCall> &c) {
        c->sink = nullptr;
        if (c->closed)
            return;
        c->closed = true;
        if (c->conn && c->ws && c->stage == WebCall::WsStage::Open && c->conn->open()) {
            std::string out;
            ws::writeFrame(out, ws::Close, std::string("\x03\xe8", 2), true); // 1000 normal
            c->conn->write(out);
            closing_.push_back(std::move(c->conn));
        }
        c->conn.reset();
    }

  private:
    std::unique_ptr<ByteStream> connection(bool forWs) {
        if (!forWs)
            while (!idle_.empty()) {
                auto s = std::move(idle_.back());
                idle_.pop_back();
                if (s->open() && !s->peerClosed())
                    return s;
            }
        return ByteStream::connect(target_.host, target_.port, target_.tls, cfg_.tls, {"http/1.1"}, now_,
                                   cfg_.connectTimeoutMs);
    }

    void finish(WebCall &c, Status s) {
        if (c.closed)
            return;
        c.closed = true;
        if (!s.ok() && s.code != StatusCode::Cancelled)
            lastError_ = s.message;
        if (c.sink)
            c.sink->onClose(std::move(s));
        c.conn.reset();
    }

    void appendCommonHeaders(std::string &h, const WebCall &c) {
        h += "Host: " + target_.authority() + "\r\n";
        h += "User-Agent: " + cfg_.userAgent + "\r\n";
        for (const auto &[k, v] : c.req.headers)
            if (k != "grpc-timeout" || !c.ws)
                h += k + ": " + v + "\r\n";
    }

    // ---- unary over HTTP/1.1 ----
    void pollUnary(WebCall &c) {
        if (!c.halfClose)
            return; // the channel sends the whole request at once
        if (!c.conn)
            c.conn = connection(false);
        const auto st = c.conn->poll(now_);
        if (st == ByteStream::State::Failed) {
            finish(c, {StatusCode::Unavailable, c.conn->error(), {}});
            return;
        }
        if (st != ByteStream::State::Open)
            return;
        if (!c.sent) {
            std::string h = "POST " + c.req.path + " HTTP/1.1\r\n";
            appendCommonHeaders(h, c);
            h += "Content-Type: application/grpc-web\r\nAccept: application/grpc-web\r\nX-Grpc-Web: 1\r\n";
            h += "Content-Length: " + std::to_string(c.body.size()) + "\r\n\r\n";
            h += c.body;
            c.conn->write(h);
            c.sent = true;
            c.conn->poll(now_);
        }
        auto r = c.parser.feed(c.conn->inbox());
        if (r == http1::Parser::Result::NeedMore && c.conn->peerClosed())
            r = c.parser.finishOnClose();
        if (r == http1::Parser::Result::Error) {
            finish(c, {StatusCode::Unavailable, "HTTP: " + c.parser.error(), {}});
            return;
        }
        if (r != http1::Parser::Result::Complete && r != http1::Parser::Result::Head)
            return;
        auto &m = c.parser.message();
        Metadata headers;
        for (const auto &kv : m.headers)
            if (kv.first != "content-length" && kv.first != "transfer-encoding" && kv.first != "connection")
                headers.push_back(kv);
        decodeBinaryValues(headers);
        Status fromHeaders;
        const bool trailersOnly = statusFromMetadata(headers, fromHeaders);
        if (m.status != 200) {
            finish(c, trailersOnly ? fromHeaders
                                   : Status{statusFromHttp(m.status), "HTTP " + std::to_string(m.status) + " " + m.reason, {}});
            return;
        }
        const std::string *ct = m.header("content-type");
        if (!ct || ct->rfind("application/grpc", 0) != 0) {
            finish(c, {StatusCode::Unknown, "unexpected content-type '" + (ct ? *ct : std::string()) + "'", {}});
            return;
        }
        if (c.parser.keepAlive() && c.conn->open() && !c.conn->peerClosed())
            idle_.push_back(std::move(c.conn));
        if (c.sink)
            c.sink->onHeaders(headers);
        Deframer d;
        d.feed(m.body);
        std::string msg, err;
        for (;;) {
            const auto fr = d.next(msg, &err);
            if (fr == Deframer::Result::Message) {
                if (c.sink)
                    c.sink->onMessage(std::move(msg));
                continue;
            }
            if (fr == Deframer::Result::Block) {
                Metadata trailers = parseHeaderBlock(msg);
                decodeBinaryValues(trailers);
                Status s;
                finish(c, statusFromMetadata(trailers, s) ? s : Status{StatusCode::Internal, "trailers without grpc-status", {}});
                return;
            }
            if (fr == Deframer::Result::Error) {
                finish(c, {StatusCode::Internal, err, {}});
                return;
            }
            break;
        }
        if (d.buffered())
            finish(c, {StatusCode::Internal, "truncated gRPC-Web body", {}});
        else
            finish(c, trailersOnly ? fromHeaders : Status{StatusCode::Internal, "response without grpc-status", {}});
    }

    // ---- streams over the WebSocket bridge ----
    void pollWs(WebCall &c) {
        if (!c.conn)
            c.conn = connection(true);
        const auto st = c.conn->poll(now_);
        if (st == ByteStream::State::Failed) {
            finish(c, {StatusCode::Unavailable, c.conn->error(), {}});
            return;
        }
        if (st != ByteStream::State::Open)
            return;
        if (c.stage == WebCall::WsStage::Connecting) {
            c.wsKey = ws::randomKey();
            std::string h = "GET " + c.req.path + " HTTP/1.1\r\n";
            h += "Host: " + target_.authority() + "\r\n";
            h += "User-Agent: " + cfg_.userAgent + "\r\n";
            h += "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n";
            h += "Sec-WebSocket-Key: " + c.wsKey + "\r\n";
            h += std::string("Sec-WebSocket-Protocol: ") + kBridgeProtocol + "\r\n\r\n";
            c.conn->write(h);
            c.stage = WebCall::WsStage::Upgrading;
            c.conn->poll(now_);
        }
        if (c.stage == WebCall::WsStage::Upgrading) {
            const auto r = c.parser.feed(c.conn->inbox(), true);
            if (r == http1::Parser::Result::Error) {
                finish(c, {StatusCode::Unavailable, "WebSocket upgrade: " + c.parser.error(), {}});
                return;
            }
            if (r == http1::Parser::Result::NeedMore) {
                if (c.conn->peerClosed())
                    finish(c, {StatusCode::Unavailable, "connection closed during the WebSocket upgrade", {}});
                return;
            }
            auto &m = c.parser.message();
            const std::string *accept = m.header("sec-websocket-accept");
            const std::string *proto = m.header("sec-websocket-protocol");
            if (m.status != 101) {
                finish(c, {statusFromHttp(m.status), "WebSocket upgrade refused: HTTP " + std::to_string(m.status), {}});
                return;
            }
            if (!accept || *accept != ws::acceptFor(c.wsKey) || !proto || *proto != kBridgeProtocol) {
                finish(c, {StatusCode::Unavailable, "server does not speak the cs-grpc-bridge-v1 WebSocket protocol", {}});
                return;
            }
            c.stage = WebCall::WsStage::Open;
            std::string first, block;
            writeHeaderBlock(c.req.headers, block);
            frameMessage(first, block, 0x80);
            std::string out;
            ws::writeFrame(out, ws::Binary, first, true);
            c.conn->write(out);
        }
        if (!c.outbox.empty()) {
            c.conn->write(c.outbox);
            c.outbox.clear();
        }
        ws::Frame f;
        for (;;) {
            const int r = ws::readFrame(c.conn->inbox(), f, false);
            if (r < 0) {
                finish(c, {StatusCode::Internal, "malformed WebSocket frame", {}});
                return;
            }
            if (r == 0)
                break;
            if (f.opcode == ws::Ping) {
                std::string out;
                ws::writeFrame(out, ws::Pong, f.payload, true);
                c.conn->write(out);
                continue;
            }
            if (f.opcode == ws::Pong)
                continue;
            if (f.opcode == ws::Close) {
                std::string out;
                ws::writeFrame(out, ws::Close, f.payload.substr(0, 2), true);
                c.conn->write(out);
                c.conn->poll(now_);
                finish(c, {StatusCode::Unavailable, "stream closed by the server before trailers", {}});
                return;
            }
            c.fragment += f.payload;
            if (!f.fin)
                continue;
            c.deframer.feed(c.fragment);
            c.fragment.clear();
            if (!drainBridge(c))
                return;
        }
        c.conn->poll(now_);
        if (c.conn->peerClosed() && !c.closed)
            finish(c, {StatusCode::Unavailable, "connection closed before trailers", {}});
    }

    bool drainBridge(WebCall &c) {
        std::string msg, err;
        for (;;) {
            const auto r = c.deframer.next(msg, &err);
            if (r == Deframer::Result::NeedMore)
                return true;
            if (r == Deframer::Result::Error) {
                finish(c, {StatusCode::Internal, err, {}});
                return false;
            }
            if (r == Deframer::Result::Message) {
                if (c.sink)
                    c.sink->onMessage(std::move(msg));
                continue;
            }
            Metadata md = parseHeaderBlock(msg);
            decodeBinaryValues(md);
            Status s;
            const bool hasStatus = statusFromMetadata(md, s);
            if (!c.headersSeen && !hasStatus) {
                c.headersSeen = true;
                if (c.sink)
                    c.sink->onHeaders(md);
                continue;
            }
            // Trailers (the bridge also sends trailers-only replies as the first block).
            if (c.conn && c.conn->open()) {
                std::string out;
                ws::writeFrame(out, ws::Close, std::string("\x03\xe8", 2), true);
                c.conn->write(out);
                closing_.push_back(std::move(c.conn));
            }
            finish(c, hasStatus ? s : Status{StatusCode::Internal, "trailers without grpc-status", {}});
            return false;
        }
    }

    ChannelConfig cfg_;
    Target target_;
    uint64_t now_ = 0;
    std::vector<std::shared_ptr<WebCall>> calls_;
    std::vector<std::unique_ptr<ByteStream>> idle_;
    std::vector<std::unique_ptr<ByteStream>> closing_;
    std::string lastError_;
};

WebStream::~WebStream() { t_->cancel(c_); }
void WebStream::send(std::string_view message) { t_->send(*c_, message); }
void WebStream::halfClose() { t_->halfClose(*c_); }

} // namespace

std::unique_ptr<Transport> makeGrpcWebTransport(const ChannelConfig &cfg, const Target &target) {
    return std::make_unique<WebTransport>(cfg, target);
}

} // namespace aiverse::grpc::detail
#endif
