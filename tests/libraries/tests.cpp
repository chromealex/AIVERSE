#include <aiverselib/Collections.h>
#include <aiverselib/Graphs.h>
#include <aiverselib/Grid.h>
#include <aiverselib/Behavior.h>
#include <aiverselib/Tasks.h>
#include <aiverselib/Rules.h>
#include <aiverselib/Progression.h>
#include <aiverselib/Logistics.h>
#include <aiverselib/Objectives.h>
#include <aiverselib/Presentation.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "Failed line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::abort(); } } while(false)
int main() {
    using namespace aiverse::lib;
    Queue<int> q; CHECK(!q.pop()); q.push(7); q.push(8); CHECK(q.pop()==7); CHECK(q.pop()==8); CHECK(q.empty());
    const std::array items{3,1,2}; auto top=topIndices<int>(items,2,[](int){return 1;}); CHECK(top==std::vector<std::size_t>({0,1}));
    CHECK(topIndices<int>(items,0,[](int x){return x;}).empty());
    auto paths=bfs({{1},{2},{},{}},0); CHECK(paths && paths->distance[2]==2 && paths->distance[3]==kUnreachable); CHECK(!bfs({{9}},0)); CHECK(!bfs({},0));
    CHECK(!topological({{1},{0}})); CHECK(topological({{2},{2},{}})==std::vector<std::size_t>({0,1,2})); CHECK(topological({{1,1},{}})==std::vector<std::size_t>({0,1}));
    CHECK(topological({})->empty());
    const std::array<std::uint8_t,6> mask{1,0,1,1,1,1}; const std::array<std::size_t,1> seeds{0};
    auto distances=gridDistance(3,2,mask,seeds); CHECK(distances && (*distances)[1]==-1 && (*distances)[2]==4);
    CHECK(!gridDistance(0,2,mask,seeds)); CHECK(!gridDistance(3,3,mask,seeds));
    const std::array<std::size_t,1> invalidSeed{6}; CHECK(!gridDistance(3,2,mask,invalidSeed));
    std::int64_t current=0; const std::array defs{Transition{0,2,1}}; CHECK(!transition(current,defs,1)); CHECK(transition(current,defs,2) && current==1);
    const std::array<std::int64_t,3> candidates{3,2,1}; CHECK(choose(candidates,[](auto){return 5;})==1); CHECK(!choose(std::span<const std::int64_t>{},[](auto){return 1;}));
    std::array tasks{Task{2,1},Task{1,1}}; CHECK(!claim(tasks,0)); CHECK(claim(tasks,1)==1); CHECK(claim(tasks,1)==1); CHECK(claim(tasks,2)==0); CHECK(!finish(tasks[1],2));
    releaseWorker(tasks,1); CHECK(claim(tasks,3)==1); CHECK(finish(tasks[1],3)); CHECK(!finish(tasks[1],3));
    using Predicate=bool(*)(const int&); const std::array<Predicate,2> ps{+[](const int& i){return i>0;},+[](const int& i){return i<5;}};
    CHECK(all<Predicate,int>(ps,2)); CHECK(!all<Predicate,int>(ps,6)); CHECK(any<Predicate,int>(ps,6)); CHECK(all<Predicate,int>({},0)); CHECK(!any<Predicate,int>({},0));
    const std::array weights{Weighted{1,0},Weighted{2,2},Weighted{3,1}};
    CHECK(weighted(weights,[](auto){return 1;})==2); CHECK(weighted(weights,[](auto n){return n;})==3); CHECK(!weighted(weights,[](auto){return 0;}));
    const std::array badWeights{Weighted{1,std::numeric_limits<std::int64_t>::max()},Weighted{2,1}};
    CHECK(!weighted(badWeights,[](auto){std::abort(); return 1;})); CHECK(!weighted(std::span<const Weighted>{},[](auto){std::abort();return 1;}));
    const std::array<std::int64_t,3> thresholds{0,10,20}; CHECK(level(10,thresholds)==2); CHECK(!level(-1,thresholds));
    const std::array<std::int64_t,2> badThresholds{10,10}; CHECK(!level(100,badThresholds));
    std::array<std::int64_t,1> stock{5}; const std::array expensive{Ingredient{0,3},Ingredient{0,3}};
    CHECK(!consume(stock,expensive) && stock[0]==5); const std::array affordable{Ingredient{0,2},Ingredient{0,3}}; CHECK(consume(stock,affordable) && stock[0]==0);
    CHECK(!consume(stock,std::array{Ingredient{1,0}})); CHECK(!consume(stock,std::array{Ingredient{0,-1}}));
    std::array<std::int64_t,1> progress{0}; const std::array goals{Goal{7,2,2}};
    CHECK(objectiveEvent(progress,goals,2)->empty()); CHECK(*objectiveEvent(progress,goals,2)==std::vector<std::int64_t>{7}); CHECK(objectiveEvent(progress,goals,2)->empty());
    CHECK(!objectiveEvent(progress,std::array{Goal{7,2,0}},2) && progress[0]==2);
    CHECK(presentation::smoothstep(-1)==0 && presentation::smoothstep(2)==1);
    CHECK(presentation::timeline(0,1,0)==0 && presentation::timeline(1,1,0)==1);
    CHECK(!presentation::timeline(0,0,-1)); CHECK(!presentation::timeline(0,0,std::numeric_limits<float>::infinity())); CHECK(presentation::lerp(0,10,.5f)==5);
    std::cout << "10 C++ library modules passed\n";
}
