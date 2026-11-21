#include "engine/view/GridGeometry.h"
#include "../AlwaysCheck.h"
#include <cstdint>
#include <vector>
int main(){
    std::vector<uint32_t> idx{99};
    aiverse::view::appendQuadGridIndices(idx,2,2);
    AIVERSE_TEST_CHECK(idx==std::vector<uint32_t>({99,0,3,1,0,2,3}));
    idx.clear();
    aiverse::view::appendQuadGridIndices(idx,3,2);
    AIVERSE_TEST_CHECK(idx==std::vector<uint32_t>({0,4,1,0,3,4,1,5,2,1,4,5}));
    auto unchanged=idx;
    aiverse::view::appendQuadGridIndices(idx,1,8);
    AIVERSE_TEST_CHECK(idx==unchanged);
}
