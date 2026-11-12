#pragma once
#include "engine/data/Json.h"
#include <map>
#include <string>
#include <vector>

namespace aiverse::render::graph {
// Presentation-only, bounded DAG. Every port carries RGBA in the host domain colour space;
// scalar constants are splatted, scalar material outputs use the red channel.
struct Op { const char* id; int inputs; unsigned domains = 7; bool vertex = true; };
bool supports(const Op& op, const std::string& domain);
const std::vector<Op>& catalog();
struct Program {
    std::string domain;
    std::string vertexGlsl, vertexMsl;
    std::vector<std::string> textures;
    std::string glsl, msl; // statements inside a fragment function
    std::map<std::string, std::string> outputs;
    int nodes = 0, samples = 0;
    int minQuality = 0; // material tier; particles use emitter.minQuality, postfx uses effect tiers
};
// Host context: sg_uv/sg_screen (vec2), sg_input/sg_color/sg_particle (vec4),
// sg_world/sg_object/sg_normal/sg_view (vec3), sg_time (float). Domains expose
// only their supported data. Particle data = age, lifetime, stable random, unused.
// Samplers: SG_SAMPLE(uv), SG_TEXTURE<n>(uv), SG_TEXTURE_LOD<n>(uv,lod).
// vertexGlsl/vertexMsl contain only ancestors of vertexOffset/vertexNormal.
// No user source strings are copied into shader code.
Program compile(const Json& graph, const std::string& expectedDomain = {});
Json makeDefault(const std::string& domain);
std::string statements(const Program& p, bool metal);
}
