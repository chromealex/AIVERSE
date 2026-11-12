#include "BwApi.h"
#include "BwWire.h"

#include <cctype>
#include <chrono>
#include <mutex>

namespace aiverse::meta::bw {

std::string HttpResponse::header(const std::string& name) const {
    for (auto& [k, v] : headers) {
        if (k.size() != name.size()) continue;
        bool same = true;
        for (size_t i = 0; i < k.size() && same; ++i) same = std::tolower(uint8_t(k[i])) == std::tolower(uint8_t(name[i]));
        if (same) return v;
    }
    return {};
}

ApiResult parseReply(int httpStatus, const std::string& body) {
    ApiResult r;
    r.httpStatus = httpStatus;
    try {
        r.raw = Json::parse(body);
    } catch (const std::exception& e) {
        r.transport = true;
        r.error.code = int(ApiCode::TransportError);
        r.error.text = std::string("bad reply: ") + e.what();
        return r;
    }
    if (!r.raw.isObject()) {
        r.transport = true;
        r.error.code = int(ApiCode::TransportError);
        r.error.text = "reply is not an object";
        return r;
    }
    const Json& e = r.raw["error"];
    if (e.isObject()) {
        r.error.code = int(jsonInt(e["code"], int64_t(ApiCode::Unknown)));
        if (r.error.code == 0) r.error.code = int(ApiCode::Unknown);   // an error object always fails
        r.error.key = e.getStr("key");
        r.error.text = e.getStr("text");
        r.error.obj = e.getStr("obj");
    } else if (httpStatus == 500) {
        r.error.code = int(ApiCode::InternalError);
    }
    r.data = r.raw["data"];
    return r;
}

struct ApiClient::Shared {
    std::mutex m;
    std::deque<std::function<void()>> ready;
    size_t inFlight = 0;
};

struct ApiClient::Call {
    std::string path;
    Json data;
    ApiCallback cb;
    CallOptions opt;
    size_t host = 0;
    bool authRetried = false;
    bool chunked = false;
    int64_t requestId = 0;
    std::vector<std::string> parts;
    size_t received = 0;
    bool failed = false;
    std::string etag;
};

ApiClient::ApiClient(HttpTransport transport, std::vector<std::string> hosts)
    : transport_(std::move(transport)), hosts_(std::move(hosts)), shared_(std::make_shared<Shared>()) {
    // Like the Unity client (DateTime ticks): unique per process start; the server keys its
    // chunk cache by profile + request id.
    nextRequestId_ = int64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
ApiClient::~ApiClient() = default;

void ApiClient::setHosts(std::vector<std::string> hosts) { hosts_ = std::move(hosts); }

size_t ApiClient::inFlight() const {
    std::lock_guard<std::mutex> l(shared_->m);
    return shared_->inFlight + shared_->ready.size();
}

std::string ApiClient::url(size_t host, const std::string& path) const {
    std::string base = host < hosts_.size() ? hosts_[host] : std::string();
    if (base.find("://") == std::string::npos) base = "https://" + base;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + "/" + (path.size() && path[0] == '/' ? path.substr(1) : path);
}

std::string ApiClient::requestBody(const Call& c) const {
    Json body = Json::object();
    if (c.chunked) body.set("id", Json::number(c.requestId));
    else body.set("i", Json::number(int64_t(-1)));
    body.set("data", c.data.isNull() ? Json::object() : c.data);
    return body.dump(-1);
}

void ApiClient::call(const std::string& path, Json data, ApiCallback cb, CallOptions opt) {
    auto c = std::make_shared<Call>();
    c->path = path;
    c->data = std::move(data);
    c->cb = std::move(cb);
    c->opt = opt;
    start(std::move(c));
}

void ApiClient::send(std::shared_ptr<Call> c, HttpRequest req, std::function<void(std::shared_ptr<Call>, HttpResponse)> next) {
    std::weak_ptr<Shared> weak = shared_;
    { std::lock_guard<std::mutex> l(shared_->m); ++shared_->inFlight; }
    if (!transport_) {
        HttpResponse r;
        r.error = "no transport";
        std::lock_guard<std::mutex> l(shared_->m);
        --shared_->inFlight;
        shared_->ready.push_back([c, next, r]() { next(c, r); });
        return;
    }
    // The completion only touches the shared queue: safe after the client is gone or from another thread.
    transport_(req, [weak, c, next](HttpResponse r) {
        auto s = weak.lock();
        if (!s) return;
        std::lock_guard<std::mutex> l(s->m);
        if (s->inFlight) --s->inFlight;
        s->ready.push_back([c, next, r = std::move(r)]() mutable { next(c, std::move(r)); });
    });
}

void ApiClient::defer(std::function<void()> f) {
    std::lock_guard<std::mutex> l(shared_->m);
    shared_->ready.push_back(std::move(f));
}

void ApiClient::pump() {
    for (int guard = 0; guard < 4096; ++guard) {
        std::function<void()> f;
        {
            std::lock_guard<std::mutex> l(shared_->m);
            if (shared_->ready.empty()) return;
            f = std::move(shared_->ready.front());
            shared_->ready.pop_front();
        }
        f();
    }
}

void ApiClient::start(std::shared_ptr<Call> c) {
    if (hosts_.empty()) {
        ApiResult r;
        r.transport = true;
        r.error.code = int(ApiCode::TransportError);
        r.error.text = "no meta host";
        std::lock_guard<std::mutex> l(shared_->m);
        shared_->ready.push_back([this, c, r]() { finish(c, r); });
        return;
    }
    c->chunked = chunks_ && c->opt.chunked;
    c->parts.clear();
    c->received = 0;
    c->failed = false;
    if (c->chunked && !c->requestId) c->requestId = c->opt.requestId ? c->opt.requestId : ++nextRequestId_;
    HttpRequest req;
    req.method = "POST";
    req.url = url(c->host, c->chunked ? c->path + "/chunk" : c->path);
    req.timeoutMs = c->opt.timeoutMs;
    req.body = requestBody(*c);
    if (!token_.empty()) req.headers.push_back({"token", token_});
    req.headers.push_back({"Content-Type", "application/json; charset=utf-8"});
    req.headers.push_back({"Accept", "application/json, text/javascript, text/json, */*"});
    if (c->opt.cacheable) {
        auto it = cache_.find(c->path + "\n" + req.body);
        if (it != cache_.end() && !it->second.etag.empty()) req.headers.push_back({"E-Tag", it->second.etag});
    }
    if (c->chunked) send(c, std::move(req), [this](std::shared_ptr<Call> k, HttpResponse r) { onFirstChunk(k, std::move(r)); });
    else send(c, std::move(req), [this](std::shared_ptr<Call> k, HttpResponse r) { onPlain(k, std::move(r)); });
}

void ApiClient::retryOtherHost(std::shared_ptr<Call> c, HttpResponse r) {
    if (c->host + 1 < hosts_.size()) {
        ++c->host;
        start(c);
        return;
    }
    ApiResult res;
    res.transport = true;
    res.httpStatus = r.status;
    res.error.code = int(r.timeout ? ApiCode::Timeout : ApiCode::TransportError);
    res.error.text = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
    finish(c, res);
}

void ApiClient::onPlain(std::shared_ptr<Call> c, HttpResponse r) {
    const std::string key = c->path + "\n" + requestBody(*c);
    if (r.status == 304) {
        auto it = cache_.find(key);
        ApiResult res;
        res.httpStatus = 304;
        res.notModified = true;
        if (it != cache_.end()) {
            res.raw = it->second.reply;
            res.data = res.raw["data"];
        } else {
            res.error.code = int(ApiCode::NotModified);
        }
        finish(c, res);
        return;
    }
    if (r.status != 200 && r.status != 500) { retryOtherHost(c, std::move(r)); return; }
    ApiResult res = parseReply(r.status, r.body);
    if (res.ok() && c->opt.cacheable) {
        const std::string etag = r.header("E-Tag");
        if (!etag.empty()) cache_[key] = {etag, res.raw};
    }
    finish(c, res);
}

void ApiClient::onFirstChunk(std::shared_ptr<Call> c, HttpResponse r) {
    if (r.status != 200 && r.status != 500 && r.status != 304) { retryOtherHost(c, std::move(r)); return; }
    if (r.status == 304) { onPlain(c, std::move(r)); return; }
    c->etag = r.header("E-Tag");
    ApiResult first = parseReply(r.status, r.body);
    if (!first.ok()) { finish(c, first); return; }
    const Json& d = first.data;
    const int64_t n = jsonInt(d["chunks"], 1);
    if (!d.isObject() || n < 1 || n > 100000 || !d["data"].isString()) {
        ApiResult bad;
        bad.transport = true;
        bad.error.code = int(ApiCode::TransportError);
        bad.error.text = "bad chunk header";
        finish(c, bad);
        return;
    }
    c->parts.assign(size_t(n), std::string());
    c->parts[0] = d.getStr("data");
    c->received = 1;
    if (n == 1) { onChunk(c, -1, {}); return; }
    for (int64_t i = 1; i < n; ++i) {
        HttpRequest req;
        req.method = "GET";
        req.timeoutMs = c->opt.timeoutMs;
        req.url = c->opt.staticData && c->path.rfind("v/", 0) == 0
                      ? url(c->host, "v/chunk/" + c->path.substr(2) + "/" + std::to_string(i))
                      : url(c->host, "v/chunk/" + std::to_string(c->requestId) + "/" + std::to_string(i));
        if (!token_.empty()) req.headers.push_back({"token", token_});
        if (!c->etag.empty()) req.headers.push_back({"E-Tag", c->etag});
        const int index = int(i);
        send(c, std::move(req), [this, index](std::shared_ptr<Call> k, HttpResponse rr) { onChunk(k, index, std::move(rr)); });
    }
}

void ApiClient::onChunk(std::shared_ptr<Call> c, int index, HttpResponse r) {
    if (c->failed) return;
    if (index >= 0) {
        ApiResult part = r.status == 200 ? parseReply(200, r.body) : ApiResult{};
        if (r.status != 200 || !part.ok() || !part.data["data"].isString()) {
            c->failed = true;
            ApiResult res = r.status == 200 ? part : ApiResult{};
            if (r.status != 200 || res.ok()) {
                res.transport = true;
                res.httpStatus = r.status;
                res.error.code = int(r.timeout ? ApiCode::Timeout : ApiCode::TransportError);
                res.error.text = "chunk " + std::to_string(index) + " failed" + (r.error.empty() ? "" : ": " + r.error);
            }
            finish(c, res);
            return;
        }
        c->parts[size_t(index)] = part.data.getStr("data");
        ++c->received;
    }
    if (c->received < c->parts.size()) return;
    std::string joined;
    for (auto& p : c->parts) joined += p;
    ApiResult res = parseReply(200, "{\"data\":" + joined + "}");
    finish(c, res);
}

void ApiClient::applyHosts(const Json& d) {
    auto list = [](const Json& h) {
        std::vector<std::string> out;
        const Json& arr = h.isObject() ? h["hosts"] : h;
        for (auto& e : arr.items()) {
            std::string host = e.getStr("host");
            const int64_t port = jsonInt(e["port"]);
            if (host.empty()) continue;
            if (port > 0) host += ":" + std::to_string(port);
            out.push_back(host);
        }
        return out;
    };
    auto pick = [&](const char* listKey, const char* singleKey, std::vector<std::string>& into) {
        auto v = list(d[listKey]);
        if (v.empty() && !d.getStr(singleKey).empty()) v.push_back(d.getStr(singleKey));
        if (!v.empty()) into = std::move(v);
    };
    pick("httpHosts", "httpHost", endpoints_.http);
    pick("dataHostsTo", "dataHost", endpoints_.http);
    pick("gameHosts", "gameHost", endpoints_.game);
    pick("chatHosts", "chatHost", endpoints_.chat);
    pick("statHosts", "statHost", endpoints_.stat);
}

void ApiClient::entryPoint(const ClientInfo& info, ApiCallback cb) {
    Json to = Json::object();
    to.set("api", Json::number(int64_t(info.api)));
    to.set("client", Json::string(info.client));
    to.set("devKey", Json::number(int64_t(info.devKey)));
    to.set("sysLang", Json::number(int64_t(info.sysLang)));
    to.set("platform", Json::string(info.platform));
    to.set("market", Json::string(info.market));
    to.set("deviceInfo", Json::string(info.deviceInfo));
    CallOptions o;
    o.noAuthRetry = true;
    call("v/entryPoint", to, [this, cb](ApiResult r) {
        if (r.ok() && r.data.isObject()) {
            chunks_ = r.data.getBool("chunks");
            applyHosts(r.data);
            std::vector<std::string> auth;
            for (auto& e : r.data["authHosts"]["hosts"].items()) {
                std::string h = e.getStr("host");
                if (jsonInt(e["port"]) > 0) h += ":" + std::to_string(jsonInt(e["port"]));
                if (!e.getStr("host").empty()) auth.push_back(h);
            }
            if (!auth.empty()) hosts_ = auth;
        }
        if (cb) cb(std::move(r));
    }, o);
}

Json ApiClient::guestAuth(const ClientInfo& info, const std::string& guid, const std::string& duid) {
    Json to = Json::object();
    to.set("api", Json::number(int64_t(info.api)));
    to.set("client", Json::string(info.client));
    to.set("versionId", Json::number(int64_t(info.versionId)));
    to.set("authType", Json::number(int64_t(0)));   // GUEST
    to.set("guid", Json::string(guid));
    to.set("duid", Json::string(duid));
    to.set("devKey", Json::number(int64_t(info.devKey)));
    to.set("sysLang", Json::number(int64_t(info.sysLang)));
    to.set("platform", Json::string(info.platform));
    to.set("market", Json::string(info.market));
    to.set("deviceInfo", Json::string(info.deviceInfo));
    return to;
}

void ApiClient::auth(Json authTO, ApiCallback cb) {
    authTO_ = authTO;
    CallOptions o;
    o.noAuthRetry = true;
    call("v/profile/auth", std::move(authTO), [this, cb](ApiResult r) {
        if (r.ok() && r.data.isObject() && !r.data.getStr("token").empty()) {
            token_ = r.data.getStr("token");
            userId_ = jsonInt(r.data["userId"]);
            applyHosts(r.data);
            if (!endpoints_.http.empty()) hosts_ = endpoints_.http;
        }
        if (cb) cb(std::move(r));
    }, o);
}

void ApiClient::finish(std::shared_ptr<Call> c, ApiResult r) {
    const bool unauthorized = r.error.code == int(ApiCode::NotAuthorized);
    if (unauthorized && !c->opt.noAuthRetry && !c->authRetried && authTO_.isObject()) {
        c->authRetried = true;
        c->requestId = 0;   // a fresh chunk id: the old one belongs to the rejected session
        waitingAuth_.push_back(c);
        if (reauthing_) return;
        reauthing_ = true;
        auth(authTO_, [this](ApiResult ar) {
            reauthing_ = false;
            auto waiting = std::move(waitingAuth_);
            waitingAuth_.clear();
            for (auto& w : waiting) {
                if (ar.ok()) { w->host = 0; start(w); continue; }
                ApiResult fail;
                fail.error.code = int(ApiCode::NotAuthorized);
                fail.error.text = "re-authentication failed: " + ar.error.text;
                if (onNotAuthorized) onNotAuthorized(fail);
                if (w->cb) w->cb(fail);
            }
        });
        return;
    }
    if (unauthorized && onNotAuthorized) onNotAuthorized(r);
    if (c->cb) c->cb(std::move(r));
}

Json ApiClient::saveCache() const {
    Json out = Json::array();
    for (auto& [k, e] : cache_) {
        Json j = Json::object();
        j.set("key", Json::string(k));
        j.set("etag", Json::string(e.etag));
        j.set("reply", e.reply);
        out.push(std::move(j));
    }
    return out;
}

void ApiClient::loadCache(const Json& j) {
    for (auto& e : j.items())
        if (e.isObject() && !e.getStr("key").empty()) cache_[e.getStr("key")] = {e.getStr("etag"), e["reply"]};
}

} // namespace aiverse::meta::bw
