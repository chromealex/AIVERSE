#include "apps/studio-native/StudioAssetCatalog.h"
#include "apps/studio-native/StudioAssetTransfers.h"
#include "apps/studio-native/StudioAssetReferences.h"
#include "apps/studio-native/StudioAssetFileDigest.h"
#include "apps/studio-native/StudioMoveJournal.h"
#include "engine/pack/Sha256.h"
#include "tests/TestCheck.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace fs=std::filesystem;
using aiverse::studio::StudioAssetCatalog;
using aiverse::studio::StudioTransferDecision;
using aiverse::studio::StudioTransferPlan;

void write(const fs::path& path,const std::string& text){
    std::ofstream file(path,std::ios::binary);
    AIVERSE_TEST_CHECK(file);
    file<<text;
}
std::string read(const fs::path& path){
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
int main(){
    const fs::path temp=fs::temp_directory_path()/("studio-transfer-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path project=temp/"Project", external=temp/"External";
    fs::create_directories(project/"content"/"Models");
    fs::create_directories(project/"content"/"Other");
    fs::create_directories(external);
    try{
        const std::string folder=aiverse::studio::studioCreateContentFolder(project,"Models","Vehicles");
        AIVERSE_TEST_CHECK(folder=="Models/Vehicles");
        write(project/"content"/"Models"/"Ship.glb","original");
        write(external/"Ship.glb","replacement");
        write(external/"Drone.glb","new");
        StudioAssetCatalog db(project);
        db.rebuild();
        const auto original=db.byPath("Models/Ship.glb");
        AIVERSE_TEST_CHECK(original);
        const auto stableGuid=original->guid;

        // S6g: expose exact map path candidates before applying a move.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            fs::create_directories(project/"maps"/".aiverse-cache");
            write(project/"maps"/"other.json",R"({"items":[{"model":"Models/Ship.glb"},{"model":"content/Missing.png"},{"note":"prefix content/Models/Ship.glb"}]})");
            write(project/"maps"/".aiverse-cache"/"ignored.json","not JSON");
            const auto current=Json::parse(R"({"a/b~c":"content/Models/Ship.glb"})");
            const auto manifest=Json::parse(R"({"maps":[{"file":"maps/other.json"}]})");
            const auto refs=studioCollectProjectAssetReferences(project,Json::object(),Json::object(),current,manifest,db.all());
            AIVERSE_TEST_CHECK(refs.size()==3);
            AIVERSE_TEST_CHECK(refs[0].document=="current map"&&refs[0].field=="/a~1b~0c"&&refs[0].guid==stableGuid&&!refs[0].writable);
            AIVERSE_TEST_CHECK(refs[1].document=="maps/other.json"&&refs[1].field=="/items/0/model"&&refs[1].guid==stableGuid);
            AIVERSE_TEST_CHECK(refs[2].path=="content/Missing.png"&&refs[2].guid.empty());
            auto preview=StudioTransferPlan::internal(project,"Other",{"Models/Ship.glb"});
            const auto impact=studioMoveReferenceImpact(preview.describe(),refs);
            AIVERSE_TEST_CHECK(impact.size()==2&&!impact[0].getBool("writable")&&!impact[1].getBool("writable"));
            bool refused=false;
            try{studioCheckMapAssetPaths(project,current,Json::object(),manifest,{{"Models/Ship.glb",false}});}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused);
            fs::remove_all(project/"maps");
        }

        // S6g.2: actual glTF resource slots, local relative paths and both edge directions.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            write(project/"content"/"Models"/"Scene.gltf",R"({"buffers":[{"uri":"../Other/mesh.bin"},{"uri":"data:application/octet-stream;base64,AA=="}],"images":[{"uri":"../Other/paint%20red.png"},{"uri":"missing.png"},{"uri":"https://example.com/a.png"},{"uri":"../../escape.png"},{"bufferView":0}],"extras":{"uri":"ignored.png"}})");
            write(project/"content"/"Other"/"mesh.bin","mesh");
            write(project/"content"/"Other"/"paint red.png","paint");
            db.rebuild();
            const auto refs=studioCollectGltfReferences(project,studioReferenceGuids(db.all()));
            AIVERSE_TEST_CHECK(refs.size()==3);
            AIVERSE_TEST_CHECK(refs[0].path=="content/Other/mesh.bin"&&refs[0].field=="/buffers/0/uri"&&!refs[0].writable);
            AIVERSE_TEST_CHECK(refs[1].path=="content/Other/paint red.png"&&refs[1].guid==db.byPath("Other/paint red.png")->guid);
            AIVERSE_TEST_CHECK(refs[2].path=="content/Models/missing.png"&&refs[2].guid.empty());
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Models/Scene.gltf",false).size()==3);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Other",true).size()==2);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Other/mesh.bin",false).size()==1);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Other/mesh",false).empty());
            const auto preview=StudioTransferPlan::internal(project,"Models",{"Other/mesh.bin"});
            AIVERSE_TEST_CHECK(studioMoveReferenceImpact(preview.describe(),refs).size()==1);
            bool refused=false;
            try{studioCheckGltfAssetPaths(project,{{"Other/mesh.bin",false}});}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused);
            AIVERSE_TEST_CHECK(studioGltfResourcePath("content/Models/Scene.gltf","%2e%2e/%2e%2e/escape.png").empty());
            for(const auto& path:{"Models/Scene.gltf","Other/mesh.bin","Other/paint red.png"}){
                fs::remove(project/"content"/path);fs::remove(studioMetaPath(project/"content"/path));
            }
            db.rebuild(false);
        }

        // S6g.3: shortest paths, shared descendants, cycle-closing edge and depth cap.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            const std::vector<StudioAssetReference> edges={
                {"content/A","glTF resource","/0","content/B","",false},
                {"content/A","glTF resource","/1","content/C","",false},
                {"content/B","glTF resource","/0","content/D","",false},
                {"content/C","glTF resource","/0","content/D","",false},
                {"content/D","glTF resource","/0","content/A","",false},
                {"maps/map.json","map","/0","content/B","",false}};
            const auto visits=studioTraverseAssetDependencies(edges,"A",false,false,16);
            AIVERSE_TEST_CHECK(visits.size()==5);
            size_t cycles=0;for(const auto& v:visits){cycles+=v.cycle?1:0;if(v.edge==4)AIVERSE_TEST_CHECK(v.depth==3);}
            AIVERSE_TEST_CHECK(cycles==1);
            AIVERSE_TEST_CHECK(studioTraverseAssetDependencies(edges,"A",false,false,1).size()==2);
            const auto incoming=studioTraverseAssetDependencies(edges,"D",false,true,16);
            AIVERSE_TEST_CHECK(incoming.size()==6);
            const StudioAssetReference ref{"content/Pack/model.gltf","glTF resource","/images/0/uri","content/Pack/tex.png","",false,"tex.png"};
            AIVERSE_TEST_CHECK(studioGltfRelocationSafe(ref,{{"content/Pack","content/Renamed",true}}));
            AIVERSE_TEST_CHECK(studioGltfRelocationSafe(ref,{{"content/Pack/model.gltf","content/Pack/renamed.gltf",false}}));
            AIVERSE_TEST_CHECK(!studioGltfRelocationSafe(ref,{{"content/Pack/tex.png","content/Pack/renamed.png",false}}));
            AIVERSE_TEST_CHECK(!studioGltfRelocationSafe(ref,{{"content/Pack/model.gltf","content/Else/model.gltf",false}}));
            fs::create_directories(project/"content"/"Pack");
            write(project/"content"/"Pack"/"model.gltf",R"({"images":[{"uri":"tex.png"}]})");
            write(project/"content"/"Pack"/"tex.png","texture");db.rebuild();
            auto rename=StudioTransferPlan::rename(project,"Pack","Renamed");
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            AIVERSE_TEST_CHECK(studioMoveReferenceImpact(rename.describe(),studioCollectGltfReferences(project,{})).size()==0);
            (void)rename.apply(rename.token(),{},{},&recorded);
            AIVERSE_TEST_CHECK(recorded&&fs::exists(project/"content"/"Renamed"/"tex.png"));
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(fs::exists(project/"content"/"Pack"/"model.gltf"));
            fs::remove_all(project/"content"/"Pack");fs::remove(studioMetaPath(project/"content"/"Pack"));db.rebuild(false);
        }

        // S6g.4: GLB JSON-only scan and actual OBJ-relative MTL texture semantics.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            fs::create_directories(project/"content"/"Formats"/"mtl");
            const auto dir=project/"content"/"Formats";
            std::string json=R"({"asset":{"version":"2.0"},"images":[{"uri":"tex.png"},{"bufferView":0}]})";
            while(json.size()%4)json+=' ';
            std::string glb="glTF";
            const auto u32=[&](uint32_t x){for(int i=0;i<4;++i)glb+=static_cast<char>((x>>(8*i))&255);};
            u32(2);u32(static_cast<uint32_t>(20+json.size()+12));u32(static_cast<uint32_t>(json.size()));u32(0x4e4f534a);
            glb+=json;u32(4);u32(0x004e4942);glb.append(4,'\0');
            write(dir/"scene.glb",glb);write(dir/"mesh.obj","mtllib mtl/a.mtl\nv 0 0 0\n");
            write(dir/"mtl"/"a.mtl","newmtl mat\nmap_Kd -s 1 1 1 tex.png\nmap_Pr ignored.png\n");
            write(dir/"tex.png","texture");db.rebuild();
            const auto refs=studioCollectGltfReferences(project,studioReferenceGuids(db.all()));
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Formats/tex.png",false).size()==2);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Formats/mtl/a.mtl",false).size()==1);
            AIVERSE_TEST_CHECK(studioAssetDependencyEdges(refs,"Formats/ignored.png",false).empty());
            const auto move=StudioTransferPlan::internal(project,"Other",{"Formats/scene.glb","Formats/tex.png"});
            const auto impact=studioMoveReferenceImpact(move.describe(),refs);
            AIVERSE_TEST_CHECK(impact.size()>=2&&impact[0].getBool("validateOnApply"));
            bool refused=false;try{studioCheckGltfRelocations(project,{{"content/Formats/tex.png","content/Other/tex.png",false}});}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused);
            // Complete folder relocation preserves the model-relative resource names.
            auto rename=StudioTransferPlan::rename(project,"Formats","FormatsMoved");
            AIVERSE_TEST_CHECK(studioMoveReferenceImpact(rename.describe(),refs).size()==0);
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            (void)rename.apply(rename.token(),{},{},&recorded);
            AIVERSE_TEST_CHECK(recorded&&fs::exists(project/"content"/"FormatsMoved"/"tex.png"));
            studioReplayAssetMove(project,*recorded,false);
            fs::remove_all(dir);fs::remove(studioMetaPath(dir));db.rebuild(false);
            fs::create_directories(project/"content"/"Group");
            write(project/"content"/"Group"/"bundle.glb",glb);
            write(project/"content"/"Group"/"tex.png","group texture");db.rebuild();
            auto group=StudioTransferPlan::internal(project,"Other",{"Group/bundle.glb","Group/tex.png"});
            const auto groupImpact=studioMoveReferenceImpact(group.describe(),studioCollectGltfReferences(project,{}));
            AIVERSE_TEST_CHECK(groupImpact.size()==2);
            for(const auto& row:groupImpact.items())AIVERSE_TEST_CHECK(row.getBool("validateOnApply")&&row.getBool("preservesDefault"));
            refused=false;try{(void)group.apply(group.token(),{{0,"import"},{1,"skip"}});}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&fs::exists(project/"content"/"Group"/"bundle.glb"));
            group=StudioTransferPlan::internal(project,"Other",{"Group/bundle.glb","Group/tex.png"});
            recorded.reset();(void)group.apply(group.token(),{},{},&recorded);
            AIVERSE_TEST_CHECK(recorded&&fs::exists(project/"content"/"Other"/"tex.png"));
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(fs::exists(project/"content"/"Group"/"bundle.glb"));
            fs::remove_all(project/"content"/"Group");fs::remove(studioMetaPath(project/"content"/"Group"));db.rebuild(false);
        }

        // S6g.5: DAE images, coverage and stable native page tokens.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            fs::create_directories(project/"content"/"Collada");
            const auto folder=project/"content"/"Collada";
            write(folder/"scene.dae",R"(<COLLADA xmlns="urn:collada"><library_images><image id="img"><init_from>red%20paint.png</init_from></image><image id="next"><init_from><ref>missing.png</ref></init_from></image></library_images><library_effects><effect><surface><init_from>img</init_from></surface></effect></library_effects></COLLADA>)");
            write(folder/"red paint.png","texture");
            write(folder/"unsupported.fbx","opaque");db.rebuild();
            StudioDependencyCoverage coverage;
            const auto refs=studioCollectGltfReferences(project,studioReferenceGuids(db.all()),&coverage);
            const auto edges=studioAssetDependencyEdges(refs,"Collada/scene.dae",false);
            AIVERSE_TEST_CHECK(edges.size()==2&&coverage.dae==1&&coverage.unsupportedModels>=1);
            AIVERSE_TEST_CHECK(edges[0].path=="content/Collada/red paint.png"&&!edges[0].guid.empty());
            AIVERSE_TEST_CHECK(edges[1].path=="content/Collada/missing.png"&&edges[1].guid.empty());
            const auto page=studioDependencyPage(refs,"Collada/scene.dae",false,16,"outgoing",0,1,coverage.json(),"");
            AIVERSE_TEST_CHECK(page["items"].size()==1&&page.getInt("total")==2&&page.getInt("unresolved")==1);
            const auto next=studioDependencyPage(refs,"Collada/scene.dae",false,16,"outgoing",1,1,coverage.json(),page.getStr("token"));
            AIVERSE_TEST_CHECK(next["items"][0].getStr("path")=="content/Collada/missing.png");
            auto changed=refs;changed[0].guid="changed";
            bool refused=false;try{(void)studioDependencyPage(changed,"Collada/scene.dae",false,16,"outgoing",1,1,coverage.json(),page.getStr("token"));}
            catch(const std::runtime_error&){refused=true;}
            // Changed selected edge (the source order also includes opaque test GLB placeholders).
            AIVERSE_TEST_CHECK(refused);
            fs::remove_all(folder);fs::remove(studioMetaPath(folder));db.rebuild(false);
        }

        // S6g.6: stationary glTF automatic repair, exact undo/redo and journal rollback.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            const auto model=project/"content"/"Models"/"Repair.gltf";
            const auto texture=project/"content"/"Models"/"repair.png";
            const std::string original="{ \"images\" : [{\"uri\":\"./repair.png\"}], \"extras\":{\"keep\":true} }\n";
            write(model,original);write(texture,"repair texture");db.rebuild();
            auto move=StudioTransferPlan::internal(project,"Other",{"Models/repair.png"});
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            const auto result=move.apply(move.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&recorded->resourceEdits.size()==1&&result.getInt("repairedResourceDocuments")==1);
            const auto changed=read(model);
            AIVERSE_TEST_CHECK(Json::parse(changed)["images"][0].getStr("uri")=="../Other/repair.png");
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(read(model)==original&&fs::exists(texture));
            studioReplayAssetMove(project,*recorded,true);
            AIVERSE_TEST_CHECK(read(model)==changed);
            editor::ProjectHistory timeline;
            auto copy=*recorded;timeline.recordMove(std::move(copy),"Repair model URI");
            {
                StudioAssetHistoryPublication publication(project);publication.save(timeline);
            }
            const auto reopened=studioLoadAssetHistoryCheckpoint(project);
            AIVERSE_TEST_CHECK(reopened.canUndo()&&reopened.next(false).move->resourceEdits.size()==1);
            studioReplayAssetMove(project,*reopened.next(false).move,false);
            fs::remove(project/".aiverse-cache"/"asset-history.json");
            move=StudioTransferPlan::internal(project,"Other",{"Models/repair.png"});
            bool refused=false;try{(void)move.apply(move.token(),{},[]{throw std::runtime_error("fixture failure");},&recorded,true);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&read(model)==original&&fs::exists(texture));
            fs::remove(model);fs::remove(texture);fs::remove(studioMetaPath(model));fs::remove(studioMetaPath(texture));db.rebuild(false);
        }

        // S6g.6.2: moving a model/folder with typed URI repair changes its
        // payload digest. Both direction-specific tree digests must survive.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            fs::create_directories(project/"content"/"Models"/"Pack");
            const auto source=project/"content"/"Models"/"Pack";
            const auto destination=project/"content"/"Other"/"Pack";
            const std::string original="{ \"images\": [{\"uri\":\"../outside.png\"}] }\n";
            write(source/"model.gltf",original);
            write(project/"content"/"Models"/"outside.png","texture");
            db.rebuild();
            auto move=StudioTransferPlan::internal(project,"Other",{"Models/Pack"});
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            const auto result=move.apply(move.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&result.getInt("repairedResourceDocuments")==1);
            AIVERSE_TEST_CHECK(!recorded->files[0].digestAfter.empty());
            AIVERSE_TEST_CHECK(Json::parse(read(destination/"model.gltf"))["images"][0].getStr("uri")=="../../Models/outside.png");
            editor::ProjectHistory timeline;
            auto copy=*recorded;timeline.recordMove(std::move(copy),"Move repaired folder");
            {
                StudioAssetHistoryPublication publication(project);publication.save(timeline);
            }
            const auto reopened=studioLoadAssetHistoryCheckpoint(project);
            AIVERSE_TEST_CHECK(reopened.canUndo()&&reopened.next(false).move->files[0].digestAfter==recorded->files[0].digestAfter);
            studioReplayAssetMove(project,*reopened.next(false).move,false);
            fs::remove(project/".aiverse-cache"/"asset-history.json");
            AIVERSE_TEST_CHECK(read(source/"model.gltf")==original);
            studioReplayAssetMove(project,*recorded,true);
            studioReplayAssetMove(project,*recorded,false);
            auto single=StudioTransferPlan::internal(project,"Other",{"Models/Pack/model.gltf"});
            const auto one=single.apply(single.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(one.getInt("repairedResourceDocuments")==1&&recorded&&!recorded->files[0].digestAfter.empty());
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(read(source/"model.gltf")==original);
            fs::remove_all(source);fs::remove(studioMetaPath(source));
            fs::remove(project/"content"/"Models"/"outside.png");
            fs::remove(studioMetaPath(project/"content"/"Models"/"outside.png"));db.rebuild(false);
        }
        // S6g.6.3: binary GLB repair retains BIN and unknown chunk bytes.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            const auto model=project/"content"/"Models"/"Binary.glb";
            const auto texture=project/"content"/"Models"/"binary.png";
            StudioModelRepairDocument doc;
            doc.binary=true;doc.json=Json::parse(R"({"asset":{"version":"2.0"},"images":[{"uri":"binary.png"}],"buffers":[{"byteLength":4}]})");
            studioGlbAppendU32(doc.tail,4);studioGlbAppendU32(doc.tail,0x004e4942);
            doc.tail.append("\0\xff\x80\x01",4);
            studioGlbAppendU32(doc.tail,4);studioGlbAppendU32(doc.tail,0x12345678);
            doc.tail.append("\xfe\0\x7f\x02",4);
            const auto original=studioEncodeModelRepair(doc);
            write(model,original);write(texture,"binary texture");db.rebuild();
            auto move=StudioTransferPlan::internal(project,"Other",{"Models/binary.png"});
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            const auto result=move.apply(move.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&result.getInt("repairedResourceDocuments")==1);
            const auto changed=read(model);
            const auto parsed=studioDecodeModelRepair(model,changed);
            AIVERSE_TEST_CHECK(parsed.binary&&parsed.tail==doc.tail);
            auto corrupt=changed;corrupt.back()^=1;
            bool rejected=false;
            try{studioValidateModelRepairBytes({fs::path("content/Models/Binary.glb"),original,corrupt});}
            catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected);
            const auto encoded=studioEncodeResourceEdit({fs::path("content/Models/Binary.glb"),original,changed});
            AIVERSE_TEST_CHECK(encoded.getStr("encoding")=="hex"&&studioDecodeResourceEdit(Json::parse(encoded.dump(-1)),true).before==original);
            rejected=false;try{(void)studioDecodeResourceEdit(encoded,false);}
            catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected);
            AIVERSE_TEST_CHECK(parsed.json["images"][0].getStr("uri")=="../Other/binary.png");
            editor::ProjectHistory timeline;auto copy=*recorded;timeline.recordMove(std::move(copy),"Repair GLB");
            {StudioAssetHistoryPublication publication(project);publication.save(timeline);}
            auto reopened=studioLoadAssetHistoryCheckpoint(project);
            AIVERSE_TEST_CHECK(reopened.canUndo()&&reopened.next(false).move->resourceEdits[0].before==original);
            studioReplayAssetMove(project,*reopened.next(false).move,false);
            AIVERSE_TEST_CHECK(read(model)==original&&fs::exists(texture));
            studioReplayAssetMove(project,*recorded,true);
            AIVERSE_TEST_CHECK(read(model)==changed);
            studioReplayAssetMove(project,*recorded,false);
            fs::remove(project/".aiverse-cache"/"asset-history.json");
            auto moved=StudioTransferPlan::internal(project,"Other",{"Models/Binary.glb"});
            (void)moved.apply(moved.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&!recorded->files[0].digestAfter.empty());
            AIVERSE_TEST_CHECK(studioDecodeModelRepair(project/"content"/"Other"/"Binary.glb",read(project/"content"/"Other"/"Binary.glb")).tail==doc.tail);
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(read(model)==original);
            fs::remove(model);fs::remove(texture);fs::remove(studioMetaPath(model));fs::remove(studioMetaPath(texture));db.rebuild(false);
        }

        // S6g.6.4: exact Wavefront and COLLADA text resource repair.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            const auto dir=project/"content"/"Models";
            const auto obj=dir/"Repair.obj",mtl=dir/"Repair.mtl",dae=dir/"Repair.dae",tex=dir/"repair-texture.png";
            const std::string objBytes="# keep comment\r\nmtllib ./Repair.mtl\r\nv 0 0 0\r\n";
            const std::string mtlBytes="newmtl paint\r\n  map_Kd -s 1 1 1 ./repair-texture.png  \r\nmap_Pr ignored.png\r\n";
            const std::string daeBytes="<COLLADA xmlns=\"urn:collada\"><library_images><image id=\"a\"><init_from><ref> ./repair-texture.png </ref></init_from></image></library_images></COLLADA>";
            write(obj,objBytes);write(mtl,mtlBytes);write(dae,daeBytes);write(tex,"texture");db.rebuild();
            auto move=StudioTransferPlan::internal(project,"Other",{"Models/repair-texture.png"});
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            const auto result=move.apply(move.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&result.getInt("repairedResourceDocuments")==2);
            AIVERSE_TEST_CHECK(read(mtl).find("  map_Kd -s 1 1 1 ../Other/repair-texture.png  \r\n")!=std::string::npos);
            AIVERSE_TEST_CHECK(read(dae).find("<ref> ../Other/repair-texture.png </ref>")!=std::string::npos);
            AIVERSE_TEST_CHECK(read(obj)==objBytes);
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(read(mtl)==mtlBytes&&read(dae)==daeBytes);
            studioReplayAssetMove(project,*recorded,true);studioReplayAssetMove(project,*recorded,false);
            move=StudioTransferPlan::internal(project,"Other",{"Models/Repair.obj"});
            (void)move.apply(move.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&read(project/"content"/"Other"/"Repair.obj").find("mtllib ../Models/Repair.mtl")!=std::string::npos);
            AIVERSE_TEST_CHECK(read(mtl).find("../Models/repair-texture.png")!=std::string::npos);
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(read(obj)==objBytes&&read(mtl)==mtlBytes);
            const auto utf16=studioDaeEncodeUtf16(daeBytes,1);
            write(dae,utf16);
            move=StudioTransferPlan::internal(project,"Other",{"Models/repair-texture.png"});
            (void)move.apply(move.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(studioDaeUtf16Endian(read(dae))==1);
            AIVERSE_TEST_CHECK(studioDaeRepairText(read(dae)).find("../Other/repair-texture.png")!=std::string::npos);
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(read(dae)==utf16);
            // Shared MTL consumers in different directories must not silently
            // rewrite the same slot to incompatible OBJ-relative targets.
            const auto otherObj=project/"content"/"Other"/"Shared.obj";
            const auto otherTex=project/"content"/"Other"/"repair-texture.png";
            write(otherObj,"mtllib ../Models/Repair.mtl\nv 0 0 0\n");
            write(otherTex,"other texture");db.rebuild();
            move=StudioTransferPlan::rename(project,"Models/repair-texture.png","renamed-texture.png");
            bool refused=false;try{(void)move.apply(move.token(),{},{},&recorded,true);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&read(mtl)==mtlBytes&&fs::exists(tex));
            for(const auto& path:{obj,mtl,dae,tex,otherObj,otherTex}){fs::remove(path);fs::remove(studioMetaPath(path));}
            db.rebuild(false);
        }

        // S6g.6.5: large GLB retains only the JSON prefix and tail identity.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            const auto model=project/"content"/"Models"/"Large.glb";
            const auto texture=project/"content"/"Models"/"large.png";
            const auto doc=Json::parse(R"({"asset":{"version":"2.0"},"images":[{"uri":"large.png"}],"buffers":[{"byteLength":12582912}]})");
            constexpr uint32_t payload=12u*1024u*1024u;
            {
                std::ofstream out(model,std::ios::binary);
                const auto prefix=studioEncodeGlbPrefix(doc,uint64_t(payload)+8);
                out.write(prefix.data(),static_cast<std::streamsize>(prefix.size()));
                std::string header;studioGlbAppendU32(header,payload);studioGlbAppendU32(header,0x004e4942);
                out.write(header.data(),8);
                const std::string block(65536,'\x9f');
                for(uint32_t i=0;i<payload;i+=static_cast<uint32_t>(block.size()))
                    out.write(block.data(),static_cast<std::streamsize>(block.size()));
                AIVERSE_TEST_CHECK(bool(out));
            }
            write(texture,"texture");db.rebuild();
            const auto original=studioReadGlbPrefix(model);
            const auto originalFileDigest=studioAssetFileDigest(model);
            auto move=StudioTransferPlan::internal(project,"Other",{"Models/large.png"});
            std::optional<editor::ProjectHistory::MovePlan> recorded;
            const auto result=move.apply(move.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&result.getInt("repairedResourceDocuments")==1);
            const auto& edit=recorded->resourceEdits[0];
            AIVERSE_TEST_CHECK(studioStreamingResource(edit)&&edit.tailDigest==original.digest&&edit.tailBytes==payload+8);
            AIVERSE_TEST_CHECK(edit.before.size()+edit.after.size()<4096);
            const auto changed=studioReadGlbPrefix(model);
            AIVERSE_TEST_CHECK(changed.digest==original.digest&&studioDecodeGlbPrefix(changed.prefix,changed.tailBytes)["images"][0].getStr("uri")=="../Other/large.png");
            editor::ProjectHistory timeline;auto copy=*recorded;timeline.recordMove(std::move(copy),"Large GLB");
            {StudioAssetHistoryPublication publication(project);publication.save(timeline);}
            AIVERSE_TEST_CHECK(fs::file_size(project/".aiverse-cache"/"asset-history.json")<65536);
            auto reopened=studioLoadAssetHistoryCheckpoint(project);
            AIVERSE_TEST_CHECK(reopened.canUndo()&&reopened.next(false).move->resourceEdits[0].tailDigest==original.digest);
            studioReplayAssetMove(project,*reopened.next(false).move,false);
            AIVERSE_TEST_CHECK(studioAssetFileDigest(model)==originalFileDigest);
            studioReplayAssetMove(project,*recorded,true);
            // A changed tail is not allowed to borrow the retained prefix.
            {
                std::fstream io(model,std::ios::binary|std::ios::in|std::ios::out);
                io.seekp(-1,std::ios::end);io.put('x');
            }
            bool rejected=false;try{studioReplayAssetMove(project,*recorded,false);}
            catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected&&fs::exists(project/"content"/"Other"/"large.png"));
            {
                std::fstream io(model,std::ios::binary|std::ios::in|std::ios::out);
                io.seekp(-1,std::ios::end);io.put('\x9f');
            }
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(studioAssetFileDigest(model)==originalFileDigest);
            fs::remove(project/".aiverse-cache"/"asset-history.json");
            auto modelMove=StudioTransferPlan::internal(project,"Other",{"Models/Large.glb"});
            (void)modelMove.apply(modelMove.token(),{},{},&recorded,true);
            AIVERSE_TEST_CHECK(recorded&&studioStreamingResource(recorded->resourceEdits[0])&&!recorded->files[0].digestAfter.empty());
            studioReplayAssetMove(project,*recorded,false);
            AIVERSE_TEST_CHECK(studioAssetFileDigest(model)==originalFileDigest);
            fs::remove(model);fs::remove(texture);fs::remove(studioMetaPath(model));fs::remove(studioMetaPath(texture));db.rebuild(false);
        }

        // S6h.1: persisted quota blocks growth, not restoration or explicit purge.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            write(project/"content"/"Quota.bin",std::string(2*1024*1024,'q'));
            db.rebuild();
            const auto guid=db.byPath("Quota.bin")->guid;
            AIVERSE_TEST_CHECK(studioRecoveryStorage(project).getInt("quotaMiB")==0);
            AIVERSE_TEST_CHECK(studioSetRecoveryQuota(project,0,1).getInt("quotaMiB")==1);
            bool refused=false;
            try{(void)studioQuarantineAsset(project,"Quota.bin",guid);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&fs::exists(project/"content"/"Quota.bin"));
            AIVERSE_TEST_CHECK(studioSetRecoveryQuota(project,1,4).getInt("quotaMiB")==4);
            auto plan=studioQuarantineAsset(project,"Quota.bin",guid);
            AIVERSE_TEST_CHECK(studioRecoveryStorage(project).getInt("trashBytes")>=2*1024*1024);
            AIVERSE_TEST_CHECK(studioSetRecoveryQuota(project,4,1).getBool("overQuota"));
            studioReplayAssetQuarantine(project,plan,false);
            AIVERSE_TEST_CHECK(fs::exists(project/"content"/"Quota.bin")&&!studioRecoveryStorage(project).getBool("overQuota"));
            refused=false;try{(void)studioSetRecoveryQuota(project,4,0);}catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&studioRecoveryStorage(project).getInt("quotaMiB")==1);
            (void)studioSetRecoveryQuota(project,1,0);
            fs::remove(project/"content"/"Quota.bin");fs::remove(studioMetaPath(project/"content"/"Quota.bin"));
            db.rebuild(false);
        }

        // S6h.2: protected history, exact batch review, changed payload refusal.
        {
            using namespace aiverse;
            using namespace aiverse::studio;
            fs::create_directories(project/"content"/"Cleanup");
            write(project/"content"/"Cleanup"/"a.bin","folder payload");
            write(project/"content"/"Purge.bin","purge payload");db.rebuild();
            auto folder=studioQuarantineAsset(project,"Cleanup",db.byPath("Cleanup")->guid);
            auto file=studioQuarantineAsset(project,"Purge.bin",db.byPath("Purge.bin")->guid);
            const auto folderId=fs::path(folder.files[0].to).parent_path().filename().string();
            const auto fileId=fs::path(file.files[0].to).parent_path().filename().string();
            editor::ProjectHistory protectedHistory;
            auto keep=folder;protectedHistory.recordMove(std::move(keep),"Protected folder");
            AIVERSE_TEST_CHECK(studioProtectedTrashIds(protectedHistory).size()==1);
            bool refused=false;
            try{(void)studioReviewTrashBatch(project,{folderId,fileId},protectedHistory);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused);
            editor::ProjectHistory empty;
            const auto plan=studioReviewTrashBatch(project,{folderId,fileId},empty);
            AIVERSE_TEST_CHECK(plan["items"].size()==2&&plan.getInt("bytes")>27);
            write(project/file.files[0].to,"changed payload");
            refused=false;try{(void)studioApplyTrashBatch(project,{folderId,fileId},plan.getStr("token"),empty);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&fs::exists(project/folder.files[0].to));
            write(project/file.files[0].to,"purge payload");
            const auto refreshed=studioReviewTrashBatch(project,{folderId,fileId},empty);
            const auto applied=studioApplyTrashBatch(project,{folderId,fileId},refreshed.getStr("token"),empty);
            AIVERSE_TEST_CHECK(applied.getBool("ok")&&applied["removed"].size()==2);
            AIVERSE_TEST_CHECK(!fs::exists(project/folder.files[0].to)&&!fs::exists(project/file.files[0].to));
        }

        // Preview name collision, skip by default and leave content unchanged.
        auto skip=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        AIVERSE_TEST_CHECK(skip.describe()["items"][0].getStr("conflict")=="name");
        auto skipped=skip.apply(skip.token(),{{0,"skip"}});
        AIVERSE_TEST_CHECK(skipped["files"].size()==0);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship.glb")=="original");

        // Keep both: no existing file touched, fresh GUID for the copy.
        auto both=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        auto copied=both.apply(both.token(),{{0,"keep-both"}});
        AIVERSE_TEST_CHECK(copied["files"].size()==1);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship (1).glb")=="replacement");
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Ship (1).glb")->guid!=stableGuid);

        // Replace is explicit and preserves destination sidecar/GUID.
        auto replace=StudioTransferPlan::external(project,"Models",{external/"Ship.glb"});
        auto replaced=replace.apply(replace.token(),{{0,"replace"}});
        AIVERSE_TEST_CHECK(replaced["files"].size()==1);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship.glb")=="replacement");
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Ship.glb")->guid==stableGuid);

        // A GUID lookup follows internal file+meta move to a different folder.
        const auto material=project/"materials.json";
        const std::string materialBefore=R"({"materials":{"test":{"texture":"content/Models/Ship.glb"}}})";
        const std::string materialAfter=R"({"materials":{"test":{"texture":"content/Other/Ship.glb"}}})";
        write(material,materialBefore);
        auto move=StudioTransferPlan::internal(project,"Other",{"Models/Ship.glb"});
        AIVERSE_TEST_CHECK(move.describe()["items"][0].getStr("conflict").empty());
        std::optional<aiverse::editor::ProjectHistory::MovePlan> history;
        const auto moved=move.apply(move.token(),{},[&]{write(material,materialAfter);},&history);
        AIVERSE_TEST_CHECK(moved["files"].size()==1);
        db.rebuild();
        AIVERSE_TEST_CHECK(!db.byPath("Models/Ship.glb"));
        AIVERSE_TEST_CHECK(db.byGuid(stableGuid)->path=="Other/Ship.glb");
        AIVERSE_TEST_CHECK(history&&history->files.size()==2);
        const auto metaBytes=read(project/"content"/"Other"/"Ship.glb.meta");
        write(project/"content"/"Models"/"Ship.glb","collision");
        bool undoRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*history,false);}catch(const std::runtime_error&){undoRefused=true;}
        AIVERSE_TEST_CHECK(undoRefused&&read(project/"content"/"Models"/"Ship.glb")=="collision");
        fs::remove(project/"content"/"Models"/"Ship.glb");
        write(material,materialAfter+"\n");
        undoRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*history,false);}catch(const std::runtime_error&){undoRefused=true;}
        AIVERSE_TEST_CHECK(undoRefused&&read(material)==materialAfter+"\n");
        write(material,materialAfter);
        write(project/"content"/"Other"/"Ship.glb","different");
        undoRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*history,false);}catch(const std::runtime_error&){undoRefused=true;}
        AIVERSE_TEST_CHECK(undoRefused&&read(project/"content"/"Other"/"Ship.glb")=="different");
        write(project/"content"/"Other"/"Ship.glb","replacement");
        aiverse::studio::studioReplayAssetMove(project,*history,false);
        AIVERSE_TEST_CHECK(read(material)==materialBefore&&read(project/"content"/"Models"/"Ship.glb")=="replacement");
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Ship.glb.meta")==metaBytes);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(stableGuid)->path=="Models/Ship.glb");
        aiverse::studio::studioReplayAssetMove(project,*history,true);
        AIVERSE_TEST_CHECK(read(material)==materialAfter&&read(project/"content"/"Other"/"Ship.glb.meta")==metaBytes);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(stableGuid)->path=="Other/Ship.glb");
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));

        // A cloned external sidecar is a GUID conflict even without a name collision.
        write(external/"Clone.glb","clone");
        fs::copy_file(project/"content"/"Other"/"Ship.glb.meta",
                      external/"Clone.glb.meta");
        auto guidConflict=StudioTransferPlan::external(project,"Models",{external/"Clone.glb"});
        AIVERSE_TEST_CHECK(guidConflict.describe()["items"][0].getStr("conflict")=="guid");
        auto fixed=guidConflict.apply(guidConflict.token(),{{0,"new-guid"}});
        AIVERSE_TEST_CHECK(fixed["files"].size()==1);
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Clone.glb")->guid!=stableGuid);

        // Reject a stale preview token before modifying anything.
        auto bad=StudioTransferPlan::external(project,"Models",{external/"Drone.glb"});
        bool rejected=false;
        try{(void)bad.apply("incorrect-token",{});}
        catch(const std::runtime_error&){rejected=true;}
        AIVERSE_TEST_CHECK(rejected);
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Models"/"Drone.glb"));

        // Copy a genuine nested external directory without trusting its .meta.
        fs::create_directories(external/"Textures"/"Rocks");
        write(external/"Textures"/"Rocks"/"Stone.png","pixels");
        auto folderPlan=StudioTransferPlan::external(project,"Models",{external/"Textures"});
        auto importedFolder=folderPlan.apply(folderPlan.token(),{});
        AIVERSE_TEST_CHECK(importedFolder["files"].size()==1);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Textures"/"Rocks"/"Stone.png")=="pixels");
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byPath("Models/Textures/Rocks/Stone.png"));
        // A failing dependent document write must revert all internal file
        // and metadata moves, not leave a partially committed project.
        write(project/"content"/"Models"/"Rollback.png","rollback-pixels");
        db.rebuild();
        const auto rollbackGuid=db.byPath("Models/Rollback.png")->guid;
        auto failMove=StudioTransferPlan::internal(project,"Other",{"Models/Rollback.png"});
        bool hookFailed=false;
        try {
            (void)failMove.apply(failMove.token(),{},[] {
                throw std::runtime_error("injected material write failure");
            });
        } catch (const std::runtime_error&) { hookFailed=true; }
        AIVERSE_TEST_CHECK(hookFailed);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Rollback.png")=="rollback-pixels");
        AIVERSE_TEST_CHECK(fs::exists(project/"content"/"Models"/"Rollback.png.meta"));
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Other"/"Rollback.png"));
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-move-journal"));
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byGuid(rollbackGuid)->path=="Models/Rollback.png");

        // Successful follow-up proves a failed move leaves the project usable.
        auto retryMove=StudioTransferPlan::internal(project,"Other",{"Models/Rollback.png"});
        const auto retry=retryMove.apply(retryMove.token(),{});
        AIVERSE_TEST_CHECK(retry["issues"].size()==0);
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byGuid(rollbackGuid)->path=="Other/Rollback.png");

        // Destructive internal replace remains forbidden until it has
        // a complete backup protocol, even if the UI is bypassed.
        write(project/"content"/"Models"/"Rollback.png","collision");
        db.rebuild();
        auto unsafe=StudioTransferPlan::internal(project,"Other",{"Models/Rollback.png"});
        bool replaceRejected=false;
        try { (void)unsafe.apply(unsafe.token(),{{0,"replace"}}); }
        catch (const std::runtime_error&) { replaceRejected=true; }
        AIVERSE_TEST_CHECK(replaceRejected);
        AIVERSE_TEST_CHECK(read(project/"content"/"Models"/"Rollback.png")=="collision");
        AIVERSE_TEST_CHECK(read(project/"content"/"Other"/"Rollback.png")=="rollback-pixels");

        // A large payload is represented by a digest, not retained bytes.
        fs::create_directories(project/"content"/"LargeTree"/"Empty");
        const auto large=project/"content"/"LargeTree"/"Large.bin";
        {
            std::ofstream out(large,std::ios::binary);
            const std::string chunk(1024*1024,'x');
            for(int i=0;i<17;++i)out.write(chunk.data(),chunk.size());
        }
        db.rebuild();
        const auto treeGuid=db.byPath("LargeTree")->guid;
        const auto largeGuid=db.byPath("LargeTree/Large.bin")->guid;
        auto tree=StudioTransferPlan::internal(project,"Other",{"LargeTree"});
        std::optional<aiverse::editor::ProjectHistory::MovePlan> treeHistory;
        const auto treeResult=tree.apply(tree.token(),{}, {},&treeHistory);
        AIVERSE_TEST_CHECK(treeResult["files"].size()==1&&treeHistory&&treeHistory->files.size()==2);
        AIVERSE_TEST_CHECK(treeHistory->files[0].directory&&treeHistory->files[0].bytes.empty()&&treeHistory->files[0].digest.size()==64);
        aiverse::editor::ProjectHistory stack;
        stack.recordMove(std::move(*treeHistory),"Tree move");
        AIVERSE_TEST_CHECK(stack.canUndo()&&stack.bytes()<1024*1024);
        fs::create_directory(project/"content"/"Other"/"LargeTree"/"Unexpected");
        bool treeRefused=false;
        try{aiverse::studio::studioReplayAssetMove(project,*stack.next(false).move,false);}
        catch(const std::runtime_error&){treeRefused=true;}
        AIVERSE_TEST_CHECK(treeRefused);
        fs::remove(project/"content"/"Other"/"LargeTree"/"Unexpected");
        aiverse::studio::studioReplayAssetMove(project,*stack.next(false).move,false);stack.advance(false);
        db.rebuild();
        AIVERSE_TEST_CHECK(db.byGuid(treeGuid)->path=="LargeTree"&&db.byGuid(largeGuid)->path=="LargeTree/Large.bin");
        AIVERSE_TEST_CHECK(fs::is_directory(project/"content"/"LargeTree"/"Empty"));
        aiverse::studio::studioReplayAssetMove(project,*stack.next(true).move,true);stack.advance(true);
        auto rename=StudioTransferPlan::rename(project,"Other/LargeTree","RenamedTree");
        std::optional<aiverse::editor::ProjectHistory::MovePlan> renameHistory;
        AIVERSE_TEST_CHECK(rename.describe()["items"][0].getStr("to")=="Other/RenamedTree");
        AIVERSE_TEST_CHECK(rename.apply(rename.token(),{}, {},&renameHistory)["files"].size()==1&&renameHistory);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(largeGuid)->path=="Other/RenamedTree/Large.bin");
        aiverse::studio::studioReplayAssetMove(project,*renameHistory,false);
        db.rebuild();AIVERSE_TEST_CHECK(db.byGuid(largeGuid)->path=="Other/LargeTree/Large.bin");
        bool extensionRefused=false;
        try{(void)StudioTransferPlan::rename(project,"Other/Ship.glb","Ship.png");}
        catch(const std::runtime_error&){extensionRefused=true;}
        AIVERSE_TEST_CHECK(extensionRefused);
        // Saved maps other than the open map are checked for path references.
        fs::create_directories(project/"maps");
        write(project/"maps"/"other.json",R"({"model":"content/Other/LargeTree/Large.bin"})");
        bool referenceRefused=false;
        try{aiverse::studio::studioCheckMapAssetPaths(project,aiverse::Json::object(),aiverse::Json::object(),aiverse::Json::object(),{{"Other/LargeTree",true}});}
        catch(const std::runtime_error&){referenceRefused=true;}
        AIVERSE_TEST_CHECK(referenceRefused);
        // Soft deletion retains the complete folder and sidecars outside content.
        fs::create_directories(project/"content"/"TrashTree");
        write(project/"content"/"TrashTree"/"Item.png","pixels");
        db.rebuild();
        const auto trashGuid=db.byPath("TrashTree")->guid;
        const auto childGuid=db.byPath("TrashTree/Item.png")->guid;
        auto deleted=aiverse::studio::studioQuarantineAsset(project,"TrashTree",trashGuid);
        AIVERSE_TEST_CHECK(deleted.quarantine&&deleted.files.size()==2);
        const auto backup=project/deleted.files[0].to;
        AIVERSE_TEST_CHECK(fs::exists(backup/"Item.png")&&!fs::exists(project/"content"/"TrashTree"));
        db.rebuild(false);AIVERSE_TEST_CHECK(!db.byGuid(trashGuid)&&!db.byGuid(childGuid));
        fs::create_directory(project/"content"/"TrashTree");
        bool collisionDelete=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,deleted,false);}catch(const std::runtime_error&){collisionDelete=true;}
        AIVERSE_TEST_CHECK(collisionDelete&&fs::exists(backup/"Item.png"));
        fs::remove(project/"content"/"TrashTree");
        aiverse::studio::studioReplayAssetQuarantine(project,deleted,false);
        db.rebuild(false);
        AIVERSE_TEST_CHECK(db.byGuid(childGuid)->path=="TrashTree/Item.png");
        bool referenced=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,deleted,true,[]{throw std::runtime_error("Referenced");});}
        catch(const std::runtime_error&){referenced=true;}
        AIVERSE_TEST_CHECK(referenced&&fs::exists(project/"content"/"TrashTree"/"Item.png"));
        aiverse::studio::studioReplayAssetQuarantine(project,deleted,true);
        AIVERSE_TEST_CHECK(fs::exists(backup/"Item.png"));
        aiverse::editor::ProjectHistory deletedHistory;
        deletedHistory.recordMove(std::move(deleted),"Delete tree");deletedHistory.reset();
        AIVERSE_TEST_CHECK(fs::exists(backup/"Item.png")); // history eviction must not purge recoverable data
        // A non-replacing import publishes the entire staged batch with sidecars.
        fs::create_directories(external/"BatchTree"/"Empty");
        write(external/"BatchTree"/"Child.png","child pixels");
        write(external/"Batch.png","batch pixels");
        auto batch=StudioTransferPlan::external(project,"Other",{external/"Batch.png",external/"BatchTree"});
        std::optional<aiverse::editor::ProjectHistory::MovePlan> imported;
        const auto batchResult=batch.apply(batch.token(),{}, {},&imported);
        AIVERSE_TEST_CHECK(batchResult["files"].size()==2&&imported&&imported->created&&imported->quarantine&&imported->files.size()==4);
        db.rebuild(false);
        const auto importedGuid=db.byPath("Other/Batch.png")->guid;
        const auto importedChildGuid=db.byPath("Other/BatchTree/Child.png")->guid;
        const auto importedMeta=read(project/"content"/"Other"/"Batch.png.meta");
        fs::remove(external/"Batch.png");fs::remove_all(external/"BatchTree");
        bool importRefused=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,*imported,true,[]{throw std::runtime_error("Referenced");});}
        catch(const std::runtime_error&){importRefused=true;}
        AIVERSE_TEST_CHECK(importRefused&&fs::exists(project/"content"/"Other"/"Batch.png"));
        aiverse::studio::studioReplayAssetQuarantine(project,*imported,true);
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Other"/"Batch.png")&&!fs::exists(project/"content"/"Other"/"BatchTree"));
        db.rebuild(false);AIVERSE_TEST_CHECK(!db.byGuid(importedGuid)&&!db.byGuid(importedChildGuid));
        // A conflict in the last item must refuse the whole batch.
        fs::create_directory(project/"content"/"Other"/"BatchTree");
        bool batchRefused=false;
        try{aiverse::studio::studioReplayAssetQuarantine(project,*imported,false);}catch(const std::runtime_error&){batchRefused=true;}
        AIVERSE_TEST_CHECK(batchRefused&&!fs::exists(project/"content"/"Other"/"Batch.png"));
        fs::remove(project/"content"/"Other"/"BatchTree");
        aiverse::studio::studioReplayAssetQuarantine(project,*imported,false);
        db.rebuild(false);
        AIVERSE_TEST_CHECK(db.byGuid(importedGuid)->path=="Other/Batch.png"&&db.byGuid(importedChildGuid)->path=="Other/BatchTree/Child.png");
        AIVERSE_TEST_CHECK(read(project/"content"/"Other"/"Batch.png")=="batch pixels");
        AIVERSE_TEST_CHECK(read(project/"content"/"Other"/"Batch.png.meta")==importedMeta);
        AIVERSE_TEST_CHECK(fs::is_directory(project/"content"/"Other"/"BatchTree"/"Empty"));
        aiverse::editor::ProjectHistory::MovePlan createdFolder;
        AIVERSE_TEST_CHECK(aiverse::studio::studioCreateContentFolder(project,"Other","NewFolder",&createdFolder)=="Other/NewFolder");
        db.rebuild(false);const auto createdGuid=db.byPath("Other/NewFolder")->guid;
        aiverse::studio::studioReplayAssetQuarantine(project,createdFolder,true);
        AIVERSE_TEST_CHECK(!fs::exists(project/"content"/"Other"/"NewFolder"));
        aiverse::studio::studioReplayAssetQuarantine(project,createdFolder,false);
        db.rebuild(false);AIVERSE_TEST_CHECK(db.byGuid(createdGuid)->path=="Other/NewFolder");
        // Replace a target without touching its GUID/settings or reading the
        // external source again during undo/redo.
        write(project/"content"/"Other"/"Replace.png","old pixels");
        write(external/"Replace.png","new pixels");
        db.rebuild();
        const auto replaceGuid=db.byPath("Other/Replace.png")->guid;
        const auto live=project/"content"/"Other"/"Replace.png";
        const auto sidecar=fs::path(live.string()+".meta");
        const auto originalSidecar=read(sidecar);
        auto staleReplace=StudioTransferPlan::external(project,"Other",{external/"Replace.png"});
        AIVERSE_TEST_CHECK(staleReplace.describe()["items"][0].getBool("replacementAvailable"));
        write(live,"external pixels");
        bool replaceConflict=false;
        std::optional<aiverse::editor::ProjectHistory::MovePlan> replaceHistory;
        try{(void)staleReplace.apply(staleReplace.token(),{{0,"replace"}},{},&replaceHistory);}
        catch(const std::runtime_error&){replaceConflict=true;}
        AIVERSE_TEST_CHECK(replaceConflict&&read(live)=="external pixels"&&!replaceHistory);
        write(live,"old pixels");
        auto replacePlan=StudioTransferPlan::external(project,"Other",{external/"Replace.png"});
        const auto replacement=replacePlan.apply(replacePlan.token(),{{0,"replace"}},{},&replaceHistory);
        AIVERSE_TEST_CHECK(replacement["files"].size()==1&&replaceHistory&&replaceHistory->replacement);
        AIVERSE_TEST_CHECK(read(live)=="new pixels"&&read(sidecar)==originalSidecar);
        db.rebuild(false);AIVERSE_TEST_CHECK(db.byGuid(replaceGuid)->path=="Other/Replace.png");
        fs::remove(external/"Replace.png");
        const auto oldBackup=project/replaceHistory->files[0].to;
        const auto newBackup=project/replaceHistory->files[1].to;
        AIVERSE_TEST_CHECK(read(oldBackup)=="old pixels"&&!fs::exists(newBackup));
        write(sidecar,originalSidecar+"\n");
        replaceConflict=false;
        try{aiverse::studio::studioReplayAssetReplacement(project,*replaceHistory,false);}
        catch(const std::runtime_error&){replaceConflict=true;}
        AIVERSE_TEST_CHECK(replaceConflict&&read(live)=="new pixels"&&read(oldBackup)=="old pixels");
        write(sidecar,originalSidecar);
        aiverse::studio::studioReplayAssetReplacement(project,*replaceHistory,false);
        AIVERSE_TEST_CHECK(read(live)=="old pixels"&&read(newBackup)=="new pixels"&&!fs::exists(oldBackup));
        aiverse::studio::studioReplayAssetReplacement(project,*replaceHistory,true);
        AIVERSE_TEST_CHECK(read(live)=="new pixels"&&read(oldBackup)=="old pixels");
        AIVERSE_TEST_CHECK(read(sidecar)==originalSidecar);
        // A mixed creation/replacement batch is refused before any publication.
        write(external/"Replace.png","another version");
        write(external/"Fresh.png","fresh");
        auto mixed=StudioTransferPlan::external(project,"Other",{external/"Replace.png",external/"Fresh.png"});
        replaceConflict=false;
        try{(void)mixed.apply(mixed.token(),{{0,"replace"},{1,"import"}},{},&replaceHistory);}
        catch(const std::runtime_error&){replaceConflict=true;}
        AIVERSE_TEST_CHECK(replaceConflict&&read(live)=="new pixels"&&!fs::exists(project/"content"/"Other"/"Fresh.png"));
        // Recovery listing is independent of the session-local history.
        const auto trashId=[](const fs::path& payload){return payload.parent_path().filename().string();};
        const auto deletedId=trashId(backup);
        const auto replacementId=trashId(oldBackup);
        auto page=aiverse::studio::studioAssetTrashList(project,0,100);
        AIVERSE_TEST_CHECK(page["total"].integer()>=2&&!page.getBool("recoveryPending"));
        bool listedDelete=false,listedVersion=false;
        for(const auto& row:page["items"].items()){
            if(row.getStr("id")==deletedId)listedDelete=row.getStr("kind")=="deleted";
            if(row.getStr("id")==replacementId)listedVersion=row.getStr("kind")=="replacement";
        }
        AIVERSE_TEST_CHECK(listedDelete&&listedVersion);
        auto restore=aiverse::studio::studioAssetTrashReview(project,deletedId,"restore");
        AIVERSE_TEST_CHECK(restore.getStr("guid")==trashGuid&&!restore.getStr("token").empty());
        bool trashRefused=false;
        try{aiverse::studio::studioAssetTrashApply(project,deletedId,"restore","stale");}
        catch(const std::runtime_error&){trashRefused=true;}
        AIVERSE_TEST_CHECK(trashRefused&&fs::exists(backup));
        aiverse::studio::studioAssetTrashApply(project,deletedId,"restore",restore.getStr("token"));
        db.rebuild(false);AIVERSE_TEST_CHECK(db.byGuid(childGuid)->path=="TrashTree/Item.png");
        AIVERSE_TEST_CHECK(!fs::exists(backup));
        auto versionReview=aiverse::studio::studioAssetTrashReview(project,replacementId,"restore");
        write(live,"changed after review");
        trashRefused=false;
        try{aiverse::studio::studioAssetTrashApply(project,replacementId,"restore",versionReview.getStr("token"));}
        catch(const std::runtime_error&){trashRefused=true;}
        AIVERSE_TEST_CHECK(trashRefused&&read(live)=="changed after review"&&fs::exists(oldBackup));
        write(live,"new pixels");
        versionReview=aiverse::studio::studioAssetTrashReview(project,replacementId,"restore");
        aiverse::studio::studioAssetTrashApply(project,replacementId,"restore",versionReview.getStr("token"));
        AIVERSE_TEST_CHECK(read(live)=="old pixels"&&read(sidecar)==originalSidecar&&!fs::exists(oldBackup));
        page=aiverse::studio::studioAssetTrashList(project,0,100);
        std::string outgoingId;
        for(const auto& row:page["items"].items())
            if(row.getStr("kind")=="replacement"&&row.getStr("path")=="Other/Replace.png")outgoingId=row.getStr("id");
        AIVERSE_TEST_CHECK(!outgoingId.empty());
        const auto purge=aiverse::studio::studioAssetTrashReview(project,outgoingId,"purge");
        trashRefused=false;
        try{aiverse::studio::studioAssetTrashApply(project,outgoingId,"purge",versionReview.getStr("token"));}
        catch(const std::runtime_error&){trashRefused=true;}
        AIVERSE_TEST_CHECK(trashRefused&&read(live)=="old pixels");
        aiverse::studio::studioAssetTrashApply(project,outgoingId,"purge",purge.getStr("token"));
        AIVERSE_TEST_CHECK(read(live)=="old pixels"&&read(sidecar)==originalSidecar);
        AIVERSE_TEST_CHECK(!fs::exists(project/".aiverse-cache"/"studio-asset-trash"/outgoingId));
        // Persist a mixed owner timeline without retaining payload bytes in memory.
        {
            using namespace aiverse::studio;using aiverse::editor::ProjectHistory;
            const auto p=temp/"Persistent";
            fs::create_directories(p/"content");
            write(p/"content"/"Image.png","original image");
            StudioAssetCatalog index(p);index.rebuild();
            const auto imageGuid=index.byPath("Image.png")->guid;
            ProjectHistory timeline;
            {
                StudioAssetHistoryPublication publication(p);
                index.patchSettings(imageGuid,"compression",aiverse::Json::string("high"),&timeline);
                ProjectHistory::MovePlan folderPlan;
                studioCreateContentFolder(p,"","Folder",&folderPlan);
                timeline.recordMove(std::move(folderPlan),"Create");
                auto move=StudioTransferPlan::internal(p,"Folder",{"Image.png"});
                std::optional<ProjectHistory::MovePlan> movePlan;
                (void)move.apply(move.token(),{}, {},&movePlan);
                AIVERSE_TEST_CHECK(movePlan);timeline.recordMove(std::move(*movePlan),"Move");
                write(external/"Image.png","replacement image");
                auto replace=StudioTransferPlan::external(p,"Folder",{external/"Image.png"});
                (void)replace.apply(replace.token(),{{0,"replace"}},{},&movePlan);
                AIVERSE_TEST_CHECK(movePlan&&movePlan->replacement);
                timeline.recordMove(std::move(*movePlan),"Replace");
                auto removal=studioQuarantineAsset(p,"Folder/Image.png",imageGuid);
                timeline.recordMove(std::move(removal),"Delete");
                // The cursor and the redo branch must both survive reopening.
                studioReplayAssetQuarantine(p,*timeline.next(false).move,false);
                timeline.advance(false);publication.save(timeline);
            }
            auto restored=studioLoadAssetHistoryCheckpoint(p);
            AIVERSE_TEST_CHECK(restored.steps().size()==5&&restored.position()==4&&restored.canRedo());
            AIVERSE_TEST_CHECK(restored.versionToken()!=timeline.versionToken());
            {
                StudioAssetHistoryPublication publication(p);
                AIVERSE_TEST_CHECK(publication.cancelUnchanged(restored));
            }
            // Verify the persisted plan can actually drive the native owners.
            {
                StudioAssetHistoryPublication publication(p);
                studioReplayAssetQuarantine(p,*restored.next(true).move,true);restored.advance(true);
                AIVERSE_TEST_CHECK(!fs::exists(p/"content"/"Folder"/"Image.png"));
                studioReplayAssetQuarantine(p,*restored.next(false).move,false);restored.advance(false);
                studioReplayAssetReplacement(p,*restored.next(false).move,false);restored.advance(false);
                studioReplayAssetMove(p,*restored.next(false).move,false);restored.advance(false);
                AIVERSE_TEST_CHECK(read(p/"content"/"Image.png")=="original image");
                publication.save(restored);
            }
            auto reopened=studioLoadAssetHistoryCheckpoint(p);
            AIVERSE_TEST_CHECK(reopened.position()==2&&reopened.steps().size()==5&&reopened.canRedo());
            // Unexpected content must never reactivate an old history.
            write(p/"content"/"Image.png","external edit");
            bool rejected=false;
            try{(void)studioLoadAssetHistoryCheckpoint(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected);
            write(p/"content"/"Image.png","original image");
            AIVERSE_TEST_CHECK(studioLoadAssetHistoryCheckpoint(p).position()==2);
            // Simulate interruption after the marker, before checkpoint commit.
            {StudioAssetHistoryPublication interrupted(p);}
            rejected=false;
            try{(void)studioLoadAssetHistoryCheckpoint(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected&&fs::exists(p/"content"/"Image.png"));
            {StudioAssetHistoryPublication publication(p);publication.save(reopened);}
            AIVERSE_TEST_CHECK(studioLoadAssetHistoryCheckpoint(p).steps().size()==5);
            // Changing a retained replacement slot invalidates the checkpoint.
            const auto& replacementPlan=*reopened.steps()[3].move;
            const auto retained=p/replacementPlan.files[1].to;
            AIVERSE_TEST_CHECK(fs::exists(retained));write(retained,"damaged backup");
            rejected=false;
            try{(void)studioLoadAssetHistoryCheckpoint(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected);
        }
        // Coordinated filesystem publication: a replacement chain and a
        // disjoint quarantine pair share an outer journal with project bytes.
        {
            using namespace aiverse;using namespace aiverse::studio;
            const auto p=temp/"Coordinated";
            fs::create_directories(p/"content");
            write(p/"game.json","before");write(p/"content"/"Version.png","old");
            write(p/"content"/"Delete.png","delete me");
            StudioAssetCatalog index(p);index.rebuild();
            const auto deleteGuid=index.byPath("Delete.png")->guid;
            write(external/"Version.png","new");
            std::optional<editor::ProjectHistory::MovePlan> replacement;
            auto replace=StudioTransferPlan::external(p,"",{external/"Version.png"});
            (void)replace.apply(replace.token(),{{0,"replace"}},{},&replacement);
            AIVERSE_TEST_CHECK(replacement&&replacement->replacement);
            const auto deletion=studioQuarantineAsset(p,"Delete.png",deleteGuid);
            StudioBatchAssetPlan staged;
            studioReplayAssetReplacement(p,*replacement,false,&staged);
            studioReplayAssetQuarantine(p,deletion,false,{},&staged);
            AIVERSE_TEST_CHECK(staged.relocations.size()==4);
            AIVERSE_TEST_CHECK(read(p/"content"/"Version.png")=="new"&&!fs::exists(p/"content"/"Delete.png"));
            staged.documents.push_back({p/"game.json","before","after"});
            const auto folder=p/".aiverse-cache"/"studio-history-batch";
            auto manifest=[&]{
                Json body=Json::object(),moves=Json::array(),documents=Json::array();
                for(const auto& m:staged.relocations){
                    Json row=Json::object();row.set("from",Json::string(m.from));row.set("to",Json::string(m.to));
                    row.set("digest",Json::string(m.digest));row.set("directory",Json::fromBool(m.directory));moves.push(std::move(row));
                }
                for(const auto& f:staged.documents){
                    Json row=Json::object();row.set("path",Json::string(f.path.lexically_relative(p).generic_string()));
                    row.set("before",Json::string(f.before));row.set("after",Json::string(f.after));documents.push(std::move(row));
                }
                body.set("moves",std::move(moves));body.set("documents",std::move(documents));
                const auto payload=body.dump(-1);
                const auto sum=pack::sha256(payload.data(),payload.size());const auto hash=pack::toHex(sum.data(),sum.size());
                Json envelope=Json::object();envelope.set("version",Json::number(int64_t(1)));
                envelope.set("payload",Json::string(payload));envelope.set("sha256",Json::string(hash));return envelope.dump(-1);
            };
            const auto envelope=manifest();
            for(size_t phase=0;phase<=staged.relocations.size();++phase){
                fs::create_directory(folder);write(folder/"pending.json",envelope);
                for(size_t i=0;i<phase;++i){const auto& m=staged.relocations[i];fs::rename(p/m.from,p/m.to);}
                if(phase==staged.relocations.size())write(p/"game.json","after");
                studioRecoverHistoryBatch(p);
                AIVERSE_TEST_CHECK(!fs::exists(folder)&&read(p/"game.json")=="before");
                AIVERSE_TEST_CHECK(read(p/"content"/"Version.png")=="new"&&!fs::exists(p/"content"/"Delete.png"));
            }
            // External bytes must stop the entire rollback, not overwrite them.
            fs::create_directory(folder);write(folder/"pending.json",envelope);
            for(const auto& m:staged.relocations)fs::rename(p/m.from,p/m.to);
            write(p/"game.json","external");
            bool rejected=false;
            try{studioRecoverHistoryBatch(p);}catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected&&read(p/"game.json")=="external"&&read(p/"content"/"Version.png")=="old");
            write(p/"game.json","after");studioRecoverHistoryBatch(p);
            AIVERSE_TEST_CHECK(read(p/"game.json")=="before"&&read(p/"content"/"Version.png")=="new");
            studioApplyHistoryBatch(p,staged.relocations,staged.documents);
            AIVERSE_TEST_CHECK(read(p/"game.json")=="after"&&read(p/"content"/"Version.png")=="old");
            AIVERSE_TEST_CHECK(read(p/"content"/"Delete.png")=="delete me"&&!fs::exists(folder));
            index.rebuild(false);AIVERSE_TEST_CHECK(index.byGuid(deleteGuid)->path=="Delete.png");
        }
        // S6f.3.1: A -> B -> C with stationary URI repairs composed
        // against the final projected graph, not nonexistent intermediate files.
        {
            using namespace aiverse;using namespace aiverse::studio;
            const auto p=temp/"MoveChain";
            for(const auto& dir:{"A","B","C","Models"})fs::create_directories(p/"content"/dir);
            const auto texture=p/"content"/"A"/"tex.png",model=p/"content"/"Models"/"Scene.gltf";
            const auto glb=p/"content"/"Models"/"Large.glb";
            const std::string initial="{ \"images\": [{\"uri\":\"../A/tex.png\"}] }\n";
            write(texture,"texture");write(model,initial);
            const auto doc=Json::parse(R"({"images":[{"uri":"../A/tex.png"}],"buffers":[{"byteLength":2097152}]})");
            constexpr uint32_t payload=2u*1024u*1024u;
            std::string tail;studioGlbAppendU32(tail,payload);studioGlbAppendU32(tail,0x004e4942);
            tail.append(payload,'\x91');
            write(glb,studioEncodeGlbPrefix(doc,tail.size())+tail);
            const auto initialGlb=studioAssetFileDigest(glb);
            StudioAssetCatalog index(p);index.rebuild();
            const auto guid=index.byPath("A/tex.png")->guid;
            std::optional<editor::ProjectHistory::MovePlan> first,second;
            auto move=StudioTransferPlan::internal(p,"B",{"A/tex.png"});
            (void)move.apply(move.token(),{},{},&first,true);
            move=StudioTransferPlan::internal(p,"C",{"B/tex.png"});
            (void)move.apply(move.token(),{},{},&second,true);
            AIVERSE_TEST_CHECK(first&&second&&first->resourceEdits.size()==2&&second->resourceEdits.size()==2);
            const auto final=read(model),finalGlb=studioAssetFileDigest(glb);
            const auto undo=studioPlanAssetMoveChain(p,{*second,*first},false);
            AIVERSE_TEST_CHECK(undo.relocations.size()==4&&undo.documents.size()==2);
            AIVERSE_TEST_CHECK(studioStreamingResource(undo.documents[0])||studioStreamingResource(undo.documents[1]));
            // Recover a v2 batch after both relocations and streamed publication.
            const auto folder=p/".aiverse-cache"/"studio-history-batch";
            Json body=Json::object(),moves=Json::array(),docs=Json::array();
            for(const auto& m:undo.relocations){
                Json row=Json::object();row.set("from",Json::string(m.from));row.set("to",Json::string(m.to));
                row.set("digest",Json::string(m.digest));row.set("directory",Json::fromBool(m.directory));moves.push(std::move(row));
            }
            for(auto edit:undo.documents){
                edit.path=edit.path.lexically_relative(fs::canonical(p));
                auto row=studioEncodeResourceEdit(edit);row.set("resource",Json::fromBool(true));docs.push(std::move(row));
            }
            body.set("moves",std::move(moves));body.set("documents",std::move(docs));
            const auto payloadJson=body.dump(-1);const auto hash=pack::sha256(payloadJson.data(),payloadJson.size());
            Json envelope=Json::object();envelope.set("version",Json::number(int64_t(2)));
            envelope.set("payload",Json::string(payloadJson));envelope.set("sha256",Json::string(pack::toHex(hash.data(),hash.size())));
            fs::create_directory(folder);write(folder/"pending.json",envelope.dump(-1));
            for(const auto& m:undo.relocations)fs::rename(p/m.from,p/m.to);
            StudioMoveJournal(p).applyResources(undo.documents,true);
            AIVERSE_TEST_CHECK(read(model)==initial&&studioAssetFileDigest(glb)==initialGlb);
            studioRecoverHistoryBatch(p);
            AIVERSE_TEST_CHECK(read(model)==final&&studioAssetFileDigest(glb)==finalGlb);
            AIVERSE_TEST_CHECK(fs::exists(p/"content"/"C"/"tex.png")&&!fs::exists(folder));
            studioApplyHistoryBatch(p,undo.relocations,undo.documents);
            AIVERSE_TEST_CHECK(fs::exists(texture)&&read(model)==initial&&studioAssetFileDigest(glb)==initialGlb);
            index.rebuild(false);AIVERSE_TEST_CHECK(index.byGuid(guid)->path=="A/tex.png");
            const auto redo=studioPlanAssetMoveChain(p,{*first,*second},true);
            studioApplyHistoryBatch(p,redo.relocations,redo.documents);
            AIVERSE_TEST_CHECK(read(model)==final&&studioAssetFileDigest(glb)==finalGlb);
            index.rebuild(false);AIVERSE_TEST_CHECK(index.byGuid(guid)->path=="C/tex.png");
            write(model,final+"\n");
            bool refused=false;try{(void)studioPlanAssetMoveChain(p,{*second,*first},false);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&fs::exists(p/"content"/"C"/"tex.png"));
        }
        // S6f.3.2: moving a directory changes its model bytes and tree digest
        // at each logical step. The batch writes only the final resource bytes.
        {
            using namespace aiverse;using namespace aiverse::studio;
            const auto p=temp/"MovingRepairChain";
            for(const auto& dir:{"A/Pack","B","C/Deep"})fs::create_directories(p/"content"/dir);
            const auto source=p/"content"/"A"/"Pack";
            const auto destination=p/"content"/"C"/"Deep"/"Pack";
            const std::string original="{ \"images\": [{\"uri\":\"../tex.png\"}] }\n";
            write(p/"content"/"A"/"tex.png","texture");write(source/"Scene.gltf",original);
            const auto doc=Json::parse(R"({"images":[{"uri":"../tex.png"}],"buffers":[{"byteLength":2097152}]})");
            std::string tail;studioGlbAppendU32(tail,2u*1024u*1024u);studioGlbAppendU32(tail,0x004e4942);
            tail.append(2u*1024u*1024u,'\xa3');
            write(source/"Large.glb",studioEncodeGlbPrefix(doc,tail.size())+tail);
            const auto originalGlb=studioAssetFileDigest(source/"Large.glb");
            StudioAssetCatalog index(p);index.rebuild();
            const auto guid=index.byPath("A/Pack")->guid;
            std::optional<editor::ProjectHistory::MovePlan> first,second;
            auto move=StudioTransferPlan::internal(p,"B",{"A/Pack"});
            (void)move.apply(move.token(),{},{},&first,true);
            move=StudioTransferPlan::internal(p,"C/Deep",{"B/Pack"});
            (void)move.apply(move.token(),{},{},&second,true);
            AIVERSE_TEST_CHECK(first&&second&&first->resourceEdits.size()==2&&second->resourceEdits.size()==2);
            AIVERSE_TEST_CHECK(!first->files[0].digestAfter.empty()&&!second->files[0].digestAfter.empty());
            const auto final=read(destination/"Scene.gltf"),finalGlb=studioAssetFileDigest(destination/"Large.glb");
            const auto undo=studioPlanAssetMoveChain(p,{*second,*first},false);
            AIVERSE_TEST_CHECK(undo.relocations.size()==4&&undo.documents.empty()&&undo.movingResources.size()==2);
            for(const auto& resource:undo.movingResources)
                AIVERSE_TEST_CHECK(resource.destination.starts_with("content/A/Pack/"));
            const auto folder=p/".aiverse-cache"/"studio-history-batch";
            Json body=Json::object(),moves=Json::array(),resources=Json::array();
            for(const auto& m:undo.relocations){
                Json row=Json::object();row.set("from",Json::string(m.from));row.set("to",Json::string(m.to));
                row.set("digest",Json::string(m.digest));row.set("directory",Json::fromBool(m.directory));moves.push(std::move(row));
            }
            for(const auto& resource:undo.movingResources){
                auto row=studioEncodeResourceEdit(resource.edit);row.set("destination",Json::string(resource.destination));
                resources.push(std::move(row));
            }
            body.set("moves",std::move(moves));body.set("documents",Json::array());body.set("movingResources",std::move(resources));
            const auto payload=body.dump(-1);const auto hash=pack::sha256(payload.data(),payload.size());
            Json envelope=Json::object();envelope.set("version",Json::number(int64_t(3)));
            envelope.set("payload",Json::string(payload));envelope.set("sha256",Json::string(pack::toHex(hash.data(),hash.size())));
            // Interrupt at every rename boundary, then after one of the
            // final edits and after all final edits. Recover exact bytes.
            for(size_t phase=0;phase<=undo.relocations.size()+2;++phase){
                fs::create_directory(folder);write(folder/"pending.json",envelope.dump(-1));
                for(size_t i=0;i<std::min(phase,undo.relocations.size());++i){
                    const auto& m=undo.relocations[i];fs::rename(p/m.from,p/m.to);
                }
                if(phase>undo.relocations.size()){
                    std::vector<editor::ProjectFileEdit> edits;
                    for(size_t i=0;i<phase-undo.relocations.size();++i){
                        auto edit=undo.movingResources[i].edit;
                        edit.path=fs::canonical(p/undo.movingResources[i].destination);
                        edits.push_back(std::move(edit));
                    }
                    StudioMoveJournal(p).applyResources(edits,true);
                }
                studioRecoverHistoryBatch(p);
                AIVERSE_TEST_CHECK(!fs::exists(folder)&&!fs::exists(source));
                AIVERSE_TEST_CHECK(read(destination/"Scene.gltf")==final&&studioAssetFileDigest(destination/"Large.glb")==finalGlb);
            }
            studioApplyHistoryBatch(p,undo.relocations,undo.documents,undo.movingResources);
            AIVERSE_TEST_CHECK(read(source/"Scene.gltf")==original&&studioAssetFileDigest(source/"Large.glb")==originalGlb);
            index.rebuild(false);AIVERSE_TEST_CHECK(index.byGuid(guid)->path=="A/Pack");
            const auto redo=studioPlanAssetMoveChain(p,{*first,*second},true);
            studioApplyHistoryBatch(p,redo.relocations,redo.documents,redo.movingResources);
            AIVERSE_TEST_CHECK(read(destination/"Scene.gltf")==final&&studioAssetFileDigest(destination/"Large.glb")==finalGlb);
            index.rebuild(false);AIVERSE_TEST_CHECK(index.byGuid(guid)->path=="C/Deep/Pack");
            write(destination/"Scene.gltf",final+"\n");
            bool rejected=false;try{(void)studioPlanAssetMoveChain(p,{*second,*first},false);}
            catch(const std::runtime_error&){rejected=true;}
            AIVERSE_TEST_CHECK(rejected&&fs::exists(destination));
        }
        // S6f.3.3: import -> move -> settings -> replace -> delete.
        // Metadata follows its identity across content and exact Trash slots.
        {
            using namespace aiverse;using namespace aiverse::studio;
            const auto p=fs::canonical(temp)/"MixedChain";
            fs::create_directories(p/"content"/"A");fs::create_directories(p/"content"/"B");
            const auto imported=external/"Mixed.png";write(imported,"old pixels");
            StudioAssetCatalog index(p);index.rebuild();
            editor::ProjectHistory history;
            std::optional<editor::ProjectHistory::MovePlan> plan;
            auto import=StudioTransferPlan::external(p,"A",{imported});
            (void)import.apply(import.token(),{},{},&plan);
            AIVERSE_TEST_CHECK(plan&&plan->created);
            const auto createdSlot=plan->files[0].to;
            history.recordMove(std::move(*plan),"Import");
            index.rebuild(false);const auto guid=index.byPath("A/Mixed.png")->guid;
            auto move=StudioTransferPlan::internal(p,"B",{"A/Mixed.png"});
            (void)move.apply(move.token(),{},{},&plan,true);
            AIVERSE_TEST_CHECK(plan);history.recordMove(std::move(*plan),"Move");
            index.rebuild(false);
            const auto originalMeta=read(p/"content"/"B"/"Mixed.png.meta");
            index.patchSettings(guid,"compression",Json::string("high"),&history);
            const auto changedMeta=read(p/"content"/"B"/"Mixed.png.meta");
            AIVERSE_TEST_CHECK(originalMeta!=changedMeta);
            write(imported,"new pixels");
            auto replace=StudioTransferPlan::external(p,"B",{imported});
            (void)replace.apply(replace.token(),{{0,"replace"}},{},&plan);
            AIVERSE_TEST_CHECK(plan&&plan->replacement);
            history.recordMove(std::move(*plan),"Replace");
            auto deletion=studioQuarantineAsset(p,"B/Mixed.png",guid);
            const auto deletedSlot=deletion.files[0].to;
            history.recordMove(std::move(deletion),"Delete");
            AIVERSE_TEST_CHECK(history.steps().size()==5&&!fs::exists(p/"content"/"B"/"Mixed.png"));
            auto reversed=history.steps();std::reverse(reversed.begin(),reversed.end());
            AIVERSE_TEST_CHECK(studioCanPlanMixedAssetChain(reversed));
            const auto undo=studioPlanMixedAssetChain(p,reversed,false);
            AIVERSE_TEST_CHECK(undo.relocations.size()==8&&undo.movingResources.size()==1);
            AIVERSE_TEST_CHECK(undo.movingResources[0].destination==createdSlot+".meta");
            // Exercise quota accounting on sources that exist only virtually.
            (void)studioSetRecoveryQuota(p,0,1);
            studioApplyHistoryBatch(p,undo.relocations,undo.documents,undo.movingResources);
            AIVERSE_TEST_CHECK(read(p/createdSlot)=="old pixels"&&read(p/(createdSlot+".meta"))==originalMeta);
            index.rebuild(false);AIVERSE_TEST_CHECK(!index.byGuid(guid));
            const auto redo=studioPlanMixedAssetChain(p,history.steps(),true);
            AIVERSE_TEST_CHECK(redo.movingResources.size()==1&&redo.movingResources[0].destination==deletedSlot+".meta");
            studioApplyHistoryBatch(p,redo.relocations,redo.documents,redo.movingResources);
            AIVERSE_TEST_CHECK(read(p/deletedSlot)=="new pixels"&&read(p/(deletedSlot+".meta"))==changedMeta);
            AIVERSE_TEST_CHECK(!fs::exists(p/"content"/"B"/"Mixed.png"));
            // Crash after final sidecar publication in a recovery slot.
            const auto pending=studioPlanMixedAssetChain(p,reversed,false);
            const auto folder=p/".aiverse-cache"/"studio-history-batch";
            Json body=Json::object(),moves=Json::array(),resources=Json::array();
            for(const auto& m:pending.relocations){
                Json row=Json::object();row.set("from",Json::string(m.from));row.set("to",Json::string(m.to));
                row.set("digest",Json::string(m.digest));row.set("directory",Json::fromBool(m.directory));moves.push(std::move(row));
            }
            for(const auto& resource:pending.movingResources){
                Json row=Json::object();row.set("metadata",Json::fromBool(true));
                row.set("path",Json::string(resource.edit.path.generic_string()));
                row.set("destination",Json::string(resource.destination));
                row.set("before",Json::string(resource.edit.before));row.set("after",Json::string(resource.edit.after));
                resources.push(std::move(row));
            }
            body.set("moves",std::move(moves));body.set("documents",Json::array());body.set("movingResources",std::move(resources));
            const auto payload=body.dump(-1);const auto hash=pack::sha256(payload.data(),payload.size());
            Json envelope=Json::object();envelope.set("version",Json::number(int64_t(3)));
            envelope.set("payload",Json::string(payload));envelope.set("sha256",Json::string(pack::toHex(hash.data(),hash.size())));
            fs::create_directory(folder);write(folder/"pending.json",envelope.dump(-1));
            for(const auto& m:pending.relocations)fs::rename(p/m.from,p/m.to);
            auto edit=pending.movingResources[0].edit;edit.path=fs::canonical(p/pending.movingResources[0].destination);
            StudioMoveJournal(p).applyResources({edit},true);
            studioRecoverHistoryBatch(p);
            AIVERSE_TEST_CHECK(read(p/deletedSlot)=="new pixels"&&read(p/(deletedSlot+".meta"))==changedMeta&&!fs::exists(folder));
            write(p/(deletedSlot+".meta"),changedMeta+"\n");
            bool refused=false;try{(void)studioPlanMixedAssetChain(p,reversed,false);}
            catch(const std::runtime_error&){refused=true;}
            AIVERSE_TEST_CHECK(refused&&fs::exists(p/deletedSlot));
        }
        std::cout<<"PASS studio conflict resolution and GUID-preserving asset transfers\n";
    }catch(...){fs::remove_all(temp);throw;}
    fs::remove_all(temp);
}
