// Plain HTTP/1.1 over the engine's non-blocking sockets, poll-driven (no threads): enough for the
// internal APIs between battle nodes, the matchmaker and the meta server inside one cluster, and
// for local/dev meta hosts. No TLS: public HTTPS goes through the host's own transport (platform
// HTTP stack or libcurl) plugged into ApiClient. Native builds only.
#pragma once
#ifndef __EMSCRIPTEN__
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "BwApi.h"
#include "engine/net/Socket.h"

namespace aiverse::meta::bw {

struct UrlParts { std::string scheme, host, path; uint16_t port = 0; };
bool parseUrl(const std::string& url, UrlParts& out);   // http://host[:port]/path

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;   // request.url = path?query
    explicit HttpServer(Handler h) : handler_(std::move(h)) {}
    bool listen(uint16_t port, bool loopbackOnly = false);
    uint16_t port() const { return listener_.localPort(); }
    void update(uint64_t nowMs);
    size_t maxBody = 1u << 20;

private:
    struct Conn { std::unique_ptr<net::TcpSocket> s; std::string in, out; uint64_t since = 0; bool responded = false; };
    Handler handler_;
    net::TcpListener listener_;
    std::vector<Conn> conns_;
};

// Asynchronous client: request() returns at once, update() drives the sockets and calls back.
class HttpClient {
public:
    void request(const HttpRequest& req, std::function<void(HttpResponse)> done);
    void update(uint64_t nowMs);
    size_t pending() const { return jobs_.size(); }
    HttpTransport transport();   // for ApiClient (plain http:// hosts only)

private:
    struct Job {
        HttpRequest req;
        std::function<void(HttpResponse)> done;
        std::unique_ptr<net::TcpSocket> s;
        std::string out, in;
        uint64_t started = 0;
        bool connected = false, sent = false;
    };
    std::vector<std::unique_ptr<Job>> jobs_;
    uint64_t now_ = 0;
};

// "HTTP/1.1 200 OK\r\n..." -> response (Content-Length or read-to-close bodies; chunked bodies are decoded).
bool parseHttpResponse(const std::string& raw, HttpResponse& out, bool complete);

} // namespace aiverse::meta::bw
#endif
