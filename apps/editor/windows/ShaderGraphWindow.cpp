#include "../Editor.h"
#include "../EditorUtil.h"
#include "../EditorGenreUi.h"
#include "ShaderGraphPreview.h"
#include "engine/render/graph/ShaderGraph.h"
#include <algorithm>
#include <map>

namespace aiverse::editor {
class ShaderGraphWindow : public EditorWindow {
public:
    const char* id() const override {return "shader_graph";}
    const char* title() const override {return "Shader Graph";}
    const char* group() const override {return "look";}
    const char* dockArea() const override {return "center";}
    bool openByDefault() const override {return false;}
    void update(Editor& ed,double dt) override {
        time_+=dt;dt_=float(std::min(dt,0.05));
        if(!registered_) {
            ed.ws().registerOp({"shader_graph_preview","Open a graph in the node editor. Refuses to replace unsaved edits.",Json::parse(R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"}}})"),[this,&ed](const Json& p){
                if(dirty_)throw std::runtime_error("save or discard the current graph first");
                Json g=p["graph"];if(g.isNull()) {auto r=ed.ws().call("shader_graph_get",p);if(r.has("error"))return r;g=r["graph"];}
                previewReady_=false;render::graph::compile(g);project_=ed.ws().project.dir;doc_=g;name_=p.getStr("name","untitled");saved_=doc_;undo_.clear();redo_.clear();selected_=0;preview_.shutdown();
                ed.focusWindow(id());Json r=Json::object();r.set("ok",Json::fromBool(true));return r;
            },true});
            ed.ws().registerOp({"shader_graph_preview_status","Current graph preview GPU diagnostics (call after a rendered frame).",Json::object(),[this](const Json&) {
                Json r=Json::object();r.set("ready",Json::fromBool(previewReady_));r.set("diagnostic",Json::string(preview_.error));return r;
            },true});registered_=true;
        }
    }
    void draw(Editor& ed) override {
        auto tr=[&](const char* key){return genreText(ed.ws(),key);};
        if(ed.ws().project.dir.empty()){ui::hint(tr("shaderGraph.openProject").c_str());return;}
        if(project_!=ed.ws().project.dir) {
            if(!project_.empty() && dirty_) { // Keep an explicit draft per project while switching.
                drafts_[project_]={name_,doc_};
            }
            project_=ed.ws().project.dir;preview_.shutdown();undo_.clear();redo_.clear();
            if(auto it=drafts_.find(project_);it!=drafts_.end()){name_=it->second.first;doc_=it->second.second;dirty_=true;}
            else {doc_=render::graph::makeDefault("material");name_="untitled";dirty_=false;}
            saved_=doc_;selected_=0;
        }
        ImGui::SetNextItemWidth(180);ui::inputString("##name",name_);ImGui::SameLine();
        if(ImGui::Button(tr("shaderGraph.save").c_str()))save(ed);
        ImGui::SameLine();ImGui::BeginDisabled(dirty_);
        if(ImGui::Button(tr("shaderGraph.load").c_str())) {Json p=Json::object();p.set("name",Json::string(name_));auto r=ed.ws().call("shader_graph_get",p);if(check(r)){doc_=saved_=r["graph"];undo_.clear();redo_.clear();selected_=0;preview_.shutdown();}}
        ImGui::SameLine();if(ImGui::Button(tr("shaderGraph.new").c_str())){doc_=render::graph::makeDefault(doc_.getStr("domain"));saved_=doc_;undo_.clear();redo_.clear();selected_=0;}
        ImGui::EndDisabled();ImGui::SameLine();
        if(ImGui::Button(tr("shaderGraph.undo").c_str())&&!undo_.empty()){redo_.push_back(doc_);doc_=undo_.back();undo_.pop_back();dirty_=true;}
        ImGui::SameLine();if(ImGui::Button(tr("shaderGraph.redo").c_str())&&!redo_.empty()){undo_.push_back(doc_);doc_=redo_.back();redo_.pop_back();dirty_=true;}
        if(dirty_){ImGui::SameLine();ImGui::TextUnformatted("*");ImGui::SameLine();if(ImGui::Button(tr("shaderGraph.discard").c_str())){doc_=saved_;dirty_=false;undo_.clear();redo_.clear();drafts_.erase(project_);}}
        const char* domains[]={"material","postfx","particle"};int domain=doc_.getStr("domain")=="postfx"?1:doc_.getStr("domain")=="particle"?2:0;
        ImGui::SetNextItemWidth(150);if(ImGui::Combo(tr("shaderGraph.domain").c_str(),&domain,domains,3)){remember();doc_.set("domain",Json::string(domains[domain]));if(domain)for(auto k:{"roughness","metallic","emission"})doc_.find("outputs")->erase(k);preview_.shutdown();}
        ImGui::SameLine();ImGui::SetNextItemWidth(170);if(ImGui::BeginCombo(tr("shaderGraph.add").c_str(),"+")) {
            for(const auto& op:render::graph::catalog())if(ImGui::Selectable(op.id))add(op);
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("%s",tr("shaderGraph.help").c_str());
        const float side=290;
        ImGui::BeginChild("graph_canvas",ImVec2(std::max(200.f,ImGui::GetContentRegionAvail().x-side-12),0),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        canvas();ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("graph_properties",ImVec2(0,0));
        properties(ed);
        previewReady_=false;bool valid=true;
        try {render::graph::compile(doc_);}catch(const std::exception& e){valid=false;ui::callout(ui::Tone::Error,e.what());}
        if(valid) {
            const auto texture=preview_.render(ed,doc_,time_,dt_);previewReady_=texture.valid();
            if(texture.valid()){bool flip=ed.device().info().renderTargetFlipY;ImGui::Image(ImTextureRef(ImTextureID(texture.id)),ImVec2(192,192),ImVec2(0,flip?0:1),ImVec2(1,flip?1:0));}
            if(!preview_.error.empty())ui::callout(ui::Tone::Error,preview_.error);
        }
        ui::inputHint("##target",tr("shaderGraph.target").c_str(),target_);
        if(domain==2)ImGui::SetNextItemWidth(120);
        if(domain==2)ImGui::InputInt(tr("shaderGraph.emitter").c_str(),&emitter_);
        ImGui::BeginDisabled(!valid);
        if(ImGui::Button(tr("shaderGraph.apply").c_str())&&save(ed)) {
            Json p=Json::object();p.set("name",Json::string(name_));p.set("target",Json::string(target_));p.set("emitter",Json::number(int64_t(emitter_)));
            auto r=ed.ws().call("shader_graph_apply",p);if(check(r))message_=tr("shaderGraph.applied");
        }
        ImGui::EndDisabled();ImGui::TextWrapped("%s",tr("shaderGraph.snapshot").c_str());
        if(!message_.empty())ui::callout(error_?ui::Tone::Error:ui::Tone::Ok,message_);
        ImGui::EndChild();
    }
private:
    void remember(){if(undo_.size()>=64)undo_.erase(undo_.begin());undo_.push_back(doc_);redo_.clear();dirty_=true;}
    bool check(const Json& r){error_=r.has("error");message_=r.getStr("error");return !error_;}
    bool save(Editor& ed){Json p=Json::object();p.set("name",Json::string(name_));p.set("graph",doc_);if(!check(ed.ws().call("shader_graph_set",p)))return false;saved_=doc_;dirty_=false;drafts_.erase(project_);return true;}
    void add(const render::graph::Op& op) {
        if(doc_["nodes"].size()>=128)return;
        remember();Json n=Json::object();
        std::string id;int serial=1;do {id="node"+std::to_string(serial++);}while(std::any_of(doc_["nodes"].items().begin(),doc_["nodes"].items().end(),[&](auto& x){return x.getStr("id")==id;}));
        n.set("id",Json::string(id));n.set("op",Json::string(op.id));Json ins=Json::array();for(int i=0;i<op.inputs;++i)ins.push(doc_["nodes"][0]["id"]);n.set("inputs",ins);
        if(std::string(op.id)=="constant")n.set("value",Json::parse("[0.2,0.6,1,1]"));
        n.set("position",Json::parse("[220,120]"));doc_.find("nodes")->push(n);selected_=int(doc_["nodes"].size())-1;
    }
    void canvas() {
        const ImVec2 origin=ImGui::GetCursorScreenPos();const auto avail=ImGui::GetContentRegionAvail();auto* dl=ImGui::GetWindowDrawList();
        if(ImGui::IsWindowHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){pan_.x+=ImGui::GetIO().MouseDelta.x;pan_.y+=ImGui::GetIO().MouseDelta.y;}
        for(float x=std::fmod(pan_.x,24.f);x<avail.x;x+=24)dl->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+avail.y},ImGui::GetColorU32(ImGuiCol_Border,0.3f));
        auto pos=[&](const Json& n){return ImVec2(origin.x+pan_.x+float(n["position"][0].toDouble()),origin.y+pan_.y+float(n["position"][1].toDouble()));};
        for(const auto& n:doc_["nodes"].items())for(size_t i=0;i<n["inputs"].size();++i)for(const auto& src:doc_["nodes"].items())if(src.getStr("id")==n["inputs"][i].str()) {
            auto a=pos(src),b=pos(n);a.x+=160;a.y+=16;b.y+=42+float(i)*22;
            dl->AddBezierCubic(a,{a.x+60,a.y},{b.x-60,b.y},b,ImGui::GetColorU32(ImGuiCol_PlotLines),2.f);
        }
        for(size_t index=0;index<doc_["nodes"].size();++index) {
            auto& n=doc_.find("nodes")->at(index);const auto p=pos(n);const float height=std::max(60.f,48.f+float(n["inputs"].size())*22.f);
            dl->AddRectFilled(p,{p.x+160,p.y+height},ImGui::GetColorU32(ImGuiCol_FrameBg),6);
            dl->AddRect(p,{p.x+160,p.y+height},ImGui::GetColorU32(int(index)==selected_?ImGuiCol_ButtonActive:ImGuiCol_Border),6);
            dl->AddText({p.x+8,p.y+8},ImGui::GetColorU32(ImGuiCol_Text),n.getStr("op").c_str());
            ImGui::PushID(int(index));ImGui::SetCursorScreenPos(p);ImGui::InvisibleButton("title",{146,30});
            if(ImGui::IsItemClicked())selected_=int(index);
            if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){if(!draggingNode_){remember();draggingNode_=true;}auto position=n["position"];if(position.size()!=2)position=Json::parse("[0,0]");position.at(0)=Json::number(position[0].toDouble()+ImGui::GetIO().MouseDelta.x);position.at(1)=Json::number(position[1].toDouble()+ImGui::GetIO().MouseDelta.y);n.set("position",position);}
            ImGui::SetCursorScreenPos({p.x+150,p.y+6});ImGui::InvisibleButton("out",{20,20});dl->AddCircleFilled({p.x+160,p.y+16},5,ImGui::GetColorU32(ImGuiCol_PlotLines));if(ImGui::IsItemClicked())connecting_=n.getStr("id");
            for(size_t k=0;k<n["inputs"].size();++k){ImGui::PushID(int(k));ImVec2 pin{p.x,p.y+42+float(k)*22};ImGui::SetCursorScreenPos({pin.x-8,pin.y-8});ImGui::InvisibleButton("in",{18,18});dl->AddCircleFilled(pin,5,ImGui::GetColorU32(ImGuiCol_TextDisabled));dl->AddText({pin.x+12,pin.y-7},ImGui::GetColorU32(ImGuiCol_Text),std::to_string(k+1).c_str());if(ImGui::IsItemClicked()&&!connecting_.empty()){remember();n.find("inputs")->at(k)=Json::string(connecting_);connecting_.clear();}ImGui::PopID();}
            ImGui::PopID();
        }
        if(!ImGui::IsMouseDown(0))draggingNode_=false;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape))connecting_.clear();
        ImGui::SetCursorScreenPos(origin);ImGui::Dummy(avail);
    }
    void properties(Editor& ed) {
        auto tr=[&](const char* key){return genreText(ed.ws(),key);};
        selected_=std::clamp(selected_,0,int(doc_["nodes"].size())-1);
        if(selected_>=0){auto& n=doc_.find("nodes")->at(size_t(selected_));ImGui::TextUnformatted(n.getStr("id").c_str());
            if(n.getStr("op")=="constant") {float c[4];for(int i=0;i<4;++i)c[i]=float(n["value"].isNumber()?n["value"].toDouble():n["value"][size_t(i)].toDouble());if(ImGui::DragFloat4(tr("shaderGraph.value").c_str(),c,.01f,-1000000,1000000)){remember();Json a=Json::array();for(float x:c)a.push(Json::number(double(x),6));n.set("value",a);}}
            for(size_t i=0;i<n["inputs"].size();++i){ImGui::PushID(int(i));if(ImGui::BeginCombo(tr("shaderGraph.input").c_str(),n["inputs"][i].str().c_str())){for(auto& src:doc_["nodes"].items())if(ImGui::Selectable(src.getStr("id").c_str())){remember();n.find("inputs")->at(i)=src["id"];}ImGui::EndCombo();}ImGui::PopID();}
            ImGui::BeginDisabled(doc_["nodes"].size()<=1);if(ImGui::Button(tr("shaderGraph.remove").c_str())){remember();doc_.find("nodes")->eraseAt(size_t(selected_));selected_=0;}ImGui::EndDisabled();
        }
        for(auto key:{"color","roughness","metallic","emission"}) {
            if(doc_.getStr("domain")!="material"&&std::string(key)!="color")continue;
            if(ImGui::BeginCombo(key,doc_["outputs"][key].str("-").c_str())) {
                if(std::string(key)!="color"&&ImGui::Selectable("-")){remember();doc_.find("outputs")->erase(key);}
                for(auto& n:doc_["nodes"].items())if(ImGui::Selectable(n.getStr("id").c_str())){remember();doc_.find("outputs")->set(key,n["id"]);}
                ImGui::EndCombo();
            }
        }
    }
    Json doc_=render::graph::makeDefault("material"),saved_=doc_;std::vector<Json> undo_,redo_;
    std::map<std::string,std::pair<std::string,Json>> drafts_;
    std::string project_,name_="untitled",target_,connecting_,message_;int selected_=0,emitter_=0;
    bool dirty_=false,error_=false,registered_=false,previewReady_=false,draggingNode_=false;ImVec2 pan_{20,20};double time_=0;float dt_=0;ShaderGraphPreview preview_;
};
AIVERSE_EDITOR_WINDOW(ShaderGraphWindow);
}
