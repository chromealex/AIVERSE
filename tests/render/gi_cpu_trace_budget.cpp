#include "engine/render/GiCpuTraceBudget.h"
#include "tests/AlwaysCheck.h"
#include <stdexcept>

using namespace aiverse::render;

int main() {
    GiCpuTraceBudget mobile;
    AIVERSE_TEST_CHECK(mobile.maxProbes()==4 && mobile.targetMs()>1.0);
    AIVERSE_TEST_CHECK(mobile.choose(0)==0);
    AIVERSE_TEST_CHECK(mobile.choose(100)<=4);

    mobile.configure(.5,16);
    AIVERSE_TEST_CHECK(mobile.choose(100)==1); // cold-start .5 ms/probe
    mobile.observe(1,.05);                    // observed .05 ms/probe
    AIVERSE_TEST_CHECK(mobile.choose(100)==10);
    AIVERSE_TEST_CHECK(mobile.choose(2)==2);
    mobile.observe(10,10.0);                 // ten slow probes, backpressure
    AIVERSE_TEST_CHECK(mobile.choose(100)==1);
    AIVERSE_TEST_CHECK(mobile.choose(0)==0);
    mobile.configure(.8,3);
    AIVERSE_TEST_CHECK(mobile.estimatedProbeMs()==0);
    AIVERSE_TEST_CHECK(mobile.choose(100)<=3);

    bool rejected=false;
    try { mobile.configure(0,4); } catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try { mobile.configure(1,0); } catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
    rejected=false;
    try { mobile.configure(1,1025); } catch(const std::invalid_argument&) { rejected=true; }
    AIVERSE_TEST_CHECK(rejected);
}
