// aiverse_graphcheck FILE...: compiles Shader Graph JSON with the engine compiler (GLSL + MSL statements, no GPU).
// A file holds one graph, or {"graphs": {name: graph, ...}} (written by tools/importers/unity_materials/cli.py).
// Prints one JSON line per graph: {"name", "ok", "error"?, "nodes", "samples", "glsl", "msl"} and exits 1 on any failure.
#include "engine/render/graph/ShaderGraph.h"
#include <fstream>
#include <iostream>
#include <sstream>

using aiverse::Json;

int main(int argc, char** argv) {
    int failed = 0, total = 0;
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary);
        std::ostringstream s;
        s << f.rdbuf();
        Json doc;
        try { doc = Json::parse(s.str()); } catch (const std::exception& e) {
            Json r = Json::object();
            r.set("name", Json::string(argv[i]));
            r.set("ok", Json::fromBool(false));
            r.set("error", Json::string(e.what()));
            std::cout << r.dump(-1) << "\n";
            ++failed; ++total;
            continue;
        }
        std::vector<std::pair<std::string, Json>> graphs;
        if (doc["graphs"].isObject()) for (const auto& [k, v] : doc["graphs"].members()) graphs.emplace_back(k, v);
        else graphs.emplace_back(argv[i], doc);
        for (const auto& [name, g] : graphs) {
            ++total;
            Json r = Json::object();
            r.set("name", Json::string(name));
            try {
                auto p = aiverse::render::graph::compile(g, g.getStr("domain"));
                r.set("ok", Json::fromBool(true));
                r.set("nodes", Json::number(int64_t(p.nodes)));
                r.set("samples", Json::number(int64_t(p.samples)));
                r.set("glsl", Json::number(int64_t(aiverse::render::graph::statements(p, false).size())));
                r.set("msl", Json::number(int64_t(aiverse::render::graph::statements(p, true).size())));
                r.set("vertex", Json::fromBool(!p.vertexGlsl.empty()));
            } catch (const std::exception& e) {
                r.set("ok", Json::fromBool(false));
                r.set("error", Json::string(e.what()));
                ++failed;
            }
            std::cout << r.dump(-1) << "\n";
        }
    }
    std::cerr << total - failed << "/" << total << " graphs compiled\n";
    return failed ? 1 : 0;
}
