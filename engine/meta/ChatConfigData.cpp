#include "Social.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace aiverse::meta {
namespace {
inline int64_t integer(const Json &j, int64_t low = 0, int64_t high = kMaxAmount) {
    int64_t n = 0;
    auto &s = j.numberLiteral();
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (!j.isNumber() || r.ec != std::errc{} || r.ptr != s.data() + s.size() || n < low || n > high)
        throw std::runtime_error("meta chat: invalid integer");
    return n;
}
inline std::string text(const Json &j) {
    if (!j.isString())
        throw std::runtime_error("meta chat: expected string");
    return j.str();
}
inline std::set<std::string> readStrings(const Json &j) {
    if (!j.isArray())
        throw std::runtime_error("meta chat: expected array");
    std::set<std::string> result;
    for (auto &x : j.items()) {
        auto s = text(x);
        if (s.empty() || !result.insert(s).second)
            throw std::runtime_error("meta chat: duplicate/empty id");
    }
    return result;
}
} // namespace
ChatConfig ChatConfig::parse(const Json &j) {
    if (!j.isObject())
        throw std::runtime_error("meta chat: expected object");
    ChatConfig c;
    const std::set<std::string> known{"maxBytes",  "maxHistory", "maxMessages", "cooldown",
                                      "retention", "kinds",      "metadata"};
    std::set<std::string> seen;
    for (auto &[k, v] : j.members()) {
        (void)v;
        if (!known.count(k) || !seen.insert(k).second)
            throw std::runtime_error("meta chat: unknown/duplicate key " + k);
    }
    if (j.has("metadata")) {
        if (!j["metadata"].isObject())
            throw std::runtime_error("meta chat: metadata must be object");
        c.metadata = j["metadata"];
    }
    if (j.has("maxBytes"))
        c.maxBytes = size_t(integer(j["maxBytes"], 1, 1048576));
    if (j.has("maxHistory"))
        c.maxHistory = size_t(integer(j["maxHistory"], 1, 1000000));
    if (j.has("maxMessages"))
        c.maxMessages = size_t(integer(j["maxMessages"], 1, 10000000));
    if (j.has("cooldown"))
        c.cooldown = integer(j["cooldown"]);
    if (j.has("retention"))
        c.retention = integer(j["retention"]);
    if (j.has("kinds"))
        c.kinds = readStrings(j["kinds"]);
    if (c.kinds.empty())
        throw std::runtime_error("meta chat: empty kinds");
    return c;
}
} // namespace aiverse::meta
