#include "engine/render/GiGpuSparsePatch.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace aiverse::render;

int main() {
    AIVERSE_TEST_CHECK(giGpuGrowCapacity(1,1,8192)==1);
    AIVERSE_TEST_CHECK(giGpuGrowCapacity(1,2,8192)==2);
    AIVERSE_TEST_CHECK(giGpuGrowCapacity(128,118,8192)==128);
    AIVERSE_TEST_CHECK(giGpuGrowCapacity(128,129,8192)==256);
    AIVERSE_TEST_CHECK(giGpuGrowCapacity(64,8192,8192)==8192);
    AIVERSE_TEST_CHECK(giGpuGrowCapacity(4,10,10)==10);
    bool rejected=false;
    try {(void)giGpuGrowCapacity(1,8193,8192);}
    catch(const std::invalid_argument&) {rejected=true;}
    AIVERSE_TEST_CHECK(rejected);

    using U=uint32_t;
    const std::vector<U> before{1,2,3,4,5,6,7,8,9,10};
    auto unchanged=giGpuPlanSparsePatch(before,before);
    AIVERSE_TEST_CHECK(!unchanged.full && unchanged.changed==0 &&
                       unchanged.elements==0 && unchanged.ranges.empty());

    auto changed=before;
    changed[4]=50;
    const auto sparse=giGpuPlanSparsePatch(before,changed);
    AIVERSE_TEST_CHECK(!sparse.full && sparse.changed==1 &&
                       sparse.bytes<U>()==4 && sparse.ranges.size()==1);
    AIVERSE_TEST_CHECK(sparse.ranges[0].first==4 &&
                       sparse.ranges[0].count==1);

    // Append within previously allocated device buffer capacity requires no
    // allocation, and only the appended tail needs an updateBuffer blit.
    auto appended=before;
    appended.push_back(11);
    appended.push_back(12);
    const auto tail=giGpuPlanSparsePatch(before,appended);
    AIVERSE_TEST_CHECK(!tail.full && tail.ranges.size()==1);
    AIVERSE_TEST_CHECK(tail.ranges[0].first==10 && tail.ranges[0].count==2);
    AIVERSE_TEST_CHECK(tail.bytes<U>()==8);

    // Shrinking does NOT need a GPU write; the shader's logical triangle/node
    // counts limit traversal so stale tail bytes cannot be read.
    const std::vector<U> trimmed(before.begin(),before.begin()+7);
    const auto shrink=giGpuPlanSparsePatch(before,trimmed);
    AIVERSE_TEST_CHECK(!shrink.full && shrink.ranges.empty() &&
                       shrink.bytes<U>()==0);

    // Many dirty tiny changes trigger one full upload rather than dozens of
    // staging allocations. All *active* elements are copied, not truncated.
    std::vector<U> manyOld(64,0),manyNew(64,0);
    for(size_t i=0;i<64;i+=2)manyNew[i]=1;
    const auto full=giGpuPlanSparsePatch(manyOld,manyNew,false,8);
    AIVERSE_TEST_CHECK(full.full && full.ranges.size()==1 &&
                       full.ranges[0].first==0 && full.ranges[0].count==64 &&
                       full.bytes<U>()==256);
    // A newly allocated buffer always uploads all active bytes.
    const auto allocated=giGpuPlanSparsePatch(before,changed,true);
    AIVERSE_TEST_CHECK(allocated.full && allocated.ranges.size()==1 &&
                       allocated.ranges[0].count==changed.size());
}
