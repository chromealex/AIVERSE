// Stage 108 deferred: bounded event ring, overflow accounting and weak
// observer lifetime. A ring is NOT evidence of hardware GPU completion.
#include "engine/render/RetirementDiagnosticRing.h"
#include "engine/render/AsyncResourceRetirementQueue.h"
#include "../AlwaysCheck.h"
#include <memory>
#include <vector>
using namespace aiverse::render;
int main() {
    auto ring=std::make_shared<RetirementDiagnosticRing>(2);
    auto observer=RetirementDiagnosticRing::observe(ring);
    observer({RetirementDiagnosticKind::Enqueued,0,1});
    observer({RetirementDiagnosticKind::TokenSubmitted,11,1});
    observer({RetirementDiagnosticKind::Pending,11,1});
    AIVERSE_TEST_CHECK(ring->receivedCount()==3);
    AIVERSE_TEST_CHECK(ring->overflowCount()==1);
    const auto snapshot=ring->snapshot();
    AIVERSE_TEST_CHECK(snapshot.size()==2);
    AIVERSE_TEST_CHECK(snapshot.front().kind==RetirementDiagnosticKind::TokenSubmitted);
    AIVERSE_TEST_CHECK(snapshot.back().kind==RetirementDiagnosticKind::Pending);
    // Host observers can tag Model and Image events while the same bounded
    // ring remains agnostic about GPU completion and retirement permission.
    observer({RetirementDiagnosticKind::Completed,11,2,
              RetirementResourceKind::Model});
    observer({RetirementDiagnosticKind::Failed,12,3,
              RetirementResourceKind::Image});
    const auto tagged=ring->snapshot();
    AIVERSE_TEST_CHECK(tagged.size()==2);
    AIVERSE_TEST_CHECK(tagged[0].resource==RetirementResourceKind::Model);
    AIVERSE_TEST_CHECK(tagged[1].resource==RetirementResourceKind::Image);
    AIVERSE_TEST_CHECK(ring->overflowCount()==3);
    observer({RetirementDiagnosticKind::TokenSubmitted,13,1,
              RetirementResourceKind::Target});
    const auto targetTagged=ring->snapshot();
    AIVERSE_TEST_CHECK(targetTagged.size()==2);
    AIVERSE_TEST_CHECK(targetTagged.back().resource==RetirementResourceKind::Target);
    AIVERSE_TEST_CHECK(ring->overflowCount()==4);
    auto empty=std::make_shared<RetirementDiagnosticRing>(0);
    RetirementDiagnosticRing::observe(empty)(
        {RetirementDiagnosticKind::Failed,12,1});
    AIVERSE_TEST_CHECK(empty->size()==0 && empty->overflowCount()==1);
    ring.reset();
    observer({RetirementDiagnosticKind::Completed,11,1}); // expired weak ref

    AsyncResourceRetirementQueue<std::unique_ptr<int>> queue;
    queue.setDiagnosticObserver([](const auto&) {
        throw 1; // diagnostic failure MUST NOT affect resource safety
    });
    queue.enqueue(std::make_unique<int>(1));
    AIVERSE_TEST_CHECK(queue.pending()==1);
}
