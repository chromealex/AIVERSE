// HTTP(S) fetcher for the web build: the browser's fetch() through emscripten_fetch (no threads:
// callbacks arrive on the main loop). The chunk lands in the in-memory file system; persistence
// across sessions comes from the browser's HTTP cache.
#include <emscripten/fetch.h>

#include <cstdio>
#include <string>

#include <memory>
#include <vector>

#include "../Chunks.h"
#include "HttpRequest.h"

namespace aiverse::pack {

namespace {
class WebDownload final : public Download {
public:
    std::string dest, err;
    bool finished = false, good = false;
    uint64_t got = 0;
    bool done() const override { return finished; }
    bool ok() const override { return good; }
    uint64_t bytes() const override { return got; }
    std::string error() const override { return err; }
    ~WebDownload() override { if (fetch && !finished) emscripten_fetch_close(fetch); }
    emscripten_fetch_t* fetch = nullptr;
};

void onSuccess(emscripten_fetch_t* f) {
    auto* d = static_cast<WebDownload*>(f->userData);
    std::FILE* out = std::fopen(d->dest.c_str(), "wb");
    d->good = out && std::fwrite(f->data, 1, size_t(f->numBytes), out) == size_t(f->numBytes);
    if (out) std::fclose(out);
    if (!d->good) d->err = "cannot write " + d->dest;
    d->got = f->numBytes;
    d->finished = true;
    d->fetch = nullptr;
    emscripten_fetch_close(f);
}
void onError(emscripten_fetch_t* f) {
    auto* d = static_cast<WebDownload*>(f->userData);
    d->err = "HTTP " + std::to_string(f->status);
    d->finished = true;
    d->fetch = nullptr;
    emscripten_fetch_close(f);
}
void onProgress(emscripten_fetch_t* f) { static_cast<WebDownload*>(f->userData)->got = f->dataOffset + f->numBytes; }
} // namespace

std::unique_ptr<Download> fetchHttp(const std::string& url, const std::string& destPath, std::string&) {
    auto d = std::make_unique<WebDownload>();
    d->dest = destPath;
    emscripten_fetch_attr_t a;
    emscripten_fetch_attr_init(&a);
    std::snprintf(a.requestMethod, sizeof a.requestMethod, "GET");
    a.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    a.onsuccess = onSuccess;
    a.onerror = onError;
    a.onprogress = onProgress;
    a.userData = d.get();
    d->fetch = emscripten_fetch(&a, url.c_str());
    return d;
}

void setAndroidJavaVm(void*) {}

} // namespace aiverse::pack

namespace aiverse::pack {

namespace {
struct WebCall {
    std::function<void(HttpReply)> done;
    std::vector<std::string> headerStore;
    std::vector<const char*> headerPtrs;
    std::string body;
};
void finishCall(emscripten_fetch_t* f, bool ok) {
    std::unique_ptr<WebCall> c(static_cast<WebCall*>(f->userData));
    HttpReply r;
    r.status = int(f->status);
    if (r.status == 0) { r.error = ok ? "no response" : "network error (CORS, offline or timeout)"; r.timeout = !ok && f->readyState == 4 && f->status == 0; }
    if (f->data && f->numBytes) r.body.assign(f->data, size_t(f->numBytes));
    const size_t hl = emscripten_fetch_get_response_headers_length(f);
    if (hl) {
        std::string raw(hl + 1, '\0');
        emscripten_fetch_get_response_headers(f, raw.data(), raw.size());
        raw.resize(hl);
        parseHeaderBlock(raw, r.headers);
    }
    emscripten_fetch_close(f);
    c->done(std::move(r));
}
void callOk(emscripten_fetch_t* f) { finishCall(f, true); }
void callErr(emscripten_fetch_t* f) { finishCall(f, f->status != 0); }   // HTTP errors still carry a status
} // namespace

bool httpRequest(const HttpCall& call, std::function<void(HttpReply)> done) {
    auto c = std::make_unique<WebCall>();
    c->done = std::move(done);
    c->body = call.body;
    for (auto& [k, v] : call.headers) { c->headerStore.push_back(k); c->headerStore.push_back(v); }
    for (auto& s : c->headerStore) c->headerPtrs.push_back(s.c_str());
    c->headerPtrs.push_back(nullptr);
    emscripten_fetch_attr_t a;
    emscripten_fetch_attr_init(&a);
    std::snprintf(a.requestMethod, sizeof a.requestMethod, "%s", call.method.c_str());
    a.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    a.requestHeaders = c->headerPtrs.data();
    if (!c->body.empty()) { a.requestData = c->body.data(); a.requestDataSize = c->body.size(); }
    a.timeoutMSecs = call.timeoutMs;
    a.onsuccess = callOk;
    a.onerror = callErr;
    a.userData = c.release();
    emscripten_fetch(&a, call.url.c_str());
    return true;
}

} // namespace aiverse::pack
