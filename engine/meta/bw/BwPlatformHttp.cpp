#include "BwPlatformHttp.h"
#include "engine/pack/http/HttpRequest.h"

namespace aiverse::meta::bw {

bool platformHttpAvailable() {
    // A probe that never runs: a stack that cannot start a request returns false right away.
    static const bool available = [] {
        pack::HttpCall probe;
        probe.url = "http://127.0.0.1:9/";
        probe.timeoutMs = 1;
        return pack::httpRequest(probe, [](pack::HttpReply) {});
    }();
    return available;
}

HttpTransport platformTransport() {
    return [](const HttpRequest& req, std::function<void(HttpResponse)> done) {
        pack::HttpCall call;
        call.method = req.method;
        call.url = req.url;
        call.headers = req.headers;
        call.body = req.body;
        call.timeoutMs = req.timeoutMs;
        const bool started = pack::httpRequest(call, [done](pack::HttpReply r) {
            HttpResponse out;
            out.status = r.status;
            out.body = std::move(r.body);
            out.headers = std::move(r.headers);
            out.timeout = r.timeout;
            out.error = std::move(r.error);
            done(std::move(out));
        });
        if (!started) {
            HttpResponse out;
            out.error = "no HTTP stack in this build";
            done(std::move(out));
        }
    };
}

} // namespace aiverse::meta::bw
