#ifndef __EMSCRIPTEN__
#include "BwHttp.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace aiverse::meta::bw {

bool parseUrl(const std::string& url, UrlParts& out) {
    const size_t p = url.find("://");
    if (p == std::string::npos) return false;
    out.scheme = url.substr(0, p);
    std::string rest = url.substr(p + 3);
    const size_t slash = rest.find('/');
    std::string hostPort = slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    out.port = out.scheme == "https" ? 443 : 80;
    if (!hostPort.empty() && hostPort[0] == '[') {   // [v6]:port
        const size_t e = hostPort.find(']');
        if (e == std::string::npos) return false;
        out.host = hostPort.substr(1, e - 1);
        if (e + 1 < hostPort.size() && hostPort[e + 1] == ':') out.port = uint16_t(std::atoi(hostPort.c_str() + e + 2));
    } else {
        const size_t c = hostPort.rfind(':');
        out.host = c == std::string::npos ? hostPort : hostPort.substr(0, c);
        if (c != std::string::npos) out.port = uint16_t(std::atoi(hostPort.c_str() + c + 1));
    }
    return !out.host.empty() && out.port != 0;
}

namespace {
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(uint8_t(c))); return s; }
const char* reason(int status) {
    switch (status) {
    case 200: return "OK"; case 201: return "Created"; case 204: return "No Content"; case 304: return "Not Modified";
    case 400: return "Bad Request"; case 401: return "Unauthorized"; case 404: return "Not Found"; case 409: return "Conflict";
    case 413: return "Payload Too Large"; case 500: return "Internal Server Error"; case 503: return "Service Unavailable";
    default: return "Status";
    }
}
// Splits head lines; returns the header block end (index after \r\n\r\n) or npos.
size_t headEnd(const std::string& s) { const size_t p = s.find("\r\n\r\n"); return p == std::string::npos ? p : p + 4; }
void parseHeaders(const std::string& head, std::vector<std::pair<std::string, std::string>>& out) {
    size_t pos = head.find("\r\n");
    while (pos != std::string::npos && pos + 2 < head.size()) {
        const size_t e = head.find("\r\n", pos + 2);
        const std::string line = head.substr(pos + 2, (e == std::string::npos ? head.size() : e) - pos - 2);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string v = line.substr(colon + 1);
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            out.push_back({line.substr(0, colon), v});
        }
        pos = e;
    }
}
std::string headerOf(const std::vector<std::pair<std::string, std::string>>& h, const std::string& name) {
    for (auto& [k, v] : h) if (lower(k) == name) return v;
    return {};
}
bool dechunk(const std::string& body, std::string& out) {
    size_t pos = 0;
    out.clear();
    for (;;) {
        const size_t e = body.find("\r\n", pos);
        if (e == std::string::npos) return false;
        const size_t n = size_t(std::strtoul(body.substr(pos, e - pos).c_str(), nullptr, 16));
        pos = e + 2;
        if (n == 0) return true;
        if (pos + n + 2 > body.size()) return false;
        out.append(body, pos, n);
        pos += n + 2;
    }
}
} // namespace

bool parseHttpResponse(const std::string& raw, HttpResponse& out, bool complete) {
    const size_t he = headEnd(raw);
    if (he == std::string::npos) return false;
    const std::string head = raw.substr(0, he - 4);
    if (head.rfind("HTTP/", 0) != 0) return false;
    const size_t sp = head.find(' ');
    out.status = sp == std::string::npos ? 0 : std::atoi(head.c_str() + sp + 1);
    out.headers.clear();
    parseHeaders(head, out.headers);
    const std::string body = raw.substr(he);
    if (lower(headerOf(out.headers, "transfer-encoding")).find("chunked") != std::string::npos) return dechunk(body, out.body);
    const std::string len = headerOf(out.headers, "content-length");
    if (!len.empty()) {
        const size_t n = size_t(std::strtoull(len.c_str(), nullptr, 10));
        if (body.size() < n) return false;
        out.body = body.substr(0, n);
        return true;
    }
    if (out.status == 204 || out.status == 304) { out.body.clear(); return true; }
    out.body = body;
    return complete;   // read to close
}

// ---------------------------------------------------------------- server

bool HttpServer::listen(uint16_t port, bool loopbackOnly) { return listener_.listen(port, loopbackOnly); }

void HttpServer::update(uint64_t now) {
    for (int k = 0; k < 64; ++k) {
        auto s = listener_.accept();
        if (!s) break;
        Conn c;
        c.s = std::move(s);
        c.since = now;
        conns_.push_back(std::move(c));
    }
    uint8_t buf[16384];
    for (auto& c : conns_) {
        if (!c.s) continue;
        if (!c.responded) {
            for (;;) {
                const int n = c.s->recv(buf, sizeof buf);
                if (n < 0) { c.s.reset(); break; }
                if (n == 0) break;
                c.in.append(reinterpret_cast<const char*>(buf), size_t(n));
                if (c.in.size() > maxBody + 65536) { c.s.reset(); break; }
            }
            if (!c.s) continue;
            const size_t he = headEnd(c.in);
            if (he != std::string::npos) {
                HttpRequest req;
                const std::string head = c.in.substr(0, he - 4);
                const size_t a = head.find(' '), b = head.find(' ', a + 1);
                req.method = head.substr(0, a);
                req.url = a == std::string::npos ? "/" : head.substr(a + 1, b - a - 1);
                parseHeaders(head, req.headers);
                const size_t len = size_t(std::strtoull(headerOf(req.headers, "content-length").c_str(), nullptr, 10));
                HttpResponse res;
                bool ready = false;
                if (len > maxBody) { res.status = 413; ready = true; }
                else if (c.in.size() >= he + len) { req.body = c.in.substr(he, len); res = handler_(req); ready = true; }
                if (ready) {
                    if (!res.status) res.status = 200;
                    std::string out = "HTTP/1.1 " + std::to_string(res.status) + " " + reason(res.status) + "\r\n";
                    bool type = false;
                    for (auto& [k, v] : res.headers) { out += k + ": " + v + "\r\n"; type |= lower(k) == "content-type"; }
                    if (!type) out += "Content-Type: application/json; charset=utf-8\r\n";
                    out += "Content-Length: " + std::to_string(res.body.size()) + "\r\nConnection: close\r\n\r\n" + res.body;
                    c.out = std::move(out);
                    c.responded = true;
                }
            }
        }
        if (c.responded && c.s && !c.out.empty()) {
            const int n = c.s->send(reinterpret_cast<const uint8_t*>(c.out.data()), c.out.size());
            if (n < 0) c.s.reset();
            else c.out.erase(0, size_t(n));
        }
        if (c.s && c.responded && c.out.empty()) c.s.reset();
        if (c.s && now - c.since > 30000) c.s.reset();   // slow or idle clients
    }
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(), [](const Conn& c) { return !c.s; }), conns_.end());
}

// ---------------------------------------------------------------- client

void HttpClient::request(const HttpRequest& req, std::function<void(HttpResponse)> done) {
    auto j = std::make_unique<Job>();
    j->req = req;
    j->done = std::move(done);
    j->started = now_;
    UrlParts u;
    HttpResponse fail;
    if (!parseUrl(req.url, u) || u.scheme != "http") {
        fail.error = "unsupported url (plain http:// only): " + req.url;
        if (j->done) j->done(fail);
        return;
    }
    net::Address addr;
    if (!net::Address::resolve(u.host, u.port, addr, true)) {
        fail.error = "cannot resolve " + u.host;
        if (j->done) j->done(fail);
        return;
    }
    j->s = std::make_unique<net::TcpSocket>();
    if (!j->s->connect(addr)) {
        fail.error = "cannot connect to " + u.host;
        if (j->done) j->done(fail);
        return;
    }
    std::string out = req.method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host + ":" + std::to_string(u.port) + "\r\n";
    for (auto& [k, v] : req.headers) out += k + ": " + v + "\r\n";
    if (req.method != "GET" || !req.body.empty()) out += "Content-Length: " + std::to_string(req.body.size()) + "\r\n";
    out += "Connection: close\r\n\r\n" + req.body;
    j->out = std::move(out);
    jobs_.push_back(std::move(j));
}

void HttpClient::update(uint64_t now) {
    now_ = now;
    uint8_t buf[16384];
    std::vector<std::pair<std::function<void(HttpResponse)>, HttpResponse>> finished;
    for (auto& j : jobs_) {
        if (!j->started) j->started = now;
        HttpResponse r;
        bool done = false;
        if (!j->connected) {
            const int c = j->s->pollConnected();
            if (c < 0) { r.error = "connection failed"; done = true; }
            else if (c > 0) j->connected = true;
        }
        if (!done && j->connected && !j->out.empty()) {
            const int n = j->s->send(reinterpret_cast<const uint8_t*>(j->out.data()), j->out.size());
            if (n < 0) { r.error = "send failed"; done = true; }
            else j->out.erase(0, size_t(n));
        }
        if (!done && j->connected && j->out.empty()) {
            bool closed = false;
            for (;;) {
                const int n = j->s->recv(buf, sizeof buf);
                if (n < 0) { closed = true; break; }
                if (n == 0) break;
                j->in.append(reinterpret_cast<const char*>(buf), size_t(n));
            }
            if (parseHttpResponse(j->in, r, closed)) done = true;
            else if (closed) { r = HttpResponse{}; r.error = "connection closed"; done = true; }
        }
        if (!done && now - j->started > j->req.timeoutMs) { r = HttpResponse{}; r.timeout = true; r.error = "timeout"; done = true; }
        if (done) {
            j->s.reset();
            finished.push_back({std::move(j->done), std::move(r)});
        }
    }
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](const std::unique_ptr<Job>& j) { return !j->s; }), jobs_.end());
    for (auto& [cb, r] : finished) if (cb) cb(std::move(r));
}

HttpTransport HttpClient::transport() {
    return [this](const HttpRequest& req, std::function<void(HttpResponse)> done) { request(req, std::move(done)); };
}

} // namespace aiverse::meta::bw
#endif
