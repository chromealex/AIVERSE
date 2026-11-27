#pragma once
#include "StudioPanelModelTarget.h"
#include "tools/modeling/Modeling.h"

namespace aiverse::studio {
class StudioModelingPreview final : public StudioPanelModelTarget {
public:
    std::vector<uint8_t> frame(const Json& document, const Json& params) {
        Json settings = params;
        if (params.getBool("useGraph", !params.has("clip")) && document["graph"].isObject())
            settings.set("graph", modeling::evaluateDocument(document)["graph"]);
        else if (!params.has("clip"))
            settings.set("clip", Json::string("Walk"));
        const float time = modeling::number(params["time"], 0, "time");
        modeling::require(time >= 0 && time <= 60, "preview time must be 0..60");
        auto built = modeling::build(document);
        if (!settings["graph"].isObject() && built.model->findClip(settings.getStr("clip")) < 0)
            settings.set("clip", Json::string(""));
        const auto pose = modeling::samplePose(*built.model, settings);
        std::vector<render::mat4> world;
        render::worldMatrices(*built.model, pose, world);
        setModel(std::move(built.model));
        render::Camera camera;
        camera.target = (model_->boundsMin + model_->boundsMax) * .5f;
        camera.distance = std::max(2.f, render::length(model_->boundsMax - model_->boundsMin) * 1.15f);
        camera.minDistance = .01f;
        camera.yaw = modeling::number(params["yaw"], .65f, "yaw");
        camera.pitch = std::clamp(modeling::number(params["pitch"], .4f, "pitch"), -.9f, 1.4f);
        return captureFrame(camera, [&](render::Renderer3D& renderer) {
            render::ModelDraw draw;
            draw.model = model_.get();
            draw.nodeWorld = &world;
            draw.pose = &pose;
            renderer.drawModel(draw);
            for (size_t i = 0; i < model_->nodes.size(); ++i) {
                const auto& node = model_->nodes[i];
                if (params.getBool("skeleton") && node.parent >= 0)
                    renderer.line(world[i].transformPoint({}), world[size_t(node.parent)].transformPoint({}), 0xff33ffff);
                if (!params.getBool("wireframe") || node.mesh < 0 || node.skin >= 0) continue;
                for (const auto& primitive : model_->meshes[size_t(node.mesh)].primitives)
                    for (uint32_t j = 0; j + 2 < primitive.indexCount; j += 3)
                        for (uint32_t k = 0; k < 3; ++k) {
                            const auto triangle = primitive.firstIndex + j;
                            const auto a = model_->vertices[model_->indices[triangle + k]].pos;
                            const auto b = model_->vertices[model_->indices[triangle + (k+1)%3]].pos;
                            renderer.line(world[i].transformPoint(a), world[i].transformPoint(b), 0xff55ddff);
                        }
            }
        });
    }
};
}
