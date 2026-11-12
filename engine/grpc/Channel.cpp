#include "Transport.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>

namespace aiverse::grpc {
namespace detail {

struct CallCore : StreamSink {
    enum class Phase { Queued, Backoff, Active, Done };
    struct Event {
        enum Kind { Headers, Message, Close } kind;
        Metadata headers;
        std::string payload;
        Status status;
    };

    Channel *channel = nullptr;
    uint64_t id = 0;
    MethodInfo method{};
    CallOptions options;
    bool unary = false;
    Phase phase = Phase::Queued;
    uint64_t deadline = 0; // absolute ms, 0 = none
    uint64_t retryAt = 0;
    int attempt = 0;
    uint64_t ticket = 0;   // CallGate FIFO ticket while waiting
    bool holdsGate = false;

    std::string request;   // unary
    std::vector<std::string> outbox;
    bool halfClose = false;
    bool halfCloseSent = false;
    bool sawHeaders = false;
    bool sawMessage = false;
    std::string response;
    // Final status. Messages that arrived before the close are still delivered first, unless the
    // client cancelled / hit the deadline / failed to parse (suppress).
    Status final;
    bool suppress = false;
    bool finishDelivered = false;

    std::function<void(const Status &, std::string)> unaryDone;
    RawStreamHandlers handlers;
    std::unique_ptr<TransportStream> ts;
    std::vector<Event> events;

    void onHeaders(Metadata h) override { events.push_back({Event::Headers, std::move(h), {}, {}}); }
    void onMessage(std::string p) override { events.push_back({Event::Message, {}, std::move(p), {}}); }
    void onClose(Status s) override { events.push_back({Event::Close, {}, {}, std::move(s)}); }
};

} // namespace detail

using detail::CallCore;

void Call::cancel() {
    auto c = core_.lock();
    if (!c || c->phase == CallCore::Phase::Done || !c->channel)
        return;
    c->ts.reset(); // cancels on the wire (RST_STREAM / WebSocket close / connection close)
    c->suppress = true;
    c->channel->finish(c, {StatusCode::Cancelled,
                           c->phase == CallCore::Phase::Active ? "cancelled by the client"
                                                               : "RPC cancelled before completion",
                           {}});
}

bool Call::active() const {
    auto c = core_.lock();
    return c && c->phase != CallCore::Phase::Done;
}

uint64_t Call::id() const {
    auto c = core_.lock();
    return c ? c->id : 0;
}

Channel::Channel(ChannelConfig config) : config_(std::move(config)) {
    Target t;
    if (parseTarget(config_.target, t)) {
#ifdef __EMSCRIPTEN__
        // Browsers expose neither raw HTTP/2 frames nor trailers to scripts: gRPC-Web only.
        transport_ = detail::makeGrpcWebTransport(config_, t);
#else
        transport_ = config_.protocol == Protocol::GrpcWeb ? detail::makeGrpcWebTransport(config_, t)
                                                           : detail::makeHttp2Transport(config_, t);
#endif
    }
}

Channel::~Channel() {
    for (auto &c : calls_) {
        c->ts.reset();
        c->channel = nullptr;
        c->phase = CallCore::Phase::Done;
        if (c->holdsGate && config_.gate)
            --config_.gate->running_;
        if (c->ticket && config_.gate)
            config_.gate->waiting_.erase(c->ticket);
    }
}

uint64_t Channel::now() const {
    if (config_.nowMs)
        return config_.nowMs();
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string Channel::lastTransportError() const {
    return transport_ ? transport_->lastError() : "invalid target '" + config_.target + "'";
}

size_t Channel::activeCalls() const {
    return size_t(std::count_if(calls_.begin(), calls_.end(),
                                [](const auto &c) { return c->phase != CallCore::Phase::Done; }));
}

Call Channel::unary(const MethodInfo &m, std::string request, CallOptions options,
                    std::function<void(const Status &, std::string)> done) {
    auto c = std::make_shared<CallCore>();
    c->channel = this;
    c->id = nextId_++;
    c->method = m;
    c->unary = true;
    c->options = std::move(options);
    c->request = std::move(request);
    c->unaryDone = std::move(done);
    const uint64_t t = now();
    c->deadline = c->options.timeoutMs ? t + c->options.timeoutMs : 0;
    if (config_.gate && c->options.throttle) {
        c->ticket = config_.gate->nextTicket_++;
        config_.gate->waiting_.insert(c->ticket);
    }
    calls_.push_back(c);
    if (!transport_)
        finish(c, {StatusCode::InvalidArgument, "invalid target '" + config_.target + "'", {}});
    return Call(c);
}

std::shared_ptr<CallCore> Channel::stream(const MethodInfo &m, CallOptions options, RawStreamHandlers handlers) {
    auto c = std::make_shared<CallCore>();
    c->channel = this;
    c->id = nextId_++;
    c->method = m;
    c->options = std::move(options);
    c->handlers = std::move(handlers);
    const uint64_t t = now();
    c->deadline = c->options.timeoutMs ? t + c->options.timeoutMs : 0;
    calls_.push_back(c);
    if (!transport_)
        finish(c, {StatusCode::InvalidArgument, "invalid target '" + config_.target + "'", {}});
    return c;
}

bool Channel::streamWrite(const std::shared_ptr<CallCore> &c, std::string message) {
    if (!c || c->phase == CallCore::Phase::Done || c->halfClose || c->channel != this)
        return false;
    c->outbox.push_back(std::move(message));
    return true;
}

void Channel::streamWritesDone(const std::shared_ptr<CallCore> &c) {
    if (c && c->phase != CallCore::Phase::Done && c->channel == this)
        c->halfClose = true;
}

void Channel::cancelAll() {
    auto calls = calls_;
    for (auto &c : calls)
        Call(c).cancel();
}

bool Channel::gateAdmits(CallCore &c) {
    if (!c.ticket)
        return true;
    CallGate &g = *config_.gate;
    return g.running_ < g.limit_ && !g.waiting_.empty() && *g.waiting_.begin() == c.ticket;
}

void Channel::releaseGate(CallCore &c) {
    if (c.ticket && config_.gate) {
        config_.gate->waiting_.erase(c.ticket);
        c.ticket = 0;
    }
    if (c.holdsGate && config_.gate) {
        --config_.gate->running_;
        c.holdsGate = false;
    }
}

Metadata Channel::requestHeaders(const CallCore &c, uint64_t t) const {
    Metadata md;
    for (const auto &[k, v] : c.options.metadata) {
        std::string key = k;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
        if (!detail::isReservedHeader(key))
            md.emplace_back(key, detail::wireValue(key, v));
    }
    if (config_.authToken) {
        const std::string token = config_.authToken();
        if (!token.empty())
            md.emplace_back("authorization", "Bearer " + token);
    }
    if (c.deadline)
        md.emplace_back("grpc-timeout", encodeTimeout(c.deadline > t ? c.deadline - t : 1));
    return md;
}

void Channel::start(const std::shared_ptr<CallCore> &c, uint64_t t) {
    if (c->ticket) {
        config_.gate->waiting_.erase(c->ticket);
        c->ticket = 0;
        c->holdsGate = true;
        ++config_.gate->running_;
    }
    ++c->attempt;
    c->phase = CallCore::Phase::Active;
    c->sawHeaders = c->sawMessage = false;
    c->response.clear();
    c->events.clear();
    detail::StreamRequest req;
    Target target;
    parseTarget(config_.target, target);
    req.path = target.pathPrefix + c->method.path;
    req.headers = requestHeaders(*c, t);
    req.unary = c->unary;
    c->ts = transport_->open(req, c.get());
    if (c->unary && c->ts) {
        c->ts->send(c->request);
        c->ts->halfClose();
    }
}

void Channel::finish(const std::shared_ptr<CallCore> &c, Status s) {
    if (c->phase == CallCore::Phase::Done)
        return;
    c->phase = CallCore::Phase::Done;
    c->ts.reset();
    releaseGate(*c);
    c->outbox.clear();
    c->final = std::move(s);
    ready_.push_back([c] {
        if (c->finishDelivered)
            return;
        c->finishDelivered = true;
        if (c->unary) {
            auto done = std::move(c->unaryDone);
            if (done)
                done(c->final, c->final.ok() ? std::move(c->response) : std::string());
        } else {
            auto fin = std::move(c->handlers.onFinish);
            if (fin)
                fin(c->final);
        }
    });
}

void Channel::abort(const std::shared_ptr<CallCore> &c, Status s) {
    c->suppress = true;
    if (c->phase != CallCore::Phase::Done)
        finish(c, std::move(s));
    else if (!c->finishDelivered)
        c->final = std::move(s); // the close was already queued: report the abort instead
}

void Channel::poll() {
    if (polling_)
        return; // re-entrant poll from a callback: ignored
    polling_ = true;
    const uint64_t t = now();

    // Deadlines, backoff expiry, gate admission, outgoing stream writes.
    for (size_t i = 0; i < calls_.size(); ++i) {
        auto c = calls_[i];
        if (c->phase == CallCore::Phase::Done)
            continue;
        if (c->deadline && t >= c->deadline) {
            c->suppress = true;
            finish(c, {StatusCode::DeadlineExceeded, "deadline exceeded", {}});
            continue;
        }
        if (c->phase == CallCore::Phase::Backoff && t >= c->retryAt) {
            c->phase = CallCore::Phase::Queued;
            if (config_.gate && c->options.throttle) {
                c->ticket = config_.gate->nextTicket_++;
                config_.gate->waiting_.insert(c->ticket);
            }
        }
        if (c->phase == CallCore::Phase::Queued && (!c->unary || gateAdmits(*c)))
            start(c, t);
        if (c->phase == CallCore::Phase::Active && c->ts && !c->unary) {
            for (auto &m : c->outbox)
                c->ts->send(m);
            c->outbox.clear();
            if (c->halfClose && !c->halfCloseSent) {
                c->ts->halfClose();
                c->halfCloseSent = true;
            }
        }
    }

    if (transport_)
        transport_->poll(t);

    // Transport events -> state changes + deferred user callbacks, in call order.
    for (size_t i = 0; i < calls_.size(); ++i) {
        auto c = calls_[i];
        auto events = std::move(c->events);
        c->events.clear();
        for (auto &e : events) {
            if (c->phase != CallCore::Phase::Active)
                break;
            if (e.kind == CallCore::Event::Headers) {
                c->sawHeaders = true;
                if (!c->unary && c->handlers.onHeaders) {
                    std::weak_ptr<CallCore> w = c;
                    ready_.push_back([w, h = std::move(e.headers)] {
                        auto cc = w.lock();
                        if (cc && !cc->suppress && !cc->finishDelivered && cc->handlers.onHeaders)
                            cc->handlers.onHeaders(h);
                    });
                }
            } else if (e.kind == CallCore::Event::Message) {
                if (c->unary) {
                    if (c->sawMessage) {
                        finish(c, {StatusCode::Internal, "unary call returned more than one message", {}});
                        break;
                    }
                    c->sawMessage = true;
                    c->response = std::move(e.payload);
                } else {
                    c->sawMessage = true;
                    std::weak_ptr<CallCore> w = c;
                    ready_.push_back([this, w, p = std::move(e.payload)]() mutable {
                        auto cc = w.lock();
                        if (!cc || cc->suppress || cc->finishDelivered || !cc->handlers.onMessage)
                            return;
                        if (!cc->handlers.onMessage(std::move(p)))
                            abort(cc, {StatusCode::Internal, "failed to parse response message", {}});
                    });
                }
            } else {
                Status s = std::move(e.status);
                if (c->unary && s.ok() && !c->sawMessage)
                    s = {StatusCode::Internal, "unary call finished without a response message", s.trailers};
                const RetryPolicy &rp = config_.retry;
                const bool retryable =
                    c->unary && c->options.retry && !c->sawMessage && c->attempt < rp.maxAttempts &&
                    std::find(rp.retryOn.begin(), rp.retryOn.end(), s.code) != rp.retryOn.end();
                int64_t pushback = -1;
                if (const std::string *pb = findMetadata(s.trailers, "grpc-retry-pushback-ms")) {
                    char *end = nullptr;
                    const long long v = std::strtoll(pb->c_str(), &end, 10);
                    pushback = (!pb->empty() && !*end && v >= 0) ? v : -2; // -2: server says don't retry
                }
                if (retryable && pushback != -2) {
                    uint64_t backoff = rp.initialBackoffMs;
                    for (int a = 1; a < c->attempt; ++a)
                        backoff = std::min<uint64_t>(rp.maxBackoffMs, backoff * rp.multiplierPercent / 100);
                    if (pushback >= 0)
                        backoff = uint64_t(pushback);
                    if (!c->deadline || t + backoff < c->deadline) {
                        c->ts.reset();
                        releaseGate(*c);
                        c->phase = CallCore::Phase::Backoff;
                        c->retryAt = t + backoff;
                        break;
                    }
                }
                finish(c, std::move(s));
                break;
            }
        }
    }

    // Run user callbacks; they may start/cancel calls (which only queue more work).
    for (size_t i = 0; i < ready_.size(); ++i) {
        auto fn = std::move(ready_[i]);
        fn();
    }
    ready_.clear();
    calls_.erase(std::remove_if(calls_.begin(), calls_.end(),
                                [](const auto &c) { return c->phase == CallCore::Phase::Done; }),
                 calls_.end());
    polling_ = false;
}

} // namespace aiverse::grpc
