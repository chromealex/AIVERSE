// engine/grpc tests: protobuf codec (golden bytes from Google protobuf 5.x), HPACK, HTTP/2 and
// gRPC-Web/WebSocket-bridge transports against the in-process MockServer (plain + TLS), deadlines,
// cancellation, throttling, retries, and the Drone Fall meta adapter (ext/ExtMeta).
#include "MockServer.h"
#include "engine/grpc/Hpack.h"
#include "engine/grpc/Tls.h"
#include "test.grpc.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>

using namespace aiverse;
using namespace aiverse::grpc;
using test::MockServer;
using test::ServerCall;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            ++g_failed;                                                                                                \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

static std::string unhex(const char *h) {
    std::string out;
    for (size_t i = 0; h[i] && h[i + 1]; i += 2) {
        auto v = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        out.push_back(char(v(h[i]) * 16 + v(h[i + 1])));
    }
    return out;
}

// Polls client and server until pred() or timeout.
template <class Pred> static bool spin(Channel &ch, MockServer &srv, Pred pred, int timeoutMs = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end) {
        ch.poll();
        srv.poll();
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    return pred();
}

// ---------------------------------------------------------------------------------------------
static void testCodec() {
    using namespace aiverse_test;
    Scalars s;
    s.d = -1.5;
    s.f = 3.25f;
    s.i32 = -7;
    s.i64 = -1234567890123ll;
    s.u32 = 4000000000u;
    s.u64 = 18446744073709551615ull;
    s.s32 = -3;
    s.s64 = -9000000000ll;
    s.fx32 = 0xdeadbeefu;
    s.fx64 = 0x0123456789abcdefull;
    s.sfx32 = -2;
    s.sfx64 = -3;
    s.b = true;
    s.s = "h\xc3\xa9llo";
    s.raw = std::string("\x00\xff", 2);
    s.color = Color::COLOR_BLUE;
    s.maybe = 0; // proto3 optional: presence is encoded even for zero
    s.class_ = 42;
    const std::string gold = unhex("09000000000000f8bf150000504018f9ffffffffffffffff0120b5f693f088dcffffff012880d0acf30e30"
                                   "ffffffffffffffffff01380540ffe78887434defbeadde51efcdab89674523015dfeffffff61fdffffff"
                                   "ffffffff6801720668c3a96c6c6f7a0200ff80010288010090012a");
    CHECK(s.serialize() == gold);
    Scalars back;
    CHECK(back.parse(gold) && back == s && back.maybe && *back.maybe == 0);

    Bag b;
    b.ints = {1, -1, 300};
    b.names = {"a", ""};
    b.counts = {{"x", 1}, {"y", -2}};
    b.colors = {Color::COLOR_RED, Color::COLOR_BLUE};
    b.ds = {0.5};
    b.zz = {-1, 1, -64};
    Item sword;
    sword.name = "sword";
    sword.count = 2;
    sword.tags.push_back({"rare"});
    b.items.push_back(sword);
    b.by_id[7].name = "seven";
    b.best.emplace().name = "best";
    b.set_item().name = "pick";
    b.next.emplace().label = "root";
    b.next->children.emplace_back().label = "kid";
    const std::string bagGold = unhex("0a0d01ffffffffffffffffff01ac0212016112001a110a0573776f726410021a060a04726172652205"
                                      "0a01781001220e0a017910feffffffffffffffff012a0b080712070a05736576656e320201023a08"
                                      "000000000000e03f42060a046265737452060a047069636b620301027f6a0d0a0512036b69641204"
                                      "726f6f74");
    CHECK(b.serialize() == bagGold);
    Bag b2;
    CHECK(b2.parse(bagGold) && b2 == b && b2.has_item() && b2.item.name == "pick");
    Bag e;
    e.set_number() = 0; // a set oneof member is encoded even when zero
    CHECK(e.serialize() == unhex("5800"));

    // Unpacked repeated encoding and unknown fields are accepted.
    Bag u;
    CHECK(u.parse(unhex("0801080208039a0603abcdef")) && u.ints == std::vector<int32_t>({1, 2, 3}));
    // Open enums keep unknown values.
    Scalars ue;
    CHECK(ue.parse(unhex("8001" "07")) && int(ue.color) == 7);
    // Malformed input never crashes and reports failure.
    std::mt19937 rng(1234);
    int rejected = 0;
    for (int i = 0; i < 20000; ++i) {
        std::string junk = bagGold;
        const int edits = 1 + int(rng() % 4);
        for (int k = 0; k < edits; ++k)
            junk[rng() % junk.size()] = char(rng());
        if (rng() % 3 == 0)
            junk.resize(rng() % junk.size());
        Bag j;
        rejected += j.parse(junk) ? 0 : 1;
    }
    CHECK(rejected > 1000);
    Bag truncated;
    CHECK(!truncated.parse(bagGold.substr(0, bagGold.size() - 3)));
    // Deep nesting is bounded (Reader::kMaxDepth = 100).
    auto nest = [](int depth) {
        std::string m;
        for (int i = 0; i < depth; ++i) {
            std::string len;
            pb::Writer(len).rawVarint(m.size());
            m = "\x0a" + len + m; // Bag2.children
        }
        return m;
    };
    Bag2 shallow, tooDeep;
    CHECK(shallow.parse(nest(50)));
    CHECK(!tooDeep.parse(nest(150)));
}

static void testHpack() {
    using namespace aiverse::grpc::hpack;
    // RFC 7541 C.4.1: first request with Huffman coding.
    Decoder dec;
    HeaderList h;
    CHECK(dec.decode(unhex("828684418cf1e3c2e5f23a6ba0ab90f4ff"), h));
    CHECK(h.size() == 4 && h[0].second == "GET" && h[3].first == ":authority" && h[3].second == "www.example.com");
    // C.4.2 uses the dynamic table entry added by C.4.1.
    h.clear();
    CHECK(dec.decode(unhex("828684be5886a8eb10649cbf"), h));
    CHECK(h.size() == 5 && h[3].second == "www.example.com" && h[4].first == "cache-control" && h[4].second == "no-cache");
    // Round trip through our encoder (static indexing + Huffman literals).
    Encoder enc;
    HeaderList in{{":method", "POST"}, {":path", "/extraction_rts.identity.IdentityService/Token"},
                  {"content-type", "application/grpc"}, {"authorization", "Bearer abc.def"}, {"x-bin", std::string("\x00\x01", 2)}};
    std::string block;
    enc.encode(in, block);
    Decoder dec2;
    HeaderList out;
    CHECK(dec2.decode(block, out) && out == in);
    // Huffman of every byte value.
    std::string all;
    for (int i = 0; i < 256; ++i)
        all.push_back(char(i));
    std::string hu, back;
    huffmanEncode(all, hu);
    CHECK(huffmanLength(all) == hu.size() && huffmanDecode(hu, back) && back == all);
    // Bad input: truncated integer, out-of-range index, oversized table update.
    HeaderList bad;
    CHECK(!Decoder().decode(unhex("ff"), bad));
    CHECK(!Decoder().decode(unhex("be"), bad));
    CHECK(Decoder(4096).decode(unhex("3fe11f"), bad) && !Decoder(4096).decode(unhex("3fe21f"), bad));
}

// ---------------------------------------------------------------------------------------------
// Echo service on the mock server.
static void installEcho(MockServer &srv, int *unavailableLeft = nullptr) {
    srv.handle("/aiverse_test.Echo/Say", [unavailableLeft](ServerCall &c, ServerCall::Event e) {
        if (e != ServerCall::Event::HalfClose)
            return;
        aiverse_test::EchoRequest req;
        if (c.received.size() != 1 || !req.parse(c.received[0])) {
            c.finish(StatusCode::InvalidArgument, "bad request");
            return;
        }
        if (req.text == "never")
            return;
        if (req.text == "unavailable" && unavailableLeft && *unavailableLeft > 0) {
            --*unavailableLeft;
            c.finish(StatusCode::Unavailable, "try again");
            return;
        }
        if (req.text == "fail") {
            c.finish(StatusCode::InvalidArgument, "bad \xe2\x9c\x93 input: 100%", {{"x-reason", "test"}});
            return;
        }
        aiverse_test::EchoReply rep;
        rep.text = req.text;
        rep.payload = req.payload;
        if (req.text == "auth") {
            const std::string *a = findMetadata(c.headers, "authorization");
            rep.text = a ? *a : "<none>";
        }
        if (req.text == "meta") {
            const std::string *b = findMetadata(c.headers, "x-blob-bin");
            const std::string *t = findMetadata(c.headers, "grpc-timeout");
            rep.text = (b ? *b : "<none>") + "|" + (t ? "timeout" : "no-timeout");
            c.sendHeaders({{"x-server", "mock"}});
        }
        c.send(rep.serialize());
        c.finish(StatusCode::Ok, {}, {{"x-trailer-bin", std::string("\x01\x02", 2)}});
    });
    srv.handle("/aiverse_test.Echo/Chat", [](ServerCall &c, ServerCall::Event e) {
        if (e == ServerCall::Event::Start) {
            c.sendHeaders({{"x-chat", "1"}});
        } else if (e == ServerCall::Event::Message) {
            aiverse_test::EchoRequest req;
            req.parse(c.received.back());
            if (req.text == "boom") {
                c.finish(StatusCode::Aborted, "boom");
                return;
            }
            aiverse_test::EchoReply rep;
            rep.text = req.text + "!";
            rep.payload = req.payload;
            c.send(rep.serialize());
        } else if (e == ServerCall::Event::HalfClose) {
            c.finish(StatusCode::Ok);
        }
    });
    srv.handle("/aiverse_test.Echo/Fan", [](ServerCall &c, ServerCall::Event e) {
        if (e != ServerCall::Event::HalfClose)
            return;
        for (int i = 0; i < 3; ++i) {
            aiverse_test::EchoReply rep;
            rep.text = std::to_string(i);
            c.send(rep.serialize());
        }
        c.finish(StatusCode::Ok);
    });
}

struct TransportMode {
    const char *name;
    MockServer::Mode server;
    bool tls;
    Protocol protocol;
    bool wsUnary;
};

static ChannelConfig configFor(const TransportMode &m, const MockServer &srv) {
    ChannelConfig cfg;
    cfg.target = srv.url();
    cfg.protocol = m.protocol;
    cfg.webSocketForUnary = m.wsUnary;
    cfg.tls.caPem = MockServer::testCaPem();
    return cfg;
}

static void testTransport(const TransportMode &m) {
    std::printf("  transport %s\n", m.name);
    MockServer srv(m.server, m.tls);
    if (!srv.ok()) {
        std::printf("  SKIP %s: %s\n", m.name, srv.error().c_str());
        return;
    }
    installEcho(srv);
    ChannelConfig cfg = configFor(m, srv);
    std::string token = "tok-1";
    cfg.authToken = [&] { return token; };
    Channel ch(cfg);
    aiverse_test::Echo::Client echo(ch);

    // Unary success with payload, trailers, auth metadata.
    {
        bool done = false;
        Status st;
        aiverse_test::EchoReply rep;
        aiverse_test::EchoRequest req;
        req.text = "auth";
        echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&r) { done = true; st = s; rep = std::move(r); });
        CHECK(spin(ch, srv, [&] { return done; }));
        CHECK(st.ok());
        CHECK(rep.text == "Bearer tok-1");
        const std::string *tb = findMetadata(st.trailers, "x-trailer-bin");
        CHECK(tb && *tb == std::string("\x01\x02", 2));
    }
    // Binary metadata + deadline header + 1 MiB payload both ways (HTTP/2 flow control).
    {
        bool done = false;
        Status st;
        aiverse_test::EchoReply rep;
        aiverse_test::EchoRequest req;
        req.text = "meta";
        req.payload.assign(1 << 20, 'x');
        req.payload[12345] = '\0';
        CallOptions o;
        o.timeoutMs = 5000;
        o.metadata = {{"X-Blob-Bin", std::string("\xff\x00z", 3)}};
        echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&r) { done = true; st = s; rep = std::move(r); }, o);
        CHECK(spin(ch, srv, [&] { return done; }, 8000));
        CHECK(st.ok());
        CHECK(rep.text == std::string("\xff\x00z", 3) + "|timeout");
        CHECK(rep.payload == req.payload);
    }
    // Error status with percent-encoded unicode message and custom trailer.
    {
        bool done = false;
        Status st;
        aiverse_test::EchoRequest req;
        req.text = "fail";
        echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
        CHECK(spin(ch, srv, [&] { return done; }));
        CHECK(st.code == StatusCode::InvalidArgument);
        CHECK(st.message == "bad \xe2\x9c\x93 input: 100%");
        const std::string *r = findMetadata(st.trailers, "x-reason");
        CHECK(r && *r == "test");
    }
    // Unknown method -> UNIMPLEMENTED (trailers-only).
    {
        bool done = false;
        Status st;
        static constexpr MethodInfo kMissing{"/aiverse_test.Echo/Missing", MethodKind::Unary};
        ch.unary(kMissing, "", {}, [&](const Status &s, std::string) { done = true; st = s; });
        CHECK(spin(ch, srv, [&] { return done; }));
        CHECK(st.code == StatusCode::Unimplemented);
    }
    // Deadline: the server never answers.
    {
        bool done = false;
        Status st;
        aiverse_test::EchoRequest req;
        req.text = "never";
        CallOptions o;
        o.timeoutMs = 150;
        const auto t0 = std::chrono::steady_clock::now();
        echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; }, o);
        CHECK(spin(ch, srv, [&] { return done; }));
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(st.code == StatusCode::DeadlineExceeded);
        CHECK(ms >= 140 && ms < 2000);
    }
    // Cancellation of an active call; the server observes it.
    {
        bool done = false;
        Status st;
        aiverse_test::EchoRequest req;
        req.text = "never";
        const size_t before = srv.callsStarted();
        Call call = echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
        CHECK(spin(ch, srv, [&] { return srv.callsStarted() > before; }));
        call.cancel();
        CHECK(!call.active());
        CHECK(spin(ch, srv, [&] { return done && srv.calls().back()->cancelled; }));
        CHECK(st.code == StatusCode::Cancelled);
    }
    // Bidi stream: messages in both directions, server headers, half-close -> OK.
    {
        std::vector<std::string> got;
        bool headers = false, finished = false;
        Status st;
        StreamHandlers<aiverse_test::EchoReply> h;
        h.onHeaders = [&](const Metadata &md) { headers = findMetadata(md, "x-chat") != nullptr; };
        h.onMessage = [&](aiverse_test::EchoReply &&r) { got.push_back(r.text); };
        h.onFinish = [&](const Status &s) { finished = true; st = s; };
        auto stream = echo.Chat(h);
        for (const char *t : {"a", "b", "c"}) {
            aiverse_test::EchoRequest r;
            r.text = t;
            CHECK(stream->write(r));
        }
        CHECK(spin(ch, srv, [&] { return got.size() == 3; }));
        CHECK(headers);
        aiverse_test::EchoRequest big;
        big.text = "big";
        big.payload.assign(300000, 'q');
        stream->write(big);
        CHECK(spin(ch, srv, [&] { return got.size() == 4; }));
        stream->writesDone();
        CHECK(!stream->write(big));
        CHECK(spin(ch, srv, [&] { return finished; }));
        CHECK(st.ok());
        CHECK(got.size() == 4 && got[0] == "a!" && got[2] == "c!" && got[3] == "big!");
    }
    // Server aborts a stream; client cancel of a stream is seen by the server.
    {
        bool finished = false;
        Status st;
        StreamHandlers<aiverse_test::EchoReply> h;
        h.onFinish = [&](const Status &s) { finished = true; st = s; };
        auto stream = echo.Chat(h);
        aiverse_test::EchoRequest r;
        r.text = "boom";
        stream->write(r);
        CHECK(spin(ch, srv, [&] { return finished; }));
        CHECK(st.code == StatusCode::Aborted && st.message == "boom");

        finished = false;
        auto s2 = echo.Chat(h);
        r.text = "hi";
        s2->write(r);
        const size_t before = srv.calls().size();
        CHECK(spin(ch, srv, [&] { return srv.calls().size() > before && !srv.calls().back()->received.empty(); }));
        auto serverSide = srv.calls().back();
        s2->cancel();
        CHECK(spin(ch, srv, [&] { return finished && serverSide->cancelled; }));
        CHECK(st.code == StatusCode::Cancelled);
    }
    // Server streaming.
    {
        std::vector<std::string> got;
        bool finished = false;
        Status st;
        StreamHandlers<aiverse_test::EchoReply> h;
        h.onMessage = [&](aiverse_test::EchoReply &&r) { got.push_back(r.text); };
        h.onFinish = [&](const Status &s) { finished = true; st = s; };
        auto s = echo.Fan(h);
        aiverse_test::EchoRequest r;
        s->write(r);
        s->writesDone();
        CHECK(spin(ch, srv, [&] { return finished; }));
        CHECK(st.ok() && got == std::vector<std::string>({"0", "1", "2"}));
        if (!st.ok() || got.size() != 3)
            std::printf("    fan: %s, %zu messages\n", st.str().c_str(), got.size());
    }
    // Connection loss mid-call -> UNAVAILABLE, then the channel reconnects.
    {
        bool done = false;
        Status st;
        aiverse_test::EchoRequest req;
        req.text = "never";
        const size_t before = srv.callsStarted();
        echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
        CHECK(spin(ch, srv, [&] { return srv.callsStarted() > before; }));
        srv.dropConnections();
        CHECK(spin(ch, srv, [&] { return done; }));
        CHECK(st.code == StatusCode::Unavailable);
        done = false;
        req.text = "again";
        aiverse_test::EchoReply rep;
        echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&r) { done = true; st = s; rep = std::move(r); });
        CHECK(spin(ch, srv, [&] { return done; }));
        CHECK(st.ok() && rep.text == "again");
    }
    CHECK(ch.activeCalls() == 0);
}

// CallsThrottle semantics: one unary call at a time across channels sharing a gate, FIFO; a call
// cancelled while queued never reaches the server.
static void testThrottleAndRetry() {
    MockServer srv(MockServer::Mode::Http2, false);
    int unavailableLeft = 2;
    installEcho(srv, &unavailableLeft);
    auto gate = std::make_shared<CallGate>(1);
    ChannelConfig cfg;
    cfg.target = srv.url();
    cfg.gate = gate;
    Channel a(cfg), b(cfg);
    aiverse_test::Echo::Client ea(a), eb(b);
    auto both = [&] {
        a.poll();
        b.poll();
    };
    auto spin2 = [&](auto pred) {
        for (int i = 0; i < 10000 && !pred(); ++i) {
            both();
            srv.poll();
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
        return pred();
    };
    std::vector<std::string> order;
    Status cancelled;
    aiverse_test::EchoRequest slow, r1, r2, r3;
    slow.text = "never";
    r1.text = "one";
    r2.text = "two";
    r3.text = "three";
    CallOptions slowOpt;
    slowOpt.timeoutMs = 200;
    bool slowDone = false;
    ea.Say(slow, [&](const Status &s, aiverse_test::EchoReply &&) { slowDone = true; order.push_back(statusName(s.code)); }, slowOpt);
    auto rec = [&](const Status &s, aiverse_test::EchoReply &&r) { order.push_back(s.ok() ? r.text : s.str()); };
    eb.Say(r1, rec);
    Call c2 = ea.Say(r2, [&](const Status &s, aiverse_test::EchoReply &&) { cancelled = s; });
    eb.Say(r3, rec);
    both();
    srv.poll();
    CHECK(gate->running() == 1 && gate->waiting() == 3);
    c2.cancel();
    CHECK(gate->waiting() == 2);
    CHECK(spin2([&] { return order.size() == 3; }));
    CHECK(slowDone);
    CHECK(order == std::vector<std::string>({"DEADLINE_EXCEEDED", "one", "three"}));
    CHECK(cancelled.code == StatusCode::Cancelled && cancelled.message == "RPC cancelled before completion");
    size_t sawTwo = 0;
    for (auto &c : srv.calls()) {
        aiverse_test::EchoRequest q;
        q.parse(c->received.empty() ? "" : c->received[0]);
        sawTwo += q.text == "two";
    }
    CHECK(sawTwo == 0);
    // Streams are not gated.
    CHECK(gate->running() == 0);

    // Retries: UNAVAILABLE twice, then OK.
    ChannelConfig rc;
    rc.target = srv.url();
    rc.retry.maxAttempts = 3;
    rc.retry.initialBackoffMs = 5;
    Channel r(rc);
    aiverse_test::Echo::Client er(r);
    aiverse_test::EchoRequest u;
    u.text = "unavailable";
    Status st;
    aiverse_test::EchoReply rep;
    bool done = false;
    const size_t before = srv.callsStarted();
    er.Say(u, [&](const Status &s, aiverse_test::EchoReply &&x) { done = true; st = s; rep = std::move(x); });
    CHECK(spin(r, srv, [&] { return done; }));
    CHECK(st.ok() && rep.text == "unavailable" && srv.callsStarted() - before == 3);
    // Not retried: retry disabled per call, or a non-retryable code.
    unavailableLeft = 5;
    done = false;
    CallOptions noRetry;
    noRetry.retry = false;
    er.Say(u, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; }, noRetry);
    CHECK(spin(r, srv, [&] { return done; }));
    CHECK(st.code == StatusCode::Unavailable);
    // Connection refused is retried too, then reported.
    ChannelConfig dead;
    dead.target = "http://127.0.0.1:1";
    dead.retry.maxAttempts = 2;
    dead.retry.initialBackoffMs = 1;
    Channel d(dead);
    aiverse_test::Echo::Client ed(d);
    done = false;
    ed.Say(u, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
    CHECK(spin(d, srv, [&] { return done; }));
    CHECK(st.code == StatusCode::Unavailable);
    // Invalid target.
    ChannelConfig bad;
    bad.target = "ftp://x";
    Channel bc(bad);
    aiverse_test::Echo::Client eb2(bc);
    done = false;
    eb2.Say(u, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
    bc.poll();
    CHECK(done && st.code == StatusCode::InvalidArgument);
    // No callbacks after the channel is destroyed.
    {
        bool fired = false;
        {
            Channel tmp(rc);
            aiverse_test::Echo::Client et(tmp);
            aiverse_test::EchoRequest q;
            q.text = "never";
            et.Say(q, [&](const Status &, aiverse_test::EchoReply &&) { fired = true; });
            tmp.poll();
        }
        CHECK(!fired);
    }
}

static void testTlsFailures() {
    if (!MockServer(MockServer::Mode::Http2, true).ok()) {
        std::printf("  SKIP TLS failures: no TLS test server with %s\n", grpc::detail::tlsBackendName());
        return;
    }
    for (auto mode : {MockServer::Mode::Http2, MockServer::Mode::GrpcWeb}) {
        MockServer srv(mode, true);
        installEcho(srv);
        // Without our test CA the self-signed certificate must be rejected.
        ChannelConfig cfg;
        cfg.target = srv.url();
        cfg.protocol = mode == MockServer::Mode::Http2 ? Protocol::Http2 : Protocol::GrpcWeb;
        Channel ch(cfg);
        aiverse_test::Echo::Client echo(ch);
        bool done = false;
        Status st;
        aiverse_test::EchoRequest r;
        r.text = "x";
        echo.Say(r, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
        CHECK(spin(ch, srv, [&] { return done; }));
        CHECK(st.code == StatusCode::Unavailable && st.message.find("certificate") != std::string::npos);
        // Wrong host name with the right CA is rejected as well.
        cfg.tls.caPem = MockServer::testCaPem();
        cfg.tls.serverName = "example.com";
        Channel ch2(cfg);
        aiverse_test::Echo::Client echo2(ch2);
        done = false;
        echo2.Say(r, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
        CHECK(spin(ch2, srv, [&] { return done; }));
        CHECK(st.code == StatusCode::Unavailable);
    }
    // HTTP/2 client against an HTTP/1.1-only TLS server: clear ALPN error.
    MockServer web(MockServer::Mode::GrpcWeb, true);
    ChannelConfig cfg;
    cfg.target = web.url();
    cfg.tls.caPem = MockServer::testCaPem();
    Channel ch(cfg);
    aiverse_test::Echo::Client echo(ch);
    bool done = false;
    Status st;
    echo.Say({}, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
    CHECK(spin(ch, web, [&] { return done; }));
    CHECK(st.code == StatusCode::Unavailable && st.message.find("ALPN") != std::string::npos);
}

static void testHelpers() {
    Target t;
    CHECK(parseTarget("https://entrypoint.stage.example.tech/", t) && t.tls && t.port == 443 &&
          t.host == "entrypoint.stage.example.tech" && t.pathPrefix.empty() && t.authority() == "entrypoint.stage.example.tech");
    CHECK(parseTarget("http://[::1]:8080/api/", t) && !t.tls && t.port == 8080 && t.host == "::1" && t.pathPrefix == "/api" &&
          t.authority() == "[::1]:8080");
    CHECK(!parseTarget("grpc://x", t) && !parseTarget("http://:80", t) && !parseTarget("http://h:99999", t));
    CHECK(encodeTimeout(150) == "150m" && encodeTimeout(200000000) == "200000S");
    CHECK(percentDecode(percentEncode("a%b\n\xd1\x84")) == "a%b\n\xd1\x84");
    std::string raw;
    CHECK(base64Decode(base64Encode(std::string("\x00\x01\xfe\xff", 4)), raw) && raw == std::string("\x00\x01\xfe\xff", 4));
    CHECK(statusFromHttp(503) == StatusCode::Unavailable && statusFromHttp(401) == StatusCode::Unauthenticated);
}

// ---------------------------------------------------------------------------------------------
// Drone Fall meta adapter against a mock of the existing backend (gRPC-Web + WebSocket bridge).
#include "engine/grpc/ext/ExtMeta.h"
#include "entrypoint.pb.h"
#include "identity.pb.h"
#include "matchmaking.pb.h"

namespace {
namespace epb = ::extraction_rts::entrypoint;
namespace ipb = ::extraction_rts::identity;
namespace mpb = ::extraction_rts::matchmaking;

struct FakeBackend {
    MockServer srv{MockServer::Mode::GrpcWeb, false};
    int64_t utc = 1000000;
    int64_t tokenLifetime = 3600;
    int tokens = 0;
    std::vector<ipb::TokenRequest> tokenRequests;
    std::vector<std::string> mmAuth;
    mpb::FindMatchParams lastParams;
    std::string matchMode = "found"; // found | cancelled | silent
    std::shared_ptr<ServerCall> findCall;

    FakeBackend() {
        srv.unary("/extraction_rts.entrypoint.EntrypointService/GetServiceMap", [this](const Metadata &md, const std::string &b) {
            epb::GetServiceMapRequest req;
            req.parse(b);
            epb::GetServiceMapResponse rep;
            if (findMetadata(md, "authorization"))
                return std::optional<std::pair<Status, std::string>>({{StatusCode::Internal, "entrypoint needs no auth", {}}, ""});
            if (req.client_version == "old")
                rep.set_incompatible_version().error_message = "Please update to 2.0";
            else {
                rep.set_success().identity_url = srv.url() + "/";
                rep.success.matchmaking_url = srv.url();
            }
            return std::optional<std::pair<Status, std::string>>({{}, rep.serialize()});
        });
        srv.unary("/extraction_rts.identity.IdentityService/Token", [this](const Metadata &, const std::string &b) {
            ipb::TokenRequest req;
            req.parse(b);
            tokenRequests.push_back(req);
            ++tokens;
            ipb::TokenResponse rep;
            rep.user_id = "user-" + (req.has_device() ? req.device.device_id.substr(0, 4) : std::string("r"));
            rep.access_token = "access-" + std::to_string(tokens);
            rep.refresh_token = "refresh-" + std::to_string(tokens);
            rep.expires_at = utc + tokenLifetime;
            rep.expires_in = tokenLifetime;
            return std::optional<std::pair<Status, std::string>>({{}, rep.serialize()});
        });
        srv.unary("/extraction_rts.identity.IdentityService/Logout", [](const Metadata &md, const std::string &) {
            const bool authed = findMetadata(md, "authorization") != nullptr;
            return std::optional<std::pair<Status, std::string>>(
                {authed ? Status{} : Status{StatusCode::Unauthenticated, "no token", {}}, ipb::LogoutResponse{}.serialize()});
        });
        srv.unary("/extraction_rts.matchmaking.MatchmakingService/GetBattleServers", [this](const Metadata &md, const std::string &) {
            const std::string *a = findMetadata(md, "authorization");
            mmAuth.push_back(a ? *a : "");
            mpb::GetBattleServersResponse rep;
            rep.servers.push_back({"eu", "10.0.0.1", 7777});
            rep.servers.push_back({"us", "10.0.0.2", 7777});
            return std::optional<std::pair<Status, std::string>>({{}, rep.serialize()});
        });
        srv.handle("/extraction_rts.matchmaking.MatchmakingService/FindMatch", [this](ServerCall &c, ServerCall::Event e) {
            if (e != ServerCall::Event::Message)
                return;
            mpb::MatchClientMessage m;
            m.parse(c.received.back());
            lastParams = m.find_match_params;
            const std::string *a = findMetadata(c.headers, "authorization");
            mmAuth.push_back(a ? *a : "");
            for (auto &sc : srv.calls())
                if (sc.get() == &c)
                    findCall = sc;
            mpb::MatchEvent ev;
            ev.set_queued();
            c.send(ev.serialize());
            ev = {};
            ev.set_queue_update().players_in_queue = 3;
            c.send(ev.serialize());
            if (matchMode == "found") {
                ev = {};
                auto &f = ev.set_match_found();
                f.match_id = "match-9";
                f.battle_server_address = "10.0.0.1";
                f.battle_server_port = 7777;
                c.send(ev.serialize());
            } else if (matchMode == "cancelled") {
                ev = {};
                ev.set_match_cancelled().reason = "server restart";
                c.send(ev.serialize());
                c.finish(StatusCode::Ok);
            }
        });
    }
};

template <class Pred> bool spinExt(ext::ExtMetaClient &cl, MockServer &srv, Pred pred, int timeoutMs = 4000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < end && !pred()) {
        cl.poll();
        srv.poll();
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    return pred();
}
} // namespace

static void testExtMeta() {
    using namespace aiverse::grpc::ext;
    FakeBackend be;
    CHECK(be.srv.ok());
    std::map<std::string, std::string> profile;
    ExtMetaConfig cfg;
    cfg.endpointUrl = be.srv.url() + "/";
    cfg.clientVersion = "1.2.3";
    cfg.platform = ClientPlatform::Android;
    cfg.refreshPeriodMs = 30;
    cfg.load = [&](const std::string &k) { return profile[k]; };
    cfg.save = [&](const std::string &k, const std::string &v) { profile[k] = v; };
    cfg.utcSeconds = [&] { return be.utc; };
    ExtMetaClient client(cfg);
    std::vector<ConnectionStatus> statuses;
    client.onStatus = [&](ConnectionStatus s) { statuses.push_back(s); };
    client.connect();
    CHECK(spinExt(client, be.srv, [&] { return client.status() == ConnectionStatus::Connected; }));
    CHECK(statuses.size() >= 2 && statuses[statuses.size() - 2] == ConnectionStatus::Connecting);
    CHECK(profile["auth_id"].size() == 36 && profile["auth_id"][14] == '4');
    CHECK(be.tokenRequests.size() == 1 && be.tokenRequests[0].has_device() &&
          be.tokenRequests[0].device.device_id == profile["auth_id"]);
    CHECK(client.accessToken() == "access-1" && profile["user_id"] == client.userId());
    CHECK(client.identityUrl() == be.srv.url() + "/");

    // Token refresh once expiry is within 60 s.
    be.utc += be.tokenLifetime - 30;
    CHECK(spinExt(client, be.srv, [&] { return client.accessToken() == "access-2"; }));
    CHECK(be.tokenRequests.back().has_refresh_token() && be.tokenRequests.back().refresh_token.refresh_token == "refresh-1");
    be.utc -= be.tokenLifetime; // far from expiry again: no further refreshes
    const size_t tokensNow = be.tokenRequests.size();

    // Matchmaking: servers -> pings (us unreachable) -> FindMatch -> MatchFound.
    std::vector<FindMatchStatus> fs;
    int queue = -1;
    client.onFindMatch = [&](FindMatchStatus s) { fs.push_back(s); };
    client.onQueueUpdate = [&](int n) { queue = n; };
    PingFn ping = [](const BattleServer &s, std::function<void(int)> done) { done(s.id == "eu" ? 42 : -1); };
    client.matchmake("square", ping);
    CHECK(spinExt(client, be.srv, [&] { return client.findMatchStatus() == FindMatchStatus::Found; }));
    CHECK(fs == std::vector<FindMatchStatus>({FindMatchStatus::Fetching, FindMatchStatus::Pinging, FindMatchStatus::Waiting,
                                              FindMatchStatus::Found}));
    CHECK(queue == 3);
    CHECK(be.lastParams.client_version == "1.2.3|protocol:14|map:square");
    CHECK(be.lastParams.server_pings.size() == 1 && be.lastParams.server_pings["eu"] == 42);
    CHECK(be.mmAuth.size() == 2 && be.mmAuth[0] == "Bearer access-2" && be.mmAuth[1] == "Bearer access-2");
    CHECK(client.ticket() && client.ticket()->matchId == "match-9" && client.ticket()->host == "10.0.0.1" &&
          client.ticket()->port == 7777 && client.ticket()->accessToken == "access-2");
    CHECK(spinExt(client, be.srv, [&] { return be.findCall && be.findCall->cancelled; })); // stream disposed
    CHECK(be.tokenRequests.size() == tokensNow);

    // Match cancelled by the server -> Error; nobody reachable -> NoServersReachable.
    be.matchMode = "cancelled";
    client.matchmake("square", ping);
    CHECK(spinExt(client, be.srv, [&] { return client.findMatchStatus() == FindMatchStatus::Error; }));
    CHECK(client.lastError().find("server restart") != std::string::npos);
    if (client.lastError().find("server restart") == std::string::npos)
        std::printf("    cancelled: %s\n", client.lastError().c_str());
    client.matchmake("square", [](const BattleServer &, std::function<void(int)> done) { done(-1); });
    CHECK(spinExt(client, be.srv, [&] { return client.findMatchStatus() == FindMatchStatus::NoServersReachable; }));
    // Cancel while waiting: the server sees the stream go away; the status is left as it was.
    be.matchMode = "silent";
    be.findCall.reset();
    client.matchmake("square", ping);
    CHECK(spinExt(client, be.srv, [&] { return client.findMatchStatus() == FindMatchStatus::Waiting && be.findCall; }));
    client.cancelMatchmaking();
    CHECK(spinExt(client, be.srv, [&] { return be.findCall->cancelled; }));
    CHECK(client.findMatchStatus() == FindMatchStatus::Waiting);
    client.matchmake("", ping);
    CHECK(client.findMatchStatus() == FindMatchStatus::Error);

    // meta::Backend adapter.
    {
        ExtMetaBackend backend(client, ping);
        be.matchMode = "found";
        meta::ServiceReply connectReply, findReply;
        bool c1 = false, f1 = false;
        backend.request({"session", "connect", "op1", Json::object()}, [&](meta::ServiceReply r) { c1 = true; connectReply = std::move(r); });
        CHECK(spinExt(client, be.srv, [&] { backend.pump(); return c1; }));
        CHECK(connectReply.result.code == meta::Code::Ok && connectReply.data.getStr("userId") == client.userId());
        CHECK(be.tokenRequests.back().has_device()); // reconnect reuses the stored auth_id
        CHECK(be.tokenRequests.back().device.device_id == profile["auth_id"]);
        Json args = Json::object();
        args.set("mapId", Json::string("square"));
        backend.request({"matchmaking", "find", "op2", args}, [&](meta::ServiceReply r) { f1 = true; findReply = std::move(r); });
        CHECK(spinExt(client, be.srv, [&] { backend.pump(); return f1; }));
        CHECK(findReply.result.code == meta::Code::Ok && findReply.data.getStr("matchId") == "match-9" &&
              findReply.data.getInt("port") == 7777);
        bool u = false;
        backend.request({"nope", "x", "op3", Json::object()}, [&](meta::ServiceReply r) { u = r.result.code == meta::Code::Unsupported; });
        backend.pump();
        CHECK(u);
        bool loggedOut = false;
        backend.request({"session", "logout", "op4", Json::object()}, [&](meta::ServiceReply r) { loggedOut = bool(r.result); });
        CHECK(spinExt(client, be.srv, [&] { backend.pump(); return loggedOut; }));
        CHECK(client.status() == ConnectionStatus::None && client.accessToken().empty());
    }

    // Version mismatch, disabled backend, unreachable endpoint with a reconnect from the callback.
    ExtMetaConfig old = cfg;
    old.clientVersion = "old";
    ExtMetaClient oldClient(old);
    oldClient.connect();
    CHECK(spinExt(oldClient, be.srv, [&] { return oldClient.status() == ConnectionStatus::VersionMismatch; }));
    CHECK(oldClient.lastError() == "Please update to 2.0");
    ExtMetaConfig off = cfg;
    off.enabled = false;
    ExtMetaClient offClient(off);
    offClient.connect();
    CHECK(offClient.status() == ConnectionStatus::Disabled);
    offClient.matchmake("square", ping);
    CHECK(offClient.findMatchStatus() == FindMatchStatus::Disabled);
    ExtMetaConfig dead = cfg;
    dead.endpointUrl = "http://127.0.0.1:1/";
    ExtMetaClient deadClient(dead);
    int errors = 0;
    deadClient.onStatus = [&](ConnectionStatus s) {
        if (s == ConnectionStatus::NetworkError && ++errors == 1)
            deadClient.connect(); // destroys the channel whose callback is running: must be safe
    };
    deadClient.connect();
    CHECK(spinExt(deadClient, be.srv, [&] { return errors == 2; }));
    CHECK(deadClient.status() == ConnectionStatus::NetworkError && !deadClient.lastError().empty());
}

// Cross-process TLS check: a Linux build serves (`--serve h2|web SECONDS`), another platform's build
// (e.g. the Windows build under Wine) connects with its own TLS backend (`--remote h2|web URL`).
static int serve(const std::string &mode, int seconds) {
    MockServer srv(mode == "h2" ? MockServer::Mode::Http2 : MockServer::Mode::GrpcWeb, true);
    if (!srv.ok()) {
        std::printf("cannot serve: %s\n", srv.error().c_str());
        return 1;
    }
    installEcho(srv);
    std::printf("%s\n", srv.url().c_str());
    std::fflush(stdout);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < end) {
        srv.poll();
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    return 0;
}

static int remote(const std::string &mode, const std::string &url) {
    ChannelConfig cfg;
    cfg.target = url;
    cfg.protocol = mode == "h2" ? Protocol::Http2 : Protocol::GrpcWeb;
    cfg.tls.caPem = MockServer::testCaPem();
    cfg.authToken = [] { return std::string("remote"); };
    Channel ch(cfg);
    aiverse_test::Echo::Client echo(ch);
    auto run = [&](auto pred) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!pred() && std::chrono::steady_clock::now() < end) {
            ch.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return pred();
    };
    bool done = false;
    Status st;
    aiverse_test::EchoReply rep;
    aiverse_test::EchoRequest req;
    req.text = "auth";
    req.payload.assign(200000, 'z');
    echo.Say(req, [&](const Status &s, aiverse_test::EchoReply &&r) { done = true; st = s; rep = std::move(r); });
    CHECK(run([&] { return done; }));
    CHECK(st.ok() && rep.text == "Bearer remote" && rep.payload == req.payload);
    if (!st.ok())
        std::printf("  remote unary: %s (%s)\n", st.str().c_str(), ch.lastTransportError().c_str());
    std::vector<std::string> got;
    bool fin = false;
    StreamHandlers<aiverse_test::EchoReply> h;
    h.onMessage = [&](aiverse_test::EchoReply &&r) { got.push_back(r.text); };
    h.onFinish = [&](const Status &s) { fin = true; st = s; };
    auto stream = echo.Chat(h);
    for (const char *t : {"x", "y"}) {
        aiverse_test::EchoRequest r;
        r.text = t;
        stream->write(r);
    }
    stream->writesDone();
    CHECK(run([&] { return fin; }));
    CHECK(st.ok() && got == std::vector<std::string>({"x!", "y!"}));
    // Wrong host name must fail certificate verification.
    cfg.tls.serverName = "example.com";
    Channel bad(cfg);
    aiverse_test::Echo::Client eb(bad);
    done = false;
    eb.Say(req, [&](const Status &s, aiverse_test::EchoReply &&) { done = true; st = s; });
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done && std::chrono::steady_clock::now() < end) {
        bad.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(done && st.code == StatusCode::Unavailable);
    std::printf("remote %s via %s: %d checks, %d failed (bad host: %s)\n", mode.c_str(), grpc::detail::tlsBackendName(),
                g_checks, g_failed, st.message.c_str());
    return g_failed ? 1 : 0;
}

#ifdef AIVERSE_GRPC_JNI_TLS
#include <jni.h>
// Desktop JVM for the JNI SSLEngine backend (the Android TLS path) in --remote mode.
static bool startJvm() {
    JavaVM *vm = nullptr;
    JNIEnv *env = nullptr;
    JavaVMInitArgs args{};
    args.version = JNI_VERSION_1_8;
    if (JNI_CreateJavaVM(&vm, reinterpret_cast<void **>(&env), &args) != JNI_OK)
        return false;
    setAndroidJavaVM(vm);
    return true;
}
#endif

int main(int argc, char **argv) {
#ifdef AIVERSE_GRPC_JNI_TLS
    if (!startJvm()) {
        std::printf("cannot start a JVM\n");
        return 1;
    }
#endif
    if (argc == 4 && std::string(argv[1]) == "--serve")
        return serve(argv[2], std::atoi(argv[3]));
    if (argc == 4 && std::string(argv[1]) == "--remote")
        return remote(argv[2], argv[3]);
    std::printf("aiverse_grpc_tests (TLS backend: %s, %s)\n", grpc::detail::tlsBackendName(),
                grpc::detail::tlsAvailable() ? "available" : "unavailable");
    testHelpers();
    testCodec();
    testHpack();
    const TransportMode modes[] = {
        {"http2 h2c", MockServer::Mode::Http2, false, Protocol::Http2, false},
        {"http2 tls", MockServer::Mode::Http2, true, Protocol::Http2, false},
        {"grpc-web + ws bridge", MockServer::Mode::GrpcWeb, false, Protocol::GrpcWeb, false},
        {"grpc-web + ws bridge, tls", MockServer::Mode::GrpcWeb, true, Protocol::GrpcWeb, false},
        {"ws bridge for unary", MockServer::Mode::GrpcWeb, false, Protocol::GrpcWeb, true},
        {"ws bridge for unary, tls", MockServer::Mode::GrpcWeb, true, Protocol::GrpcWeb, true},
    };
    for (const auto &m : modes)
        testTransport(m);
    testThrottleAndRetry();
    testTlsFailures();
    testExtMeta();
    std::printf("aiverse_grpc_tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
