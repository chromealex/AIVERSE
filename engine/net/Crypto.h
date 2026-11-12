// Small crypto toolbox for the battle transport (Monocypher underneath): random keys, AEAD
// (XChaCha20-Poly1305), keyed BLAKE2b, hex. Networking/host code only; never the simulation.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aiverse::net {

using Key = std::array<uint8_t, 32>;
using Nonce = std::array<uint8_t, 24>;
constexpr size_t kMacSize = 16;

// OS entropy (arc4random / getrandom / BCryptGenRandom / getentropy). Never seeded from time.
void secureRandom(void* out, size_t n);
Key randomKey();
bool isZero(const Key& k);

// out = cipher || mac (plain.size() + 16 bytes). ad is authenticated, not encrypted.
std::vector<uint8_t> aeadSeal(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adSize, const uint8_t* plain, size_t n);
// Inverse of aeadSeal; false (and `plain` cleared) when the data was altered or the key is wrong.
bool aeadOpen(const Key& key, const Nonce& nonce, const uint8_t* ad, size_t adSize, const uint8_t* sealed, size_t n,
              std::vector<uint8_t>& plain);

// Keyed BLAKE2b (outSize 16..64): message authentication for control requests and ticket ids.
std::vector<uint8_t> keyedHash(const Key& key, const uint8_t* data, size_t n, size_t outSize = 32);
// Constant-time comparison.
bool equalBytes(const uint8_t* a, const uint8_t* b, size_t n);
void wipe(void* p, size_t n);

std::string toHexString(const uint8_t* data, size_t n);
bool fromHexString(const std::string& hex, std::vector<uint8_t>& out);
std::optional<Key> keyFromHex(const std::string& hex);

} // namespace aiverse::net
