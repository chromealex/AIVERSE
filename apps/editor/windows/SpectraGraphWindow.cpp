// AIVERSE SpectraFX v1 node canvas. GUI-only authoring: never owns gameplay state.
// The graph-v1 compiler remains authoritative for validation and CPU effects.
// Native/editor MCP operations remain in apps/editor/core/SpectraGraphOps.cpp.
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/FxOps.h"
#include "engine/fx/SpectraFX.h"
#include "engine/fx/SpectraModuleSchema.h"
#include <string_view>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {
namespace {
Json defaultGraph() {
    return Json::parse(R"JSON({
      "version":1,
      "parameters":{},
      "nodes":[
        {"id":"spawn","type":"Spawn","properties":{"burst":16},"position":[20,110]},
        {"id":"initialize","type":"Initialize","properties":{"life":1,"speed":1,"size":0.4},"position":[255,110]},
        {"id":"output","type":"SpriteOutput","properties":{"texture":"builtin:soft"},"position":[490,110]}
      ],
      "links":[{"from":"spawn","to":"initialize"},{"from":"initialize","to":"output"}],
      "outputs":["output"]
    })JSON");
}
Json pairJson(ImVec2 pos) {
    Json a=Json::array();
    a.push(Json::number(double(std::clamp(pos.x,-100000.f,100000.f)),2));
    a.push(Json::number(double(std::clamp(pos.y,-100000.f,100000.f)),2));
    return a;
}
bool validId(const std::string& id) {
    if(id.empty()||id.size()>64)return false;
    for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    return true;
}
bool isOutput(const std::string& type) {
    return type=="SpriteOutput"||type=="MeshOutput"||type=="RibbonOutput";
}
bool existsNode(const Json& graph,const std::string& id) {
    for(const auto& node:graph["nodes"].items())if(node.getStr("id")==id)return true;
    return false;
}
std::string nodeType(const Json& graph,const std::string& id) {
    for(const auto& node:graph["nodes"].items())if(node.getStr("id")==id)return node.getStr("type");
    return {};
}
}

class SpectraGraphWindow final : public EditorWindow {
public:
    const char* id() const override {return "spectrafx_graph";}
    const char* title() const override {return "SpectraFX";}
    const char* group() const override {return "look";}
    const char* dockArea() const override {return "center";}
    bool openByDefault() const override {return false;}
    int order() const override {return 6;}

    void update(Editor& ed,double) override {
        const std::string project=ed.ws().project.dir;
        if(project!=project_) {
            if(dirty_ && !project_.empty())drafts_[project_]={name_,doc_};
            project_=project;revision_=-1;names_.clear();undo_.clear();redo_.clear();
            if(auto it=drafts_.find(project);it!=drafts_.end()) {
                name_=it->second.first;doc_=it->second.second;dirty_=true;
            } else {
                name_="new_spectra";doc_=defaultGraph();dirty_=false;
            }
            selected_=-1;pendingFrom_.clear();refreshDiagnostics();refreshNames(ed);
        } else if(!dirty_ && revision_!=ed.fxRevision)refreshNames(ed);
    }

    void draw(Editor& ed) override {
        if(ed.ws().project.dir.empty()) {
            ImGui::TextWrapped("Откройте проект AIVERSE для редактирования SpectraFX.");
            return;
        }
        toolbar(ed);
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        if(!errors_.empty())for(const std::string& error:errors_)
            ui::toneText(ui::Tone::Error,"%s",error.c_str());

        const float listWidth=165.f;
        const float detailsWidth=310.f;
        ImGui::BeginChild("##spectra_assets",ImVec2(listWidth,0),ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Графы проекта");
        for(const std::string& name:names_) {
            if(ImGui::Selectable(name.c_str(),name==name_)) {
                if(dirty_) message_="Сначала сохраните или отмените изменения.";
                else load(ed,name);
            }
        }
        ImGui::EndChild(); ImGui::SameLine();
        const float avail=ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild("##spectra_canvas",ImVec2(std::max(220.f,avail-detailsWidth-10.f),0),
                          ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        canvas();
        ImGui::EndChild(); ImGui::SameLine();
        ImGui::BeginChild("##spectra_details",ImVec2(0,0),ImGuiChildFlags_Borders);
        properties(ed);
        ImGui::EndChild();
    }
private:
    void refreshDiagnostics() {
        fx::EffectDef result;
        valid_=spectra::compileGraph(doc_,result,errors_);
        emitters_=valid_?int(result.emitters.size()):0;
    }
    void remember() {
        if(undo_.size()==64)undo_.erase(undo_.begin());
        undo_.push_back(doc_);
        redo_.clear();dirty_=true;
    }
    bool checkResult(const Json& result) {
        message_=result.getStr("error");
        if(result.has("ok")&&!result.getBool("ok")) {
            if(message_.empty())message_=result["errors"].dump(2);
            return false;
        }
        return !result.has("error");
    }
    void refreshNames(Editor& ed) {
        auto r=ed.ws().call("fx_graph_list",Json::object());
        if(!checkResult(r))return;
        names_.clear();
        for(const auto& n:r["graphs"].items())names_.push_back(n.getStr("name"));
        revision_=ed.fxRevision;
    }
    void load(Editor& ed,const std::string& name) {
        Json p=Json::object();p.set("name",Json::string(name));
        const auto result=ed.ws().call("fx_graph_get",p);
        if(!checkResult(result))return;
        if(!result["graph"].isObject()) {message_="Граф не распознан.";return;}
        name_=name;doc_=result["graph"];dirty_=false;undo_.clear();redo_.clear();
        selected_=-1;pendingFrom_.clear();propertyText_.clear();refreshDiagnostics();
    }
    void save(Editor& ed) {
        if(name_.size()>48||!validFxName(name_)||name_=="fx") {
            message_="Имя: только a-z, 0-9, _ (1..48).";return;
        }
        refreshDiagnostics();
        if(!valid_) {message_="Исправьте ошибки графа перед сохранением.";return;}
        Json p=Json::object();p.set("name",Json::string(name_));p.set("graph",doc_);
        const auto result=ed.ws().call("fx_graph_set",p);
        if(!checkResult(result))return;
        dirty_=false;drafts_.erase(project_);refreshNames(ed);
        message_="Сохранено: fx/graphs/"+name_+".vfx.json";
    }
    void toolbar(Editor& ed) {
        ImGui::SetNextItemWidth(175);
        ui::inputString("Имя##spectra_name",name_);
        ImGui::SameLine();
        if(ImGui::Button("Новый")) {
            if(dirty_) {message_="Сохраните текущие изменения перед созданием нового графа.";return;}
            doc_=defaultGraph();name_="new_spectra";selected_=-1;pendingFrom_.clear();
            undo_.clear();redo_.clear();dirty_=true;message_.clear();refreshDiagnostics();
        }
        ImGui::SameLine();
        if(ImGui::Button("Сохранить"))save(ed);
        ImGui::SameLine();
        if(ImGui::Button("Проверить")) {refreshDiagnostics();message_=valid_?"Компиляция CPU: OK":"Ошибки компиляции";}
        ImGui::SameLine();
        ImGui::BeginDisabled(undo_.empty());
        if(ImGui::Button("Отменить")) {
            redo_.push_back(doc_);doc_=undo_.back();undo_.pop_back();dirty_=true;refreshDiagnostics();selectNode(selected_);
        }
        ImGui::EndDisabled();ImGui::SameLine();
        ImGui::BeginDisabled(redo_.empty());
        if(ImGui::Button("Вернуть")) {
            undo_.push_back(doc_);doc_=redo_.back();redo_.pop_back();dirty_=true;refreshDiagnostics();selectNode(selected_);
        }
        ImGui::EndDisabled();ImGui::SameLine();
        ImGui::BeginDisabled(!valid_);
        if(ImGui::Button("Предпросмотр")) {
            Json p=Json::object();p.set("graph",doc_);p.set("name",Json::string(name_));
            auto r=ed.ws().call("fx_graph_preview",p);
            if(checkResult(r)){message_="Предпросмотр в окне Сцена.";ed.focusWindow("scene");}
        }
        ImGui::EndDisabled();
        ImGui::SameLine();ImGui::TextDisabled("%s  (%d)",dirty_?"*":"",emitters_);
    }
    void selectNode(int i) {
        if(i<0||size_t(i)>=doc_["nodes"].size()) {
            selected_=-1;propertyText_.clear();return;
        }
        selected_=i;
        const Json& n=doc_["nodes"][size_t(i)];
        propertyText_=(n["properties"].isObject()?n["properties"]:Json::object()).dump(2);
    }
    void addNode(const char* type) {
        if(doc_["nodes"].size()>=256)return;
        std::string nid;int serial=1;
        do{nid="node"+std::to_string(serial++);}while(existsNode(doc_,nid));
        Json n=Json::object();n.set("id",Json::string(nid));n.set("type",Json::string(type));
        Json p=Json::object();
        if(std::string(type)=="Spawn")p=Json::parse(R"({"burst":12})");
        if(std::string(type)=="Initialize")p=Json::parse(R"({"life":1,"size":0.4,"speed":1})");
        if(std::string(type)=="SpriteOutput")p=Json::parse(R"({"texture":"builtin:soft"})");
        if(std::string(type)=="MeshOutput")p=Json::parse(R"({"meshes":["builtin:cube"]})");
        n.set("properties",p);n.set("position",pairJson({50.f+float(doc_["nodes"].size())*30.f,200.f}));
        remember();doc_.find("nodes")->push(n);
        if(isOutput(type))doc_.find("outputs")->push(Json::string(nid));
        selectNode(int(doc_["nodes"].size())-1);refreshDiagnostics();
    }
    void deleteNode() {
        if(selected_<0)return;
        const std::string id=doc_["nodes"][size_t(selected_)].getStr("id");
        remember();
        doc_.find("nodes")->eraseAt(size_t(selected_));
        Json links=Json::array();
        for(const auto& e:doc_["links"].items())
            if(e.getStr("from")!=id && e.getStr("to")!=id)links.push(e);
        doc_.set("links",links);
        Json outputs=Json::array();
        for(const auto& out:doc_["outputs"].items())if(out.str()!=id)outputs.push(out);
        doc_.set("outputs",outputs);
        pendingFrom_.clear();selectNode(-1);refreshDiagnostics();
    }
    bool wouldCycle(const std::string& from,const std::string& to) const {
        std::string cursor=from;
        for(size_t i=0;i<=doc_["nodes"].size();++i) {
            if(cursor==to)return true;
            std::string parent;
            for(const auto& link:doc_["links"].items())
                if(link.getStr("to")==cursor){parent=link.getStr("from");break;}
            if(parent.empty())return false;
            cursor=parent;
        }
        return true;
    }
    void link(const std::string& from,const std::string& to) {
        if(!existsNode(doc_,from)||!existsNode(doc_,to)||from==to||
           isOutput(nodeType(doc_,from))||nodeType(doc_,to)=="Spawn"||wouldCycle(from,to)) {
            message_="Неверная связь (тип узла или цикл).";return;
        }
        remember();
        Json links=Json::array();
        for(const auto& item:doc_["links"].items())
            if(item.getStr("to")!=to)links.push(item);
        Json entry=Json::object();entry.set("from",Json::string(from));entry.set("to",Json::string(to));
        links.push(entry);doc_.set("links",links);refreshDiagnostics();
    }
    void canvas() {
        const ImVec2 origin=ImGui::GetCursorScreenPos();
        const ImVec2 space=ImGui::GetContentRegionAvail();
        auto* dl=ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("##spectra_background",{std::max(50.f,space.x),std::max(100.f,space.y)});
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            pan_.x+=ImGui::GetIO().MouseDelta.x;pan_.y+=ImGui::GetIO().MouseDelta.y;
        }
        const ImU32 grid=ImGui::GetColorU32(ImGuiCol_Border,0.25f);
        for(float x=std::fmod(pan_.x,32.f);x<space.x;x+=32.f)
            dl->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+space.y},grid);
        for(float y=std::fmod(pan_.y,32.f);y<space.y;y+=32.f)
            dl->AddLine({origin.x,origin.y+y},{origin.x+space.x,origin.y+y},grid);
        const auto nodePos=[&](const Json& node,size_t index) {
            const auto& p=node["position"];
            return ImVec2(origin.x+pan_.x+float(p.isArray()&&p.size()==2?p[size_t(0)].toDouble():30.0+index*210.0),
                          origin.y+pan_.y+float(p.isArray()&&p.size()==2?p[size_t(1)].toDouble():130.0));
        };
        for(const auto& e:doc_["links"].items()) {
            int a=-1,b=-1;
            for(size_t i=0;i<doc_["nodes"].size();++i) {
                const auto& n=doc_["nodes"][i];
                if(n.getStr("id")==e.getStr("from"))a=int(i);
                if(n.getStr("id")==e.getStr("to"))b=int(i);
            }
            if(a<0||b<0)continue;
            ImVec2 p=nodePos(doc_["nodes"][size_t(a)],size_t(a));
            ImVec2 q=nodePos(doc_["nodes"][size_t(b)],size_t(b));
            p.x+=175;p.y+=24;q.y+=24;
            dl->AddBezierCubic(p,{p.x+70,p.y},{q.x-70,q.y},q,ImGui::GetColorU32(ImGuiCol_PlotLines),2.f);
        }
        std::string requestedTo;
        for(size_t i=0;i<doc_["nodes"].size();++i) {
            Json& n=doc_.find("nodes")->at(i);
            const std::string id=n.getStr("id"),type=n.getStr("type");
            const ImVec2 p=nodePos(n,i),size{175,72};
            const ImU32 color=ImGui::GetColorU32(ImGuiCol_FrameBg);
            dl->AddRectFilled(p,{p.x+size.x,p.y+size.y},color,5.f);
            dl->AddRect(p,{p.x+size.x,p.y+size.y},
                        ImGui::GetColorU32(int(i)==selected_?ImGuiCol_ButtonActive:ImGuiCol_Border),5.f);
            dl->AddText({p.x+9,p.y+7},ImGui::GetColorU32(ImGuiCol_Text),type.c_str());
            dl->AddText({p.x+9,p.y+42},ImGui::GetColorU32(ImGuiCol_TextDisabled),id.c_str());
            ImGui::PushID(int(i));
            ImGui::SetCursorScreenPos({p.x+8,p.y+5});
            ImGui::InvisibleButton("node",{137,27});
            if(ImGui::IsItemClicked())selectNode(int(i));
            if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                if(!dragging_) {remember();dragging_=true;}
                ImVec2 v{float(n["position"][size_t(0)].toDouble(30)),float(n["position"][size_t(1)].toDouble(130))};
                v.x+=ImGui::GetIO().MouseDelta.x;v.y+=ImGui::GetIO().MouseDelta.y;
                n.set("position",pairJson(v));
            }
            ImGui::SetCursorScreenPos({p.x+165,p.y+14});
            ImGui::InvisibleButton("output",{18,18});
            dl->AddCircleFilled({p.x+175,p.y+23},5.f,ImGui::GetColorU32(ImGuiCol_PlotLines));
            if(ImGui::IsItemClicked())pendingFrom_=id;
            ImGui::SetCursorScreenPos({p.x-9,p.y+14});
            ImGui::InvisibleButton("input",{18,18});
            dl->AddCircleFilled({p.x,p.y+23},5.f,ImGui::GetColorU32(ImGuiCol_TextDisabled));
            if(ImGui::IsItemClicked()&&!pendingFrom_.empty())requestedTo=id;
            ImGui::PopID();
        }
        if(!requestedTo.empty()) {link(pendingFrom_,requestedTo);pendingFrom_.clear();}
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))dragging_=false;
        if(ImGui::IsKeyPressed(ImGuiKey_Escape))pendingFrom_.clear();
    }
    // Shared typed schema, independent of Editor/MCP. Complex curves and
    // unlisted emitter modules are deliberately preserved in the raw JSON.
    void typedFields() {
        if(selected_<0 || size_t(selected_)>=doc_["nodes"].size()) return;
        const std::string type=doc_["nodes"][size_t(selected_)].getStr("type");
        for(const auto& field:spectra::fieldsForNode(type)) {
            ImGui::PushID(field.key);
            const Json current=doc_["nodes"][size_t(selected_)]["properties"][field.key];
            if(current.isNull()) {
                ImGui::TextDisabled("%s",field.label);
                ImGui::SameLine();
                if(ImGui::SmallButton("+")) {
                    const Json initial=Json::parse(field.initial);
                    std::string error;
                    Json probe=doc_["nodes"][size_t(selected_)];
                    if(spectra::applyTypedField(probe,field.key,initial,error)) {
                        remember();
                        doc_.find("nodes")->at(size_t(selected_))=std::move(probe);
                        propertyText_=doc_["nodes"][size_t(selected_)]["properties"].dump(2);
                        refreshDiagnostics();
                    } else message_=error;
                }
                ImGui::PopID();
                continue;
            }
            // A range may be a curve/two values; a colour may be a gradient.
            // Do NOT silently replace either with a scalar widget value.
            std::string error;
            if(!spectra::validateTypedField(field,current,error)) {
                ImGui::TextWrapped("%s: complex value (edit JSON)",field.label);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",error.c_str());
                ImGui::PopID();
                continue;
            }

            Json updated;
            bool changed=false;
            bool started=false;
            switch(field.kind) {
            case spectra::FieldKind::Number: {
                float value=float(current.toDouble());
                changed=ImGui::DragFloat(field.label,&value,0.025f,
                    float(field.minimum),float(field.maximum),"%.3f",ImGuiSliderFlags_AlwaysClamp);
                started=ImGui::IsItemActivated();
                if(changed)updated=Json::number(double(value),4);
                break;
            }
            case spectra::FieldKind::Integer: {
                int value=int(current.integer());
                changed=ImGui::DragInt(field.label,&value,1,
                    int(field.minimum),int(field.maximum),"%d",ImGuiSliderFlags_AlwaysClamp);
                started=ImGui::IsItemActivated();
                if(changed)updated=Json::number(int64_t(value));
                break;
            }
            case spectra::FieldKind::Boolean: {
                bool value=current.boolean();
                changed=ImGui::Checkbox(field.label,&value);
                started=ImGui::IsItemActivated();
                if(changed)updated=Json::fromBool(value);
                break;
            }
            case spectra::FieldKind::String: {
                std::string value=current.str();
                changed=ui::inputString(field.label,value);
                started=ImGui::IsItemActivated();
                if(changed)updated=Json::string(std::move(value));
                break;
            }
            case spectra::FieldKind::Choice: {
                if(ImGui::BeginCombo(field.label,current.str().c_str())) {
                    const std::string_view choices(field.choices);
                    for(size_t from=0;from<choices.size();) {
                        const size_t to=choices.find('|',from);
                        const std::string option(choices.substr(from,to==std::string_view::npos?to:to-from));
                        if(ImGui::Selectable(option.c_str(),current.str()==option)) {
                            updated=Json::string(option);changed=true;
                        }
                        if(to==std::string_view::npos)break;
                        from=to+1;
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case spectra::FieldKind::Vector3: {
                float value[3]={float(current[size_t(0)].toDouble()),
                                float(current[size_t(1)].toDouble()),
                                float(current[size_t(2)].toDouble())};
                changed=ImGui::DragFloat3(field.label,value,0.04f);
                started=ImGui::IsItemActivated();
                if(changed) {
                    updated=Json::array();
                    for(float v:value) updated.push(Json::number(double(v),4));
                }
                break;
            }
            }
            // One undo checkpoint per active drag/text edit, not one per frame.
            if(started) remember();
            if(changed) {
                Json probe=doc_["nodes"][size_t(selected_)];
                if(spectra::applyTypedField(probe,field.key,updated,error)) {
                    if(!started && !ImGui::IsItemActive())remember();
                    doc_.find("nodes")->at(size_t(selected_))=std::move(probe);
                    propertyText_=doc_["nodes"][size_t(selected_)]["properties"].dump(2);
                    refreshDiagnostics();
                } else message_=error;
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("x")) {
                Json probe=doc_["nodes"][size_t(selected_)];
                if(spectra::removeTypedField(probe,field.key,error)) {
                    remember();
                    doc_.find("nodes")->at(size_t(selected_))=std::move(probe);
                    propertyText_=doc_["nodes"][size_t(selected_)]["properties"].dump(2);
                    refreshDiagnostics();
                } else message_=error;
            }
            ImGui::PopID();
        }
    }

    void properties(Editor& ed) {
        ImGui::TextUnformatted("Узлы");
        if(ImGui::BeginCombo("Добавить узел","Выберите тип")) {
            for(const char* type:{"Spawn","Initialize","Update","SpriteOutput","MeshOutput","RibbonOutput"})
                if(ImGui::Selectable(type))addNode(type);
            ImGui::EndCombo();
        }
        if(!pendingFrom_.empty())ImGui::TextWrapped("Соединение: %s -> выберите вход",pendingFrom_.c_str());
        ImGui::Separator();
        if(selected_>=0 && size_t(selected_)<doc_["nodes"].size()) {
            const Json& n=doc_["nodes"][size_t(selected_)];
            ImGui::Text("%s / %s",n.getStr("type").c_str(),n.getStr("id").c_str());
            if(ImGui::CollapsingHeader("Типизированные модули",ImGuiTreeNodeFlags_DefaultOpen))
                typedFields();
            ImGui::Separator();
            ImGui::TextUnformatted("Расширенные свойства (JSON)");
            ui::inputMultiline("##spectra_props",propertyText_,ImVec2(-1,160));
            if(ImGui::Button("Применить свойства")) {
                try{
                    const Json p=Json::parse(propertyText_);
                    if(!p.isObject())throw std::runtime_error("ожидается JSON объект");
                    remember();doc_.find("nodes")->at(size_t(selected_)).set("properties",p);
                    refreshDiagnostics();message_="Свойства применены.";
                }catch(const std::exception& ex){message_=ex.what();}
            }
            ImGui::SameLine();
            if(ImGui::Button("Удалить узел"))deleteNode();
        } else ImGui::TextDisabled("Выберите узел на холсте");
        ImGui::Separator();
        ImGui::TextUnformatted("Общие параметры графа");
        if(ImGui::Button("Редактировать параметры")) {
            parametersText_=(doc_["parameters"].isObject()?doc_["parameters"]:Json::object()).dump(2);
            ImGui::OpenPopup("##spectra_parameters");
        }
        if(ImGui::BeginPopupModal("##spectra_parameters",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ui::inputMultiline("##spectra_params_text",parametersText_,ImVec2(390,180));
            if(ImGui::Button("Применить")) {
                try {
                    Json p=Json::parse(parametersText_);
                    if(!p.isObject())throw std::runtime_error("parameters must be an object");
                    remember();doc_.set("parameters",std::move(p));refreshDiagnostics();
                    message_="Параметры обновлены.";ImGui::CloseCurrentPopup();
                } catch(const std::exception& ex) {message_=ex.what();}
            }
            ImGui::SameLine();
            if(ImGui::Button("Отмена"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();
        ImGui::Text("CPU emitters: %d",emitters_);
        ImGui::TextWrapped("V1: Spawn -> Initialize -> Update* -> Output. "
                           "Положение узлов не влияет на симуляцию. "
                           "GPU compute и математические dataflow узлы пока не поддерживаются.");
    }

    std::string project_,name_="new_spectra",message_,propertyText_,parametersText_,pendingFrom_;
    std::vector<std::string> names_,errors_;
    Json doc_=defaultGraph();
    std::vector<Json> undo_,redo_;
    std::map<std::string,std::pair<std::string,Json>> drafts_;
    ImVec2 pan_{10,10};
    int selected_=-1,revision_=-1,emitters_=0;
    bool dirty_=false,valid_=false,dragging_=false;
};

AIVERSE_EDITOR_WINDOW(SpectraGraphWindow);
} // namespace aiverse::editor
