// A technique selects an existing model material. Rules are evaluated in authored order.
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::render {
enum MaterialTechniqueCapability : unsigned {
    TechniqueCompute = 1u,
    TechniqueHdr = 2u,
    TechniqueTextureArrays = 4u
};
struct MaterialTechnique {
    int material = -1;
    std::string scheme; // empty applies to every scheme
    double minDistance = 0;
    double maxDistance = std::numeric_limits<double>::infinity();
    unsigned required = 0;
};
struct MaterialTechniques {
    std::vector<std::vector<MaterialTechnique>> bySource;
    int select(int source, double distance, unsigned capabilities, std::string_view scheme) const {
        if (source < 0 || size_t(source) >= bySource.size() || !std::isfinite(distance)) return source;
        for (const auto& rule : bySource[size_t(source)]) {
            if (distance >= rule.minDistance && distance < rule.maxDistance &&
                (capabilities & rule.required) == rule.required &&
                (rule.scheme.empty() || rule.scheme == scheme)) return rule.material;
        }
        return source;
    }
};
} // namespace aiverse::render
