// Build a normal Shader Graph document. The regular material loader, graph compiler
// and texture dependencies handle the resulting material on every model path.
#pragma once
#include "ShaderGraph.h"
#include <cmath>
#include <stdexcept>

namespace aiverse::render::graph {

inline Json makeTriplanarMaterial(const std::string& colorMap, const std::string& normalMap,
                                  double repeatMetres) {
    if (!std::isfinite(repeatMetres) || repeatMetres < 0.001 || repeatMetres > 100000)
        throw std::invalid_argument("Triplanar repeat must be 0.001..100000 metres");

    Json nodes = Json::array();
    auto node = [&](const std::string& id, const char* op, std::initializer_list<std::string> inputs) -> Json& {
        Json row = Json::object();
        row.set("id", Json::string(id));
        row.set("op", Json::string(op));
        if (inputs.size()) {
            Json list = Json::array();
            for (const auto& input : inputs) list.push(Json::string(input));
            row.set("inputs", std::move(list));
        }
        nodes.push(std::move(row));
        return nodes.at(nodes.size() - 1);
    };
    node("zero","constant",{}).set("value",Json::number(0.0));
    node("four","constant",{}).set("value",Json::number(4.0));
    node("repeatMetres","parameter",{}).set("value",Json::number(repeatMetres,6));
    node("position","worldPosition",{});
    node("normal","normal",{});
    node("scaled","divide",{"position","repeatMetres"});
    node("absolute","abs",{"normal"});
    node("weight","pow",{"absolute","four"});
    node("faces","sign",{"normal"});
    const char* axes[] = {"x","y","z"};
    const char* channels[] = {"red","green","blue"};
    for (int i=0;i<3;++i) {
        const std::string axis = axes[i];
        node(axis,channels[i],{"scaled"});
        node(axis+"Weight",channels[i],{"weight"});
        node(axis+"Sign",channels[i],{"faces"});
    }
    node("xyWeight","add",{"xWeight","yWeight"});
    node("totalWeight","add",{"xyWeight","zWeight"});
    // Right-handed projection bases: X=(-signX*Z,Y), Y=(X,-signY*Z),
    // Z=(signZ*X,Y). Reverse faces keep the same tangent-space handedness.
    node("xU","negate",{"xSign"});
    node("yV","negate",{"ySign"});
    node("xCoord","multiply",{"xU","z"});
    node("yCoord","multiply",{"yV","z"});
    node("zCoord","multiply",{"zSign","x"});
    node("xUV","combine",{"xCoord","y","zero","zero"});
    node("yUV","combine",{"x","yCoord","zero","zero"});
    node("zUV","combine",{"zCoord","y","zero","zero"});
    for (const std::string axis : axes) {
        node(axis+"Blend","divide",{axis+"Weight","totalWeight"});
        auto& color = node(axis+"Color","sampleTexture",{axis+"UV"});
        color.set("texture",Json::string(colorMap));
        color.set("srgb",Json::fromBool(true));
        node(axis+"Tint","multiply",{axis+"Color",axis+"Blend"});
        if (normalMap.empty()) continue;
        node(axis+"Map","sampleTexture",{axis+"UV"}).set("texture",Json::string(normalMap));
        node(axis+"Tangent","normalUnpack",{axis+"Map"});
        node(axis+"R","red",{axis+"Tangent"});
        node(axis+"G","green",{axis+"Tangent"});
    }
    node("xyColor","add",{"xTint","yTint"});
    node("color","add",{"xyColor","zTint"});
    Json outputs = Json::object();
    outputs.set("color",Json::string("color"));
    if (!normalMap.empty()) {
        node("xZ","multiply",{"xR","xU"});
        node("yZ","multiply",{"yG","yV"});
        node("zX","multiply",{"zR","zSign"});
        node("xWorld","combine",{"zero","xG","xZ","zero"});
        node("yWorld","combine",{"yR","zero","yZ","zero"});
        node("zWorld","combine",{"zX","zG","zero","zero"});
        for (const std::string axis : axes) node(axis+"Detail","multiply",{axis+"World",axis+"Blend"});
        node("xyDetail","add",{"xDetail","yDetail"});
        node("detail","add",{"xyDetail","zDetail"});
        node("perturbed","add",{"normal","detail"});
        node("shadingNormal","normalize3",{"perturbed"});
        outputs.set("normal",Json::string("shadingNormal"));
    }
    Json graph = Json::object();
    graph.set("version",Json::number(int64_t(1)));
    graph.set("domain",Json::string("material"));
    graph.set("nodes",std::move(nodes));
    graph.set("outputs",std::move(outputs));
    compile(graph,"material");
    Json material = Json::object();
    material.set("shaderGraph",std::move(graph));
    return material;
}
}
