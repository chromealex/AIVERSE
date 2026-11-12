// Request/response HTTP(S) on the same per-platform stacks as the chunk fetcher (fetchHttp):
// libcurl (Linux, dlopen), NSURLSession (macOS/iOS), HttpURLConnection over JNI (Android), WinHTTP
// (Windows), fetch() (web). Any method, headers and body; the reply keeps status, headers and body
// in memory. Used by remote meta backends (engine/meta/bw). Not for simulation code.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::pack {

struct HttpCall {
    std::string method = "GET";
    std::string url;                                            // http:// or https://
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    uint32_t timeoutMs = 30000;
};
struct HttpReply {
    int status = 0;                                             // 0 = no HTTP response
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    bool timeout = false;
    std::string error;                                          // transport detail when status == 0
};
// Starts the request in the background (a thread natively, the browser's fetch on the web) and
// calls done exactly once: natively from that thread, on the web from the main loop. False (and
// done not called) when this build has no HTTP stack.
bool httpRequest(const HttpCall& call, std::function<void(HttpReply)> done);

// "Name: value\r\n..." -> pairs (shared by the platform implementations).
inline void parseHeaderBlock(const std::string& raw, std::vector<std::pair<std::string, std::string>>& out) {
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t e = raw.find('\n', pos);
        if (e == std::string::npos) e = raw.size();
        std::string line = raw.substr(pos, e - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t c = line.find(':');
        if (c != std::string::npos && line.rfind("HTTP/", 0) != 0) {
            size_t v = c + 1;
            while (v < line.size() && line[v] == ' ') ++v;
            out.push_back({line.substr(0, c), line.substr(v)});
        }
        pos = e + 1;
    }
}

} // namespace aiverse::pack
