#include "apps/studio-native/StudioSpectraTransferLease.h"
#include "apps/studio-native/StudioSpectraIdentity.h"
#include <set>
#include "tests/TestCheck.h"
#include <iostream>
using namespace aiverse::studio;
using namespace std::chrono_literals;

int main() {
    std::set<uint64_t> identities;
    for(int i=0;i<4096;++i) {
        const auto id=nextSpectraIdentity();
        AIVERSE_TEST_CHECK(id>0 && id<=9007199254740991ULL && identities.insert(id).second);
    }
    SpectraTransferLease lease;
    const auto start=SpectraTransferLease::TimePoint{};
    AIVERSE_TEST_CHECK(!lease.expire(start+60s));
    lease.id=1;
    lease.touched=start;
    lease.bytes.resize(1024*1024);
    AIVERSE_TEST_CHECK(!lease.expire(start+29s));
    AIVERSE_TEST_CHECK(lease.id==1 && lease.bytes.size()==1024*1024);
    lease.touched=start+29s;
    AIVERSE_TEST_CHECK(!lease.expire(start+58s));
    AIVERSE_TEST_CHECK(!lease.expire(start+59s,true));
    AIVERSE_TEST_CHECK(lease.expire(start+59s));
    AIVERSE_TEST_CHECK(lease.id==0 && lease.bytes.empty() && lease.bytes.capacity()==0);
    lease.id=2;
    lease.bytes.resize(32);
    lease.reset();
    AIVERSE_TEST_CHECK(lease.id==0 && lease.bytes.capacity()==0);
    AIVERSE_TEST_CHECK(!lease.expire(start+100s));
    std::cout<<"SpectraFX transfer lease contract PASS\n";
}
