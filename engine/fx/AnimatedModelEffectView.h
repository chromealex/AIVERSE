#pragma once
#include "ParticleSystem.h"
#include "../render/Animation.h"
#include "../render/ModelAssetProvider.h"
#include "../render/Renderer3D.h"

namespace aiverse::fx {
// Model and texture lifetime belongs to the native provider. The submitted draw borrows
// frame-local pose/material arrays, retained here through the renderer's scene submission.
class AnimatedModelEffectView {
    struct Frame {
        render::Pose pose;
        std::vector<render::mat4> nodes;
        std::vector<render::Material> materials;
    };
    render::ModelAssetProvider assets_;
    std::vector<ParticleSystem::ModelInstance> instances_;
    std::vector<Frame> frames_;
public:
    void setReader(render::FileReader reader) { assets_.setReader(std::move(reader)); }
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease lease) { assets_.setGpuDeviceLease(std::move(lease)); }
    void shutdown(gfx::Device& dev) { frames_.clear(); instances_.clear(); assets_.shutdown(dev); }

    void submit(gfx::Device& dev, render::Renderer3D& renderer, const ParticleSystem& ps) {
        ps.modelInstances(instances_);
        frames_.resize(instances_.size());
        for (size_t i=0; i<instances_.size(); ++i) {
            const auto& instance = instances_[i];
            const auto& effect = ps.def(instance.effect)->model;
            const auto* model = assets_.load(dev,effect.source);
            if (!model) continue;
            auto& frame = frames_[i];
            const float age = effect.loop ? std::fmod(instance.time,effect.duration) : instance.time;
            const float phase = std::clamp(age/effect.duration,0.f,1.f);
            render::bindPose(*model,frame.pose);
            const int clip = effect.clip.empty() ? (model->clips.empty() ? -1 : 0) : model->findClip(effect.clip);
            if (clip >= 0) render::sampleClip(model->clips[size_t(clip)],phase*model->clips[size_t(clip)].duration,frame.pose);
            render::worldMatrices(*model,frame.pose,frame.nodes);
            render::ModelDraw draw;
            draw.model = model; draw.world = instance.world; draw.tint = instance.tint;
            if (!effect.castShadow) draw.flags |= render::kDrawNoCastShadow;
            draw.nodeWorld = &frame.nodes; draw.pose = &frame.pose;
            if (!effect.materials.empty()) {
                frame.materials = model->materials;
                for (const auto& track : effect.materials) {
                    for (auto& material : frame.materials) {
                        if (!track.material.empty() && material.name != track.material) continue;
                        if(!track.blend.empty())render::parseMaterialBlend(track.blend,material.alpha);
                        if (!track.color.empty()) {
                            const auto c = sampleColor(track.color,phase);
                            material.baseColor.x *= c.x; material.baseColor.y *= c.y;
                            material.baseColor.z *= c.z; material.baseColor.w *= c.w;
                            if (material.baseColor.w < 1 && !render::alphaBlended(material.alpha)) material.alpha = render::AlphaMode::Blend;
                        }
                        material.uvOffset.x += age*track.uvSpeed.x;
                        material.uvOffset.y += age*track.uvSpeed.y;
                        if (!track.textures.empty()) {
                            const size_t at = std::min(track.textures.size()-1,size_t(phase*track.textures.size()));
                            const auto& name = track.textures[at];
                            const int image = model->findImage(name);
                            if (image >= 0) material.baseTex = image;
                        }
                    }
                }
                draw.materials = &frame.materials;
            }
            renderer.drawModel(draw);
        }
    }
};
} // namespace aiverse::fx
