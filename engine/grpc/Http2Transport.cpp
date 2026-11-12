// gRPC over HTTP/2: one multiplexed connection per channel, reconnected on demand.
#ifndef __EMSCRIPTEN__
#include "ByteStream.h"
#include "Http2.h"
#include "Transport.h"
#include <algorithm>
#include <cstdlib>
#include <map>

namespace aiverse::grpc::detail {
namespace {

class H2Transport;

struct H2Call {
    StreamSink *sink = nullptr; // null once the owner went away
    StreamRequest req;
    uint32_t sid = 0;           // 0 = not opened on the wire yet
    std::string pendingOut;     // framed messages written before the stream opened
    bool halfClose = false; // requested by the channel
    bool endSent = false;   // END_STREAM queued on the wire
    bool headersDone = false;
    bool closed = false;
    Deframer deframer;
};

class H2Stream final : public TransportStream {
  public:
    H2Stream(H2Transport *t, std::shared_ptr<H2Call> c) : t_(t), c_(std::move(c)) {}
    ~H2Stream() override;
    void send(std::string_view message) override;
    void halfClose() override;

  private:
    H2Transport *t_;
    std::shared_ptr<H2Call> c_;
};

class H2Transport final : public Transport, public h2::Connection::Handler {
  public:
    H2Transport(const ChannelConfig &cfg, const Target &t) : cfg_(cfg), target_(t) {}

    std::unique_ptr<TransportStream> open(const StreamRequest &req, StreamSink *sink) override {
        auto c = std::make_shared<H2Call>();
        c->sink = sink;
        c->req = req;
        waiting_.push_back(c);
        return std::make_unique<H2Stream>(this, c);
    }

    void poll(uint64_t now) override {
        now_ = now;
        if (!conn_ && !waiting_.empty())
            connect();
        if (!conn_)
            return;
        const ByteStream::State st = stream_->poll(now);
        if (st == ByteStream::State::Failed || st == ByteStream::State::Closed) {
            drop(stream_->error().empty() ? "connection closed" : stream_->error());
            return;
        }
        if (st != ByteStream::State::Open)
            return;
        if (!started_) {
            if (target_.tls && stream_->alpn() != "h2") {
                drop("server did not negotiate HTTP/2 (ALPN '" + stream_->alpn() +
                     "'); use Protocol::GrpcWeb for HTTP/1.1 backends");
                return;
            }
            std::string out;
            h2_->start(out);
            stream_->write(out);
            started_ = true;
        }
        std::string out;
        if (!stream_->inbox().empty() && !h2_->receive(stream_->inbox(), out)) {
            stream_->write(out);
            stream_->poll(now);
            drop("HTTP/2 protocol error: " + h2_->error());
            return;
        }
        openWaiting(out);
        for (auto &[sid, c] : live_)
            pump(*c, out);
        h2_->flush(out);
        if (!out.empty()) {
            stream_->write(out);
            stream_->poll(now);
        }
        // Closed streams leave the table once their final event was delivered.
        for (auto it = live_.begin(); it != live_.end();)
            it = it->second->closed ? live_.erase(it) : std::next(it);
        if (stream_->peerClosed()) {
            drop("connection closed by the server");
            return;
        }
        if (h2_->goawayReceived() && live_.empty())
            reset();
    }

    std::string lastError() const override { return lastError_; }

    // Called by H2Stream.
    void cancel(const std::shared_ptr<H2Call> &c) {
        c->sink = nullptr;
        if (c->closed)
            return;
        c->closed = true;
        if (c->sid && conn_) {
            std::string out;
            h2_->reset(c->sid, h2::Cancel, out);
            stream_->write(out);
            live_.erase(c->sid);
        }
        waiting_.erase(std::remove(waiting_.begin(), waiting_.end(), c), waiting_.end());
    }
    void send(H2Call &c, std::string_view msg) {
        if (c.closed || c.halfClose)
            return;
        frameMessage(c.pendingOut, msg);
    }

  private:
    void connect() {
        stream_ = ByteStream::connect(target_.host, target_.port, target_.tls, cfg_.tls, {"h2"}, now_,
                                      cfg_.connectTimeoutMs);
        h2_ = std::make_unique<h2::Connection>(true, this);
        conn_ = true;
        started_ = false;
    }
    void reset() {
        conn_ = false;
        stream_.reset();
        h2_.reset();
    }
    void drop(const std::string &why) {
        lastError_ = why;
        dropping_ = true;
        // Streams that never reached the server can be retried by the channel (UNAVAILABLE).
        auto fail = [&](H2Call &c) {
            if (c.closed)
                return;
            c.closed = true;
            if (c.sink)
                c.sink->onClose({StatusCode::Unavailable, why, {}});
        };
        for (auto &[sid, c] : live_)
            fail(*c);
        for (auto &c : waiting_)
            fail(*c);
        live_.clear();
        waiting_.clear();
        reset();
        dropping_ = false;
    }
    void openWaiting(std::string &out) {
        while (!waiting_.empty() && h2_->canOpenStream() && !h2_->goawayReceived()) {
            auto c = waiting_.front();
            waiting_.erase(waiting_.begin());
            if (c->closed)
                continue;
            c->sid = h2_->newStreamId();
            hpack::HeaderList h{{":method", "POST"},
                                {":scheme", target_.tls ? "https" : "http"},
                                {":path", c->req.path},
                                {":authority", target_.authority()},
                                {"content-type", "application/grpc"},
                                {"te", "trailers"},
                                {"grpc-accept-encoding", "identity"},
                                {"user-agent", cfg_.userAgent}};
            for (const auto &kv : c->req.headers)
                h.push_back(kv);
            h2_->sendHeaders(c->sid, h, false, out);
            live_[c->sid] = c;
        }
    }
    void pump(H2Call &c, std::string &out) {
        if (c.closed || !c.sid || c.endSent)
            return;
        if (!c.pendingOut.empty() || c.halfClose) {
            h2_->sendData(c.sid, c.pendingOut, c.halfClose, out);
            c.pendingOut.clear();
            c.endSent = c.halfClose;
        }
    }

    H2Call *byId(uint32_t sid) {
        auto it = live_.find(sid);
        return it == live_.end() || it->second->closed ? nullptr : it->second.get();
    }
    void close(H2Call &c, Status s) {
        if (c.closed)
            return;
        c.closed = true;
        if (c.sid && !c.endSent && conn_ && !dropping_) {
            // The server finished first: release our half of the stream.
            std::string out;
            h2_->reset(c.sid, h2::Cancel, out);
            stream_->write(out);
        }
        if (c.sink)
            c.sink->onClose(std::move(s));
    }

    // h2::Connection::Handler
    void onHeaders(uint32_t sid, hpack::HeaderList headers, bool endStream) override {
        H2Call *c = byId(sid);
        if (!c)
            return;
        Metadata md;
        int httpStatus = 0;
        std::string contentType;
        for (auto &[k, v] : headers) {
            if (k == ":status")
                httpStatus = std::atoi(v.c_str());
            else if (k == "content-type")
                contentType = v;
            if (!k.empty() && k[0] != ':')
                md.emplace_back(std::move(k), std::move(v));
        }
        decodeBinaryValues(md);
        if (!c->headersDone) {
            c->headersDone = true;
            if (httpStatus != 200) {
                Status s;
                if (!statusFromMetadata(md, s))
                    s = {statusFromHttp(httpStatus), "HTTP status " + std::to_string(httpStatus), {}};
                close(*c, s);
                return;
            }
            if (contentType.rfind("application/grpc", 0) != 0) {
                close(*c, {StatusCode::Unknown, "unexpected content-type '" + contentType + "'", {}});
                return;
            }
            if (endStream) { // trailers-only response
                Status s;
                if (!statusFromMetadata(md, s))
                    s = {StatusCode::Internal, "response without grpc-status", {}};
                close(*c, s);
                return;
            }
            if (c->sink)
                c->sink->onHeaders(md);
            return;
        }
        Status s;
        if (!statusFromMetadata(md, s))
            s = {StatusCode::Internal, "trailers without grpc-status", {}};
        if (s.ok() && c->deframer.buffered())
            s = {StatusCode::Internal, "truncated message before trailers", {}};
        close(*c, s);
    }
    void onData(uint32_t sid, std::string_view data, bool endStream) override {
        H2Call *c = byId(sid);
        if (!c)
            return;
        c->deframer.feed(data);
        std::string msg, err;
        for (;;) {
            const auto r = c->deframer.next(msg, &err);
            if (r == Deframer::Result::NeedMore)
                break;
            if (r != Deframer::Result::Message) {
                close(*c, {StatusCode::Internal, r == Deframer::Result::Block ? "unexpected trailer frame" : err, {}});
                return;
            }
            if (c->sink)
                c->sink->onMessage(std::move(msg));
        }
        if (endStream)
            close(*c, {StatusCode::Internal, "stream ended without trailers", {}});
    }
    void onReset(uint32_t sid, uint32_t code) override {
        if (H2Call *c = byId(sid))
            close(*c, {code == h2::RefusedStream ? StatusCode::Unavailable
                       : code == h2::Cancel      ? StatusCode::Cancelled
                       : code == h2::EnhanceYourCalm ? StatusCode::ResourceExhausted
                                                 : StatusCode::Internal,
                       "stream reset by the server (HTTP/2 error " + std::to_string(code) + ")", {}});
    }
    void onGoaway(uint32_t lastSid, uint32_t code, const std::string &debug) override {
        lastError_ = "GOAWAY " + std::to_string(code) + (debug.empty() ? "" : ": " + debug);
        for (auto &[sid, c] : live_)
            if (sid > lastSid)
                close(*c, {StatusCode::Unavailable, "server is going away (" + lastError_ + ")", {}});
    }

    ChannelConfig cfg_;
    Target target_;
    uint64_t now_ = 0;
    bool conn_ = false;
    bool started_ = false;
    std::unique_ptr<ByteStream> stream_;
    std::unique_ptr<h2::Connection> h2_;
    std::vector<std::shared_ptr<H2Call>> waiting_;
    std::map<uint32_t, std::shared_ptr<H2Call>> live_;
    bool dropping_ = false;
    std::string lastError_;
};

H2Stream::~H2Stream() { t_->cancel(c_); }
void H2Stream::send(std::string_view message) { t_->send(*c_, message); }
void H2Stream::halfClose() { c_->halfClose = true; }

} // namespace

std::unique_ptr<Transport> makeHttp2Transport(const ChannelConfig &cfg, const Target &target) {
    return std::make_unique<H2Transport>(cfg, target);
}

} // namespace aiverse::grpc::detail
#endif
