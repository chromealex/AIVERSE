// Host-only authoring. Documents are independent of uploaded runtime models.
#pragma once
#include "engine/data/Json.h"
#include "engine/render/Model.h"
#include <atomic>
#include <map>
#include <mutex>
namespace aiverse::modeling {
using namespace render;
struct Mesh {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
};
struct GeometryCache {
    std::mutex mutex;
    std::map<std::string, Mesh> meshes;
    size_t vertices = 0;
};
struct Build {
    std::unique_ptr<Model> model;
    Json report;
};
Json catalog();
Json emptyDocument();
Json evaluateDocument(const Json &document);
Json robotDocument();
Json edit(const Json &document, const Json &commands);
Build build(const Json &document, const std::atomic<bool> *cancel = nullptr, GeometryCache *cache = nullptr);
Mesh geometry(const Json &specification, const std::atomic<bool> *cancel = nullptr);
Mesh booleanMesh(const Mesh &a, const Mesh &b, const std::string &operation);
void bevelConvex(Mesh &mesh, float amount);
void smoothSubdivision(Mesh &mesh, int levels);
void modifier(Mesh &mesh, const Json &operation);
std::string exportGlb(const Model &model);
Json sample(const Model &model, const Json &request);
Json autoWeights(const Json &document, const Json &request);
Json generateClip(const Json &document, const Json &request);
Json duplicateParts(const Json &document, const Json &request);
Json bake(const Json &document, const Json &request);
float number(const Json &value, float fallback, const std::string &path);
float3 vector3(const Json &value, float3 fallback, const std::string &path);
void require(bool condition, const std::string &message);
} // namespace aiverse::modeling
