#include "Encoding.h"
#include <algorithm>
#include <cstring>

namespace aiverse::meta::enc {

std::string base64(const uint8_t* p, size_t n, bool url, bool pad) {
    const char* abc = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                          : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const uint32_t v = uint32_t(p[i]) << 16 | uint32_t(p[i + 1]) << 8 | p[i + 2];
        out += abc[v >> 18 & 63]; out += abc[v >> 12 & 63]; out += abc[v >> 6 & 63]; out += abc[v & 63];
    }
    if (i < n) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i + 1 < n) v |= uint32_t(p[i + 1]) << 8;
        out += abc[v >> 18 & 63];
        out += abc[v >> 12 & 63];
        if (i + 1 < n) out += abc[v >> 6 & 63];
        else if (pad) out += '=';
        if (pad) out += '=';
    }
    return out;
}

std::optional<std::vector<uint8_t>> unbase64(std::string_view s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    size_t padding = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') { ++padding; continue; }
        else return std::nullopt;
        if (padding) return std::nullopt;   // data after padding
        acc = acc << 6 | uint32_t(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back(uint8_t(acc >> bits & 0xff)); }
    }
    if (padding > 2 || bits >= 6) return std::nullopt;
    return out;
}

std::string hexUpper(const uint8_t* p, size_t n) {
    static const char* d = "0123456789ABCDEF";
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; ++i) { out[2 * i] = d[p[i] >> 4]; out[2 * i + 1] = d[p[i] & 15]; }
    return out;
}

std::optional<std::vector<uint8_t>> unhex(std::string_view s) {
    if (s.size() % 2) return std::nullopt;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> out(s.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        const int h = nib(s[2 * i]), l = nib(s[2 * i + 1]);
        if (h < 0 || l < 0) return std::nullopt;
        out[i] = uint8_t(h << 4 | l);
    }
    return out;
}

namespace {
constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline uint32_t rotr(uint32_t x, int n) { return x >> n | x << (32 - n); }

struct Sha {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64] = {};
    size_t used = 0;
    uint64_t total = 0;
    void block(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t n) {
        total += n;
        while (n) {
            const size_t take = std::min(n, size_t(64) - used);
            std::memcpy(buf + used, p, take);
            used += take; p += take; n -= take;
            if (used == 64) { block(buf); used = 0; }
        }
    }
    std::array<uint8_t, 32> finish() {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (used != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = uint8_t(bits >> (56 - 8 * i));
        update(len, 8);
        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[size_t(4 * i + j)] = uint8_t(h[i] >> (24 - 8 * j));
        return out;
    }
};
} // namespace

std::array<uint8_t, 32> sha256(const void* data, size_t n) {
    Sha s;
    s.update(static_cast<const uint8_t*>(data), n);
    return s.finish();
}

std::array<uint8_t, 32> hmacSha256(std::string_view key, std::string_view message) {
    uint8_t k[64] = {};
    if (key.size() > 64) { auto d = sha256(key.data(), key.size()); std::memcpy(k, d.data(), 32); }
    else std::memcpy(k, key.data(), key.size());
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha inner;
    inner.update(ipad, 64);
    inner.update(reinterpret_cast<const uint8_t*>(message.data()), message.size());
    const auto ih = inner.finish();
    Sha outer;
    outer.update(opad, 64);
    outer.update(ih.data(), ih.size());
    return outer.finish();
}

bool equalBytes(const uint8_t* a, size_t an, const uint8_t* b, size_t bn) {
    if (an != bn) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < an; ++i) diff |= uint8_t(a[i] ^ b[i]);
    return diff == 0;
}

} // namespace aiverse::meta::enc
