#include "engine/view/SceneLayerProviders.h"
#include "../AlwaysCheck.h"
#include <stdexcept>
#include <string>
#include <vector>
int main(){
    using namespace aiverse::view;
    ViewLayerProviders providers;
    ViewLayerQueue queue;std::vector<std::string> drawn;
    providers.add("first",[&](auto& q,const ViewFrameInfo& frame) {
        AIVERSE_TEST_CHECK(frame.seconds==2);
        q.append("a",LayerPhase::Geometry,[&]{drawn.push_back("a");});
    });
    providers.add("hidden",[&](auto& q,const ViewFrameInfo&){
        q.append("bad",LayerPhase::Effects,[&]{drawn.push_back("bad");});
    },false);
    providers.add("last",[&](auto& q,const ViewFrameInfo&){
        q.append("b",LayerPhase::Overlay,[&]{drawn.push_back("b");});
    });
    providers.collect(queue,{2.0,.016f,7});
    AIVERSE_TEST_CHECK(queue.flush().executed==2);
    AIVERSE_TEST_CHECK(drawn==std::vector<std::string>({"a","b"}));
    AIVERSE_TEST_CHECK(providers.setEnabled("hidden",true));
    drawn.clear();providers.collect(queue,{2.0,.016f,8});
    AIVERSE_TEST_CHECK(queue.flush().executed==3);
    AIVERSE_TEST_CHECK(drawn==std::vector<std::string>({"a","bad","b"}));
    bool failed=false;
    try{providers.add("last",[](auto&,const auto&){});}
    catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);providers.clear();AIVERSE_TEST_CHECK(!providers.size());
}
