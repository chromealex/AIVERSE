// Byte encodings and hashes used by remote-backend adapters: base64 / base64url, hex,
// SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104). Self-contained so meta libraries stay free of
// engine/pack. Not for simulation state.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::meta::enc {

std::string base64(const uint8_t* p, size_t n, bool url = false, bool pad = true);
inline std::string base64(std::string_view s, bool url = false, bool pad = true) {
    return base64(reinterpret_cast<const uint8_t*>(s.data()), s.size(), url, pad);
}
// Accepts both alphabets, with or without padding; nullopt on any other character.
std::optional<std::vector<uint8_t>> unbase64(std::string_view s);

std::string hexUpper(const uint8_t* p, size_t n);
std::optional<std::vector<uint8_t>> unhex(std::string_view s);   // case-insensitive, even length

std::array<uint8_t, 32> sha256(const void* data, size_t n);
std::array<uint8_t, 32> hmacSha256(std::string_view key, std::string_view message);
// Constant-time equality for signatures.
bool equalBytes(const uint8_t* a, size_t an, const uint8_t* b, size_t bn);

} // namespace aiverse::meta::enc
