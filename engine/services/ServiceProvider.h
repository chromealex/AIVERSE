// Provider choice shared by the client services (social, push, links): game.json "<section>":
// {"providers": [...]} (per-platform values allowed) or the default order; the first plugin built
// into this app wins. `all` returns every available one (links / push events come from several).
#pragma once
#include <string>
#include <vector>
#include "engine/platform/PluginJson.h"
#include "engine/platform/Plugins.h"

namespace aiverse::services {

inline std::vector<std::string> providerOrder(const std::string& section, std::vector<std::string> defaults) {
    namespace json = plugins::json;
    json::Value cfg = json::parseOr(plugins::gameSection(section), json::Value::object());
    std::vector<std::string> order;
    const json::Value& prov = cfg["providers"];
    if (prov.isArray()) for (auto& v : prov.arr) order.push_back(v.str());
    else if (!prov.isNull() && !prov.str().empty()) order.push_back(prov.str());
    if (order.empty()) order = std::move(defaults);
    return order;
}

inline std::vector<std::string> availableProviders(const std::string& section, std::vector<std::string> defaults) {
    std::vector<std::string> out;
    for (auto& n : providerOrder(section, std::move(defaults)))
        if (plugins::available(n)) out.push_back(n);
    return out;
}

} // namespace aiverse::services
