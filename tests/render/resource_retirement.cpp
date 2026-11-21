// Stage 99: CPU contract for exact retirement fence and staging order.
// Registered for deferred native execution; no GPU Device is required here.
#include "engine/render/ResourceRetirementQueue.h"
#include "../AlwaysCheck.h"
#include <memory>
#include <vector>

int main() {
    aiverse::render::ResourceRetirementQueue<std::unique_ptr<int>> retired;
    retired.enqueue(std::make_unique<int>(11));
    retired.enqueue(std::make_unique<int>(22));
    AIVERSE_TEST_CHECK(retired.pending()==2);
    bool idle=false;
    int fences=0;
    std::vector<int> destroyed;
    auto fence=[&]{++fences;return idle;};
    auto dispose=[&](const std::unique_ptr<int>& item) {
        destroyed.push_back(*item);
    };

    // Frame number, wall time and age of the queue do not release anything.
    AIVERSE_TEST_CHECK(!retired.collect(fence,dispose));
    AIVERSE_TEST_CHECK(retired.pending()==2 && destroyed.empty() && fences==1);
    AIVERSE_TEST_CHECK(!retired.collect(fence,dispose));
    AIVERSE_TEST_CHECK(destroyed.empty() && fences==2);

    idle=true;
    AIVERSE_TEST_CHECK(retired.collect(fence,dispose));
    AIVERSE_TEST_CHECK(retired.empty() && fences==3);
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{11,22}));
    AIVERSE_TEST_CHECK(retired.collect(fence,dispose));
    AIVERSE_TEST_CHECK(fences==3); // no unnecessary GPU wait when nothing is pending

    retired.enqueue(std::make_unique<int>(33));
    idle=false;
    AIVERSE_TEST_CHECK(!retired.collect(fence,dispose));
    AIVERSE_TEST_CHECK(destroyed.size()==2 && retired.pending()==1);
    idle=true;
    AIVERSE_TEST_CHECK(retired.collect(fence,dispose));
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{11,22,33}));
}
