// Default HTTP(S) transport for bw::ApiClient on the engine's per-platform stacks
// (engine/pack/http/HttpRequest.h): libcurl on Linux, NSURLSession on macOS/iOS, HttpURLConnection
// on Android, WinHTTP on Windows, fetch() on the web. Out of the box:
//   bw::ApiClient api(bw::platformTransport(), {"https://meta.example.com"});
// Completions arrive on a worker thread (natively) and are delivered by ApiClient::pump().
#pragma once
#include "BwApi.h"

namespace aiverse::meta::bw {

HttpTransport platformTransport();
bool platformHttpAvailable();   // false: this build has no HTTP stack (the transport reports a transport error)

} // namespace aiverse::meta::bw
