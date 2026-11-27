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
        const bool skeleton = params.getBool("skeleton");
        render::float3 low{}, high{};
        bool hasPoint = false;
        auto include = [&](render::float3 point) {
            if (!hasPoint) { low = high = point; hasPoint = true; return; }
            low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
            high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
        };
        std::vector<render::mat4> palette;
        const auto& model = *built.model;
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            const auto& node = model.nodes[i];
            if (skeleton) include(world[i].transformPoint({}));
            if (node.mesh < 0) continue;
            for (const auto& primitive : model.meshes[size_t(node.mesh)].primitives) {
                palette.clear();
                if (node.skin >= 0 && !primitive.jointPalette.empty())
                    render::paletteMatrices(model, model.skins[size_t(node.skin)], primitive, world, palette);
                for (uint32_t j = 0; j < primitive.indexCount; ++j) {
                    const auto& vertex = model.vertices[model.indices[primitive.firstIndex + j]];
                    if (palette.empty()) {
                        include(world[i].transformPoint(vertex.pos));
                        continue;
                    }
                    render::float3 position{};
                    for (size_t k = 0; k < 4; ++k)
                        if (vertex.weights[k]) position = position + palette[vertex.joints[k]].transformPoint(vertex.pos) * (vertex.weights[k] / 255.f);
                    include(position);
                }
            }
        }
        setModel(std::move(built.model));
        render::Camera camera;
        camera.target = (low + high) * .5f;
        const float radius = std::max(.01f, render::length(high - low) * .5f);
        camera.distance = radius * 1.08f / std::sin(camera.fovY * .5f);
        camera.zNear = camera.distance - radius * 1.05f;
        camera.zFar = camera.distance + radius * 1.05f;
        camera.minDistance = .001f;
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
                if (skeleton && node.parent >= 0)
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
