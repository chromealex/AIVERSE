#include "Editor.h"
#include "EditorGenreUi.h"
#include "modules/fps/FpsModule.h"
#include <filesystem>
namespace aiverse::editor {
namespace {
std::string quote(const std::string& s) {
#ifdef _WIN32
    // cmd.exe expands these even within quotes; refuse paths/addresses containing them.
    if(s.find_first_of("\"%\r\n!")!=std::string::npos)throw std::runtime_error("unsupported shell characters in FPS launch path");
    return "\""+s+"\"";
#else
    std::string result="'";for(char c:s)result+=c=='\''?"'\\''":std::string(1,c);return result+"'";
#endif
}
render::float3 point(const Json& j){return {float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};}
}
void Editor::startFpsPlay(bool listen,const std::string& address) {
    if(!ws_.project.isFps()||ws_.building())return;
    try {
        auto cfg=fps::readConfig(ws_.project.rulesJson);Simulation check({1,cfg.tickRate,0},std::make_unique<fps::Module>());check.setup(ws_.project.rulesJson,doc().toJson());
        std::string error;
        if(doc().dirty&&!saveMap(doc().path))return;
        if(!ws_.project.saveRules(&error)||!ws_.project.saveManifest(&error))throw std::runtime_error(error);
        namespace fs=std::filesystem;
        std::string name="aiverse_fps_client";
#ifdef _WIN32
        name+=".exe";
#endif
        std::vector<fs::path> roots{fs::path(opt_.editorExe).parent_path(),fs::path(ws_.buildTool).parent_path(),fs::path(ws_.engineDir)/"bin"};
        auto parent=fs::path(opt_.editorExe).parent_path();for(int i=0;i<4;++i){roots.push_back(parent);parent=parent.parent_path();}
        fs::path exe;std::error_code ec;
        for(const auto& root:roots)for(const auto& rel:{fs::path(name),fs::path("modules/fps")/name,fs::path("modules/fps/Release")/name})if(exe.empty()&&fs::is_regular_file(root/rel,ec))exe=root/rel;
        if(exe.empty()){status=genreText(ws_,"fps.editor.missing");ws_.log("fps",status,true);return;}
        std::string cmd="cd "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#ifdef _WIN32
        cmd="cd /d "+quote(ws_.engineDir)+" && "+quote(exe.string())+" --project "+quote(ws_.project.dir);
#endif
        if(listen)cmd+=" --listen";else if(!address.empty())cmd+=" --connect "+quote(address);
        ws_.startCommand(cmd,cmd,"fps-play");status=genreText(ws_,"fps.editor.running");
    }catch(const std::exception& e){status=genreText(ws_,"fps.editor.invalid");ws_.log("fps",e.what(),true);}
}
void Editor::renderFpsScene(Viewport& vp,const render::RTSCamera& camera) {
    using namespace render;
    if(!fpsPreviewMesh_.vb.valid()){MeshBuilder mesh;mesh.color(rgba(255,255,255)).box({0,0.5f,0},{1,1,1});fpsPreviewMesh_=mesh.upload(*dev_);}
    r3d_.beginScene(camera,vp.w,vp.h);
    for(const auto& b:doc().data.fps["colliders"].items()) {
        auto lo=point(b["min"]),hi=point(b["max"]);
        float3 base{(lo.x+hi.x)*0.5f,lo.y,(lo.z+hi.z)*0.5f};
        if(length(base-camera.target)>1000&&hi.y>0)continue;
        r3d_.drawMesh(fpsPreviewMesh_,mat4::translate(base)*mat4::scale(hi-lo),{0.35f,0.42f,0.44f,1});
    }
    for(const auto& spawn:doc().data.fps["spawns"].items()) {
        auto p=point(spawn);r3d_.drawMesh(fpsPreviewMesh_,mat4::translate(p)*mat4::scale({0.6f,1.8f,0.6f}),{0.1f,0.65f,0.85f,1});
    }
    gfx::PassDesc pass;pass.colorTarget=vp.color;pass.depthTarget=vp.depth;
    pass.clearColor[0]=0.12f;pass.clearColor[1]=0.16f;pass.clearColor[2]=0.2f;
    r3d_.render(pass);dev_->endPass();
}
}
