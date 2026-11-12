#include "Transport.h"
#include "Tls.h"
#include <cctype>
#include <cstdlib>

namespace aiverse::grpc {

#if !defined(__ANDROID__) && !defined(AIVERSE_GRPC_JNI_TLS)
void setAndroidJavaVM(void *) {}
#endif

const char *statusName(StatusCode c) {
    static const char *names[] = {"OK", "CANCELLED", "UNKNOWN", "INVALID_ARGUMENT", "DEADLINE_EXCEEDED",
                                  "NOT_FOUND", "ALREADY_EXISTS", "PERMISSION_DENIED", "RESOURCE_EXHAUSTED",
                                  "FAILED_PRECONDITION", "ABORTED", "OUT_OF_RANGE", "UNIMPLEMENTED", "INTERNAL",
                                  "UNAVAILABLE", "DATA_LOSS", "UNAUTHENTICATED"};
    const int i = int(c);
    return i >= 0 && i <= 16 ? names[i] : "UNKNOWN";
}

std::string Status::str() const {
    return message.empty() ? statusName(code) : std::string(statusName(code)) + ": " + message;
}

const std::string *findMetadata(const Metadata &md, std::string_view key) {
    for (const auto &[k, v] : md)
        if (k == key)
            return &v;
    return nullptr;
}

std::string Target::authority() const {
    const bool defaultPort = (tls && port == 443) || (!tls && port == 80);
    const bool v6 = host.find(':') != std::string::npos;
    std::string h = v6 ? "[" + host + "]" : host;
    return defaultPort ? h : h + ":" + std::to_string(port);
}

bool parseTarget(std::string_view url, Target &out) {
    out = Target{};
    std::string_view rest;
    if (url.substr(0, 8) == "https://") {
        out.tls = true;
        out.port = 443;
        rest = url.substr(8);
    } else if (url.substr(0, 7) == "http://") {
        out.tls = false;
        out.port = 80;
        rest = url.substr(7);
    } else {
        return false;
    }
    const size_t slash = rest.find('/');
    std::string_view hostport = rest.substr(0, slash);
    std::string_view path = slash == std::string_view::npos ? std::string_view() : rest.substr(slash);
    while (!path.empty() && path.back() == '/')
        path.remove_suffix(1);
    out.pathPrefix = std::string(path);
    if (hostport.empty())
        return false;
    if (hostport[0] == '[') {
        const size_t close = hostport.find(']');
        if (close == std::string_view::npos)
            return false;
        out.host = std::string(hostport.substr(1, close - 1));
        hostport = hostport.substr(close + 1);
        if (!hostport.empty() && hostport[0] != ':')
            return false;
    } else {
        const size_t colon = hostport.find(':');
        out.host = std::string(hostport.substr(0, colon));
        hostport = colon == std::string_view::npos ? std::string_view() : hostport.substr(colon);
    }
    if (!hostport.empty()) {
        const std::string p(hostport.substr(1));
        char *end = nullptr;
        const long v = std::strtol(p.c_str(), &end, 10);
        if (p.empty() || *end || v <= 0 || v > 65535)
            return false;
        out.port = uint16_t(v);
    }
    return !out.host.empty();
}

std::string encodeTimeout(uint64_t ms) {
    // At most 8 digits (spec); switch units as needed.
    if (ms < 100000000ull)
        return std::to_string(ms) + "m";
    if (ms / 1000 < 100000000ull)
        return std::to_string(ms / 1000) + "S";
    return std::to_string(ms / 60000) + "M";
}

StatusCode statusFromHttp(int http) {
    switch (http) {
    case 400: return StatusCode::Internal;
    case 401: return StatusCode::Unauthenticated;
    case 403: return StatusCode::PermissionDenied;
    case 404: return StatusCode::Unimplemented;
    case 429: case 502: case 503: case 504: return StatusCode::Unavailable;
    default: return StatusCode::Unknown;
    }
}

std::string percentEncode(std::string_view s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (c >= 0x20 && c <= 0x7e && c != '%') {
            out.push_back(char(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string percentDecode(std::string_view s) {
    auto hexv = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int a = hexv(s[i + 1]);
            const int b = hexv(s[i + 2]);
            if (a >= 0 && b >= 0) {
                out.push_back(char(a * 16 + b));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string base64Encode(std::string_view in) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = (uint32_t(uint8_t(in[i])) << 16) | (uint32_t(uint8_t(in[i + 1])) << 8) | uint8_t(in[i + 2]);
        out += {tbl[v >> 18], tbl[(v >> 12) & 63], tbl[(v >> 6) & 63], tbl[v & 63]};
    }
    if (i + 1 == in.size()) {
        const uint32_t v = uint32_t(uint8_t(in[i])) << 16;
        out += {tbl[v >> 18], tbl[(v >> 12) & 63], '=', '='};
    } else if (i + 2 == in.size()) {
        const uint32_t v = (uint32_t(uint8_t(in[i])) << 16) | (uint32_t(uint8_t(in[i + 1])) << 8);
        out += {tbl[v >> 18], tbl[(v >> 12) & 63], tbl[(v >> 6) & 63], '='};
    }
    return out;
}

bool base64Decode(std::string_view in, std::string &out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(char(uint8_t(acc >> bits)));
        }
    }
    return true;
}

namespace detail {

Metadata parseHeaderBlock(std::string_view block) {
    Metadata md;
    while (!block.empty()) {
        size_t eol = block.find("\r\n");
        std::string_view line = block.substr(0, eol);
        block = eol == std::string_view::npos ? std::string_view() : block.substr(eol + 2);
        const size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;
        auto trim = [](std::string_view v) {
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
            return v;
        };
        std::string key(trim(line.substr(0, colon)));
        for (char &c : key)
            c = char(std::tolower(uint8_t(c)));
        md.emplace_back(std::move(key), std::string(trim(line.substr(colon + 1))));
    }
    return md;
}

void writeHeaderBlock(const Metadata &md, std::string &out) {
    for (const auto &[k, v] : md) {
        out += k;
        out += ": ";
        out += v;
        out += "\r\n";
    }
}

bool statusFromMetadata(const Metadata &md, Status &out) {
    const std::string *code = findMetadata(md, "grpc-status");
    if (!code)
        return false;
    char *end = nullptr;
    const long v = std::strtol(code->c_str(), &end, 10);
    out.code = (code->empty() || *end || v < 0 || v > 16) ? StatusCode::Unknown : StatusCode(v);
    const std::string *msg = findMetadata(md, "grpc-message");
    out.message = msg ? percentDecode(*msg) : std::string();
    out.trailers.clear();
    for (const auto &kv : md)
        if (kv.first != "grpc-status" && kv.first != "grpc-message")
            out.trailers.push_back(kv);
    return true;
}

void decodeBinaryValues(Metadata &md) {
    for (auto &[k, v] : md) {
        if (k.size() > 4 && k.compare(k.size() - 4, 4, "-bin") == 0) {
            std::string raw;
            if (base64Decode(v, raw))
                v = std::move(raw);
        }
    }
}

std::string wireValue(const std::string &key, const std::string &value) {
    if (key.size() > 4 && key.compare(key.size() - 4, 4, "-bin") == 0)
        return base64Encode(value);
    return value;
}

bool isReservedHeader(std::string_view k) {
    return k.empty() || k[0] == ':' || k == "content-type" || k == "te" || k == "user-agent" || k == "host" ||
           k == "connection" || k == "grpc-timeout" || k == "grpc-encoding" || k == "grpc-accept-encoding" ||
           k == "content-length" || k == "transfer-encoding" || k == "upgrade";
}

} // namespace detail
} // namespace aiverse::grpc
