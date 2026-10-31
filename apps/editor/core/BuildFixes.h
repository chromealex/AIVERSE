#pragma once
#include "engine/data/Json.h"
#include <set>

namespace rts::editor {

// Put exact size overrides before wildcard rules (packaging uses the first matching size).
// Preserve compression/exclusion settings and all unrelated rules.
inline Json atlasSizeFixRules(const Json& existing, const Json& excluded) {
    Json out = Json::array();
    for (const auto& item : excluded.items()) {
        const std::string path = item.getStr("path");
        const auto limit = item["fixMaxSize"].integer();
        if (path.empty() || limit <= 0) continue;
        Json rule = Json::object();
        for (const auto& old : existing.items())
            if (old.getStr("match") == path) { rule = old; break; }
        rule.set("match", Json::string(path));
        rule.erase("scale");
        rule.set("maxSize", Json::number(limit));
        out.push(rule);
    }
    std::set<std::string> replacedPaths;
    for (const auto& old : existing.items()) {
        bool replaced = false;
        for (const auto& item : excluded.items())
            if (item["fixMaxSize"].integer() > 0 && item.getStr("path") == old.getStr("match")) { replaced = true; break; }
        if (!replaced || !replacedPaths.insert(old.getStr("match")).second) out.push(old);
    }
    return out;
}

} // namespace rts::editor
