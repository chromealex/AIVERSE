#include "engine/view/SceneLayers.h"
#include <cassert>
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
    assert(q.size()==3);
    auto stats=q.flush();
    assert(stats.scheduled==3 && stats.executed==2 && stats.disabled==1);
    assert(called==std::vector<std::string>({"background","effects"}));
    assert(q.empty());
    assert(q.flush().scheduled==0);
    q.append("error",LayerPhase::Overlay,[]{throw std::runtime_error("expected");});
    bool caught=false;
    try {q.flush();}catch(const std::runtime_error&){caught=true;}
    assert(caught && q.empty());
    q.append("reentrant",LayerPhase::Post,[&]{
        bool rejected=false;
        try {q.append("nested",LayerPhase::Post,[]{});}
        catch(const std::logic_error&){rejected=true;}
        assert(rejected);
    });
    assert(q.flush().executed==1);
    q.append("clear",LayerPhase::Geometry,[]{});q.clear();
    assert(q.empty());
}
