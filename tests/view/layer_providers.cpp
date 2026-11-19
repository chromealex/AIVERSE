#include "engine/view/SceneLayerProviders.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>
int main(){
    using namespace aiverse::view;
    ViewLayerProviders providers;
    ViewLayerQueue queue;std::vector<std::string> drawn;
    providers.add("first",[&](auto& q,const ViewFrameInfo& frame) {
        assert(frame.seconds==2);
        q.append("a",LayerPhase::Geometry,[&]{drawn.push_back("a");});
    });
    providers.add("hidden",[&](auto& q,const ViewFrameInfo&){
        q.append("bad",LayerPhase::Effects,[&]{drawn.push_back("bad");});
    },false);
    providers.add("last",[&](auto& q,const ViewFrameInfo&){
        q.append("b",LayerPhase::Overlay,[&]{drawn.push_back("b");});
    });
    providers.collect(queue,{2.0,.016f,7});
    assert(queue.flush().executed==2);
    assert(drawn==std::vector<std::string>({"a","b"}));
    assert(providers.setEnabled("hidden",true));
    drawn.clear();providers.collect(queue,{2.0,.016f,8});
    assert(queue.flush().executed==3);
    assert(drawn==std::vector<std::string>({"a","bad","b"}));
    bool failed=false;
    try{providers.add("last",[](auto&,const auto&){});}
    catch(const std::invalid_argument&){failed=true;}
    assert(failed);providers.clear();assert(!providers.size());
}
