// RHI GPU timing router: deterministic tests with a fake tagged backend.
// No graphics device, GPU driver, network, or TerraWeave world is required.
#include "engine/gfx/GpuTimingRouter.h"
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

using aiverse::gfx::GpuTimingRouter;

namespace {
struct FakeDevice {
    struct Timing {
        uint64_t tag;
        double milliseconds;
        bool valid;
    };
    bool supported=true;
    bool busy=false;
    std::vector<uint64_t> accepted;
    std::vector<Timing> ready;
    bool timeGpuFrame(uint64_t tag) {
        if (!supported || busy) return false;
        accepted.push_back(tag);
        return true;
    }
    std::vector<Timing> takeGpuTimings() {
        return std::exchange(ready,{});
    }
    void complete(size_t index,double milliseconds=1.0,bool valid=true) {
        assert(index<accepted.size());
        ready.push_back({accepted[index],milliseconds,valid});
    }
};

void testInterleavedSubscribersAndOriginalTags() {
    GpuTimingRouter router;
    FakeDevice gpu;
    const auto a=router.subscribe(), b=router.subscribe();
    assert(a && b && a!=b);
    assert(router.request(gpu,a,42));
    assert(router.request(gpu,b,42)); // same client tag, different backend tags
    assert(router.request(gpu,a,43));
    assert(gpu.accepted[0]!=gpu.accepted[1]);
    gpu.complete(1,2.5);
    gpu.complete(2,3.5,false);
    gpu.complete(0,1.25);
    router.poll(gpu);
    assert(router.pendingRequests()==0);
    const auto forB=router.take(b);
    assert(forB.size()==1 && forB[0].tag==42);
    assert(forB[0].valid && forB[0].milliseconds==2.5);
    const auto forA=router.take(a);
    assert(forA.size()==2);
    assert(forA[0].tag==43 && !forA[0].valid);
    assert(forA[1].tag==42 && forA[1].valid);
    assert(forA[1].milliseconds==1.25);
    assert(router.take(a).empty() && router.take(b).empty());
}

void testCancellationUnsubscribeAndLateCompletion() {
    GpuTimingRouter router;
    FakeDevice gpu;
    const auto a=router.subscribe(), b=router.subscribe();
    assert(router.request(gpu,a,7));
    assert(router.request(gpu,b,7));
    assert(router.request(gpu,a,8));
    router.cancel(a,7);
    router.unsubscribe(b);
    assert(router.pendingRequests()==1);
    assert(router.subscribers()==1);
    const auto c=router.subscribe();
    assert(c && c!=a && c!=b);
    assert(router.request(gpu,c,7));
    gpu.complete(0); // canceled a
    gpu.complete(1); // unsubscribed b
    gpu.complete(3,4.0); // c
    gpu.complete(2,8.0); // surviving a
    router.poll(gpu);
    assert(router.unownedResults()==2);
    assert(router.pendingRequests()==0);
    const auto aResult=router.take(a);
    const auto cResult=router.take(c);
    assert(aResult.size()==1 && aResult[0].tag==8);
    assert(cResult.size()==1 && cResult[0].tag==7);
    assert(cResult[0].milliseconds==4.0);
    assert(router.take(b).empty());
}

void testBackendRejectionAndBusyGpu() {
    GpuTimingRouter router;
    FakeDevice gpu;
    const auto owner=router.subscribe();
    gpu.supported=false;
    assert(!router.request(gpu,owner,1));
    assert(router.pendingRequests()==0 && gpu.accepted.empty());
    gpu.supported=true;
    gpu.busy=true; // GL backend can reject a second exclusive timer
    assert(!router.request(gpu,owner,2));
    assert(router.pendingRequests()==0);
    gpu.busy=false;
    assert(router.request(gpu,owner,3));
    gpu.busy=true;
    assert(!router.request(gpu,owner,4));
    gpu.complete(0,0.9);
    router.poll(gpu);
    const auto samples=router.take(owner);
    assert(samples.size()==1 && samples[0].tag==3);
    assert(!router.request(gpu,0,5));
    assert(!router.request(gpu,99,5));
}

void testSubscriberAndPendingCaps() {
    GpuTimingRouter router;
    FakeDevice gpu;
    std::vector<GpuTimingRouter::Subscriber> owners;
    for (int n=0;n<32;++n) {
        const auto id=router.subscribe();
        assert(id);
        owners.push_back(id);
    }
    assert(router.subscribe()==0 && router.subscribers()==32);
    for (int n=0;n<64;++n)
        assert(router.request(gpu,owners[n%32],static_cast<uint64_t>(n)));
    assert(router.pendingRequests()==64);
    assert(!router.request(gpu,owners[0],65));
    assert(gpu.accepted.size()==64);
    for (size_t i=0;i<64;++i) gpu.complete(i);
    router.poll(gpu);
    assert(router.pendingRequests()==0);
    for (int n=0;n<32;++n) {
        const auto samples=router.take(owners[n]);
        assert(samples.size()==2);
        assert(samples[0].tag==static_cast<uint64_t>(n));
        assert(samples[1].tag==static_cast<uint64_t>(n+32));
    }
    router.unsubscribe(owners[0]);
    assert(router.subscribe()!=0);
}

void testMailboxOverflowAndDeviceIsolation() {
    GpuTimingRouter router;
    FakeDevice gpu;
    const auto a=router.subscribe();
    for (uint64_t n=0;n<65;++n) {
        assert(router.request(gpu,a,n));
        gpu.complete(static_cast<size_t>(n));
        router.poll(gpu);
    }
    assert(router.droppedResults()==1);
    const auto results=router.take(a);
    assert(results.size()==64);
    assert(results.front().tag==1 && results.back().tag==64);
    assert(router.droppedResults()==1);
    assert(router.pendingRequests()==0);

    // Each Device embeds a separate router. The raw tag sequence may restart,
    // but its backend queue and subscriber mailboxes never cross devices.
    GpuTimingRouter otherRouter;
    FakeDevice otherGpu;
    const auto owner=otherRouter.subscribe();
    assert(otherRouter.request(otherGpu,owner,100));
    otherGpu.complete(0,5.0);
    otherRouter.poll(otherGpu);
    const auto other=otherRouter.take(owner);
    assert(other.size()==1 && other[0].tag==100);
    assert(other[0].milliseconds==5.0);
    assert(router.take(a).empty());
}
} // namespace

int main() {
    testInterleavedSubscribersAndOriginalTags();
    testCancellationUnsubscribeAndLateCompletion();
    testBackendRejectionAndBusyGpu();
    testSubscriberAndPendingCaps();
    testMailboxOverflowAndDeviceIsolation();
    return 0;
}
