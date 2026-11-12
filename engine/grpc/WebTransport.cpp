// Browser transport (Emscripten): gRPC-Web unary calls through fetch() and streams through the
// browser WebSocket with the cs-grpc-bridge-v1 sub-protocol. TLS, HTTP/2 and connection reuse are the
// browser's. Results arrive from the browser event loop and are queued until Transport::poll().
// The server must allow CORS for the request headers and expose grpc-status / grpc-message.
// [unverified: emcc not available in the VM]
#ifdef __EMSCRIPTEN__
#include "Tls.h"
#include "Transport.h"
#include <emscripten/fetch.h>
#include <emscripten/websocket.h>
#include <algorithm>
#include <cstring>
#include <map>

namespace aiverse::grpc::detail {
namespace {

struct BrowserCall {
    uint64_t id = 0;
    StreamSink *sink = nullptr;
    StreamRequest req;
    bool ws = false;
    bool started = false;
    bool closed = false;
    bool halfClose = false;
    std::string body;               // unary request (framed)
    emscripten_fetch_t *fetch = nullptr;
    // fetch result
    bool fetchDone = false;
    int httpStatus = 0;
    std::string respHeaders, respBody, fetchError;
    // websocket
    EMSCRIPTEN_WEBSOCKET_T sock = 0;
    bool open = false;
    std::vector<std::string> outbox; // binary messages queued before onopen
    std::string inbound;            // received bytes not yet processed
    bool peerClosed = false;
    bool headersSeen = false;
    Deframer deframer;
};

std::map<uint64_t, std::shared_ptr<BrowserCall>> &registry() {
    static std::map<uint64_t, std::shared_ptr<BrowserCall>> r;
    return r;
}
uint64_t g_nextId = 1;

std::shared_ptr<BrowserCall> lookup(void *user) {
    auto it = registry().find(uint64_t(uintptr_t(user)));
    return it == registry().end() ? nullptr : it->second;
}

void onFetchDone(emscripten_fetch_t *f) {
    auto c = lookup(f->userData);
    if (!c)
        return; // cancelled: BrowserStream already closed the fetch
    {
        c->fetchDone = true;
        c->httpStatus = f->status;
        c->respBody.assign(f->data ? f->data : "", size_t(f->numBytes));
        const size_t n = emscripten_fetch_get_response_headers_length(f);
        std::string h(n + 1, '\0');
        emscripten_fetch_get_response_headers(f, h.data(), n + 1);
        h.resize(n);
        c->respHeaders = std::move(h);
        if (f->status == 0)
            c->fetchError = "network error (CORS or connection refused)";
        c->fetch = nullptr;
    }
    emscripten_fetch_close(f);
}

EM_BOOL onWsOpen(int, const EmscriptenWebSocketOpenEvent *, void *user) {
    if (auto c = lookup(user))
        c->open = true;
    return EM_TRUE;
}
EM_BOOL onWsMessage(int, const EmscriptenWebSocketMessageEvent *e, void *user) {
    if (auto c = lookup(user))
        if (!e->isText)
            c->inbound.append(reinterpret_cast<const char *>(e->data), e->numBytes);
    return EM_TRUE;
}
EM_BOOL onWsClose(int, const EmscriptenWebSocketCloseEvent *, void *user) {
    if (auto c = lookup(user))
        c->peerClosed = true;
    return EM_TRUE;
}
EM_BOOL onWsError(int, const EmscriptenWebSocketErrorEvent *, void *user) {
    if (auto c = lookup(user))
        c->peerClosed = true;
    return EM_TRUE;
}

class BrowserStream final : public TransportStream {
  public:
    explicit BrowserStream(std::shared_ptr<BrowserCall> c) : c_(std::move(c)) {}
    ~BrowserStream() override {
        c_->sink = nullptr;
        if (c_->fetch) {
            emscripten_fetch_close(c_->fetch); // aborts
            c_->fetch = nullptr;
        }
        if (c_->sock) {
            emscripten_websocket_close(c_->sock, 1000, "");
            emscripten_websocket_delete(c_->sock);
            c_->sock = 0;
        }
        c_->closed = true;
        registry().erase(c_->id);
    }
    void send(std::string_view m) override {
        if (c_->closed)
            return;
        if (!c_->ws) {
            frameMessage(c_->body, m);
            return;
        }
        std::string framed;
        frameMessage(framed, m);
        c_->outbox.push_back(std::move(framed));
    }
    void halfClose() override {
        if (c_->closed || c_->halfClose)
            return;
        c_->halfClose = true;
        if (c_->ws)
            c_->outbox.push_back(std::string("\x80\0\0\0\0", 5));
    }

  private:
    std::shared_ptr<BrowserCall> c_;
};

class BrowserTransport final : public Transport {
  public:
    BrowserTransport(const ChannelConfig &cfg, const Target &t) : cfg_(cfg), target_(t) {}

    std::unique_ptr<TransportStream> open(const StreamRequest &req, StreamSink *sink) override {
        auto c = std::make_shared<BrowserCall>();
        c->id = g_nextId++;
        c->sink = sink;
        c->req = req;
        c->ws = !req.unary || cfg_.webSocketForUnary;
        registry()[c->id] = c;
        calls_.push_back(c);
        return std::make_unique<BrowserStream>(c);
    }

    void poll(uint64_t) override {
        auto calls = calls_;
        for (auto &c : calls)
            if (!c->closed && c->sink)
                c->ws ? pollWs(*c) : pollFetch(*c);
        calls_.erase(std::remove_if(calls_.begin(), calls_.end(), [](const auto &c) { return c->closed || !c->sink; }),
                     calls_.end());
    }
    std::string lastError() const override { return lastError_; }

  private:
    std::string url(bool ws) const {
        const char *scheme = ws ? (target_.tls ? "wss://" : "ws://") : (target_.tls ? "https://" : "http://");
        return scheme + target_.authority();
    }
    void finish(BrowserCall &c, Status s) {
        if (c.closed)
            return;
        c.closed = true;
        if (!s.ok())
            lastError_ = s.message;
        if (c.sink)
            c.sink->onClose(std::move(s));
    }

    void pollFetch(BrowserCall &c) {
        if (!c.started && c.halfClose) {
            c.started = true;
            emscripten_fetch_attr_t a;
            emscripten_fetch_attr_init(&a);
            std::strcpy(a.requestMethod, "POST");
            a.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
            a.onsuccess = onFetchDone;
            a.onerror = onFetchDone;
            a.userData = reinterpret_cast<void *>(uintptr_t(c.id));
            headerStore_.clear();
            headerStore_ = {"Content-Type", "application/grpc-web", "Accept", "application/grpc-web", "X-Grpc-Web", "1"};
            for (const auto &[k, v] : c.req.headers) {
                headerStore_.push_back(k);
                headerStore_.push_back(v);
            }
            std::vector<const char *> ptrs;
            for (const auto &s : headerStore_)
                ptrs.push_back(s.c_str());
            ptrs.push_back(nullptr);
            a.requestHeaders = ptrs.data();
            a.requestData = c.body.data();
            a.requestDataSize = c.body.size();
            const std::string u = url(false) + c.req.path;
            c.fetch = emscripten_fetch(&a, u.c_str()); // copies headers and body
            return;
        }
        if (!c.fetchDone)
            return;
        if (!c.fetchError.empty())
            return finish(c, {StatusCode::Unavailable, c.fetchError, {}});
        Metadata headers = parseHeaderBlock(c.respHeaders);
        decodeBinaryValues(headers);
        Status fromHeaders;
        const bool trailersOnly = statusFromMetadata(headers, fromHeaders);
        if (c.httpStatus != 200)
            return finish(c, trailersOnly ? fromHeaders : Status{statusFromHttp(c.httpStatus), "HTTP " + std::to_string(c.httpStatus), {}});
        if (c.sink)
            c.sink->onHeaders(headers);
        Deframer d;
        d.feed(c.respBody);
        std::string msg, err;
        for (;;) {
            const auto r = d.next(msg, &err);
            if (r == Deframer::Result::Message) {
                if (c.sink)
                    c.sink->onMessage(std::move(msg));
                continue;
            }
            if (r == Deframer::Result::Block) {
                Metadata t = parseHeaderBlock(msg);
                decodeBinaryValues(t);
                Status s;
                return finish(c, statusFromMetadata(t, s) ? s : Status{StatusCode::Internal, "trailers without grpc-status", {}});
            }
            if (r == Deframer::Result::Error)
                return finish(c, {StatusCode::Internal, err, {}});
            break;
        }
        finish(c, trailersOnly ? fromHeaders : Status{StatusCode::Internal, "response without grpc-status", {}});
    }

    void pollWs(BrowserCall &c) {
        if (!c.started) {
            c.started = true;
            if (!emscripten_websocket_is_supported())
                return finish(c, {StatusCode::Unavailable, "WebSocket unsupported", {}});
            EmscriptenWebSocketCreateAttributes a;
            emscripten_websocket_init_create_attributes(&a);
            const std::string u = url(true) + c.req.path;
            a.url = u.c_str();
            a.protocols = "cs-grpc-bridge-v1";
            a.createOnMainThread = EM_TRUE;
            c.sock = emscripten_websocket_new(&a);
            if (c.sock <= 0)
                return finish(c, {StatusCode::Unavailable, "cannot create WebSocket", {}});
            void *user = reinterpret_cast<void *>(uintptr_t(c.id));
            emscripten_websocket_set_onopen_callback(c.sock, user, onWsOpen);
            emscripten_websocket_set_onmessage_callback(c.sock, user, onWsMessage);
            emscripten_websocket_set_onclose_callback(c.sock, user, onWsClose);
            emscripten_websocket_set_onerror_callback(c.sock, user, onWsError);
            std::string block, first;
            writeHeaderBlock(c.req.headers, block);
            frameMessage(first, block, 0x80);
            c.outbox.insert(c.outbox.begin(), std::move(first));
            return;
        }
        if (c.open) {
            for (auto &m : c.outbox)
                emscripten_websocket_send_binary(c.sock, m.data(), uint32_t(m.size()));
            c.outbox.clear();
        }
        if (!c.inbound.empty()) {
            c.deframer.feed(c.inbound);
            c.inbound.clear();
            std::string msg, err;
            for (;;) {
                const auto r = c.deframer.next(msg, &err);
                if (r == Deframer::Result::NeedMore)
                    break;
                if (r == Deframer::Result::Error)
                    return finish(c, {StatusCode::Internal, err, {}});
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
                return finish(c, hasStatus ? s : Status{StatusCode::Internal, "trailers without grpc-status", {}});
            }
        }
        if (c.peerClosed)
            finish(c, {StatusCode::Unavailable, "WebSocket closed before trailers", {}});
    }

    ChannelConfig cfg_;
    Target target_;
    std::vector<std::shared_ptr<BrowserCall>> calls_;
    std::vector<std::string> headerStore_;
    std::string lastError_;
};

} // namespace

std::unique_ptr<Transport> makeGrpcWebTransport(const ChannelConfig &cfg, const Target &target) {
    return std::make_unique<BrowserTransport>(cfg, target);
}
std::unique_ptr<Transport> makeHttp2Transport(const ChannelConfig &cfg, const Target &target) {
    return makeGrpcWebTransport(cfg, target);
}

const char *tlsBackendName() { return "browser"; }
bool tlsAvailable() { return true; }
std::unique_ptr<TlsEngine> createTlsClient(const TlsConfig &, const std::string &, const std::vector<std::string> &,
                                           std::string *error) {
    if (error)
        *error = "the browser owns TLS";
    return nullptr;
}
std::unique_ptr<TlsEngine> createTlsServer(const std::string &, const std::string &, const std::vector<std::string> &,
                                           std::string *error) {
    if (error)
        *error = "the browser owns TLS";
    return nullptr;
}

} // namespace aiverse::grpc::detail
#endif
