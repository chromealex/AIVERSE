#include "apps/editor/core/ProjectHistory.h"
#include "apps/studio-native/StudioAgentHistoryPolicy.h"
#include "tests/TestCheck.h"
#include <iostream>
using namespace aiverse::editor;
int main(){
    ProjectHistory a,b;
    AIVERSE_TEST_CHECK(a.versionToken()!=b.versionToken());
    std::cout<<"PASS distinct owner epochs\n";
    a.record({{"f.json","a","b"}},"first");
    const auto id=a.next(false).id;
    AIVERSE_TEST_CHECK(id==a.versionToken());
    a.advance(false);AIVERSE_TEST_CHECK(a.canRedo()&&a.next(true).id==id);
    a.advance(true);AIVERSE_TEST_CHECK(a.next(false).id==id);
    std::cout<<"PASS stable step IDs across undo/redo\n";
    a.advance(false);const auto version=a.version();a.record({{"f.json","a","a"}},"no-op");
    AIVERSE_TEST_CHECK(a.canRedo()&&a.version()==version);
    a.record({{"f.json","a","c"}},"branch");AIVERSE_TEST_CHECK(!a.canRedo()&&a.next(false).id!=id);
    std::cout<<"PASS no-op and redo branch fencing\n";
    for(size_t i=0;i<100;++i)a.record({{"f.json","a","b"}},"edit");
    AIVERSE_TEST_CHECK(a.steps().size()==64&&a.position()==64);
    std::cout<<"PASS bounded eviction\n";
    ProjectHistory::MovePlan m;m.quarantine=true;m.created=true;
    m.files.push_back({"asset","trash","","digest",false});
    a.recordMove(std::move(m),"import");
    AIVERSE_TEST_CHECK(a.next(false).move->created&&a.next(false).id==a.versionToken());
    std::cout<<"PASS typed move recording\n";
    a.record({{"f.json",std::string(ProjectHistory::MaxBytes,'x'),"b"}},"oversize");
    AIVERSE_TEST_CHECK(a.steps().empty()&&a.bytes()==0);
    std::cout<<"PASS oversized history barrier\n";
    using aiverse::studio::studioAgentHistoryPolicy;
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("fake_get"))=="unverified");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("heightmap_export"))=="unverified");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("rules_get"))=="read");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("studio_asset_move_commit"))=="assets");
    std::cout<<"PASS explicit agent policy\n";
}
