#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace aiverse::meta {
using Amount = int64_t;
using Time = int64_t; // nonnegative UTC seconds, supplied by the authority
using Bundle = std::map<std::string, Amount>;
constexpr Amount kMaxAmount = 1000000000000LL;
enum class Code { Ok, Duplicate, Invalid, Missing, Unavailable, Insufficient, Limit, Conflict, Denied, Unsupported };
struct Result {
    Code code = Code::Ok;
    std::string detail;
    explicit operator bool() const { return code == Code::Ok || code == Code::Duplicate; }
};
} // namespace aiverse::meta
