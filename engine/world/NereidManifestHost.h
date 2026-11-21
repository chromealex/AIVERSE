// Small native presentation host shared by editor and renderbench.
// Reuses NEREID parsing/residency/draw; does not create gameplay water authority.
#pragma once
#include "engine/world/NereidWaterManifest.h"
#include "engine/world/NereidUnderwaterPrisma.h"
namespace aiverse::world {
class NereidManifestHost {
public:
    // Parse completely before changing the last-good scene. A missing block
    // explicitly clears water when opening another project/map.
    void configure(const Json& root) {
        const auto signature=nereidEntityManifest(root).dump();
        if(signature==signature_)return;
        auto config=parseNereidWaterManifest(root);
        scene_.setMaxUploadsPerFrame(config.maxUploadsPerFrame);
        scene_.setAsyncPreparation(config.asyncPrepare);
        scene_.setMaxWaterStagingBytes(config.maxStagingBytes);
        scene_.setMaxEstimatedUploadBytes(config.maxEstimatedUploadBytes);
        scene_.setVisibilityRange(config.visibilityRange);
        configured_=config.surfaces.size();
        scene_.setSurfaces(std::move(config.surfaces));
        signature_=signature;
    }
    void prepare(gfx::Device& device,render::float3 camera) {
        const auto fixed=[](float v){
            if(!std::isfinite(v)||std::abs(v)>1000000.f)throw std::out_of_range("NEREID host camera");
            return Fixed::fromRaw(int64_t(std::llround(double(v)*Fixed::kOne)));
        };
        const Vec3 observer{fixed(camera.x),fixed(camera.y),fixed(camera.z)};
        scene_.update(device,&observer);
        observer_=observer;
    }
    NereidUnderwaterView underwater()const{return sampleNereidUnderwater(scene_,observer_);}
    void updateUnderwater(render::Prisma& prisma,float deltaSeconds)const{
        prisma.setUnderwater(nereidPrismaUnderwaterInput(underwater(),observer_.y.toDouble()),deltaSeconds);
    }
    void draw(render::Renderer3D& renderer)const{scene_.draw(renderer,{},1);}
    void shutdown(gfx::Device& device){scene_.shutdown(device);signature_.clear();configured_=0;}
    size_t configuredSurfaces()const{return configured_;}
private:
    NereidWaterScene scene_;
    Vec3 observer_{};
    std::string signature_;
    size_t configured_=0;
};
} // namespace aiverse::world
