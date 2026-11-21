// Stage 106 deferred: failed fence is NOT normal pending and NEVER
// permits GPU resource release. Only a proven full-idle drain may reclaim.
#include "engine/render/AsyncResourceRetirementQueue.h"
#include "../AlwaysCheck.h"
#include <cstdint>
#include <memory>
#include <vector>

int main() {
    using Queue=aiverse::render::AsyncResourceRetirementQueue<std::unique_ptr<int>>;
    Queue queue;
    uint64_t next=0;
    bool tokenFailed=false,done=false,idle=false;
    std::vector<int> freed;
    std::vector<uint64_t> closed;
    auto begin=[&]{return ++next;};
    auto completed=[&](uint64_t){return done;};
    auto close=[&](uint64_t token){closed.push_back(token);};
    auto wait=[&]{return idle;};
    auto release=[&](std::unique_ptr<int>& model){freed.push_back(*model);};
    auto failed=[&](uint64_t){return tokenFailed;};

    queue.enqueue(std::make_unique<int>(10));
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,wait,release,failed));
    AIVERSE_TEST_CHECK(queue.stats().pendingResources==1);
    AIVERSE_TEST_CHECK(queue.stats().failedFenceBatches==0);
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,wait,release,failed));
    AIVERSE_TEST_CHECK(queue.stats().failedFenceBatches==0);
    AIVERSE_TEST_CHECK(freed.empty());

    tokenFailed=true;
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,wait,release,failed));
    AIVERSE_TEST_CHECK(queue.stats().failedFenceBatches==1);
    AIVERSE_TEST_CHECK(queue.stats().unresolvedFailedFences==1);
    for(int i=0;i<5;++i)
        AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,wait,release,failed));
    AIVERSE_TEST_CHECK(queue.stats().failedFenceBatches==1);
    AIVERSE_TEST_CHECK(freed.empty());
    AIVERSE_TEST_CHECK(closed.empty());

    // Even a later contradictory success callback cannot free an
    // already failed batch: it remains flagged until explicit idle.
    done=true;
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,wait,release,failed));
    AIVERSE_TEST_CHECK(freed.empty());
    queue.enqueue(std::make_unique<int>(20));
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,wait,release,failed));
    AIVERSE_TEST_CHECK(queue.pending()==2);
    AIVERSE_TEST_CHECK(!queue.drain(wait,close,release));
    AIVERSE_TEST_CHECK(freed.empty() && closed.empty());
    idle=true; // explicit owner-side full GPU completion proof
    AIVERSE_TEST_CHECK(queue.drain(wait,close,release));
    AIVERSE_TEST_CHECK((freed==std::vector<int>{10,20}));
    AIVERSE_TEST_CHECK((closed==std::vector<uint64_t>{1,2}));
    AIVERSE_TEST_CHECK(queue.stats().unresolvedFailedFences==0);
    AIVERSE_TEST_CHECK(queue.empty());
}
