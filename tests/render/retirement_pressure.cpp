// Stage 101 deferred: backpressure never bypasses GPU completion.
#include "engine/render/AsyncResourceRetirementQueue.h"
#include "../AlwaysCheck.h"
#include <memory>
#include <set>
#include <vector>

int main() {
    using Queue=aiverse::render::AsyncResourceRetirementQueue<std::unique_ptr<int>>;
    Queue queue;
    std::set<uint64_t> completed;
    std::vector<int> destroyed;
    uint64_t next=1;
    bool waitOk=false;
    auto issue=[&]{return next++;};
    auto done=[&](uint64_t token){return completed.contains(token);};
    auto close=[](uint64_t){};
    auto wait=[&]{return waitOk;};
    auto release=[&](std::unique_ptr<int>& ptr){destroyed.push_back(*ptr);};

    queue.enqueue(std::make_unique<int>(10));
    queue.enqueue(std::make_unique<int>(20));
    AIVERSE_TEST_CHECK(queue.saturated(2));
    AIVERSE_TEST_CHECK(queue.stats().highWatermark==2);
    AIVERSE_TEST_CHECK(!queue.collect(issue,done,close,wait,release));
    AIVERSE_TEST_CHECK(queue.stats().pendingBatches==1);
    AIVERSE_TEST_CHECK(queue.stats().unsubmittedResources==0);
    for(int i=0;i<4;++i)
        AIVERSE_TEST_CHECK(!queue.collect(issue,done,close,wait,release));
    AIVERSE_TEST_CHECK(queue.stats().pendingPolls>=4);
    AIVERSE_TEST_CHECK(destroyed.empty()); // pressure is not permission to free

    queue.enqueue(std::make_unique<int>(30));
    AIVERSE_TEST_CHECK(queue.stats().highWatermark==3);
    AIVERSE_TEST_CHECK(!queue.collect(issue,done,close,wait,release));
    AIVERSE_TEST_CHECK(queue.stats().pendingBatches==2);
    AIVERSE_TEST_CHECK(queue.stats().pendingResources==3);
    completed.insert(1);
    AIVERSE_TEST_CHECK(!queue.collect(issue,done,close,wait,release));
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{10,20}));
    AIVERSE_TEST_CHECK(queue.stats().completedResources==2);
    AIVERSE_TEST_CHECK(queue.stats().pendingResources==1);
    AIVERSE_TEST_CHECK(!queue.saturated(2));

    completed.insert(2);
    AIVERSE_TEST_CHECK(queue.collect(issue,done,close,wait,release));
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{10,20,30}));
    AIVERSE_TEST_CHECK(queue.stats().completedResources==3);
    AIVERSE_TEST_CHECK(queue.stats().pendingBatches==0);
    AIVERSE_TEST_CHECK(queue.stats().highWatermark==3);

    // No token available and no full idle fence: resources are retained.
    queue.enqueue(std::make_unique<int>(40));
    auto noToken=[]()->uint64_t{return 0;};
    AIVERSE_TEST_CHECK(!queue.collect(noToken,done,close,wait,release));
    AIVERSE_TEST_CHECK(queue.stats().tokenFailures==1);
    AIVERSE_TEST_CHECK(queue.stats().idleFallbackFailures==1);
    AIVERSE_TEST_CHECK(queue.stats().pendingResources==1);
    AIVERSE_TEST_CHECK(destroyed.size()==3);
    waitOk=true;
    AIVERSE_TEST_CHECK(queue.collect(noToken,done,close,wait,release));
    AIVERSE_TEST_CHECK(queue.empty());
    AIVERSE_TEST_CHECK(destroyed.back()==40);
}
