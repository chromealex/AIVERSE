#pragma once
#include "LiveOps.h"
#include "ServiceInternal.h"
#include <algorithm>
namespace aiverse::meta::live {
using namespace detail;
struct Stop : std::runtime_error {
    Code code;
    Stop(Code c, const std::string &s) : std::runtime_error(s), code(c) {}
};
inline void need(bool valid, Code code, const std::string &why) {
    if (!valid)
        throw Stop(code, why);
}
inline Json row(std::initializer_list<std::pair<std::string, Json>> fields) {
    auto j = Json::object();
    for (auto &[k, v] : fields)
        j.set(k, v);
    return j;
}
inline std::string key(const std::string &a, const std::string &b) {
    auto j = Json::array();
    j.push(str(a));
    j.push(str(b));
    return j.dump(-1);
}
inline int64_t add(int64_t a, int64_t b) {
    need(a >= 0 && b >= 0 && b <= kMaxAmount - a, Code::Limit, "amount overflow");
    return a + b;
}
inline int64_t mul(int64_t a, int64_t b) {
    need(a >= 0 && b >= 0 && (!a || b <= kMaxAmount / a), Code::Limit, "amount overflow");
    return a * b;
}
inline Json encode(const Bundle &b) {
    auto j = Json::object();
    for (auto &[k, n] : b)
        j.set(k, number(n));
    return j;
}
inline bool active(const Json &d, Time now) {
    return now >= d.getInt("start") && (!d.getInt("end") || now < d.getInt("end"));
}
struct Call {
    const ServiceConfig &cfg;
    Json &state;
    const Principal &who;
    const ServiceRequest &req;
    Time now;
    const Context &context;
    const RewardDraw &draw;
    std::string id, operation;
    const Json &def;
    Json &self;
    Json &shared;
    Call(const ServiceConfig &c, Json &s, const Principal &w, const ServiceRequest &r, Time t, const Context &ctx,
         const RewardDraw &rng);
    const Json &args() const { return req.arguments; }
    void authority() const { need(who.authority, Code::Denied, "trusted authority required"); }
    Json &player(const std::string &p);
    void pay(const Bundle &cost, const Bundle &reward, const std::string &suffix = "", const std::string &player = "");
    void mail(const std::string &p, const Bundle &reward, const std::string &suffix);
    int64_t balance(const std::string &asset, const std::string &player = "") const;
    int64_t metric(const std::string &metric, const std::string &player = "") const;
    void once(const std::string &providerEvent);
};
} // namespace aiverse::meta::live
