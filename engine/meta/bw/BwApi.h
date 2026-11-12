// HTTP client for the Bunker Wars meta API (BunkerWars-Server mw2-server-web, "ServerApi" in the
// Unity client): POST {host}/{path}, body {"i":-1,"data":{...}}, header "token", reply
// {"data":...,"error":{"code","key","text","obj"}}. HTTP 500 carries a handled error, 304 means
// "E-Tag unchanged", chunked replies go through {path}/chunk + GET v/chunk/{requestId}/{n}.
//
// The host supplies the HTTP transport (HTTPS, proxies, certificate policy) as a callback and
// calls pump() on its UI thread; the client creates no threads. See docs/meta-bunkerwars.md.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/data/Json.h"

namespace aiverse::meta::bw {

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    uint32_t timeoutMs = 30000;
};
struct HttpResponse {
    int status = 0;                // 0 = no HTTP response (transport failure)
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
    bool timeout = false;
    std::string error;             // transport detail
    std::string header(const std::string& name) const;   // case-insensitive, "" if absent
};
// Completion may be called synchronously or later from any thread; ApiClient queues it and
// delivers callbacks in pump() on the owning thread.
using HttpTransport = std::function<void(const HttpRequest&, std::function<void(HttpResponse)>)>;

// MW2.ServerApi.ErrorCode
enum class ApiCode : int {
    Ok = 0, NotEnoughMoney = 1, NotAvailable = 2, AlreadyHave = 3, NotFound = 4, NotModified = 9,
    CustomKicked = 40, CustomHostLeft = 41, PromoCodeInvalid = 50, PromoCodeInvalidLevel = 51, PromoCodeAlreadyActivated = 52,
    PromoCodeIsYour = 53, InvalidSecureLink = 88, UpdateAuth = 89, WrongAuth = 90, Banned = 91, Timeout = 92, Validation = 93,
    NotImplemented = 94, NeedClientUpgrade = 95, NotAuthorized = 96, BadRequest = 97, TransportError = 98, InternalError = 99,
    Unknown = 100, NoCache = 101, Maintenance = 102,
};
struct ApiError {
    int code = 0;
    std::string key, text, obj;
};
struct ApiResult {
    ApiError error;
    Json data;                 // the reply's "data" (null when absent)
    Json raw;                  // whole reply object (offers, i, ...)
    int httpStatus = 0;
    bool notModified = false;  // 304: data comes from the E-Tag cache
    bool transport = false;    // no definitive server answer (timeout, connection, bad body)
    bool ok() const { return error.code == 0; }
};
using ApiCallback = std::function<void(ApiResult)>;

struct CallOptions {
    bool chunked = false;      // use the chunk protocol when the server enabled it (entryPoint "chunks")
    bool cacheable = false;    // send the cached E-Tag; 304 returns the cached data
    bool staticData = false;   // v/db/*: chunk continuation is GET v/chunk/db/{name}/{n}
    int64_t requestId = 0;     // chunk request id; 0 = next id. A retry with the same id gets the server's cached reply
    uint32_t timeoutMs = 30000;
    bool noAuthRetry = false;  // do not re-authenticate on NOT_AUTHORIZED
};

struct ClientInfo {            // EntryPointTO / AuthTO common fields
    int api = -60;             // Constants.API_LEVEL
    std::string client = "0.0.0";
    int versionId = 0, devKey = 0, sysLang = 10;   // 10 = SystemLanguage.English
    std::string platform = "Linux", market = "", deviceInfo = "aiverse";
};

struct Hosts { std::vector<std::string> http, game, chat, stat; };

class ApiClient {
public:
    ApiClient(HttpTransport transport, std::vector<std::string> hosts);
    ~ApiClient();
    ApiClient(const ApiClient&) = delete;
    ApiClient& operator=(const ApiClient&) = delete;

    // Base URLs: "https://meta.example.com" or "host:port" (http:// is added).
    void setHosts(std::vector<std::string> hosts);
    const std::vector<std::string>& hosts() const { return hosts_; }
    void setToken(std::string token) { token_ = std::move(token); }
    const std::string& token() const { return token_; }
    void setChunks(bool on) { chunks_ = on; }
    bool chunks() const { return chunks_; }
    const Hosts& endpoints() const { return endpoints_; }   // from entryPoint / auth
    int64_t userId() const { return userId_; }

    // Any API path ("v/profile/get"). data = the TO object (null = empty data).
    void call(const std::string& path, Json data, ApiCallback cb, CallOptions opt = {});
    // v/entryPoint: chunk support and host lists (no token needed).
    void entryPoint(const ClientInfo& info, ApiCallback cb);
    // v/profile/auth with an AuthTO object (authType, guid/duid/accessToken...). On success stores the
    // token, user id and host lists; the AuthTO is kept for silent re-auth on NOT_AUTHORIZED.
    void auth(Json authTO, ApiCallback cb);
    static Json guestAuth(const ClientInfo& info, const std::string& guid, const std::string& duid);

    // Delivers completed calls (and starts follow-up chunk / re-auth requests). Owning thread only.
    void pump();
    size_t inFlight() const;
    void defer(std::function<void()> f);   // runs in the next pump() (replies that need no request)

    // Seconds of the reply cache entries (E-Tag); persisted by the host if it wants offline starts.
    Json saveCache() const;
    void loadCache(const Json& j);

    // Called (in pump) when a reply is NOT_AUTHORIZED and silent re-auth failed or was not possible.
    std::function<void(const ApiResult&)> onNotAuthorized;

private:
    struct Call;
    struct Shared;
    void start(std::shared_ptr<Call> c);
    void send(std::shared_ptr<Call> c, HttpRequest req, std::function<void(std::shared_ptr<Call>, HttpResponse)> next);
    void onPlain(std::shared_ptr<Call> c, HttpResponse r);
    void onFirstChunk(std::shared_ptr<Call> c, HttpResponse r);
    void onChunk(std::shared_ptr<Call> c, int index, HttpResponse r);
    void finish(std::shared_ptr<Call> c, ApiResult r);
    void retryOtherHost(std::shared_ptr<Call> c, HttpResponse r);
    std::string url(size_t host, const std::string& path) const;
    std::string requestBody(const Call& c) const;
    void applyHosts(const Json& data);

    HttpTransport transport_;
    std::vector<std::string> hosts_;
    std::string token_;
    bool chunks_ = false;
    Hosts endpoints_;
    int64_t userId_ = 0;
    Json authTO_;
    bool reauthing_ = false;
    std::vector<std::shared_ptr<Call>> waitingAuth_;
    int64_t nextRequestId_ = 0;
    struct CacheEntry { std::string etag; Json reply; };
    std::map<std::string, CacheEntry> cache_;
    std::shared_ptr<Shared> shared_;
};

// Parses a reply body ({"data","error"}) into a result. Used by ApiClient and by tests.
ApiResult parseReply(int httpStatus, const std::string& body);

} // namespace aiverse::meta::bw
