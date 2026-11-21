// Stage 107 deferred deterministic trace: no actual GPU needed.
// Verify pending vs failure, sticky failure, no early model disposal,
// and explicit full-idle recovery using replayed completion statuses.
#include "engine/render/RetirementStatusReplay.h"
#include "engine/render/AsyncResourceRetirementQueue.h"
#include <algorithm>
#include "../AlwaysCheck.h"
#include <memory>
#include <vector>

int main() {
    using namespace aiverse;
    using namespace aiverse::render;
    using Status=gfx::ResourceRetirementStatus;
    RetirementStatusReplay replay;
    replay.script(1,{Status::Pending,Status::Pending,Status::Complete});
    replay.script(2,{Status::Pending,Status::Failed});
    replay.script(3,{Status::Pending,Status::Complete});
    AsyncResourceRetirementQueue<std::unique_ptr<int>> queue;
    std::vector<int> destroyed;
    std::vector<RetirementDiagnosticEvent> trace;
    queue.setDiagnosticObserver([&](const auto& event){trace.push_back(event);});
    bool idle=false;
    auto begin=[&]{return replay.issue();};
    auto completed=[&](uint64_t token){return replay.complete(token);};
    auto close=[](uint64_t){};
    auto waitIdle=[&]{return idle;};
    auto destroy=[&](std::unique_ptr<int>& ptr){destroyed.push_back(*ptr);};
    auto failed=[&](uint64_t token){return replay.failed(token);};

    queue.enqueue(std::make_unique<int>(10));
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,waitIdle,destroy,failed));
    AIVERSE_TEST_CHECK(queue.stats().pendingResources==1);
    replay.advance();
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,waitIdle,destroy,failed));
    AIVERSE_TEST_CHECK(queue.stats().failedFenceBatches==0);
    replay.advance();
    AIVERSE_TEST_CHECK(queue.collect(begin,completed,close,waitIdle,destroy,failed));
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{10}));

    queue.enqueue(std::make_unique<int>(20));
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,waitIdle,destroy,failed));
    replay.advance(); // token #2 fails
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,waitIdle,destroy,failed));
    AIVERSE_TEST_CHECK(queue.stats().failedFenceBatches==1);
    AIVERSE_TEST_CHECK(queue.stats().unresolvedFailedFences==1);
    queue.enqueue(std::make_unique<int>(30));
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,waitIdle,destroy,failed));
    replay.advance(); // token #3 completes but token #2 stays failed
    AIVERSE_TEST_CHECK(!queue.collect(begin,completed,close,waitIdle,destroy,failed));
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{10}));
    AIVERSE_TEST_CHECK(!queue.drain(waitIdle,close,destroy));
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{10}));
    idle=true; // synthetic explicit full-GPU completion barrier
    AIVERSE_TEST_CHECK(queue.drain(waitIdle,close,destroy));
    AIVERSE_TEST_CHECK((destroyed==std::vector<int>{10,20,30}));
    AIVERSE_TEST_CHECK(queue.stats().unresolvedFailedFences==0);

    const auto failures=std::count_if(trace.begin(),trace.end(),[](const auto& e) {
        return e.kind==RetirementDiagnosticKind::Failed;
    });
    const auto recoveries=std::count_if(trace.begin(),trace.end(),[](const auto& e) {
        return e.kind==RetirementDiagnosticKind::RecoveredAfterIdle;
    });
    AIVERSE_TEST_CHECK(failures==1 && recoveries==1);
    AIVERSE_TEST_CHECK(replay.events().size()>=5);
    AIVERSE_TEST_CHECK(replay.status(999)==Status::Failed); // unknown token never completes
}
