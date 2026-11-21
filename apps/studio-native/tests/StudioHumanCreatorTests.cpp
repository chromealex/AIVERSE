// Studio Digital Human Creator window backend: schema, validated DNA edits,
// routes, rig/skin edits, rest-expression bake, document round trip, storage.
#include "engine/character/vivid/HumanCreatorWorkbench.h"
#include <iostream>
using namespace aiverse;
#define REQ(c,m) do{if(!(c)){std::cerr<<"FAIL: "<<m<<"\n";return 1;}}while(0)
int main(){
 vivid::HumanCreatorWorkbench c; std::string e;
 REQ(c.schema().size()==10,"10 sections");
 REQ(c.set("skin.melanin",Json::number(.7,3),&e),e);
 REQ(std::abs(c.dna().skin.melanin-.7f)<1e-5,"melanin applied");
 REQ(!c.set("skin.melanin",Json::number(3.0,3),&e)&&std::abs(c.dna().skin.melanin-.7f)<1e-5,"out-of-range rejected, unchanged");
 REQ(!c.set("skin.bogus",Json::number(.1,3),&e),"unknown key rejected");
 REQ(c.set("eyes.irisColor",Json::parse("[0.1,0.2,0.3]"),&e),e);
 REQ(c.set("expressionRest.24",Json::number(.8,3),&e),e);
 REQ(!c.set("expressionRest.52",Json::number(.5,3),&e),"channel 52 rejected");
 REQ(c.set("rig.jawBone",Json::string("jaw_01"),&e),e);
 REQ(!c.set("rig.headBone",Json::string("jaw_01"),&e),"duplicate bone rejected");
 REQ(c.undo()&&c.dna().rig.jawBone=="jaw","undo"); REQ(c.redo(),"redo");
 REQ(c.preset("warm",&e),e);
 REQ(c.setRoute(0,"jawOpen","jawOpen",2.0,&e),e);
 REQ(!c.setRoute(0,"notAChannel","jawOpen",1.0,&e),"bad channel");
 REQ(!c.setRoute(0,"jawOpen","jawOpen",5.0,&e),"bad gain");
 REQ(c.setRig("head",{0,0.01f,0},&e),e); REQ(!c.setRig("head",{0,2,0},&e),"rig limit");
 const auto cat=c.catalog(); REQ(cat["error"].str()=="",cat["error"].str()); REQ(cat["nodes"].size()>0&&cat["targets"].size()>0,"catalog names");
 vivid::HumanCreatorWorkbench base; REQ(base.preset("warm",&e),e); auto b=base.buildModels(vivid::GroomQuality::Low);
 auto m=c.buildModels(vivid::GroomQuality::Low); REQ(m.head&&m.hair&&!m.head->vertices.empty(),"models built");
 {std::size_t i=0;for(;i<(std::size_t)vivid::kChannelCount;++i)if(vivid::channelName(vivid::Channel(i))=="jawOpen")break;
  REQ(c.set("expressionRest."+std::to_string(i),Json::number(1.0,3),&e),e);}
 auto baked=c.buildModels(vivid::GroomQuality::Low);
 double moved=0;REQ(baked.head->vertices.size()==b.head->vertices.size(),"same topology");
 for(std::size_t i=0;i<b.head->vertices.size();++i){auto p=baked.head->vertices[i].pos,q=b.head->vertices[i].pos;moved=std::max(moved,double(std::abs(p.y-q.y)+std::abs(p.z-q.z)+std::abs(p.x-q.x)));}
 REQ(moved>1e-4,"jawOpen rest channel moves vertices");
 const auto doc=c.toDocument();
 vivid::HumanCreatorWorkbench d; REQ(d.loadDocument(doc,&e),e); REQ(d.toDocument()==doc,"round trip");
 auto bad=doc; bad.set("version",Json::number(int64_t(2))); REQ(!d.loadDocument(bad,&e)&&d.toDocument()==doc,"bad doc keeps state");
 namespace fs=std::filesystem; auto root=fs::temp_directory_path()/"hc-proj"; fs::remove_all(root); fs::create_directories(root);
 vivid::saveHumanCreator(root.string(),"ada",doc);
 REQ(vivid::readHumanCreator(root.string(),"ada")==doc,"save/read");
 REQ(vivid::listHumanCreators(root.string())["humans"].size()==1,"list");
 bool threw=false; try{vivid::saveHumanCreator(root.string(),"../x",doc);}catch(...){threw=true;} REQ(threw,"traversal rejected");
 fs::remove_all(root);
 std::cout<<"studio human creator PASS ("<<cat["nodes"].size()<<" nodes, "<<cat["targets"].size()<<" targets, "<<m.head->vertices.size()<<" verts)\n";
}
