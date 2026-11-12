#include "apps/renderbench/CameraTour.h"
#include <cassert>
#include <iostream>
int main(int argc,char** argv){assert(argc==2);using namespace aiverse;using namespace aiverse::bench;
CameraTour t;t.load(Json::parseFile(argv[1]));int shot;
for(size_t i=0;i<t.shots.size();++i){double start=0;for(size_t j=0;j<i;++j)start+=t.shots[j].duration;
auto a=t.sample(start,shot);assert(shot==int(i));assert(render::length(a.eye()-t.shots[i].from.eye())<.0001f);
auto b=t.sample(start+t.shots[i].duration-1e-5,shot);assert(render::length(b.eye()-t.shots[i].to.eye())<.0001f);
for(int k=0;k<1000;++k){auto c=t.sample(start+t.shots[i].duration*k/1000,shot);assert(std::isfinite(c.eye().x));assert(c.distance>0);}}
auto e=t.sample(1e6,shot);assert(shot==2);assert(render::length(e.eye()-t.shots.back().to.eye())<.0001f);
bool rejected=false;try{t.load(Json::parse("{\"shots\":[]}"));}catch(...){rejected=true;}assert(rejected);
std::cout<<"camera tour endpoints, cuts, finite poses and invalid track PASS\n";}
