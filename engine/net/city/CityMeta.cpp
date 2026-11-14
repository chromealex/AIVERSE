#include "CityMeta.h"
#include <ctime>

namespace aiverse::net::city {
namespace { const char* kServer = "city-server"; }

MetaAccounts::MetaAccounts(meta::Service& service, std::function<int64_t()> clock) : service_(service), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return int64_t(std::time(nullptr)); };
    next_ = uint64_t(service_.save().getInt("revision"));   // operation ids never repeat across restarts
}
meta::ServiceReply MetaAccounts::call(const std::string& player, const std::string& service, const std::string& action, Json args) {
    meta::ServiceRequest r{service, action, "city-" + std::to_string(clock_()) + "-" + std::to_string(++next_), std::move(args)};
    return service_.execute({player, true}, r, clock_());
}
Json MetaAccounts::enter(const std::string& player) {
    call(player, "profile", "create", Json::object());   // a duplicate is fine
    const auto reply = call(player, "world", "enter", Json::object());
    if (!reply.result) { ++failed_; return {}; }
    return reply.data.has("account") ? reply.data["account"] : Json{};
}
void MetaAccounts::save(const std::string& player, const Json& account, bool leaving) {
    Json args = Json::object();
    args.set("account", account);
    if (!call(player, "world", leaving ? "leave" : "save", args).result) ++failed_;
}
std::vector<std::pair<std::string, Json>> MetaAccounts::known() {
    std::vector<std::pair<std::string, Json>> out;
    for (auto& [player, extra] : service_.save()["extras"].members())
        if (extra["world"].isObject() && extra["world"].has("account")) out.push_back({player, extra["world"]["account"]});
    return out;
}
std::vector<std::string> MetaAccounts::absent(int64_t seconds) {
    call(kServer, "profile", "create", Json::object());
    Json args = Json::object();
    args.set("seconds", Json::number(seconds));
    const auto reply = call(kServer, "world", "absent", args);
    std::vector<std::string> out;
    if (!reply.result) { ++failed_; return out; }
    for (const Json& p : reply.data["players"].items()) out.push_back(p.str());
    return out;
}
}
