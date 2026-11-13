#include "engine/cables/Cables.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace aiverse::cables;
using namespace aiverse::render;
void check(bool ok,const char *message) {if(!ok) throw std::runtime_error(message);}
template<class F> void rejects(F f) {bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"invalid input accepted");}
float3 centre(const Mesh &m,unsigned ring,unsigned sides) {
    float3 c{};for(unsigned k=0;k<sides;++k)c+=m.vertices[ring*(sides+1)+k].position;
    return c*(1.f/float(sides));
}
void geometryChecks(const Mesh &m) {
    check(m.indices.size()%3==0,"triangle count");
    for(auto &v:m.vertices) {
        check(std::isfinite(v.position.x+v.position.y+v.position.z+v.uv.x+v.uv.y),"finite attributes");
        check(std::abs(length(v.normal)-1)<1e-4,"unit normals");
    }
    for(size_t i=0;i<m.indices.size();i+=3) {
        auto a=m.vertices.at(m.indices[i]),b=m.vertices.at(m.indices[i+1]),c=m.vertices.at(m.indices[i+2]);
        auto n=cross(b.position-a.position,c.position-a.position);
        check(length(n)>1e-10,"nondegenerate faces");
        check(dot(n,a.normal+b.normal+c.normal)>0,"outward winding");
    }
}
int main() {try {
    Options o;o.anchors={{0,4,0},{10,6,0}};o.sag=1;o.radius=.04f;
    auto m=build(o);geometryChecks(m);
    check(length(centre(m,0,o.sides)-o.anchors[0])<1e-5,"start remains attached");
    check(length(centre(m,o.segments,o.sides)-o.anchors[1])<1e-5,"end remains attached");
    check(std::abs(centre(m,o.segments/2,o.sides).y-4)<1e-5,"specified midpoint sag");
    check(m.length>length(o.anchors[1]-o.anchors[0]),"sag adds length");
    check(std::abs(m.vertices[o.segments*(o.sides+1)].uv.x-m.length/o.uvRepeat)<1e-5,"metric UV");
    auto single=m;o.strands=3;o.spacing=.2f;auto bundle=build(o);geometryChecks(bundle);
    check(bundle.vertices.size()==single.vertices.size()*3,"bundle topology");
    check(std::abs(centre(bundle,0,o.sides).z+.2f)<1e-5,"centred bundle offsets");
    o.strands=1;o.anchors.push_back({13,5,7});geometryChecks(build(o));
    o.anchors={{0,0,0},{0,10,0}};o.sag=0;auto vertical=build(o);geometryChecks(vertical);
    check(std::abs(vertical.length-10)<1e-5,"vertical taut length");
    o.sag=1;rejects([&]{build(o);});o.sag=0;
    o.anchors[1]=o.anchors[0];rejects([&]{build(o);});
    o.anchors={{0,0,0},{1,1,0}};o.sag=std::numeric_limits<float>::quiet_NaN();rejects([&]{build(o);});
    o.sag=0;o.strands=8;o.spacing=0;rejects([&]{build(o);});
    o.spacing=.2f;o.anchors.clear();for(int i=0;i<128;++i)o.anchors.push_back({float(i),0,0});
    o.segments=256;o.sides=32;rejects([&]{build(o);});
    o=Options{};o.anchors={{0,0,0},{5,0,0}};std::atomic<bool> cancel{true};rejects([&]{build(o,&cancel);});
    std::cout<<"Cable attachment, sag, bundles, UV, winding, vertical span, input/budget/cancellation checks passed\n";
    return 0;
} catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
