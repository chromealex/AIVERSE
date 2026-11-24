// MetaHuman adapter: synthetic fixture always; real exports when
// AIVERSE_METAHUMAN_SAMPLES lists glTF/GLB paths (';'-separated) — the
// verification run on KINGSTON uses the local Ada / Bruce exports (not vendored).
#include "engine/character/vivid/MetaHumanAdapter.h"
#include "engine/character/vivid/Face.h"
#include "engine/character/vivid/HumanAppearance.h"
#include "engine/character/vivid/HumanFaceShape.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
using namespace aiverse;
static int failures=0;
static void check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);++failures;}}
int main(){
    render::Model m;
    for(const char* n:{"root_1","pelvis_2","spine_01_3","spine_05_4","neck_01_5","neck_02_6","head_7","upperarm_correctiveRoot_l_8"}){render::Node node{};node.name=n;m.nodes.push_back(node);}
    for(int i=0;i<60;++i){render::Node n;n.name="FACIAL_L_Joint"+std::to_string(i)+"_"+std::to_string(100+i);m.nodes.push_back(n);}
    render::MeshDef face;face.name="Ada_FaceMesh_LOD0";
    for(std::size_t i=0;i<vivid::kChannelCount-1;++i)face.morphTargetNames.push_back(std::string(vivid::channelName(vivid::Channel(i))));
    render::MeshDef brows;brows.name="Eyebrows_M_Thin_CardsMesh_Group0_LOD0";render::MeshDef body;body.name="f_med_nrw_body_LOD3";
    m.meshes={face,brows,body};
    for(const char* n:{"ada_facemesh_head_shader_shader","ada_facemesh_eyeLeft_shader_shader","ada_facemesh_eyeshell_shader_shader","ada_facemesh_eyelashes_shader_shader",
        "f_med_nrw_body_MI_BodySynthesized","hair_s_coil_cardsmesh_group0_lod0_Cards_M","f_med_nrw_top_shirt_nrm_cinematic_lambert1","ada_facemesh_teeth_shader_shader"}){render::Material x;x.name=n;m.materials.push_back(x);}
    const auto r=vivid::adaptMetaHuman(m);
    check(r.detected&&r.headBone=="head","synthetic MetaHuman not detected");
    check(r.faceMeshes.size()==2,"face + brows expected in the face selection");
    check(r.channelsFound.size()==vivid::kChannelCount-1&&r.channelsMissing.size()==1,"ARKit coverage miscounted");
    check(r.lodLevels==4,"LOD levels from _LOD3");
    check(r.facialJoints==60&&r.correctiveJoints>=1,"skeleton facts");
    check(r.hiddenMaterials.size()==1,"eye shell must be hidden");
    const auto look=vivid::HumanAppearance::fromJson(r.look());
    check(look.materials.at("ada_facemesh_head_shader_shader")=="skin"&&look.materials.at("ada_facemesh_eyeLeft_shader_shader")=="eyes"&&
          look.materials.at("ada_facemesh_eyelashes_shader_shader")=="lashes"&&look.materials.at("hair_s_coil_cardsmesh_group0_lod0_Cards_M")=="hair"&&
          look.materials.at("f_med_nrw_top_shirt_nrm_cinematic_lambert1")=="cloth"&&!look.materials.contains("ada_facemesh_teeth_shader_shader"),"material roles");
    check(vivid::HumanFaceShape::fromJson(r.face()).meshes.size()==2,"face selection JSON");
    {   // Runtime drivers: a "_half" joint beside its base follows half the bend.
        render::Model s;
        for(const char* n:{"root","pelvis","spine_05","head","elbow_parent","lowerarm_l","lowerarm_half_l"}){render::Node x;x.name=n;s.nodes.push_back(x);}
        s.nodes[4].children={5,6};s.nodes[5].parent=4;s.nodes[6].parent=4;
        const auto rig=vivid::buildMetaHumanRig(s);
        check(rig.drivers.size()==1&&rig.drivers[0].base==5&&rig.drivers[0].factor>0,"half driver not built");
        render::Pose pose;render::bindPose(s,pose);
        pose[5].r=render::quat::axisAngle({0,0,1},1.0f);
        vivid::applyMetaHumanRig(rig,s,pose);
        const float angle=2.f*std::acos(std::clamp(std::fabs(pose[6].r.w),0.f,1.f));
        check(std::fabs(angle-.5f)<1e-3f,"half joint must take half the base rotation");
    }
    render::Model plain;render::Node hips{};hips.name="Hips";plain.nodes.push_back(hips);render::MeshDef x;x.name="Body";plain.meshes.push_back(x);
    check(!vivid::adaptMetaHuman(plain).detected,"non-MetaHuman detected");
    if(const char* list=std::getenv("AIVERSE_METAHUMAN_SAMPLES")) {
        std::stringstream ss(list);std::string path;
        while(std::getline(ss,path,';')) {
            if(path.empty())continue;
            const auto dir=path.substr(0,path.find_last_of('/')+1);
            auto model=render::loadModel(path.substr(dir.size()),[&](const std::string& key,std::string& out){std::ifstream in(dir+key,std::ios::binary);if(!in)return false;out.assign(std::istreambuf_iterator<char>(in),{});return true;});
            check(bool(model),"real sample failed to load");if(!model)continue;
            const auto rr=vivid::adaptMetaHuman(*model);
            std::printf("%s: %s\n",path.c_str(),rr.toJson().dump().c_str());
            check(rr.detected&&!rr.faceMeshes.empty()&&rr.channelsFound.size()>=40,"real MetaHuman export not adapted");
            (void)vivid::HumanAppearance::fromJson(rr.look());(void)vivid::HumanFaceShape::fromJson(rr.face());
        }
    }
    if(failures)return 1;
    std::puts("metahuman_adapter ok");
}
