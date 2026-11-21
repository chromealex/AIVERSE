// Stage 104 deferred regression: cross-cache shared page identity is not
// charged per model. Backend page-capacity validation needs a real GPU.
#include "engine/render/ModelResourceCache.h"
#include "engine/render/GlobalGeometryPool.h"
#include "../AlwaysCheck.h"
#include <memory>
#include <set>
#include <vector>

namespace aiverse::platform {
bool readContentFile(const std::string&,std::string&){return false;}
}
int main() {
    using namespace aiverse::render;
    auto backing=std::make_shared<int>(1);
    // The page layout is deliberately private to GlobalGeometryPool.cpp.
    // An aliasing shared_ptr provides an identity without accessing that
    // incomplete type or allocating fake GPU buffers.
    auto page=std::shared_ptr<GeometryPoolPage>(
        backing,reinterpret_cast<GeometryPoolPage*>(backing.get()));
    ModelResourceCache one,two;
    auto make=[&] {
        auto model=std::make_unique<Model>();
        model->geometryPoolPage=page;
        return model;
    };
    one.insertIfAbsent("first.glb",make());
    one.insertIfAbsent("second.glb",make());
    two.insertIfAbsent("third.glb",make());
    const auto a=one.geometryPoolModelReferences();
    const auto b=two.geometryPoolModelReferences();
    AIVERSE_TEST_CHECK(a.size()==2 && b.size()==1);
    std::set<const GeometryPoolPage*> identities;
    for(const Model* model:a)identities.insert(model->geometryPoolPage.get());
    for(const Model* model:b)identities.insert(model->geometryPoolPage.get());
    AIVERSE_TEST_CHECK(identities.size()==1); // device owner would charge page once
    AIVERSE_TEST_CHECK(one.gpuOwnerEstimate().sharedGeometryResidentModels==2);
    AIVERSE_TEST_CHECK(two.gpuOwnerEstimate().sharedGeometryResidentModels==1);
    // geometryPoolReferencedStats(device, combined models) is the
    // real backend/device-owner entrypoint. Do not sum host reports.
}
