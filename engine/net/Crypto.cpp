#include "Crypto.h"
#include <cstdlib>
#include <cstring>
#include "../../third_party/monocypher/monocypher.h"
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <stdlib.h>
#elif defined(__EMSCRIPTEN__)
#include <unistd.h>
#elif defined(__ANDROID__)
#include <cstdio>
#else
#include <cstdio>
#include <sys/random.h>
#endif

namespace aiverse::net {

void secureRandom(void* out, size_t n) {
    auto* p = static_cast<uint8_t*>(out);
#if defined(_WIN32)
    BCryptGenRandom(nullptr, p, ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#elif defined(__APPLE__)
    arc4random_buf(p, n);
#elif defined(__EMSCRIPTEN__)
    for (size_t off = 0; off < n; off += 256) getentropy(p + off, n - off < 256 ? n - off : 256);
#elif defined(__ANDROID__)
    // getrandom() needs API 28; the device is always there.
    FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f || std::fread(p, 1, n, f) != n) std::abort();
    std::fclose(f);
#else
    size_t got = 0;
    while (got < n) {
        ssize_t r = getrandom(p + got, n - got, 0);
        if (r > 0) { got += size_t(r); continue; }
        // Old kernels / seccomp: fall back to the device. Failing to get entropy is fatal for keys.
        FILE* f = std::fopen("/dev/urandom", "rb");
        if (!f || std::fread(p + got, 1, n - got, f) != n - got) std::abort();
        std::fclose(f);
        got = n;
    }
#endif
}

Key randomKey() { Key k; secureRandom(k.data(), k.size()); return k; }

bool isZero(const Key& k) { uint8_t acc = 0; for (uint8_t b : k) acc |= b; return acc == 0; }

std::vector<uint8_t> aeadSeal(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adSize, const uint8_t* plain, size_t n) {
    std::vector<uint8_t> out(n + kMacSize);
    crypto_aead_lock(out.data(), out.data() + n, key.data(), nonce.data(), ad, adSize, plain, n);
    return out;
}

bool aeadOpen(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adSize, const uint8_t* sealed, size_t n,
              std::vector<uint8_t>& plain) {
    plain.clear();
    if (n < kMacSize) return false;
    const size_t len = n - kMacSize;
    plain.resize(len);
    if (crypto_aead_unlock(plain.data(), sealed + len, key.data(), nonce.data(), ad, adSize, sealed, len) != 0) { plain.clear(); return false; }
    return true;
}

std::vector<uint8_t> keyedHash(const Key& key, const uint8_t* data, size_t n, size_t outSize) {
    if (outSize < 16) outSize = 16;
    if (outSize > 64) outSize = 64;
    std::vector<uint8_t> out(outSize);
    crypto_blake2b_keyed(out.data(), outSize, key.data(), key.size(), data, n);
    return out;
}

bool equalBytes(const uint8_t* a, const uint8_t* b, size_t n) {
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc |= uint8_t(a[i] ^ b[i]);
    return acc == 0;
}

void wipe(void* p, size_t n) { crypto_wipe(p, n); }

std::string toHexString(const uint8_t* data, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; ++i) { s[2 * i] = d[data[i] >> 4]; s[2 * i + 1] = d[data[i] & 15]; }
    return s;
}

bool fromHexString(const std::string& hex, std::vector<uint8_t>& out) {
    out.clear();
    if (hex.size() % 2) return false;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = v(hex[i]), lo = v(hex[i + 1]);
        if (hi < 0 || lo < 0) { out.clear(); return false; }
        out.push_back(uint8_t(hi * 16 + lo));
    }
    return true;
}

std::optional<Key> keyFromHex(const std::string& hex) {
    std::vector<uint8_t> b;
    if (!fromHexString(hex, b) || b.size() != 32) return std::nullopt;
    Key k;
    std::memcpy(k.data(), b.data(), 32);
    wipe(b.data(), b.size());
    return k;
}

} // namespace aiverse::net
