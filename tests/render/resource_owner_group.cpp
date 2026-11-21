// Stage 105 deferred CPU-only regression: independent owners aggregated
// without counting duplicate host registrations or borrowed images as VRAM.
// Do not construct a GPU Device or pretend estimates equal driver heap bytes.
#include "engine/render/RenderResourceOwnerGroup.h"
#include "../AlwaysCheck.h"
#include <cstdint>
#include <memory>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::render;
    ModelResourceCache scene,portrait,raw;
    auto model=[](uint32_t id) {
        auto out=std::make_unique<Model>();
        out->vb={id};
        out->vertices.resize(8);
        out->ib={id+20};
        out->indices.resize(12);
        // Model A and B may refer to the same externally owned texture
        // handle; neither owns it, so neither is billed for that image.
        Image borrowed;
        borrowed.tex={100};
        borrowed.width=128;
        borrowed.height=128;
        borrowed.borrowedTexture=true;
        out->images.push_back(std::move(borrowed));
        return out;
    };
    scene.insertIfAbsent("scene.glb",model(1));
    portrait.insertIfAbsent("portrait.glb",model(2));
    raw.insertIfAbsent("raw.glb",model(3));
    RenderResourceOwnerGroup group;
    group.add(scene);
    group.add(portrait);
    group.add(raw);
    group.add(scene); // idempotent registration
    AIVERSE_TEST_CHECK(group.size()==3);
    const auto healthy=group.snapshotOwners();
    AIVERSE_TEST_CHECK(healthy.uniqueOwners==3);
    AIVERSE_TEST_CHECK(healthy.residentModels==3);
    AIVERSE_TEST_CHECK(healthy.retiredModels==0);
    AIVERSE_TEST_CHECK(healthy.uniqueTextureOwners==3);
    AIVERSE_TEST_CHECK(healthy.uniqueTextures==0); // borrowed handles not cache-owned
    AIVERSE_TEST_CHECK(healthy.geometryPages.uniquePages==0);
    AIVERSE_TEST_CHECK(healthy.health==RenderResourceHealth::Ready);
    const auto bytes=uint64_t(3)*(8*sizeof(ModelVertex)+12*sizeof(uint32_t));
    AIVERSE_TEST_CHECK(healthy.residentModelKnownBytes==bytes);
    AIVERSE_TEST_CHECK(healthy.estimatedKnownBytes()==bytes);
    group.setEstimatedBudgetBytes(bytes);
    AIVERSE_TEST_CHECK(!group.mayAdmitNewSources(healthy));
    group.setEstimatedBudgetBytes(bytes+1);
    AIVERSE_TEST_CHECK(group.mayAdmitNewSources(healthy));
    const auto unhealthy=group.snapshotOwners(false);
    AIVERSE_TEST_CHECK(unhealthy.health==RenderResourceHealth::DeviceUnhealthy);
    AIVERSE_TEST_CHECK(!group.mayAdmitNewSources(unhealthy));
    group.clear();
    AIVERSE_TEST_CHECK(group.size()==0);
    AIVERSE_TEST_CHECK(group.snapshotOwners().estimatedKnownBytes()==0);
}
