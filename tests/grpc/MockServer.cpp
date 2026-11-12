#include "MockServer.h"
#include "engine/grpc/ByteStream.h"
#include "engine/grpc/Http1.h"
#include "engine/grpc/Http2.h"
#include "engine/grpc/Transport.h"
#include "engine/net/Socket.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace aiverse::grpc::test {

using namespace aiverse::grpc::detail;

namespace {
// Test-only self-signed certificate for localhost / 127.0.0.1 (valid until 2126). Not a secret.
const char *kCert = R"(-----BEGIN CERTIFICATE-----
MIIBmjCCAUGgAwIBAgIUUojJBwHiJQR1ZAniXGzEdUPY2jkwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MTExMjEyMzAzMFoYDzIxMjYxMDE5
MTIzMDMwWjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjO
PQMBBwNCAAQJD25kpMtZ6QvuzCOMNSCvYstI3qdAG+C3iVCuYH7W3lO3t/ZEWssw
EfFqRwODL5N8kPzWMxmeow9pHAxTt8+Io28wbTAdBgNVHQ4EFgQUJMZZOcGhn+tf
peidHrvNLpmmsHEwHwYDVR0jBBgwFoAUJMZZOcGhn+tfpeidHrvNLpmmsHEwDwYD
VR0TAQH/BAUwAwEB/zAaBgNVHREEEzARgglsb2NhbGhvc3SHBH8AAAEwCgYIKoZI
zj0EAwIDRwAwRAIgXEeAq3vto1VW+P6OutHRBBQDz0L8fet+VOFRUO8+HCECIHNN
2gAeOZ/o8/x0Iwt4Z30XZ59N/6c6j+fZdVlvVqZj
-----END CERTIFICATE-----
)";
const char *kKey = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQglHK8mr4qJVp8L7pv
OMHJoScgLgmzrYvkPdf7ckIqQryhRANCAAQJD25kpMtZ6QvuzCOMNSCvYstI3qdA
G+C3iVCuYH7W3lO3t/ZEWsswEfFqRwODL5N8kPzWMxmeow9pHAxTt8+I
-----END PRIVATE KEY-----
)";

uint64_t nowMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

Metadata trailerList(const Status &s) {
    Metadata md{{"grpc-status", std::to_string(int(s.code))}};
    if (!s.message.empty())
        md.emplace_back("grpc-message", percentEncode(s.message));
    for (const auto &kv : s.trailers)
        md.emplace_back(kv.first, wireValue(kv.first, kv.second));
    return md;
}
} // namespace

void ServerCall::sendHeaders(Metadata md) {
    if (out.headersQueued || finished)
        return;
    out.headersQueued = true;
    out.headers = std::move(md);
}
void ServerCall::send(std::string message) {
    if (finished)
        return;
    sendHeaders();
    out.messages.push_back(std::move(message));
}
void ServerCall::finish(StatusCode code, std::string message, Metadata trailers) {
    if (finished)
        return;
    finished = true;
    out.finishQueued = true;
    out.status = {code, std::move(message), std::move(trailers)};
}

const char *MockServer::testCaPem() { return kCert; }

struct MockServer::Conn : h2::Connection::Handler {
    MockServer *srv = nullptr;
    std::unique_ptr<ByteStream> bs;
    bool dead = false;
    // HTTP/2
    std::unique_ptr<h2::Connection> h2;
    std::map<uint32_t, std::shared_ptr<ServerCall>> streams;
    std::map<uint32_t, Deframer> deframers;
    std::string out;
    // gRPC-Web
    http1::Parser parser{false};
    bool ws = false;
    std::shared_ptr<ServerCall> call; // current unary or websocket call
    bool wsHeaders = false;
    bool wsClosedSent = false;
    std::string fragment;
    Deframer wsDeframer;

    void start(std::shared_ptr<ServerCall> c) {
        srv->calls_.push_back(c);
        ++srv->started_;
        srv->dispatch(*c, ServerCall::Event::Start);
    }

    // --- HTTP/2 handler ---
    void onHeaders(uint32_t sid, hpack::HeaderList headers, bool endStream) override {
        auto c = std::make_shared<ServerCall>();
        c->id = srv->nextId_++;
        for (auto &[k, v] : headers) {
            if (k == ":path")
                c->path = v;
            else if (!k.empty() && k[0] != ':')
                c->headers.emplace_back(k, v);
        }
        decodeBinaryValues(c->headers);
        streams[sid] = c;
        start(c);
        if (endStream && !c->halfClosed) {
            c->halfClosed = true;
            srv->dispatch(*c, ServerCall::Event::HalfClose);
        }
    }
    void onData(uint32_t sid, std::string_view data, bool endStream) override {
        auto it = streams.find(sid);
        if (it == streams.end())
            return;
        auto c = it->second;
        Deframer &d = deframers[sid];
        d.feed(data);
        std::string m;
        while (d.next(m, nullptr) == Deframer::Result::Message) {
            c->received.push_back(m);
            srv->dispatch(*c, ServerCall::Event::Message);
        }
        if (endStream && !c->halfClosed) {
            c->halfClosed = true;
            srv->dispatch(*c, ServerCall::Event::HalfClose);
        }
    }
    void onReset(uint32_t sid, uint32_t) override {
        auto it = streams.find(sid);
        if (it == streams.end())
            return;
        auto c = it->second;
        streams.erase(it);
        if (!c->finished) {
            c->cancelled = true;
            srv->dispatch(*c, ServerCall::Event::Cancel);
        }
    }
    void onGoaway(uint32_t, uint32_t, const std::string &) override {}

    void flushH2() {
        for (auto it = streams.begin(); it != streams.end();) {
            auto &c = *it->second;
            const uint32_t sid = it->first;
            if (c.out.headersQueued && !c.out.headersSent) {
                hpack::HeaderList h{{":status", "200"}, {"content-type", "application/grpc"}};
                for (const auto &kv : c.out.headers)
                    h.emplace_back(kv.first, wireValue(kv.first, kv.second));
                h2->sendHeaders(sid, h, false, out);
                c.out.headersSent = true;
            }
            for (auto &m : c.out.messages) {
                std::string framed;
                frameMessage(framed, m);
                h2->sendData(sid, framed, false, out);
            }
            c.out.messages.clear();
            if (c.out.finishQueued && !h2->streamPending(sid)) {
                hpack::HeaderList h;
                if (!c.out.headersSent)
                    h = {{":status", "200"}, {"content-type", "application/grpc"}};
                for (auto &kv : trailerList(c.out.status))
                    h.push_back(kv);
                h2->sendHeaders(sid, h, true, out);
                if (!c.halfClosed)
                    h2->reset(sid, h2::NoError, out); // stop reading the client's stream
                c.out.finishQueued = false;
                it = streams.erase(it);
                continue;
            }
            ++it;
        }
    }

    // --- gRPC-Web / bridge ---
    void pollWeb() {
        if (!ws) {
            if (call) { // unary waiting for the handler
                if (call->out.finishQueued)
                    respondUnary();
                return;
            }
            const auto r = parser.feed(bs->inbox());
            if (r == http1::Parser::Result::Error) {
                dead = true;
                return;
            }
            if (r == http1::Parser::Result::NeedMore)
                return;
            auto &m = parser.message();
            const std::string *upgrade = m.header("upgrade");
            if (upgrade && (*upgrade == "websocket" || *upgrade == "WebSocket")) {
                const std::string *key = m.header("sec-websocket-key");
                const std::string *proto = m.header("sec-websocket-protocol");
                if (!key || !proto || proto->find("cs-grpc-bridge-v1") == std::string::npos) {
                    bs->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
                    dead = true;
                    return;
                }
                bs->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: " + ws::acceptFor(*key) +
                          "\r\nSec-WebSocket-Protocol: cs-grpc-bridge-v1\r\n\r\n");
                ws = true;
                call = std::make_shared<ServerCall>();
                call->id = srv->nextId_++;
                call->path = m.target;
                return;
            }
            const std::string *ct = m.header("content-type");
            if (m.method != "POST" || !ct || ct->rfind("application/grpc-web", 0) != 0) {
                bs->write("HTTP/1.1 415 Unsupported Media Type\r\nContent-Length: 0\r\n\r\n");
                parser = http1::Parser(false);
                return;
            }
            call = std::make_shared<ServerCall>();
            call->id = srv->nextId_++;
            call->path = m.target;
            for (const auto &kv : m.headers)
                if (kv.first != "content-length" && kv.first != "host" && kv.first != "content-type")
                    call->headers.push_back(kv);
            decodeBinaryValues(call->headers);
            Deframer d;
            d.feed(m.body);
            std::string msg;
            start(call);
            while (d.next(msg, nullptr) == Deframer::Result::Message) {
                call->received.push_back(msg);
                srv->dispatch(*call, ServerCall::Event::Message);
            }
            call->halfClosed = true;
            srv->dispatch(*call, ServerCall::Event::HalfClose);
            if (call->out.finishQueued)
                respondUnary();
            return;
        }
        // WebSocket bridge
        ws::Frame f;
        for (;;) {
            const int r = ws::readFrame(bs->inbox(), f, true);
            if (r < 0) {
                dead = true;
                return;
            }
            if (r == 0)
                break;
            if (f.opcode == ws::Close) {
                if (!wsClosedSent) {
                    std::string o;
                    ws::writeFrame(o, ws::Close, f.payload.substr(0, 2), false);
                    bs->write(o);
                }
                if (call && !call->finished) {
                    call->cancelled = true;
                    srv->dispatch(*call, ServerCall::Event::Cancel);
                }
                dead = true;
                return;
            }
            if (f.opcode != ws::Binary && f.opcode != ws::Continuation)
                continue;
            fragment += f.payload;
            if (!f.fin)
                continue;
            wsDeframer.feed(fragment);
            fragment.clear();
            std::string msg;
            for (;;) {
                const auto dr = wsDeframer.next(msg, nullptr);
                if (dr == Deframer::Result::NeedMore || dr == Deframer::Result::Error)
                    break;
                if (dr == Deframer::Result::Block) {
                    if (!wsHeaders) {
                        wsHeaders = true;
                        call->headers = parseHeaderBlock(msg);
                        decodeBinaryValues(call->headers);
                        start(call);
                    } else if (!call->halfClosed) {
                        call->halfClosed = true;
                        srv->dispatch(*call, ServerCall::Event::HalfClose);
                    }
                    continue;
                }
                call->received.push_back(msg);
                srv->dispatch(*call, ServerCall::Event::Message);
            }
        }
        flushWs();
    }

    void flushWs() {
        if (!call || !wsHeaders)
            return;
        auto &o = call->out;
        std::string wire;
        auto block = [&](const Metadata &md) {
            std::string text, framed;
            writeHeaderBlock(md, text);
            frameMessage(framed, text, 0x80);
            ws::writeFrame(wire, ws::Binary, framed, false);
        };
        if (o.headersQueued && !o.headersSent) {
            block(o.headers);
            o.headersSent = true;
        }
        for (auto &m : o.messages) {
            std::string framed;
            frameMessage(framed, m);
            // Split each message over two WebSocket messages to exercise reassembly.
            const size_t half = framed.size() / 2;
            ws::writeFrame(wire, ws::Binary, std::string_view(framed).substr(0, half), false);
            ws::writeFrame(wire, ws::Binary, std::string_view(framed).substr(half), false);
        }
        o.messages.clear();
        if (o.finishQueued) {
            block(trailerList(o.status));
            o.finishQueued = false;
        }
        if (!wire.empty())
            bs->write(wire);
    }

    void respondUnary() {
        auto &o = call->out;
        std::string body;
        for (auto &m : o.messages)
            frameMessage(body, m);
        std::string head = "HTTP/1.1 200 OK\r\nContent-Type: application/grpc-web+proto\r\n";
        for (const auto &kv : o.headers)
            head += kv.first + ": " + wireValue(kv.first, kv.second) + "\r\n";
        if (o.messages.empty() && !o.headersQueued) {
            for (const auto &kv : trailerList(o.status)) // trailers-only: status in headers
                head += kv.first + ": " + kv.second + "\r\n";
        } else {
            std::string text;
            writeHeaderBlock(trailerList(o.status), text);
            frameMessage(body, text, 0x80);
        }
        // Chunked encoding, two chunks, to exercise the client's de-chunker.
        head += "Transfer-Encoding: chunked\r\n\r\n";
        const size_t half = body.size() / 2;
        auto chunk = [&](std::string_view p) {
            char hex[32];
            std::snprintf(hex, sizeof hex, "%zx\r\n", p.size());
            head += hex;
            head.append(p.data(), p.size());
            head += "\r\n";
        };
        if (half)
            chunk(std::string_view(body).substr(0, half));
        if (body.size() - half)
            chunk(std::string_view(body).substr(half));
        head += "0\r\n\r\n";
        bs->write(head);
        o.messages.clear();
        o.finishQueued = false;
        call.reset();
        parser = http1::Parser(false);
    }
};

struct MockServer::Impl {
    net::TcpListener listener;
    std::vector<std::unique_ptr<Conn>> conns;
};

MockServer::MockServer(Mode mode, bool tls) : mode_(mode), tls_(tls), impl_(std::make_unique<Impl>()) {
    if (tls) {
        // The test server side exists only in the OpenSSL backend (Linux).
        std::string err;
        if (!createTlsServer(kCert, kKey, {"h2"}, &err)) {
            error_ = std::string("TLS test server unavailable (") + tlsBackendName() + "): " + err;
            return;
        }
    }
    ok_ = impl_->listener.listen(0, true);
    if (!ok_)
        error_ = "cannot listen on loopback";
}

MockServer::~MockServer() = default;

uint16_t MockServer::port() const { return impl_->listener.localPort(); }

std::string MockServer::url() const {
    return std::string(tls_ ? "https://localhost:" : "http://127.0.0.1:") + std::to_string(port());
}

void MockServer::unary(const std::string &path, UnaryFn fn) {
    handle(path, [fn = std::move(fn)](ServerCall &c, ServerCall::Event e) {
        if (e != ServerCall::Event::HalfClose)
            return;
        auto r = fn(c.headers, c.received.empty() ? std::string() : c.received.front());
        if (!r)
            return;
        if (r->first.ok())
            c.send(r->second);
        c.finish(r->first.code, r->first.message, r->first.trailers);
    });
}

void MockServer::dispatch(ServerCall &c, ServerCall::Event e) {
    auto it = handlers_.find(c.path);
    if (it != handlers_.end()) {
        it->second(c, e);
    } else if (e == ServerCall::Event::Start) {
        c.finish(StatusCode::Unimplemented, "unknown method " + c.path);
    }
}

void MockServer::dropConnections() { impl_->conns.clear(); }

void MockServer::poll() {
    if (!ok_)
        return;
    const uint64_t now = nowMs();
    while (auto s = impl_->listener.accept()) {
        std::unique_ptr<TlsEngine> tls;
        if (tls_) {
            std::string err;
            tls = createTlsServer(kCert, kKey, {mode_ == Mode::Http2 ? "h2" : "http/1.1"}, &err);
            if (!tls) {
                error_ = err;
                continue;
            }
        }
        auto c = std::make_unique<Conn>();
        c->srv = this;
        c->bs = std::make_unique<ByteStream>(std::move(s), std::move(tls));
        impl_->conns.push_back(std::move(c));
        ++accepted_;
    }
    for (auto &c : impl_->conns) {
        const auto st = c->bs->poll(now);
        if (st == ByteStream::State::Failed || st == ByteStream::State::Closed) {
            c->dead = true;
            continue;
        }
        if (st != ByteStream::State::Open)
            continue;
        if (mode_ == Mode::Http2) {
            if (!c->h2) {
                c->h2 = std::make_unique<h2::Connection>(false, c.get());
                c->h2->start(c->out);
            }
            if (!c->h2->receive(c->bs->inbox(), c->out))
                c->dead = true;
            c->flushH2();
            c->h2->flush(c->out);
            if (!c->out.empty()) {
                c->bs->write(c->out);
                c->out.clear();
            }
        } else {
            c->pollWeb();
        }
        c->bs->poll(now);
        if (c->bs->peerClosed())
            c->dead = true;
    }
    for (auto &c : impl_->conns) {
        if (!c->dead)
            continue;
        auto cancel = [&](const std::shared_ptr<ServerCall> &call) {
            if (call && !call->finished && !call->cancelled) {
                call->cancelled = true;
                dispatch(*call, ServerCall::Event::Cancel);
            }
        };
        for (auto &kv : c->streams)
            cancel(kv.second);
        cancel(c->call);
    }
    impl_->conns.erase(std::remove_if(impl_->conns.begin(), impl_->conns.end(), [](const auto &c) { return c->dead; }),
                       impl_->conns.end());
}

} // namespace aiverse::grpc::test
