#include "selected_test_kit.h"
#include <stdexcept>
#include <string>

int main() {
    using namespace aiverse;
    using namespace aiverse::meta;
    auto config = ServiceConfig::parse(Json::parse(R"({"economy":{"assets":{"gold":{"cap":1000},"sword":{"cap":10}},"offers":{"sword":{"cost":{"gold":10},"reward":{"sword":1}}}},"extensions":{"initial":{"gold":20}}})"));
    auto modules = kits::selected_test_kit();
    Service service(modules, config);
    auto call = [&](std::string subsystem, std::string action, std::string op, Json arguments) {
        return service.execute({"p", false}, {subsystem, action, op, arguments}, 10);
    };
    if (!call("profile", "create", "init", Json::object()).result) return 1;
    auto arguments = Json::parse(R"({"id":"sword"})");
    if (!call("economy", "buy", "buy", arguments).result) return 2;
    if (service.save()["profiles"]["p"]["balances"].getInt("gold") != 10) return 3;
    auto saved = service.save();
    if (call("chat", "send", "disabled", Json::object()).result.code != Code::Unsupported) return 4;
    if (call("economy", "daily", "daily", arguments).result.code != Code::Unsupported) return 5;
    if (saved.dump(-1) != service.save().dump(-1)) return 6;
    Service restored(modules, config, saved);
    if (restored.execute({"p", false}, {"economy", "buy", "buy", arguments}, 11).result.code != Code::Duplicate) return 7;
    if (saved.dump(-1) != restored.save().dump(-1)) return 8;
    Service conflict(modules, config, saved, [](int64_t, const Json&) { return false; });
    if (conflict.execute({"p", false}, {"economy", "buy", "new", arguments}, 11).result.code != Code::Conflict) return 9;
    if (saved.dump(-1) != conflict.save().dump(-1)) return 10;
    config.chats.emplace("global", ChatConfig{});
    try { Service rejected(modules, config); }
    catch (const std::runtime_error& e) {
        return std::string(e.what()).find("without library") != std::string::npos ? 0 : 11;
    }
    return 12;
}
