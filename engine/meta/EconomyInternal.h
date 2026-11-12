#pragma once
#include "Meta.h"
namespace aiverse::meta {
namespace {
inline Amount value(const Bundle &b, const std::string &k) {
    auto i = b.find(k);
    return i == b.end() ? 0 : i->second;
}
inline std::string payload(const std::string &kind, const std::string &id, const Bundle &cost = {},
                           const Bundle &reward = {}) {
    Json j = Json::array();
    j.push(Json::string(kind));
    j.push(Json::string(id));
    for (const auto *b : {&cost, &reward}) {
        Json x = Json::object();
        for (auto &[k, v] : *b)
            x.set(k, Json::number(v));
        j.push(x);
    }
    return j.dump(-1);
}
inline std::string key(const std::string &id, int64_t bucket) {
    return "@meta/" + payload("bucket", id, {{"n", bucket}});
}
inline bool validTime(Time t) {
    return t >= 0 && t <= kMaxAmount;
}
} // namespace
} // namespace aiverse::meta
