#pragma once
#include "Model.h"
#include "../data/Json.h"
#include <stdexcept>

namespace aiverse::render {
inline MaterialTechniques parseMaterialTechniques(const Json& rules, const std::vector<Material>& materials) {
    MaterialTechniques result;
    if (!rules.isArray() || rules.size() > 256)
        throw std::invalid_argument("materialTechniques expects an array of up to 256 rules");
    if (rules.size() == 0) return result;
    result.bySource.resize(materials.size());
    auto materialIndex = [&](const Json& name) {
        if (!name.isString() || name.str().empty()) throw std::invalid_argument("expected a material name");
        const auto value = name.str();
        int found = -1;
        for (size_t i = 0; i < materials.size(); ++i) {
            if (materials[i].name != value) continue;
            if (found >= 0) throw std::invalid_argument("ambiguous material: " + value);
            found = int(i);
        }
        if (found < 0) throw std::invalid_argument("unknown material: " + value);
        return found;
    };
    for (const auto& row : rules.items()) {
        for (const auto& [key, _] : row.members())
            if (key != "source" && key != "use" && key != "minDistance" && key != "maxDistance" &&
                key != "scheme" && key != "requires") throw std::invalid_argument("unknown technique field: " + key);
        MaterialTechnique rule;
        const auto source = materialIndex(row["source"]);
        rule.material = materialIndex(row["use"]);
        auto distance = [&](const char* field, double fallback) {
            if (!row.has(field)) return fallback;
            double d = row[field].toDouble(-1);
            if (!row[field].isNumber() || !std::isfinite(d) || d < 0)
                throw std::invalid_argument(std::string(field) + " must be finite and nonnegative");
            return d;
        };
        rule.minDistance = distance("minDistance", rule.minDistance);
        rule.maxDistance = distance("maxDistance", rule.maxDistance);
        if (rule.minDistance >= rule.maxDistance) throw std::invalid_argument("empty material technique distance range");
        if (row.has("scheme") && !row["scheme"].isString()) throw std::invalid_argument("scheme must be a string");
        rule.scheme = row.getStr("scheme");
        if (row.has("requires") && !row["requires"].isArray()) throw std::invalid_argument("requires must be an array");
        for (const auto& entry : row["requires"].items()) {
            const auto cap = entry.str();
            if (cap == "compute") rule.required |= TechniqueCompute;
            else if (cap == "rgba16f") rule.required |= TechniqueHdr;
            else if (cap == "texture2dArray") rule.required |= TechniqueTextureArrays;
            else throw std::invalid_argument("unknown material technique capability: " + cap);
        }
        result.bySource[size_t(source)].push_back(std::move(rule));
    }
    return result;
}
} // namespace aiverse::render
