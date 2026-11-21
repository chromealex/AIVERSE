// AIVERSE SpectraFX: universal Niagara-style VFX graph compiler, version 1.
// It compiles graph assets to the existing CPU particle backend (EffectDef).
// A graph is presentation-only; it never changes authoritative ECS state.
// This is deliberately a small, validated IR; GPU execution is NOT implemented here.
#pragma once

#include "FxEffect.h"
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::fx {

// Version-2 frontend is defined by SpectraGraphV2.h at the end of this
// header to keep all existing v1 callers source-compatible.
inline bool compileSpectraGraphV2(const Json& graph, const Json& overrides,
                                 EffectDef& out, std::vector<std::string>& errors);

namespace vfxgraph_detail {
inline bool fail(std::vector<std::string>& errors, const std::string& reason) {
    errors.push_back("vfx graph: " + reason);
    return false;
}
inline bool nameOk(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char ch : id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return false;
    return true;
}
inline bool outputType(const std::string& type) {
    return type == "SpriteOutput" || type == "MeshOutput" || type == "RibbonOutput";
}
inline bool nodeType(const std::string& type) {
    return type == "Spawn" || type == "Initialize" || type == "Update" || outputType(type);
}
inline void overlay(Json& target, const Json& layer) {
    for (const auto& [key, value] : layer.members()) {
        Json* old = target.find(key);
        if (old && old->isObject() && value.isObject()) overlay(*old, value);
        else target.set(key, value);
    }
}
inline bool resolve(const Json& input, const Json& defaults, const Json& overrides,
                    Json& output, std::vector<std::string>& errors, unsigned depth = 0) {
    if (depth > 24) return fail(errors, "property nesting exceeds 24");
    if (input.isObject()) {
        if (input.has("$param")) {
            if (input.size() != 1 || !input["$param"].isString())
                return fail(errors, "$param must be the only property and contain a string");
            const std::string key = input["$param"].str();
            if (!defaults.has(key)) return fail(errors, "unknown parameter '" + key + "'");
            output = overrides.has(key) ? overrides[key] : defaults[key];
            // A parameter is a typed literal, not executable source.
            return true;
        }
        output = Json::object();
        for (const auto& [key, value] : input.members()) {
            Json resolved;
            if (!resolve(value, defaults, overrides, resolved, errors, depth + 1)) return false;
            output.set(key, std::move(resolved));
        }
        return true;
    }
    if (input.isArray()) {
        output = Json::array();
        for (const auto& value : input.items()) {
            Json resolved;
            if (!resolve(value, defaults, overrides, resolved, errors, depth + 1)) return false;
            output.push(std::move(resolved));
        }
        return true;
    }
    output = input;
    return true;
}
} // namespace vfxgraph_detail

// Versioned authoring format:
// {
//   "version": 1,
//   "parameters": {"color":"#ffffffff"},
//   "nodes": [
//     {"id":"spawn","type":"Spawn","properties":{"burst":10}},
//     {"id":"init","type":"Initialize","properties":{"life":1}},
//     {"id":"update","type":"Update","properties":{"color":{"$param":"color"}}},
//     {"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:soft"}}
//   ],
//   "links": [{"from":"spawn","to":"init"}, ...],
//   "outputs": ["out"]
// }
// Each output is a single-parent path; paths may fan out (multiple emitters).
// Properties are existing FxEffect.h emitter fields; later nodes override earlier.
// Nested module objects merge recursively. Unknown properties are rejected by parseEffect.
// Graph validation does not perform any IO, shader compilation, or runtime mutation.
inline bool compileSpectraGraph(const Json& graph, const Json& overrides, EffectDef& out,
                            std::vector<std::string>& errors) {
    if(graph.isObject() && graph.getInt("version")==2)
        return compileSpectraGraphV2(graph,overrides,out,errors);
    errors.clear();
    out = {};
    if (!graph.isObject() || !graph["version"].isNumber() || graph.getInt("version") != 1)
        return vfxgraph_detail::fail(errors, "expected version 1 graph object");
    for (const auto& [key, value] : graph.members()) {
        (void)value;
        if (key != "version" && key != "parameters" && key != "nodes" &&
            key != "links" && key != "outputs" && key != "forceFields")
            return vfxgraph_detail::fail(errors, "unknown root property '" + key + "'");
    }
    const Json& defaults = graph["parameters"];
    if (!defaults.isNull() && !defaults.isObject())
        return vfxgraph_detail::fail(errors, "parameters must be an object");
    if (!overrides.isObject())
        return vfxgraph_detail::fail(errors, "overrides must be an object");
    for (const auto& [name, value] : overrides.members()) {
        (void)value;
        if (!defaults.has(name))
            return vfxgraph_detail::fail(errors, "unknown override '" + name + "'");
    }
    const Json& nodes = graph["nodes"];
    const Json& links = graph["links"];
    const Json& outputs = graph["outputs"];
    if (!nodes.isArray() || nodes.size() == 0 || nodes.size() > 256)
        return vfxgraph_detail::fail(errors, "nodes must contain 1..256 entries");
    if (!links.isArray() || links.size() > 512)
        return vfxgraph_detail::fail(errors, "links must be an array of up to 512 entries");
    if (!outputs.isArray() || outputs.size() == 0 || outputs.size() > 16)
        return vfxgraph_detail::fail(errors, "outputs must contain 1..16 node IDs");

    std::map<std::string, size_t> indices;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Json& n = nodes[i];
        if (!n.isObject() || !n["id"].isString() || !n["type"].isString())
            return vfxgraph_detail::fail(errors, "node requires string id and type");
        const std::string id = n["id"].str(), type = n["type"].str();
        if (!vfxgraph_detail::nameOk(id) || !vfxgraph_detail::nodeType(type))
            return vfxgraph_detail::fail(errors, "invalid id/type at node " + std::to_string(i));
        if (!indices.emplace(id, i).second)
            return vfxgraph_detail::fail(errors, "duplicate node id '" + id + "'");
        if (!n["properties"].isNull() && !n["properties"].isObject())
            return vfxgraph_detail::fail(errors, "properties of '" + id + "' must be an object");
        // Authoring-only metadata: ignored by the compiler and particle backend.
        // Coordinates travel with graph files so graph layout survives editor restarts.
        if (n.has("position")) {
            const Json& pos = n["position"];
            if (!pos.isArray() || pos.size() != 2 || !pos[size_t(0)].isNumber() || !pos[size_t(1)].isNumber() ||
                std::abs(pos[size_t(0)].toDouble()) > 100000.0 || std::abs(pos[size_t(1)].toDouble()) > 100000.0)
                return vfxgraph_detail::fail(errors, "position of '" + id + "' must be two bounded numeric coordinates");
        }
        for (const auto& [key, value] : n.members()) {
            (void)value;
            if (key != "id" && key != "type" && key != "properties" && key != "position")
                return vfxgraph_detail::fail(errors, "unknown node key '" + key + "'");
        }
    }

    std::map<std::string, std::string> parent;
    std::set<std::pair<std::string, std::string>> edges;
    for (const auto& link : links.items()) {
        if (!link.isObject() || !link["from"].isString() || !link["to"].isString() || link.size() != 2)
            return vfxgraph_detail::fail(errors, "link requires {from,to} strings");
        const std::string from = link["from"].str(), to = link["to"].str();
        if (!indices.count(from) || !indices.count(to) || from == to)
            return vfxgraph_detail::fail(errors, "invalid link " + from + " -> " + to);
        if (!edges.emplace(from, to).second || parent.count(to))
            return vfxgraph_detail::fail(errors, "duplicate link or multiple inputs to '" + to + "'");
        if (vfxgraph_detail::outputType(nodes[indices.at(from)]["type"].str()) ||
            nodes[indices.at(to)]["type"].str() == "Spawn")
            return vfxgraph_detail::fail(errors, "output cannot have outgoing links; Spawn cannot have inputs");
        parent[to] = from;
    }

    // Reject cycles even in unconnected/unused graph branches.
    for (const auto& [id, idx] : indices) {
        (void)idx;
        std::set<std::string> seen;
        std::string cur = id;
        while (!cur.empty()) {
            if (!seen.emplace(cur).second)
                return vfxgraph_detail::fail(errors, "cycle involving '" + cur + "'");
            auto it = parent.find(cur);
            cur = it == parent.end() ? std::string{} : it->second;
        }
    }

    Json emitterArray = Json::array();
    std::set<std::string> seenOutputs;
    for (const auto& outputId : outputs.items()) {
        if (!outputId.isString() || !indices.count(outputId.str()))
            return vfxgraph_detail::fail(errors, "outputs must reference existing node IDs");
        const std::string outId = outputId.str();
        if (!seenOutputs.emplace(outId).second ||
            !vfxgraph_detail::outputType(nodes[indices.at(outId)]["type"].str()))
            return vfxgraph_detail::fail(errors, "duplicate or non-output node '" + outId + "'");
        std::vector<std::string> path;
        for (std::string cur = outId; !cur.empty();) {
            path.push_back(cur);
            auto it = parent.find(cur);
            cur = it == parent.end() ? std::string{} : it->second;
        }
        if (path.size() < 3 || nodes[indices.at(path.back())]["type"].str() != "Spawn" ||
            nodes[indices.at(path[path.size()-2])]["type"].str() != "Initialize")
            return vfxgraph_detail::fail(errors, "output '" + outId + "' needs Spawn -> Initialize -> [Update...] -> Output");
        for (size_t i = 1; i + 2 < path.size(); ++i)
            if (nodes[indices.at(path[i])]["type"].str() != "Update")
                return vfxgraph_detail::fail(errors, "only Update nodes may appear between Initialize and Output");
        Json emitter = Json::object();
        for (size_t i = path.size(); i-- > 0;) {
            const Json& node = nodes[indices.at(path[i])];
            if (node["properties"].isNull()) continue;
            Json resolved;
            if (!vfxgraph_detail::resolve(node["properties"], defaults, overrides, resolved, errors))
                return false;
            vfxgraph_detail::overlay(emitter, resolved);
        }
        const std::string type = nodes[indices.at(outId)]["type"].str();
        if (type == "RibbonOutput") {
            emitter.set("hidden", Json::fromBool(true));
            Json trail = emitter["trail"];
            if (!trail.isObject()) trail = Json::object();
            trail.set("mode", Json::string("ribbon"));
            emitter.set("trail", std::move(trail));
        } else if (type == "MeshOutput" && !emitter.has("meshes"))
            return vfxgraph_detail::fail(errors, "MeshOutput '" + outId + "' needs meshes property");
        emitterArray.push(std::move(emitter));
    }
    Json compiled = Json::object();
    compiled.set("emitters", std::move(emitterArray));
    if (graph.has("forceFields")) {
        Json resolved;
        if (!vfxgraph_detail::resolve(graph["forceFields"], defaults, overrides, resolved, errors))
            return false;
        compiled.set("forceFields", std::move(resolved));
    }
    EffectDef parsed;
    if (!parseEffect(compiled, "vfx graph", parsed, errors)) {
        out = {};
        return false;
    }
    out = std::move(parsed);
    return true;
}

inline bool compileSpectraGraph(const Json& graph, EffectDef& out,
                            std::vector<std::string>& errors) {
    return compileSpectraGraph(graph, Json::object(), out, errors);
}


// Compatibility aliases for code written before the SpectraFX public API.
// Keep until dependent native projects have migrated.
inline bool compileVfxGraph(const Json& graph, const Json& overrides,
                            EffectDef& out, std::vector<std::string>& errors) {
    return compileSpectraGraph(graph, overrides, out, errors);
}
inline bool compileVfxGraph(const Json& graph, EffectDef& out,
                            std::vector<std::string>& errors) {
    return compileSpectraGraph(graph, out, errors);
}

} // namespace aiverse::fx

// The v2 frontend lowers to the v1 compiler above. Include only AFTER
// its definition: the header guard makes direct V2 includes safe, too.
#include "SpectraGraphV2.h"
