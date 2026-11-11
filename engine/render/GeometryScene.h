#pragma once
#include "Geometry.h"
#include <map>

namespace aiverse::render {
class Renderer3D;
// Static presentation geometry, never collision or simulation objects. HLOD proxies merge
// compatible materials and use bounded-error cluster simplification. Sources stay available.
class GeometryScene {
public:
    struct Stats { uint32_t instances=0, groups=0, proxies=0, submitted=0, surfaceProxiesSubmitted=0; };
    void load(gfx::Device& dev, const Json& json, const FileReader& reader);
    void shutdown(gfx::Device& dev);
    void draw(Renderer3D& renderer);
    const Stats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    static void validate(const Json& json);
private:
    struct Instance { Model* model=nullptr; mat4 world; GeometryPolicy policy; Model* surfaceProxy=nullptr; float proxyPixels=96; };
    struct Group {
        std::vector<size_t> instances;
        int left=-1,right=-1;
        std::unique_ptr<Model> proxy;
        float3 min,max;
        float error=0;
        bool occlusion=false;
    };
    int buildGroup(gfx::Device& dev, std::vector<size_t> ids);
    void drawGroup(Renderer3D& renderer,int group);
    void drawInstance(Renderer3D& renderer,const Instance& instance);
    std::unique_ptr<Model> makeProxy(const std::vector<size_t>& ids,float& error);
    std::map<std::string,std::unique_ptr<Model>> models_;
    std::vector<Instance> instances_;
    std::vector<Group> groups_;
    std::vector<std::string> warnings_;
    Stats stats_;
    bool hlod_=false;
    float errorPixels_=2, maxPixels_=256;
    size_t proxyVertices_=0;
};
}
