#include "apps/editor/core/ProjectHistory.h"
#include "apps/studio-native/StudioAgentHistoryPolicy.h"
#include "apps/studio-native/StudioHistoryCoordinator.h"
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
    for(const auto* op:{"world_entity","world_entities","world_regions_status","rules_get"})
        AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy(op))=="read");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("studio_asset_move_commit"))=="assets");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("studio_owner_history"))=="read");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("studio_asset_history_reset_review"))=="read");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("studio_save_documents"))=="unverified");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("studio_owner_history_step"))=="unverified");
    AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy("planet_forest_get"))=="read");
    for(const auto* op:{"planet_forest_set","planet_forest_plant","planet_forest_move","planet_forest_remove"})
        AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy(op))=="map");
    for(const auto* op:{"view_ground","view_pick","studio_planet_forest_live","studio_planet_scene_get"})
        AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy(op))=="read"&&aiverse::studio::studioSceneViewAgentOperation(op));
    for(const auto* op:{"view_gizmos","view_focus","studio_planet_scene_body_set","studio_planet_scene_camera_set"})
        AIVERSE_TEST_CHECK(std::string(studioAgentHistoryPolicy(op))=="view"&&aiverse::studio::studioSceneViewAgentOperation(op));
    AIVERSE_TEST_CHECK(!aiverse::studio::studioSceneViewAgentOperation("studio.planetForestSet"));
    std::cout<<"PASS explicit agent policy\n";
    using namespace aiverse::studio;
    std::map<std::string,HistoryBatchOwner> owners={
        {"map",{{"user","m1","m2"},3}},{"project",{{"p1"},1}},{"assets",{{"a1"},1}}
    };
    const std::vector<HistoryBatchStep> links={{"map","m1"},{"project","p1"},{"assets","a1"},{"map","m2"}};
    const auto undo=planHistoryBatch(links,owners,false);
    AIVERSE_TEST_CHECK(undo.front().id=="m2"&&undo.back().id=="m1");
    owners["map"].position=1;owners["project"].position=0;owners["assets"].position=0;
    const auto redo=planHistoryBatch(links,owners,true);
    AIVERSE_TEST_CHECK(redo.front().id=="m1"&&redo.back().id=="m2");
    owners["map"].ids.insert(owners["map"].ids.begin()+2,"manual");
    bool rejected=false;
    try{(void)planHistoryBatch(links,owners,true);}catch(const std::runtime_error&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    std::cout<<"PASS cross-owner batch boundaries and intervening edit refusal\n";
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("studio-batch-chain-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto file=root/"data.json";
    std::ofstream(file)<<"a";
    HistoryFileBatch batch;batch.append({{file,"a","b"}});batch.append({{file,"b","c"}});
    const auto plan=batch.plan();
    AIVERSE_TEST_CHECK(plan.size()==1&&plan[0].before=="a"&&plan[0].after=="c");
    rejected=false;
    try{batch.append({{file,"wrong","d"}});}catch(const std::runtime_error&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    std::ofstream(file)<<"external";
    rejected=false;
    try{(void)batch.plan();}catch(const std::runtime_error&){rejected=true;}
    AIVERSE_TEST_CHECK(rejected);
    fs::remove_all(root);
    std::cout<<"PASS merged file chain and external conflict refusal\n";
}
