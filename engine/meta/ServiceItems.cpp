#include "ServicePolicy.h"
namespace aiverse::meta {
using namespace detail;
using namespace policy;
namespace {
ServiceReply handle(const ServiceConfig &config_, Json &s, const Principal &who, const ServiceRequest &r,
                    [[maybe_unused]] Time now) {
    [[maybe_unused]] const auto &a = r.arguments;
    require(a.isObject(), "arguments");
    auto p = who.player;
    auto op = pairKey(p, r.operation);
    auto action = r.action;
    auto service = r.service;
    [[maybe_unused]] auto ok = [](Json data = Json::object()) -> ServiceReply { return {{}, std::move(data)}; };
    [[maybe_unused]] auto missing = []() { return fail(Code::Missing, "definition or object"); };
    [[maybe_unused]] auto denied = []() { return fail(Code::Denied, "permission"); };
    [[maybe_unused]] auto limit = []() { return fail(Code::Limit, "capacity or limit"); };
    [[maybe_unused]] auto target = [&]() {
        auto t = id(a["target"]);
        require(s["profiles"].has(t), "unknown player");
        return t;
    };
    [[maybe_unused]] auto &x = config_.extensions;
    if (service == "items") {
        keys(a, {"id", "type", "slot", "inputs"});
        auto &extra = object(s.member("extras"), p);
        object(extra, "items");
        object(extra, "equipment");
        auto &items = extra.member("items");
        auto &equipment = extra.member("equipment");
        auto uid = id(a["id"]);
        if (action == "grant" || action == "redeem") {
            if (action == "grant" && !who.authority)
                return denied();
            auto type = id(a["type"]);
            if (!x["items"].has(type))
                return missing();
            if (items.has(uid))
                return fail(Code::Conflict, "instance exists");
            if (action == "redeem") {
                auto asset = x["items"][type].getStr("asset");
                if (asset.empty())
                    return missing();
                auto rr = transact(s, config_, p, {{asset, 1}}, {}, op);
                if (!rr)
                    return {rr, {}};
            }
            auto item = Json::object();
            item.set("type", str(type));
            item.set("level", number(0));
            items.set(uid, item);
            return ok();
        }
        if (!items.has(uid))
            return missing();
        auto item = items[uid];
        auto type = item.getStr("type");
        auto def = x["items"][type];
        if (def.isNull())
            return missing();
        auto equipped = [&](const std::string &key) {
            for (auto &[slot, v] : equipment.members()) {
                (void)slot;
                if (v.str() == key)
                    return true;
            }
            return false;
        };
        if (action == "equip") {
            auto slot = id(a["slot"]);
            bool valid = false;
            for (auto &v : def["slots"].items())
                if (v.str() == slot)
                    valid = true;
            if (!valid || equipped(uid))
                return denied();
            if (equipment.has(slot))
                return fail(Code::Conflict, "slot occupied");
            equipment.set(slot, str(uid));
            return ok();
        }
        if (action == "unequip") {
            for (auto it = equipment.membersMut().begin(); it != equipment.membersMut().end();) {
                if (it->second.str() == uid)
                    it = equipment.membersMut().erase(it);
                else
                    ++it;
            }
            return ok();
        }
        if (action == "upgrade") {
            auto level = item.getInt("level");
            if (level < 0 || size_t(level) >= def["upgradeCosts"].size())
                return limit();
            auto rr = transact(s, config_, p, bundle(def["upgradeCosts"][size_t(level)]), {}, op);
            if (!rr)
                return {rr, {}};
            item.set("level", number(level + 1));
            items.set(uid, item);
            return ok();
        }
        if (equipped(uid))
            return denied();
        if (action == "salvage") {
            auto rr = transact(s, config_, p, {}, bundle(def["salvage"]), op);
            if (!rr)
                return {rr, {}};
            items.erase(uid);
            return ok();
        }
        if (action == "merge") {
            if (!def.has("mergeCount"))
                return missing();
            require(a["inputs"].isArray(), "merge inputs");
            std::set<std::string> inputs{uid};
            for (auto &v : a["inputs"].items())
                require(inputs.insert(id(v)).second, "duplicate merge input");
            if (inputs.size() != size_t(def.getInt("mergeCount")))
                return fail(Code::Invalid, "merge count includes id");
            for (auto &key : inputs)
                if (!items.has(key) || items[key].getStr("type") != type ||
                    items[key].getInt("level") != item.getInt("level") || equipped(key))
                    return denied();
            for (auto &key : inputs)
                items.erase(key);
            item.set("type", def["mergeTo"]);
            item.set("level", number(0));
            items.set(uid, item);
            return ok();
        }
        return fail(Code::Unsupported, action);
    }
    return fail(Code::Unsupported, r.service);
}
} // namespace
void modules::items(Modules &m) {
    m.features.insert("items");
    m.handlers["items"] = handle;
}
} // namespace aiverse::meta
