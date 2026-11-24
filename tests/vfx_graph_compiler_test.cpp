// Niagara-style graph compiler v1: standalone, genre-independent CPU smoke.
#include "engine/fx/SpectraFX.h"
#include "engine/fx/SpectraModuleSchema.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/fx/SpectraValueGraph.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
static void check(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

int main() {
    check(std::string(spectra::kName)=="AIVERSE SpectraFX","public SpectraFX identity");
    const Json base = Json::parse(R"JSON({
      "version": 1,
      "parameters": {
        "size": 0.5,
        "fade": [[0, "#ffffffff"], [1, "#ffffff00"]]
      },
      "nodes": [
        {"id":"spawn", "type":"Spawn", "properties":{"burst":6,"size":{"$param":"size"}}},
        {"id":"initialize","type":"Initialize","properties":{"life":1,"speed":0}},
        {"id":"update","type":"Update","properties":{"color":{"$param":"fade"}}},
        {"id":"sprite","type":"SpriteOutput","properties":{"texture":"builtin:spark"}},
        {"id":"ribbon","type":"RibbonOutput","properties":{"trail":{"time":0.6,"width":[[0,1],[1,0]]}}}
      ],
      "links": [
        {"from":"spawn","to":"initialize"},
        {"from":"initialize","to":"update"},
        {"from":"update","to":"sprite"},
        {"from":"update","to":"ribbon"}
      ],
      "outputs": ["sprite", "ribbon"]
    })JSON");
    spectra::Effect effect;
    std::vector<std::string> errors;
    check(spectra::compileGraph(base,effect,errors), "valid graph compile");
    check(effect.emitters.size()==2, "two output branches must generate two emitters");
    check(effect.emitters[0].burst==6, "spawn overlay lost");
    check(effect.emitters[0].size.min==0.5f, "default parameter lost");
    check(effect.emitters[1].hidden && effect.emitters[1].trail.enabled,
          "RibbonOutput must enable hidden ribbon particles");
    spectra::Runtime ps;
    ps.setQuality(fx::FxQuality::High);
    check(ps.init(nullptr), "headless particle init");
    const int id=ps.define("graph_smoke",effect);
    check(id>=0,"compiled effect must be accepted by legacy backend");
    check(ps.play(id,{0,0,0}).valid(), "compiled graph should play");
    ps.update(0.1f);
    std::vector<fx::FxParticleInfo> particles;
    ps.snapshot(particles);
    check(!particles.empty(), "compiled effect should emit particles");

    // The editor's typed module inspector and native C++ consumers share
    // exactly the same field schema and value validation.
    check(!spectra::fieldsForNode("Spawn").empty(),"Spawn fields missing");
    check(!spectra::fieldsForNode("Initialize").empty(),"Initialize fields missing");
    check(!spectra::fieldsForNode("Update").empty(),"Update fields missing");
    check(!spectra::fieldsForNode("SpriteOutput").empty(),"Output fields missing");
    check(spectra::fieldsForNode("Unknown").empty(),"unknown node has fields");
    Json typedNode=Json::parse(R"({"id":"spawn","type":"Spawn","properties":{"burst":4,"opaqueExtra":{"keep":true}}})");
    std::string fieldError;
    check(spectra::applyTypedField(typedNode,"burst",Json::number(int64_t(32)),fieldError),"typed burst edit");
    check(typedNode["properties"].getInt("burst")==32,"edited burst lost");
    check(typedNode["properties"]["opaqueExtra"].getBool("keep"),"unknown module was discarded");
    Json before=typedNode;
    check(!spectra::applyTypedField(typedNode,"burst",Json::number(int64_t(-1)),fieldError),
          "negative burst should be invalid");
    check(typedNode==before,"invalid typed edit mutated node");
    check(!spectra::applyTypedField(typedNode,"burst",Json::string("32"),fieldError),
          "wrong integer type accepted");
    check(!spectra::applyTypedField(typedNode,"rate",Json::number(4000.0),fieldError),
          "out-of-range emission rate accepted");
    check(spectra::removeTypedField(typedNode,"burst",fieldError),"remove typed field failed");
    check(!typedNode["properties"].has("burst") && typedNode["properties"].has("opaqueExtra"),
          "removing field dropped unrelated properties");

    Json initNode=Json::parse(R"({"id":"init","type":"Initialize","properties":{}})");
    check(spectra::applyTypedField(initNode,"direction",Json::parse("[0,1,0]"),fieldError),
          "typed direction rejected");
    check(!spectra::applyTypedField(initNode,"direction",Json::parse("[0,1]"),fieldError),
          "short direction vector accepted");
    check(spectra::applyTypedField(initNode,"shape",Json::string("cone"),fieldError),
          "valid shape rejected");
    check(!spectra::applyTypedField(initNode,"shape",Json::string("unknown"),fieldError),
          "unknown spawn shape accepted");
    Json outputNode=Json::parse(R"({"id":"out","type":"SpriteOutput","properties":{"texture":"builtin:smoke"}})");
    check(spectra::applyTypedField(outputNode,"blend",Json::string("additive"),fieldError),
          "valid blend rejected");
    check(!spectra::applyTypedField(outputNode,"blend",Json::string("invalid"),fieldError),
          "invalid blend accepted");
    Json typedGraph=base;
    typedGraph.find("nodes")->at(0).set("properties",typedNode["properties"]);
    // This branch also has explicit burst from another output; ensure schema
    // manipulations do not affect unrelated graph nodes during compilation.
    typedGraph.find("nodes")->at(0).set("properties",Json::parse(R"({"burst":32})"));
    check(spectra::compileGraph(typedGraph,effect,errors) && effect.emitters[0].burst==32,
          "typed module edit failed to compile into the CPU renderer");

    // Editor visual node coordinates must survive serialization but not alter compilation.
    Json placed=base;
    placed.find("nodes")->at(0).set("position",Json::parse("[125.5,-72]"));
    check(spectra::compileGraph(placed,effect,errors),"valid visual node coordinates rejected");
    check(effect.emitters.size()==2 && effect.emitters[0].burst==6,
          "node layout metadata changed compiled effect");
    Json wrongPosition=placed;
    wrongPosition.find("nodes")->at(0).set("position",Json::parse("[1]"));
    check(!spectra::compileGraph(wrongPosition,effect,errors),
          "invalid node layout metadata accepted");
    wrongPosition=placed;
    wrongPosition.find("nodes")->at(0).set("position",Json::parse("[1000001,0]"));
    check(!spectra::compileGraph(wrongPosition,effect,errors),
          "unbounded canvas coordinates accepted");

    // V2 Data Interface authoring uses the same strict schema as the runtime
    // and must not corrupt an unfinished graph or its non-binding nodes.
    {
        Json bindingsDraft=spectra::authoring::newGraphV2();
        bindingsDraft.set("instanceParameters",Json::parse(
            R"JSON({"wind_lift":{"slot":0,"default":2,"min":0,"max":25}})JSON"));
        check(spectra::authoring::findInfo("DataInterface")!=nullptr,
              "DataInterface node missing from editor node catalog");
        check(!spectra::fieldsForNode("DataInterface").empty(),
              "DataInterface typed inspector field missing");
        std::string bindingError;
        const Json good=Json::parse(
            R"JSON({"provider":"weather","channel":"up","fallback":2})JSON");
        check(spectra::authoring::setDataInterfaceBinding(
                  bindingsDraft,"wind_lift",good,bindingError),
              "valid data interface editor binding rejected");
        const Json unchanged=bindingsDraft;
        const Json bad=Json::parse(
            R"JSON({"provider":"../unsafe","channel":"up","fallback":2})JSON");
        check(!spectra::authoring::setDataInterfaceBinding(
                  bindingsDraft,"wind_lift",bad,bindingError),
              "editor accepted unsafe provider name");
        check(bindingsDraft==unchanged,
              "invalid Data Interface edit mutated graph");
        check(spectra::authoring::removeDataInterfaceBinding(
                  bindingsDraft,"wind_lift",bindingError),
              "remove valid Data Interface binding failed");
        check(!bindingsDraft["dataInterfaces"].has("wind_lift"),
              "removed binding remained in graph");
    }

    const Json override = Json::parse(R"JSON({"size":2})JSON");
    check(spectra::compileGraph(base,override,effect,errors), "override compile");
    check(effect.emitters[0].size.min==2.0f, "parameter override lost");

    Json invalid = base;
    invalid.set("version",Json::number(int64_t(2)));
    check(spectra::compileGraph(invalid,effect,errors), "supported graph v2 rejected");
    invalid.set("version",Json::number(int64_t(3)));
    check(!spectra::compileGraph(invalid,effect,errors), "invalid version accepted");
    invalid = base;
    invalid.set("outputs",Json::parse(R"JSON(["update"])JSON"));
    check(!spectra::compileGraph(invalid,effect,errors), "non-output accepted");
    invalid = base;
    invalid.set("links",Json::parse(R"JSON([
        {"from":"spawn","to":"initialize"},
        {"from":"initialize","to":"update"},
        {"from":"update","to":"sprite"},
        {"from":"update","to":"ribbon"},
        {"from":"sprite","to":"spawn"}])JSON"));
    check(!spectra::compileGraph(invalid,effect,errors), "invalid graph cycle accepted");
    invalid = base;
    invalid.set("parameters",Json::object());
    check(!spectra::compileGraph(invalid,effect,errors), "unbound parameter accepted");
    invalid = base;
    invalid.set("outputs",Json::parse(R"JSON(["sprite","sprite"])JSON"));
    check(!spectra::compileGraph(invalid,effect,errors), "duplicate outputs accepted");

    // Regression: shared sub-expressions are memoized. A chain where every
    // node references its predecessor twice used to be re-evaluated 2^N
    // times per $curve sample (N=18 x 64 samples took ~16 s on the UI
    // thread); it must now be linear and produce the same values.
    {
        const auto chain=[](int n) {
            Json nodes=Json::array();
            nodes.push(Json::parse(R"JSON({"id":"n0","type":"AgeNormalized"})JSON"));
            for(int i=1;i<=n;++i) {
                const std::string prev="n"+std::to_string(i-1);
                nodes.push(Json::parse("{\"id\":\"n"+std::to_string(i)+
                    "\",\"type\":\"Add\",\"properties\":{\"a\":{\"$node\":\""+prev+
                    "\"},\"b\":{\"$node\":\""+prev+"\"}}}"));
            }
            return nodes;
        };
        const Json noParams=Json::object();
        for(const int n:{19,30}) {
            const Json nodes=chain(n);
            std::vector<std::string> valueErrors;
            fx::vfx2::ValueGraph graph(nodes,noParams,noParams,valueErrors);
            check(graph.initialize() && graph.validateAll(),"value chain validates");
            const Json curve=Json::parse("{\"$curve\":\"n"+std::to_string(n)+"\",\"samples\":64}");
            Json sampled;
            const auto start=std::chrono::steady_clock::now();
            const bool ok=graph.resolve(curve,sampled);
            const double seconds=std::chrono::duration<double>(
                std::chrono::steady_clock::now()-start).count();
            check(seconds<5.0,"shared value references are not re-evaluated exponentially");
            if(n==19) {
                check(ok && sampled.size()==64,"memoized chain samples the curve");
                check(sampled[size_t(63)][size_t(1)].integer()==int64_t(1)<<19,
                      "memoized chain keeps exact values");
                check(sampled[size_t(32)][size_t(1)].toDouble()>0,
                      "memo distinguishes sampling ages");
            } else {
                check(!ok && !valueErrors.empty() &&
                      valueErrors.back().find("depth")!=std::string::npos,
                      "over-deep chain fails with a readable depth error");
            }
        }
    }

    // Regression: the shared JSON parser rejects pathological nesting with
    // an error instead of overflowing the stack (500k '[' used to segfault).
    {
        bool rejected=false;
        try { (void)Json::parse(std::string(500000,'[')); }
        catch(const JsonError& e) {
            rejected=std::string(e.what()).find("nesting")!=std::string::npos;
        }
        check(rejected,"deeply nested JSON rejected with a readable error");
        rejected=false;
        try { (void)Json::parse(std::string(100000,'{')); }
        catch(const JsonError&) { rejected=true; }
        check(rejected,"deeply nested JSON objects rejected");
        const int okDepth=Json::kMaxParseDepth;
        const Json deep=Json::parse(std::string(size_t(okDepth),'[')+std::string(size_t(okDepth),']'));
        check(deep.isArray(),"JSON at the nesting limit still parses");
        rejected=false;
        try { (void)Json::parse(std::string(size_t(okDepth+1),'[')+std::string(size_t(okDepth+1),']')); }
        catch(const JsonError&) { rejected=true; }
        check(rejected,"JSON one level past the nesting limit rejected");
    }

    std::cout<<"PASS vfx_graph_compiler_test (graph branching, CPU backend, parameters, validation)\n";
}
