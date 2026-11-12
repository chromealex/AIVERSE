// Builds a light bake scene from engine models (agent-lightbake): loads every model once from disk
// (CPU geometry), generates missing lightmap UVs (writes "<model>.lmuv" through `write`), converts
// materials (base colour, base texture for albedo / alpha test, emissive) and adds one instance per
// node with a mesh. Also turns a height function into a terrain mesh with planar lightmap UVs.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "BakeScene.h"

namespace aiverse::bake {

class SceneBuilder {
public:
    SceneBuilder(BakeScene& scene, render::FileReader read, WriteFn write, float texelsPerUnit)
        : scene_(scene), read_(std::move(read)), write_(std::move(write)), tpu_(texelsPerUnit) {}
    // nodeWorld: world matrix of every node (as drawn); overrides: per-instance materials (null = the model's).
    // False with a message when the model can't be loaded.
    bool addModel(const std::string& path, const std::vector<mat4>& nodeWorld, const std::vector<render::Material>* overrides,
                  bool receive, float scale, std::string& error);
    // Terrain over [x0, x0 + ex] x [z0, z0 + ez], one vertex every `step` world units.
    void addTerrain(const std::function<float(float, float)>& heightAt, float x0, float z0, float ex, float ez, float step, float3 albedo);
    std::vector<std::string> warnings;
    int unwrapped = 0;   // models that got generated lightmap UVs
private:
    struct Loaded { std::unique_ptr<render::Model> model; std::vector<int> meshIndex; std::map<int, int> material; };
    Loaded* load(const std::string& path, std::string& error);
    int material(Loaded& l, const render::Material& m, int key);
    BakeScene& scene_;
    render::FileReader read_;
    WriteFn write_;
    float tpu_;
    std::map<std::string, Loaded> models_;
    std::map<std::string, std::shared_ptr<render::Image>> images_;
    std::map<uint64_t, int> materialIds_;
};

} // namespace aiverse::bake
