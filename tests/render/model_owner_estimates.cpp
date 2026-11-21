// Stage 103 deferred CPU test: retirement estimate by owner excludes
// borrowed images; unique shared textures are counted at cache owner once.
#include "engine/render/ModelResidentEstimate.h"
#include "engine/render/AsyncResourceRetirementQueue.h"
#include "../AlwaysCheck.h"
#include <cstdint>
#include <memory>
#include <vector>

int main() {
    using namespace aiverse::render;
    AsyncResourceRetirementQueue<std::unique_ptr<Model>> queue;
    auto first=std::make_unique<Model>();
    first->vb={1};first->ib={2};
    first->vertices.resize(8);
    first->indices.resize(12);
    Image borrowed;
    borrowed.tex={11};borrowed.borrowedTexture=true;
    borrowed.width=512;borrowed.height=512;
    first->images.push_back(borrowed);
    Image owned;
    owned.tex={12};owned.width=16;owned.height=16;
    first->images.push_back(owned);
    auto second=std::make_unique<Model>();
    second->images.push_back(borrowed); // same lease; NOT model-owned
    const uint64_t firstBytes=estimateModelResident(*first).knownBytes();
    AIVERSE_TEST_CHECK(firstBytes==8*sizeof(ModelVertex)+12*sizeof(uint32_t)+16*16*4);
    AIVERSE_TEST_CHECK(estimateModelResident(*second).knownBytes()==0);
    queue.enqueue(std::move(first));
    queue.enqueue(std::move(second));
    const uint64_t known=queue.estimatedPendingBytes(
        [](const std::unique_ptr<Model>& m)->uint64_t {
            return m?estimateModelResident(*m).knownBytes():0;
        });
    AIVERSE_TEST_CHECK(known==firstBytes);
    AIVERSE_TEST_CHECK(queue.pending()==2);
    const auto before=queue.stats();
    AIVERSE_TEST_CHECK(before.highWatermark==2);
    AIVERSE_TEST_CHECK(!queue.saturated(3));
    AIVERSE_TEST_CHECK(queue.saturated(2));
}
