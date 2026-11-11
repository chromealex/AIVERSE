#pragma once
#include "engine/data/Json.h"
#include <map>
#include <string>
#include <vector>

namespace aiverse::render::graph {
// Presentation-only, bounded DAG. Every port carries RGBA in the host domain colour space;
// scalar constants are splatted, scalar material outputs use the red channel.
struct Op { const char* id; int inputs; };
const std::vector<Op>& catalog();
struct Program {
    std::string domain;
    std::string glsl, msl; // statements inside a fragment function
    std::map<std::string, std::string> outputs;
    int nodes = 0, samples = 0;
};
// Context names: sg_uv (vec2), sg_input (vec4), SG_SAMPLE(uv) (vec4).
// No user source strings are copied into shader code.
Program compile(const Json& graph, const std::string& expectedDomain = {});
Json makeDefault(const std::string& domain);
std::string statements(const Program& p, bool metal);
}
