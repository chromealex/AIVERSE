#include "engine/render/AnimationGraph.h"
#include <cassert>
#include <cmath>
using namespace aiverse;
using namespace aiverse::render;
namespace aiverse::platform {bool readContentFile(const std::string&,std::string&){return false;}}
int main(){
    Model m;m.nodes.resize(1);m.nodes[0].name="root";m.order={0};
    AnimClip c;c.name="Loop";c.duration=.25f;
    AnimChannel x;x.node=0;x.times={0,.25f};x.values={0,0,0,1,0,0};
    c.channels.push_back(x);m.clips.push_back(c);
    auto graph=AnimationGraph::compile(m,Json::parse(R"({
        "nodes":[{"id":"run","kind":"clip","clip":"Loop","loop":true,
                  "notifies":[{"time":0,"name":"step"},{"time":0.125,"name":"lift"}]}],
        "output":"run"})"));
    AnimationGraphInstance a;a.reset(graph);
    a.update(.5f);
    assert(a.notifies().size()==5);
    assert(a.notifies()[0].name=="step");
    assert(a.notifies()[1].name=="lift");
    assert(a.notifies()[2].name=="step");
    assert(a.notifies()[3].name=="lift");
    assert(a.notifies()[4].name=="step");
    a.update(.125f);
    assert(a.notifies().size()==1 && a.notifies()[0].name=="lift");
    return 0;
}
