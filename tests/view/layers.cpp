#include "engine/view/SceneLayers.h"
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <string>
#include <vector>
int main(){
    using namespace aiverse::view;
    ViewLayerQueue q;
    std::vector<std::string> called;
    q.append("backdrop",LayerPhase::Background,[&]{called.push_back("background");});
    q.append("disabled",LayerPhase::Geometry,[&]{called.push_back("bad");},false);
    q.append("lights",LayerPhase::Effects,[&]{called.push_back("effects");});
    AIVERSE_TEST_CHECK(q.size()==3);
    auto stats=q.flush();
    AIVERSE_TEST_CHECK(stats.scheduled==3 && stats.executed==2 && stats.disabled==1);
    AIVERSE_TEST_CHECK(called==std::vector<std::string>({"background","effects"}));
    AIVERSE_TEST_CHECK(q.empty());
    AIVERSE_TEST_CHECK(q.flush().scheduled==0);
    q.append("error",LayerPhase::Overlay,[]{throw std::runtime_error("expected");});
    bool caught=false;
    try {q.flush();}catch(const std::runtime_error&){caught=true;}
    AIVERSE_TEST_CHECK(caught && q.empty());
    q.append("reentrant",LayerPhase::Post,[&]{
        bool rejected=false;
        try {q.append("nested",LayerPhase::Post,[]{});}
        catch(const std::logic_error&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
    });
    AIVERSE_TEST_CHECK(q.flush().executed==1);
    q.append("clear",LayerPhase::Geometry,[]{});q.clear();
    AIVERSE_TEST_CHECK(q.empty());
}
