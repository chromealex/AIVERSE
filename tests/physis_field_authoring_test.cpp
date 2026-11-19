#include "apps/editor/PhysisAuthoring.h"
#include "apps/editor/PhysisGizmoGeometry.h"
#include <cstdio>
#include <cmath>
#include <limits>
using namespace aiverse;
using namespace aiverse::editor;
using namespace aiverse::editor::physis_authoring;
namespace {
int failures=0;
#define CHECK(v) do { if(!(v)){std::printf("PHYSIS FIELD EDIT FAIL %s:%d %s\n",__FILE__,__LINE__,#v);++failures;} } while(0)
void testOneDragOneUndo() {
    MapDoc doc;doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,10,0,-10,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,-5,1,5,gravity,&error));
    CHECK(!wind.empty() && !storm.empty() && !gravity.empty());
    const Json before=read(doc);
    const auto rev=doc.revision;
    CHECK(moveField(doc,wind,5.5,2,7,true,&error));  // first actual drag
    CHECK(moveField(doc,wind,7.5,3,8,false,&error)); // same drag no snapshot
    CHECK(doc.revision>rev);
    std::string key;
    std::size_t index=0;
    Json field;
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed::fromRatio(15,2));
    doc.undo();
    CHECK(read(doc)==before); // one Undo must revert the WHOLE drag
    doc.redo();
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed::fromRatio(15,2));
    const auto stableRev=doc.revision;
    CHECK(!moveField(doc,wind,2000000,0,0,true,&error));
    CHECK(doc.revision==stableRev);
    CHECK(!error.empty());

    Json copy=field;
    copy.set("customField",Json::string("preserved"));
    CHECK(replaceField(doc,wind,copy,&error));
    CHECK(read(doc)["windZones"][0]["customField"].str()=="preserved");
    CHECK(removeField(doc,storm,&error));
    CHECK(!findField(read(doc),storm,key,index));
    doc.undo();
    CHECK(findField(read(doc),storm,key,index));
}
void testRadiusResizeAndSnapping() {
    MapDoc doc; doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,1,2,3,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,4,5,6,gravity,&error));
    CHECK(snapMeters(-2.6,5.0) == -5.0);
    CHECK(snapMeters(2.6,5.0) == 5.0);
    CHECK(snapMeters(-2.5,5.0) == -5.0);
    CHECK(snapMeters(1.2,0.25) == 1.25);
    CHECK(snapMeters(1.2,0.0) == 1.2);
    CHECK(snapMeters(1000001.0,5.0) == 1000000.0);
    CHECK(moveField(doc,wind,snapMeters(-12.2,5),0,snapMeters(12.2,5),true,&error));
    std::string key; std::size_t index=0; Json field;
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx() == Fixed::fromInt(-10));
    const Json before=read(doc);
    CHECK(resizeField(doc,storm,250.25,true,&error));
    CHECK(resizeField(doc,storm,300.75,false,&error));
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["outerRadiusMeters"].fx() == Fixed::fromRatio(1203,4));
    CHECK(field["eyeRadiusMeters"].fx() == Fixed::fromInt(20));
    doc.undo();
    CHECK(read(doc)==before); // entire resize drag is one Undo entry
    doc.redo();
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["outerRadiusMeters"].fx() == Fixed::fromRatio(1203,4));
    const auto rev=doc.revision;
    CHECK(resizeField(doc,storm,300.75,true,&error)); // no-op
    CHECK(doc.revision==rev);
    CHECK(!resizeField(doc,storm,19.0,true,&error)); // eye radius violation
    CHECK(doc.revision==rev);
    CHECK(!resizeField(doc,gravity,100001.0,true,&error));
    CHECK(doc.revision==rev);
    CHECK(!resizeField(doc,wind,std::numeric_limits<double>::infinity(),true,&error));
    CHECK(doc.revision==rev);
    CHECK(resizeField(doc,gravity,10.0,true,&error));
    CHECK(findField(read(doc),gravity,key,index,&field));
    CHECK(field["radiusMeters"].fx() == Fixed::fromInt(10));
}

void testGroupOperations() {
    MapDoc doc; doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,-10,3,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,20,5,-30,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,1,-2,4,gravity,&error));
    std::string collection; std::size_t index=0;
    Json source;
    CHECK(findField(read(doc),wind,collection,index,&source));
    source.set("customTag",Json::string("keep"));
    CHECK(replaceField(doc,wind,source,&error));
    const Json before = read(doc);
    const auto revBefore = doc.revision;
    CHECK(moveFields(doc,{{wind,-5,3,10},{storm,25,5,-20},{gravity,6,-2,14}},true,&error));
    CHECK(moveFields(doc,{{wind,0,3,12},{storm,30,5,-18},{gravity,11,-2,16}},false,&error));
    CHECK(doc.revision>revBefore);
    CHECK(findField(read(doc),storm,collection,index,&source));
    CHECK(source["center"][0].fx()==Fixed::fromInt(30));
    doc.undo();
    CHECK(read(doc)==before);
    doc.redo();
    CHECK(findField(read(doc),wind,collection,index,&source));
    CHECK(source["center"][0].fx()==Fixed::fromInt(0));
    CHECK(source["customTag"].str()=="keep");
    const Json moved = read(doc);
    const auto rev=doc.revision;
    CHECK(moveFields(doc,{{wind,0,3,12},{storm,30,5,-18}},true,&error));
    CHECK(doc.revision==rev);
    CHECK(!moveFields(doc,{{wind,100,0,0},{"missing",20,0,0}},true,&error));
    CHECK(read(doc)==moved && doc.revision==rev);
    CHECK(!moveFields(doc,{{wind,100,0,0},{storm,2000000,0,0}},true,&error));
    CHECK(read(doc)==moved && doc.revision==rev);
    CHECK(!moveFields(doc,{{wind,100,0,0},{wind,200,0,0}},true,&error));
    CHECK(read(doc)==moved && doc.revision==rev);

    std::vector<std::string> copies;
    CHECK(duplicateFields(doc,{gravity,storm,wind},5,0,5,copies,&error));
    CHECK(copies.size()==3);
    CHECK(read(doc)["windZones"].size()==2);
    CHECK(read(doc)["storms"].size()==2);
    CHECK(read(doc)["gravityFields"].size()==2);
    CHECK(findField(read(doc),wind+"-copy",collection,index,&source));
    CHECK(source["customTag"].str()=="keep");
    CHECK(source["center"][0].fx()==Fixed::fromInt(5));
    doc.undo();
    CHECK(read(doc)==moved);
    doc.redo();
    CHECK(read(doc)["windZones"].size()==2);
    const Json duplicated=read(doc);
    const auto dupeRev=doc.revision;
    CHECK(!duplicateFields(doc,{wind,"missing"},5,0,5,copies,&error));
    CHECK(read(doc)==duplicated && doc.revision==dupeRev);
    CHECK(!duplicateFields(doc,{wind,storm},2000000,0,0,copies,&error));
    CHECK(read(doc)==duplicated && doc.revision==dupeRev);
    std::vector<std::string> secondCopies;
    CHECK(duplicateFields(doc,{wind},0,0,0,secondCopies,&error));
    CHECK(secondCopies.size()==1);
    CHECK(secondCopies[0]==wind+"-copy-2");
    doc.undo();
    CHECK(read(doc)==duplicated);
    CHECK(removeFields(doc,{wind,storm},&error));
    CHECK(!findField(read(doc),wind,collection,index));
    CHECK(!findField(read(doc),storm,collection,index));
    CHECK(findField(read(doc),wind+"-copy",collection,index));
    doc.undo();
    CHECK(read(doc)==duplicated);
    const auto safeRev=doc.revision;
    CHECK(!removeFields(doc,{wind,"unknown"},&error));
    CHECK(read(doc)==duplicated && doc.revision==safeRev);
}
void testGroupFpsMap() {
    MapDoc fps;
    std::string error;
    CHECK(fps.loadJson(Json::parse(R"({"name":"fps","colliders":[],"spawns":[],"other":"unchanged"})"),&error));
    CHECK(apply(fps,defaultConfig(),&error));
    std::string wind,gravity;
    CHECK(addField(fps,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(fps,physis::AuthoredFieldKind::Gravity,10,0,10,gravity,&error));
    const Json before=read(fps);
    CHECK(moveFields(fps,{{wind,5,0,5},{gravity,15,0,15}},true,&error));
    CHECK(fps.toJson().getStr("other")=="unchanged");
    fps.undo();
    CHECK(read(fps)==before);
    std::vector<std::string> copies;
    CHECK(duplicateFields(fps,{wind,gravity},4,0,4,copies,&error));
    MapDoc roundtrip;
    CHECK(roundtrip.loadJson(fps.toJson(),&error));
    CHECK(read(roundtrip)["windZones"].size()==2);
    CHECK(read(roundtrip)["gravityFields"].size()==2);
    CHECK(removeFields(fps,copies,&error));
    CHECK(read(fps)==before);
    fps.undo();
    CHECK(read(fps)["windZones"].size()==2);
}

void testGroupRotationAndScale() {
    MapDoc doc;doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm,gravity;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,10,0,0,storm,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,-10,0,0,gravity,&error));
    const Json before=read(doc);
    const auto rev=doc.revision;
    CHECK(transformFields(doc,{gravity,wind,storm},0,0,0,90.0,2.0,&error));
    CHECK(doc.revision>rev);
    std::string key;std::size_t index=0;Json field;
    CHECK(findField(read(doc),wind,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed{});
    CHECK(field["radiusMeters"].fx()==Fixed::fromInt(200));
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["center"][0].fx()==Fixed{});
    CHECK(field["center"][2].fx()==Fixed::fromInt(20));
    CHECK(field["eyeRadiusMeters"].fx()==Fixed::fromInt(40));
    CHECK(field["outerRadiusMeters"].fx()==Fixed::fromInt(400));
    CHECK(field["heightMeters"].fx()==Fixed::fromInt(800));
    CHECK(findField(read(doc),gravity,key,index,&field));
    CHECK(field["center"][2].fx()==Fixed::fromInt(-20));
    CHECK(field["radiusMeters"].fx()==Fixed::fromInt(240));
    doc.undo();
    CHECK(read(doc)==before); // rotation + scale = one Undo
    doc.redo();
    CHECK(findField(read(doc),storm,key,index,&field));
    CHECK(field["center"][2].fx()==Fixed::fromInt(20));
    const Json after=read(doc);
    const auto stableRev=doc.revision;
    CHECK(!transformFields(doc,{wind,"missing"},0,0,0,20,1.0,&error));
    CHECK(doc.revision==stableRev && read(doc)==after);
    CHECK(!transformFields(doc,{wind,storm},0,0,0,0,-1,&error));
    CHECK(doc.revision==stableRev && read(doc)==after);
    CHECK(!transformFields(doc,{wind,storm},0,0,0,0,0.001,&error));
    CHECK(doc.revision==stableRev && read(doc)==after);
}

void testPivotAndGizmoGeometry() {
    MapDoc doc;doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string a,b,c;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,-4,2,8,a,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Gravity,6,4,14,b,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,2,8,-2,c,&error));
    FieldPivot pivot{999,999,999},custom{11,12,-13};
    CHECK(computeFieldPivot(read(doc),{a,b,c},b,FieldPivotMode::Active,custom,pivot,&error));
    CHECK(pivot.x==6 && pivot.y==4 && pivot.z==14);
    CHECK(computeFieldPivot(read(doc),{c,b,a},b,FieldPivotMode::Median,custom,pivot,&error));
    CHECK(std::abs(pivot.x-4.0/3.0)<1e-6 && pivot.y==14.0/3.0 &&
          std::abs(pivot.z-20.0/3.0)<1e-6);
    CHECK(computeFieldPivot(read(doc),{a,b,c},b,FieldPivotMode::BoundsCenter,custom,pivot,&error));
    CHECK(pivot.x==1 && pivot.y==5 && pivot.z==6);
    CHECK(computeFieldPivot(read(doc),{a},a,FieldPivotMode::WorldOrigin,custom,pivot,&error));
    CHECK(pivot.x==0 && pivot.y==0 && pivot.z==0);
    CHECK(computeFieldPivot(read(doc),{a,b},a,FieldPivotMode::Custom,custom,pivot,&error));
    CHECK(pivot.x==11 && pivot.y==12 && pivot.z==-13);
    const auto previous=pivot;
    CHECK(!computeFieldPivot(read(doc),{a,"missing"},a,FieldPivotMode::Median,custom,pivot,&error));
    CHECK(pivot.x==previous.x);
    CHECK(!computeFieldPivot(read(doc),{a,a},a,FieldPivotMode::Active,custom,pivot,&error));
    CHECK(!computeFieldPivot(read(doc),{a},a,FieldPivotMode::Custom,{2000000,0,0},pivot,&error));
    const auto picked=physis_gizmo::idsInScreenRect({{"b",10,10},{"a",5,5},{"a",7,7},{"c",99,0}},
                                                   11,11,0,0);
    CHECK(picked.size()==2 && picked[0]=="a" && picked[1]=="b");
    CHECK(physis_gizmo::idsInScreenRect({{"a",1,1}},5,5,6,6).empty());
    const auto backwards=physis_gizmo::idsInScreenRect(
        {{"alpha",3,4},{"beta",18,19},{"gamma",19,30}},20,20,0,0);
    CHECK(backwards.size()==2 && backwards[0]=="alpha" && backwards[1]=="beta");
    CHECK(physis_gizmo::idsInScreenRect({{"x",0,0}},
        std::numeric_limits<double>::quiet_NaN(),0,10,10).empty());
    double parameter=42;
    CHECK(physis_gizmo::closestRayAxisParameter({0,0,10},{0,0,-1},
                                              {0,0,0},0,parameter));
    CHECK(std::abs(parameter)<1e-5);
    CHECK(physis_gizmo::closestRayAxisParameter({0,0,10},{0,0,-1},
                                              {5,0,0},0,parameter));
    CHECK(std::abs(parameter+5)<1e-5);
    CHECK(!physis_gizmo::closestRayAxisParameter({0,0,10},{0,0,-1},
                                               {0,0,0},2,parameter));
    CHECK(!physis_gizmo::closestRayAxisParameter({0,0,10},{0,0,-1},
                                               {0,0,0},99,parameter));
    CHECK(!physis_gizmo::closestRayAxisParameter({0,0,10},{0,0,1},
                                               {0,0,0},0,parameter));
    CHECK(physis_gizmo::segmentDistanceSq(5,1,0,0,10,0)<1.01);
    CHECK(physis_gizmo::segmentDistanceSq(5,20,0,0,10,0)>300);
}


void testTransformFromFrozenBaseline() {
    MapDoc doc; doc.reset(8,8);
    std::string error;
    CHECK(apply(doc,defaultConfig(),&error));
    std::string wind,storm;
    CHECK(addField(doc,physis::AuthoredFieldKind::Wind,0,0,0,wind,&error));
    CHECK(addField(doc,physis::AuthoredFieldKind::Storm,10,0,0,storm,&error));
    const Json original=read(doc);
    const auto rev=doc.revision;
    CHECK(transformFields(doc,{wind,storm},0,0,0,45,1.0,&error,&original,true));
    CHECK(doc.revision>rev);
    CHECK(transformFields(doc,{wind,storm},0,0,0,90,2.0,&error,&original,false));
    std::string collection;std::size_t index=0;Json field;
    CHECK(findField(read(doc),storm,collection,index,&field));
    CHECK(field["center"][0].fx()==Fixed{});
    CHECK(field["center"][2].fx()==Fixed::fromInt(20));
    CHECK(field["outerRadiusMeters"].fx()==Fixed::fromInt(400));
    doc.undo();
    CHECK(read(doc)==original); // complete multi-frame drag: one undo
    doc.redo();
    CHECK(findField(read(doc),storm,collection,index,&field));
    CHECK(field["outerRadiusMeters"].fx()==Fixed::fromInt(400));
    const auto lastRevision=doc.revision;
    CHECK(!transformFields(doc,{wind,storm},0,0,0,0,0.0001,&error,&original,true));
    CHECK(doc.revision==lastRevision);
}

void testFpsAndDuplicateIds() {
    MapDoc fps;
    std::string error;
    CHECK(fps.loadJson(Json::parse(R"({"name":"fps","colliders":[],"spawns":[],"mod":"preserve"})"),&error));
    CHECK(apply(fps,defaultConfig(),&error));
    std::string id;
    CHECK(addField(fps,physis::AuthoredFieldKind::Storm,0,0,0,id,&error));
    CHECK(fps.toJson()["mod"].str()=="preserve");
    CHECK(fps.toJson()["physis"]["storms"].size()==1);
    MapDoc reread;
    CHECK(reread.loadJson(fps.toJson(),&error));
    CHECK(read(reread)["storms"].size()==1);
    fps.undo();
    CHECK(read(fps)["storms"].isNull());
    fps.redo();
    CHECK(read(fps)["storms"].size()==1);
    const Json fpsBeforeResize=read(fps);
    CHECK(resizeField(fps,id,350.5,true,&error));
    CHECK(read(fps)["storms"][0]["outerRadiusMeters"].fx()==Fixed::fromRatio(701,2));
    fps.undo();
    CHECK(read(fps)==fpsBeforeResize);
    fps.redo();
    CHECK(read(fps)["storms"][0]["outerRadiusMeters"].fx()==Fixed::fromRatio(701,2));

    Json invalid=read(fps);
    auto storms=invalid["storms"];
    auto clone=storms[0];
    storms.push(clone);
    invalid.set("storms",storms);
    const auto revision=fps.revision;
    CHECK(!apply(fps,invalid,&error));
    CHECK(fps.revision==revision);
}
}
int main(){testOneDragOneUndo();testRadiusResizeAndSnapping();testGroupOperations();testGroupFpsMap();testGroupRotationAndScale();testPivotAndGizmoGeometry();testTransformFromFrozenBaseline();testFpsAndDuplicateIds();
std::printf("PHYSIS Field Authoring: %s\n",failures?"FAIL":"PASS");return failures?1:0;}
