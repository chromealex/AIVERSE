// SpectraFX graph authoring window. Edits the canonical fx/VfxGraph.h schema
// (Spawn / Initialize / Update / SpriteOutput, MeshOutput, RibbonOutput).
// This is not the separate SpectraFxGraph cue-routing DAG.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "engine/fx/SpectraFX.h"
#include "engine/render/TextureCache.h"
#include "engine/fx/FxModelMesh.h"

namespace aiverse::editor {
namespace fs = std::filesystem;

class SpectraFxGraphWindow : public EditorWindow {
public:
    // Distinct from SpectraGraphWindow, which keeps the legacy saved id
    // "spectrafx_graph" (layouts, focusWindow, docs). Duplicate ids made the
    // registry/layout resolve the wrong window.
    const char* id() const override { return "spectrafx_vfx_graph"; }
    const char* title() const override { return "SpectraFX Graph"; }
    const char* group() const override { return "look"; }
    const char* dockArea() const override { return "center"; }
    bool openByDefault() const override { return false; }
    ~SpectraFxGraphWindow() override {
        if (device_) { previewViewport_.release(*device_); preview_.shutdown(); previewTextures_.shutdown(); }
    }

    void update(Editor& ed,double) override {
        if (project_!=ed.ws().project.dir) {
            project_=ed.ws().project.dir;
            current_.clear();
            document_=defaultGraph();
            saved_=document_;
            selected_=1;
            undo_.clear();redo_.clear();
            dirty_=false;layoutDirty_=false;
            previewId_=-1; previewHandle_={}; previewHash_=0;
            if(device_) preview_.clear();
            layout_.clear();pendingLink_.clear();
            refresh();
        }
    }

    void draw(Editor& ed) override {
        if (project_.empty()) {
            ImGui::TextUnformatted("Open an AIVERSE project to edit SpectraFX graphs.");
            return;
        }
        ImGui::BeginChild("##spectrafx_assets",ImVec2(175,0),
                          ImGuiChildFlags_Borders|ImGuiChildFlags_ResizeX);
        if (ImGui::Button("Refresh")) refresh();
        for (const auto& name : files_) {
            if (ImGui::Selectable(name.c_str(),name==current_)) {
                if (dirty_) pending_=name;
                else load(name);
            }
        }
        ImGui::Separator();
        ui::inputString("New name",newName_);
        if (ImGui::Button("Create") && validName(newName_) &&
            std::find(files_.begin(),files_.end(),newName_)==files_.end()) {
            if (dirty_) pending_=newName_, createPending_=true;
            else create(newName_);
        }
        if (!pending_.empty()) {
            ImGui::Separator();
            ImGui::TextWrapped("Discard unsaved graph edits?");
            if (ImGui::Button("Discard edits")) {
                if (createPending_) create(pending_);
                else load(pending_);
                pending_.clear();createPending_=false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                pending_.clear();createPending_=false;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##spectrafx_editor",ImVec2(0,0));
        if (current_.empty()) {
            ImGui::TextUnformatted("Select or create an effect graph.");
            ImGui::EndChild();return;
        }
        ImGui::Text("%s%s",current_.c_str(),(dirty_||layoutDirty_)?" *":"");
        ImGui::SameLine();
        if(ImGui::Button("Restart preview")) {preview_.clear();previewHandle_={};lastPreviewTime_=-1;}
        if(ImGui::Button("Save")) save(ed);
        ImGui::SameLine();
        if(ImGui::Button("Reload") && !dirty_) load(current_);
        ImGui::SameLine();
        ImGui::BeginDisabled(undo_.empty());
        if(ImGui::Button("Undo")) {
            redo_.push_back(document_);
            document_=undo_.back();undo_.pop_back();
            dirty_=true;selected_=-1;pendingLink_.clear();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(redo_.empty());
        if(ImGui::Button("Redo")) {
            undo_.push_back(document_);
            document_=redo_.back();redo_.pop_back();
            dirty_=true;selected_=-1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::Button("Validate")) validate();
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::Separator();
        ImGui::BeginChild("##spectrafx_canvas",ImVec2(0,300),ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        drawCanvas();
        ImGui::EndChild();
        ImGui::BeginChild("##spectrafx_nodes",ImVec2(230,0),ImGuiChildFlags_Borders);
        auto& nodes=document_.find("nodes")->itemsMut();
        for(size_t i=0;i<nodes.size();++i) {
            const auto title=nodes[i].getStr("id")+" : "+nodes[i].getStr("type");
            if(ImGui::Selectable(title.c_str(),selected_==int(i))) {
                selected_=int(i);propertiesText_=nodes[i]["properties"].dump(2);
            }
        }
        ImGui::Separator();
        if(ImGui::BeginCombo("Add node","Choose")) {
            for(auto type : {"Update","SpriteOutput",
                             "MeshOutput","RibbonOutput"}) {
                if(ImGui::Selectable(type)) add(type);
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(selected_<0 || selected_>=int(nodes.size()));
        if(ImGui::Button("Remove selected")) removeSelected();
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##spectrafx_properties",ImVec2(0,0));
        if(selected_>=0 && selected_<int(nodes.size())) {
            // All changes to canonical VFX Graph JSON go through a full
            // compile before they're committed to the editor document.
            const Json& node=nodes[size_t(selected_)];
            ImGui::Text("Node: %s",node.getStr("id").c_str());
            ImGui::Text("Type: %s",node.getStr("type").c_str());
            if(node.getStr("type")!="Spawn") {
                const std::string cur=parentOf(node.getStr("id"));
                if(ImGui::BeginCombo("Input",cur.empty()?"(unconnected)":cur.c_str())) {
                    for(const auto& parent : nodes) {
                        const std::string id=parent.getStr("id"), type=parent.getStr("type");
                        if(id==node.getStr("id") || isOutput(type)) continue;
                        if(ImGui::Selectable(id.c_str(),id==cur)) relink(node.getStr("id"),id);
                    }
                    ImGui::EndCombo();
                }
            }
            ui::inputMultiline("Properties (JSON)",propertiesText_,ImVec2(-1,220));
            if(ImGui::Button("Apply properties")) {
                try {
                    Json properties=Json::parse(propertiesText_);
                    if(!properties.isObject()) message_="Properties must be a JSON object";
                    else {
                        Json next=document_;
                        next.find("nodes")->at(size_t(selected_)).set("properties",std::move(properties));
                        commit(std::move(next));
                    }
                } catch(const std::exception& e) {message_=e.what();}
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Full graph JSON");
        }
        ui::inputMultiline("##raw_spectrafx",raw_,ImVec2(-1,220));
        if(ImGui::Button("Apply full JSON")) {
            try { commit(Json::parse(raw_)); }
            catch(const std::exception& e) {message_=e.what();}
        }
        ImGui::Separator();
        ImGui::TextUnformatted("Live SpectraFX preview (CPU ParticleSystem)");
        drawPreview(ed,300,210);
        ImGui::EndChild();
        ImGui::EndChild();
    }

private:
    void initPreview(Editor& ed) {
        if(device_) return;
        device_=&ed.device();
        preview_.setQuality(fx::FxQuality::High);
        preview_.setFileReader([this](const std::string& name,std::string& out) {
            std::ifstream input(fs::path(project_)/name,std::ios::binary);
            if(!input) return false;
            out.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
            return true;
        });
        previewTextures_.init(*device_);
        preview_.setMeshLoader([this](const std::string& name,fx::FxMesh& out) {
            return fx::meshFromModelFile(name,[this](const std::string& p,std::string& data) {
                std::ifstream input(fs::path(project_)/p,std::ios::binary);
                if(!input) return false;
                data.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
                return true;
            },out);
        });
        preview_.setTextureLoader([this](const std::string& name,fx::FxTexture& out) {
            render::TextureRef texture=previewTextures_.resolve(name,false,project_+"/");
            if(!texture.valid()) return false;
            out={texture.tex,texture.u0,texture.v0,texture.u1,texture.v1};
            return true;
        });
        if(!preview_.init(device_)) message_=preview_.lastError();
    }
    void drawPreview(Editor& ed,float width,float height) {
        initPreview(ed);
        if(!device_) return;
        // A cheap source hash is sufficient for editor-only invalidation.
        const std::string json=document_.dump(-1);
        uint64_t hash=14695981039346656037ull;
        for(unsigned char ch:json) {hash^=ch;hash*=1099511628211ull;}
        if(hash!=previewHash_) {
            previewHash_=hash;
            fx::EffectDef compiled;
            std::vector<std::string> errors;
            if(fx::compileSpectraGraph(document_,compiled,errors)) {
                previewId_=preview_.define("__spectrafx_editor_preview",compiled);
                previewHandle_={};
                lastPreviewTime_=-1;
            } else {
                previewId_=-1;
                preview_.clear();
            }
        }
        const double now=ed.time();
        float dt=lastPreviewTime_<0?0.0f:float(now-lastPreviewTime_);
        lastPreviewTime_=now;
        dt=std::clamp(dt,0.0f,0.05f);
        if(previewId_>=0 && !preview_.playing(previewHandle_))
            previewHandle_=preview_.play(previewId_,{0,0.5f,0});
        const float dpi=ImGui::GetIO().DisplayFramebufferScale.x;
        previewViewport_.ensure(*device_,int(width*dpi),int(height*dpi),
                                ed.viewportRetirement(), "SpectraFX Graph");
        auto& renderer=ed.renderer();
        renderer.beginScene(previewCamera_,previewViewport_.w,previewViewport_.h);
        const uint32_t grid=render::rgba(85,90,100,255);
        for(int i=-5;i<=5;++i) {
            renderer.line({float(i),0,-5},{float(i),0,5},grid);
            renderer.line({-5,0,float(i)},{5,0,float(i)},grid);
        }
        preview_.setView(previewCamera_,renderer.viewProj(),
                         previewViewport_.w,previewViewport_.h);
        preview_.update(dt);
        gfx::PassDesc pass;
        pass.colorTarget=previewViewport_.color;
        pass.depthTarget=previewViewport_.depth;
        pass.clearColor[0]=0.12f;pass.clearColor[1]=0.13f;pass.clearColor[2]=0.15f;
        renderer.render(pass);
        preview_.render({renderer.lighting.fogColor,0});
        device_->endPass();
        previewViewport_.image(*device_,width,height);
        if(ImGui::IsItemHovered()) {
            const ImGuiIO& io=ImGui::GetIO();
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                previewCamera_.yaw-=io.MouseDelta.x*0.01f;
                previewCamera_.pitch=std::clamp(previewCamera_.pitch+io.MouseDelta.y*0.01f,0.05f,1.5f);
            }
            if(io.MouseWheel)
                previewCamera_.distance=std::clamp(previewCamera_.distance*
                    (io.MouseWheel>0?0.9f:1.1f),2.0f,60.0f);
        }
    }
    static bool validName(const std::string& name) {
        if(name.empty() || name.size()>64) return false;
        for(unsigned char c:name)
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                 (c>='0'&&c<='9')||c=='_'||c=='-')) return false;
        return true;
    }
    static bool isOutput(const std::string& s) {
        return s=="SpriteOutput"||s=="MeshOutput"||s=="RibbonOutput";
    }
    static Json node(const std::string& id,const std::string& type,Json props=Json::object()) {
        Json n=Json::object();
        n.set("id",Json::string(id));n.set("type",Json::string(type));
        n.set("properties",std::move(props));return n;
    }
    static Json defaultGraph() {
        Json g=Json::object();
        g.set("version",Json::number(int64_t(1)));
        g.set("parameters",Json::object());
        Json nodes=Json::array();
        Json spawn=Json::object();spawn.set("burst",Json::number(int64_t(16)));
        Json init=Json::object();
        init.set("life",Json::number(1.0));
        init.set("size",Json::number(0.4));
        Json output=Json::object();output.set("texture",Json::string("builtin:soft"));
        nodes.push(node("spawn","Spawn",std::move(spawn)));
        nodes.push(node("init","Initialize",std::move(init)));
        nodes.push(node("out","SpriteOutput",std::move(output)));
        g.set("nodes",std::move(nodes));
        Json links=Json::array();
        for(auto pair: {std::pair{"spawn","init"},std::pair{"init","out"}}) {
            Json link=Json::object();
            link.set("from",Json::string(pair.first));
            link.set("to",Json::string(pair.second));
            links.push(std::move(link));
        }
        g.set("links",std::move(links));
        Json outputs=Json::array();outputs.push(Json::string("out"));
        g.set("outputs",std::move(outputs));
        return g;
    }
    ImVec2 canvasNodePosition(const std::string& id,size_t index) {
        const auto it=layout_.find(id);
        if(it!=layout_.end()) return it->second;
        const float x=20.0f+float(index % 4)*205.0f;
        const float y=22.0f+float(index / 4)*118.0f;
        return layout_[id]=ImVec2(x,y);
    }
    void drawCanvas() {
        const ImVec2 origin=ImGui::GetCursorScreenPos();
        const ImVec2 available=ImGui::GetContentRegionAvail();
        auto* dl=ImGui::GetWindowDrawList();
        const auto& nodes=document_["nodes"].items();
        ImGui::InvisibleButton("##spectrafx_background",available);
        if(ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            pan_.x+=ImGui::GetIO().MouseDelta.x;
            pan_.y+=ImGui::GetIO().MouseDelta.y;
            layoutDirty_=true;
        }
        const ImU32 gridColor=ImGui::GetColorU32(ImGuiCol_Border,0.25f);
        for(float x=std::fmod(pan_.x,32.0f);x<available.x;x+=32.0f)
            dl->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+available.y},gridColor);
        for(float y=std::fmod(pan_.y,32.0f);y<available.y;y+=32.0f)
            dl->AddLine({origin.x,origin.y+y},{origin.x+available.x,origin.y+y},gridColor);
        const auto position=[&](const std::string& id)->ImVec2 {
            for(size_t i=0;i<nodes.size();++i)
                if(nodes[i].getStr("id")==id) {
                    const ImVec2 p=canvasNodePosition(id,i);
                    return {origin.x+pan_.x+p.x,origin.y+pan_.y+p.y};
                }
            return origin;
        };
        const ImU32 wireColor=ImGui::GetColorU32(ImGuiCol_PlotLines);
        for(const auto& link:document_["links"].items()) {
            const ImVec2 a=position(link.getStr("from"));
            const ImVec2 b=position(link.getStr("to"));
            const ImVec2 start{a.x+168.0f,a.y+42.0f},end{b.x,b.y+42.0f};
            dl->AddBezierCubic(start,{start.x+55,start.y},
                               {end.x-55,end.y},end,wireColor,2.0f);
        }
        std::string linkFrom, linkTo;
        // Node titles drag only editor layout, not the canonical particle
        // graph. Port clicks invoke the existing compiler-backed relink().
        for(size_t i=0;i<nodes.size();++i) {
            const std::string id=nodes[i].getStr("id");
            const std::string type=nodes[i].getStr("type");
            const ImVec2 p=position(id);
            const ImVec2 end{p.x+168,p.y+70};
            dl->AddRectFilled(p,end,ImGui::GetColorU32(ImGuiCol_FrameBg),6.0f);
            dl->AddRect(p,end,ImGui::GetColorU32(selected_==int(i)?
                     ImGuiCol_ButtonActive:ImGuiCol_Border),6.0f);
            dl->AddText({p.x+9,p.y+9},ImGui::GetColorU32(ImGuiCol_Text),id.c_str());
            dl->AddText({p.x+9,p.y+31},ImGui::GetColorU32(ImGuiCol_TextDisabled),type.c_str());
            ImGui::PushID(int(i));
            ImGui::SetCursorScreenPos(p);
            ImGui::InvisibleButton("drag",ImVec2(146,24));
            if(ImGui::IsItemClicked()) {
                selected_=int(i);
                propertiesText_=nodes[i]["properties"].dump(2);
            }
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2& np=layout_[id];
                np.x+=ImGui::GetIO().MouseDelta.x;
                np.y+=ImGui::GetIO().MouseDelta.y;
                layoutDirty_=true;
            }
            if(type!="Spawn") {
                const ImVec2 input{p.x,p.y+42};
                dl->AddCircleFilled(input,5,wireColor);
                ImGui::SetCursorScreenPos({input.x-8,input.y-8});
                ImGui::InvisibleButton("input",ImVec2(16,16));
                if(ImGui::IsItemClicked() && !pendingLink_.empty()) {
                    const std::string from=pendingLink_;
                    pendingLink_.clear();
                    if(from!=id) { linkFrom=from; linkTo=id; }
                }
            }
            if(!isOutput(type)) {
                const ImVec2 output{p.x+168,p.y+42};
                dl->AddCircleFilled(output,5,wireColor);
                ImGui::SetCursorScreenPos({output.x-8,output.y-8});
                ImGui::InvisibleButton("output",ImVec2(16,16));
                if(ImGui::IsItemClicked()) pendingLink_=id;
            }
            ImGui::PopID();
        }
        if(!linkFrom.empty()) relink(linkTo,linkFrom);
        if(!pendingLink_.empty()) {
            ImGui::SetCursorScreenPos({origin.x+8,origin.y+available.y-22});
            ImGui::Text("Link from %s: click a destination input (Esc cancels)",pendingLink_.c_str());
            if(ImGui::IsKeyPressed(ImGuiKey_Escape)) pendingLink_.clear();
        }
    }
    fs::path dir() const {return fs::path(project_)/"fx"/"graphs";}
    fs::path layoutPath(const std::string& name) const {
        return dir()/(name+".layout.json");
    }
    void loadLayout(const std::string& name) {
        layout_.clear();pan_={};layoutDirty_=false;
        std::ifstream file(layoutPath(name),std::ios::binary);
        if(!file) return;
        try {
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            Json root=Json::parse(content);
            if(!root.isObject() || root.getInt("version")!=1 ||
               !root["nodes"].isObject()) return;
            for(const auto& e:root["nodes"].members()) {
                if(!e.second.isArray() || e.second.size()!=2) continue;
                const float x=float(e.second[0].toDouble());
                const float y=float(e.second[1].toDouble());
                if(!std::isfinite(x)||!std::isfinite(y)||
                   std::abs(x)>100000||std::abs(y)>100000) continue;
                layout_[e.first]={x,y};
            }
            const Json& pan=root["pan"];
            if(pan.isArray() && pan.size()==2) {
                const float x=float(pan[0].toDouble());
                const float y=float(pan[1].toDouble());
                if(std::isfinite(x)&&std::isfinite(y)&&
                   std::abs(x)<100000&&std::abs(y)<100000) pan_={x,y};
            }
        } catch(const std::exception&) {
            // Corrupt editor-only metadata must never invalidate gameplay FX.
            layout_.clear();pan_={};
        }
    }
    bool saveLayout() {
        Json root=Json::object();
        root.set("version",Json::number(int64_t(1)));
        Json nodes=Json::object();
        for(const auto& pair:layout_) {
            Json xy=Json::array();
            xy.push(Json::number(double(pair.second.x),3));
            xy.push(Json::number(double(pair.second.y),3));
            nodes.set(pair.first,std::move(xy));
        }
        root.set("nodes",std::move(nodes));
        Json pan=Json::array();
        pan.push(Json::number(double(pan_.x),3));
        pan.push(Json::number(double(pan_.y),3));
        root.set("pan",std::move(pan));
        const fs::path dest=layoutPath(current_);
        const fs::path temp=dest.string()+".tmp";
        {
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);
            out<<root.dump(2)<<"\n";
            out.flush();
            if(!out) {message_="Unable to save SpectraFX canvas layout";return false;}
        }
        std::error_code ec;
        fs::rename(temp,dest,ec);
        if(ec) {fs::remove(temp);message_=ec.message();return false;}
        layoutDirty_=false;
        return true;
    }
    void refresh() {
        files_.clear();
        std::error_code ec;
        for(fs::directory_iterator it(dir(),ec),end;!ec&&it!=end;it.increment(ec)) {
            if(!it->is_regular_file(ec)) continue;
            const auto file=it->path().filename().string();
            if(file.size()>9 && file.substr(file.size()-9)==".vfx.json")
                files_.push_back(file.substr(0,file.size()-9));
        }
        std::sort(files_.begin(),files_.end());
    }
    bool compile(const Json& doc,std::string& error) const {
        fx::EffectDef effect;
        std::vector<std::string> errors;
        if(fx::compileSpectraGraph(doc,effect,errors)) { error.clear();return true; }
        error=errors.empty()?"Invalid SpectraFX graph":errors.front();
        return false;
    }
    void validate() {
        if(compile(document_,message_)) message_="Graph valid";
    }
    bool commit(Json next) {
        std::string error;
        if(!compile(next,error)) {message_=error;return false;}
        if(undo_.size()>=64) undo_.erase(undo_.begin());
        undo_.push_back(document_);redo_.clear();
        document_=std::move(next);raw_=document_.dump(2);dirty_=true;
        if(selected_>=0 && selected_<int(document_["nodes"].size()))
            propertiesText_=document_["nodes"][size_t(selected_)]["properties"].dump(2);
        message_.clear();return true;
    }
    void create(const std::string& name) {
        current_=name;document_=defaultGraph();saved_=document_;
        layout_.clear();pan_={};pendingLink_.clear();layoutDirty_=true;
        raw_=document_.dump(2);dirty_=true;selected_=1;
        propertiesText_=document_["nodes"][1]["properties"].dump(2);
        undo_.clear();redo_.clear();message_.clear();
    }
    void load(const std::string& name) {
        std::ifstream stream(dir()/(name+".vfx.json"),std::ios::binary);
        if(!stream) {message_="Unable to open graph";return;}
        std::string raw((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
        try {
            Json doc=Json::parse(raw);
            std::string error;
            if(!compile(doc,error)) {message_=error;return;}
            current_=name;document_=saved_=std::move(doc);
            pendingLink_.clear();loadLayout(name);
            raw_=document_.dump(2);dirty_=false;selected_=0;
            propertiesText_=document_["nodes"][0]["properties"].dump(2);
            undo_.clear();redo_.clear();message_.clear();
        } catch(const std::exception& e) {message_=e.what();}
    }
    void save(Editor& ed) {
        std::string error;
        if(!compile(document_,error)) {message_=error;return;}
        std::error_code ec;fs::create_directories(dir(),ec);
        if(ec) {message_=ec.message();return;}
        const fs::path target=dir()/(current_+".vfx.json");
        const fs::path temp=target.string()+".tmp";
        {
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);
            out<<document_.dump(2)<<"\n";out.flush();
            if(!out) {message_="Unable to write graph";return;}
        }
        fs::rename(temp,target,ec);
        if(ec) {fs::remove(temp);message_=ec.message();return;}
        if(!saveLayout()) return;
        saved_=document_;dirty_=false;refresh();ed.reloadFx();
        message_="Saved "+target.string();
    }
    std::string parentOf(const std::string& id) const {
        for(const auto& l:document_["links"].items())
            if(l.getStr("to")==id) return l.getStr("from");
        return {};
    }
    void relink(const std::string& to,const std::string& from) {
        Json next=document_;
        Json& links=*next.find("links");
        for(size_t i=links.size();i-- >0;)
            if(links[i].getStr("to")==to) links.eraseAt(i);
        Json l=Json::object();l.set("from",Json::string(from));
        l.set("to",Json::string(to));links.push(std::move(l));
        commit(std::move(next));
    }
    void add(const std::string& type) {
        // Extend one valid emitter path. A new Update goes directly before
        // an existing Output, preserving a valid Spawn -> Initialize -> Update
        // -> Output route. A new Output branches from an existing Initialize
        // or Update. The compiler validates the complete draft transactionally.
        Json next=document_;
        Json& nodes=*next.find("nodes");
        if(nodes.size()>=256) {message_="Node limit reached";return;}
        std::set<std::string> ids;
        for(const auto& n:nodes.items()) ids.insert(n.getStr("id"));
        std::string id;
        for(int i=1;i<10000;++i) {
            id="node"+std::to_string(i);
            if(!ids.count(id)) break;
        }
        std::string source;
        if(type=="Update") {
            // Find a currently connected output.
            for(const auto& n:nodes.items())
                if(isOutput(n.getStr("type"))) {
                    source=parentOf(n.getStr("id"));
                    if(!source.empty()) break;
                }
        } else {
            for(const auto& n:nodes.items()) {
                const auto kind=n.getStr("type");
                if(kind=="Update" || kind=="Initialize")
                    source=n.getStr("id");
            }
        }
        if(source.empty()) {message_="No compatible source node";return;}
        Json properties=Json::object();
        if(type=="MeshOutput") {
            Json meshes=Json::array();
            meshes.push(Json::string("builtin:cube"));
            properties.set("meshes",std::move(meshes));
        }
        if(isOutput(type)) properties.set("texture",Json::string("builtin:soft"));
        if(type=="Update") {
            // Insert in the path of an output that already consumes source.
            std::string to;
            for(const auto& n:nodes.items())
                if(isOutput(n.getStr("type")) && parentOf(n.getStr("id"))==source) {
                    to=n.getStr("id");break;
                }
            if(to.empty()) {message_="No output to extend";return;}
            Json& links=*next.find("links");
            for(size_t i=0;i<links.size();++i)
                if(links[i].getStr("from")==source && links[i].getStr("to")==to) {
                    links.at(i).set("from",Json::string(id));break;
                }
        } else next.find("outputs")->push(Json::string(id));
        next.find("nodes")->push(node(id,type,std::move(properties)));
        Json link=Json::object();
        link.set("from",Json::string(source));
        link.set("to",Json::string(id));
        next.find("links")->push(std::move(link));
        if(commit(std::move(next)))
            selected_=int(document_["nodes"].size())-1;
    }
    void removeSelected() {
        if(selected_<0 || selected_>=int(document_["nodes"].size())) return;
        const std::string id=document_["nodes"][size_t(selected_)].getStr("id");
        Json next=document_;
        next.find("nodes")->eraseAt(size_t(selected_));
        for(auto key:{"links","outputs"}) {
            auto* arr=next.find(key);
            for(size_t i=arr->size();i-- >0;) {
                if(std::string(key)=="links"?
                   (arr->at(i).getStr("from")==id||arr->at(i).getStr("to")==id):
                   arr->at(i).str()==id) arr->eraseAt(i);
            }
        }
        if(commit(std::move(next))) selected_=-1;
    }
    std::string project_,current_,pending_,newName_,propertiesText_,raw_,message_;
    std::vector<std::string> files_;
    Json document_=defaultGraph(),saved_;
    std::vector<Json> undo_,redo_;
    int selected_=-1;
    bool dirty_=false,createPending_=false,layoutDirty_=false;
    gfx::Device* device_=nullptr;
    fx::ParticleSystem preview_;
    render::TextureCache previewTextures_;
    Viewport previewViewport_;
    render::RTSCamera previewCamera_{{0,0.8f,0},9.0f,0.45f,0.7f};
    fx::FxHandle previewHandle_;
    int previewId_=-1;
    uint64_t previewHash_=0;
    double lastPreviewTime_=-1;
    std::unordered_map<std::string,ImVec2> layout_;
    ImVec2 pan_{};
    std::string pendingLink_;
};

AIVERSE_EDITOR_WINDOW(SpectraFxGraphWindow);
} // namespace aiverse::editor
