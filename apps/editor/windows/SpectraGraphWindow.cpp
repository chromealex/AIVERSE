// AIVERSE SpectraFX v1/v2 visual graph canvas. Presentation-only authoring.
// V2 typed expressions compile through the canonical strict FxEffect backend.
// Native/editor MCP operations remain in apps/editor/core/SpectraGraphOps.cpp.
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/FxOps.h"
#include "engine/fx/SpectraFX.h"
#include "libraries/include/aiverselib/genre/SpectraGpuCaptureMailbox.h"
#include "libraries/include/aiverselib/genre/SpectraGpuRecoveryHost.h"
#include "engine/fx/SpectraModuleSchema.h"
#include "engine/fx/SpectraAuthoring.h"
#include <string_view>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {
namespace {
Json defaultGraph() { return spectra::authoring::newGraphV2(); }
Json pairJson(ImVec2 pos) {
    Json a=Json::array();
    a.push(Json::number(double(std::clamp(pos.x,-100000.f,100000.f)),2));
    a.push(Json::number(double(std::clamp(pos.y,-100000.f,100000.f)),2));
    return a;
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
            if(dirty_ && !project_.empty()) {
                drafts_[project_]={name_,doc_};
                draftModes_[project_]=editingDraft_;
            }
            project_=project;revision_=-1;names_.clear();undo_.clear();redo_.clear();
            if(auto it=drafts_.find(project);it!=drafts_.end()) {
                name_=it->second.first;doc_=it->second.second;dirty_=true;
                editingDraft_=draftModes_[project];
            } else {
                name_="new_spectra";doc_=defaultGraph();dirty_=false;
                editingDraft_=false;
            }
            selected_=-1;pendingFrom_.clear();pendingDataFrom_.clear();
            refreshDiagnostics();refreshNames(ed);
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
        ImGui::TextUnformatted("Опубликованные");
        for(const std::string& name:names_) {
            if(ImGui::Selectable(name.c_str(),name==name_ && !editingDraft_)) {
                if(dirty_) message_="Сначала сохраните или отмените изменения.";
                else load(ed,name);
            }
        }
        ImGui::SeparatorText("Черновики");
        for(const std::string& name:draftNames_) {
            const std::string label="✎ "+name;
            if(ImGui::Selectable(label.c_str(),name==name_ && editingDraft_)) {
                if(dirty_) message_="Сначала сохраните или отмените изменения.";
                else loadDraft(ed,name);
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
        gpuEligible_=false;
        gpuVmEligible_=false;
        gpuReason_.clear();
        gpuVmReason_.clear();
        if(valid_) {
            const auto eligibility=spectra::inspectGraphGpuEligibility(result);
            gpuEligible_=eligibility.eligible;
            gpuReason_=eligibility.eligible
                ? "Metal compute subset compatible (device check still required)"
                : eligibility.reason;
            spectra::GraphRuntimeOptions vmOptions;
            vmOptions.gpuProgram=true;
            const auto vm=spectra::inspectGraphGpuEligibility(result,vmOptions);
            const bool hasProgram=result.emitters.size()==1 &&
                !result.emitters[0].particleProgram.isNull();
            gpuVmEligible_=hasProgram && vm.eligible;
            gpuVmReason_=!hasProgram?"Нет программы частиц"
                :vm.eligible?"Совместимо (экспериментально, GPU не проверен)"
                :vm.reason;
        }
    }
    void remember() {
        rememberSnapshot(doc_);
    }
    void rememberSnapshot(Json snapshot) {
        if(undo_.size()==64)undo_.erase(undo_.begin());
        undo_.push_back(std::move(snapshot));
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
        const Json drafts=ed.ws().call("fx_graph_draft_list",Json::object());
        draftNames_.clear();
        for(const auto& n:drafts["drafts"].items())
            if(n.isString())draftNames_.push_back(n.str());
        revision_=ed.fxRevision;
    }
    void load(Editor& ed,const std::string& name) {
        Json p=Json::object();p.set("name",Json::string(name));
        const auto result=ed.ws().call("fx_graph_get",p);
        if(!checkResult(result))return;
        if(!result["graph"].isObject()) {message_="Граф не распознан.";return;}
        name_=name;doc_=result["graph"];dirty_=false;editingDraft_=false;
        undo_.clear();redo_.clear();
        selected_=-1;pendingFrom_.clear();pendingDataFrom_.clear();
        propertyText_.clear();refreshDiagnostics();
    }
    void loadDraft(Editor& ed,const std::string& name) {
        Json p=Json::object();
        p.set("name",Json::string(name));
        const Json result=ed.ws().call("fx_graph_draft_get",p);
        if(!checkResult(result)||!result["graph"].isObject())return;
        doc_=result["graph"];name_=name;editingDraft_=true;dirty_=false;
        selected_=-1;pendingFrom_.clear();pendingDataFrom_.clear();
        undo_.clear();redo_.clear();propertyText_.clear();refreshDiagnostics();
        message_="Открыт черновик (не используется в игре до публикации).";
    }
    void saveDraft(Editor& ed) {
        if(name_.size()>48||!validFxName(name_)||name_=="fx") {
            message_="Недопустимое имя черновика";return;
        }
        Json p=Json::object();
        p.set("name",Json::string(name_));
        p.set("graph",doc_);
        const Json result=ed.ws().call("fx_graph_draft_set",p);
        if(!checkResult(result))return;
        editingDraft_=true;dirty_=false;drafts_.erase(project_);
        refreshNames(ed);
        message_="Черновик сохранён отдельно. Ошибки компиляции не мешают редактированию.";
    }
    void publishDraft(Editor& ed) {
        if(name_.size()>48||!validFxName(name_)||name_=="fx") {
            message_="Недопустимое имя черновика";return;
        }
        refreshDiagnostics();
        if(!valid_){message_="Исправьте ошибки графа перед публикацией.";return;}
        if(editingDraft_) {
            // Persist the current editor state first, then publish the exact
            // same asset atomically through the strict compiler gate.
            Json p=Json::object();
            p.set("name",Json::string(name_));
            p.set("graph",doc_);
            if(!checkResult(ed.ws().call("fx_graph_draft_set",p)))return;
            Json target=Json::object();
            target.set("name",Json::string(name_));
            if(!checkResult(ed.ws().call("fx_graph_draft_publish",target)))return;
            editingDraft_=false;dirty_=false;drafts_.erase(project_);
            refreshNames(ed);
            message_="Граф опубликован: fx/graphs/"+name_+".vfx.json";
        } else save(ed);
    }
    void save(Editor& ed) {
        if(editingDraft_) {saveDraft(ed);return;}
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
    void preview(Editor& ed,bool gpu) {
        Json request=Json::object();
        request.set("graph",doc_);
        request.set("name",Json::string(name_));
        request.set("gpu",Json::fromBool(gpu));
        if(doc_.getInt("version")==2 && previewInstanceValues_.isObject()) {
            spectra::InstanceParameterTable schema;
            std::vector<std::string> diagnostics;
            if(spectra::loadInstanceParameterTable(
                   doc_["instanceParameters"],schema,diagnostics)) {
                Json values=Json::object();
                for(const auto& [name,definition]:schema) {
                    const Json& candidate=previewInstanceValues_[name];
                    if(candidate.isNumber() && spectra::checkInstanceParameterValue(
                           definition,float(candidate.toDouble())))
                        values.set(name,candidate);
                }
                request.set("instanceValues",std::move(values));
            }
        }
        const auto result=ed.ws().call("fx_graph_preview",request);
        if(checkResult(result)) {
            message_=gpu?"Metal GPU preview queued (strict subset)."
                        :"CPU preview in Scene.";
            ed.focusWindow(result.getStr("view","scene"));
        }
    }
    void toolbar(Editor& ed) {
        ImGui::SetNextItemWidth(175);
        ui::inputString("Имя##spectra_name",name_);
        ImGui::SameLine();
        if(ImGui::Button("Новый")) {
            if(dirty_) {message_="Сохраните текущие изменения перед созданием нового графа.";return;}
            doc_=defaultGraph();name_="new_spectra";selected_=-1;pendingFrom_.clear();
            pendingDataFrom_.clear();editingDraft_=false;
            undo_.clear();redo_.clear();dirty_=true;message_.clear();refreshDiagnostics();
        }
        if(doc_.getInt("version")==1) {
            ImGui::SameLine();
            if(ImGui::Button("v1 → v2")) {
                // The v2 frontend accepts all existing v1 stage nodes and
                // execution links. This non-destructive upgrade preserves
                // authored settings, node positions and effect identity.
                remember();
                doc_.set("version",Json::number(int64_t(2)));
                refreshDiagnostics();
                message_="Граф переведён в v2. Исходный файл не изменён до сохранения.";
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Сохранить"))save(ed);
        ImGui::SameLine();
        if(ImGui::Button("Черновик"))saveDraft(ed);
        ImGui::SameLine();
        if(ImGui::Button("Опубликовать"))publishDraft(ed);
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
        if(ImGui::Button("Предпросмотр"))preview(ed,false);
        ImGui::SameLine();
        if(ImGui::Button("Metal GPU"))preview(ed,true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!ed.spectraGpuPreviewActive());
        if(ImGui::Button(ed.spectraGpuPreviewPaused()?"Play GPU":"Pause GPU"))
            ed.setSpectraGpuPreviewPaused(!ed.spectraGpuPreviewPaused());
        ImGui::SameLine();
        bool frozen=ed.spectraGpuPreviewFrozen();
        if(ImGui::Checkbox("Freeze GPU",&frozen))
            ed.setSpectraGpuPreviewFrozen(frozen);
        ImGui::SameLine();
        bool loop=ed.spectraGpuPreviewLoop();
        if(ImGui::Checkbox("Loop GPU",&loop))
            ed.setSpectraGpuPreviewLoop(loop);
        ImGui::SameLine();
        if(ImGui::Button("Stop GPU"))ed.stopSpectraGpuPreview();
        ImGui::SameLine();
        if(ImGui::Button("Step GPU")) {
            ed.setSpectraGpuPreviewFrozen(true);
            ed.stepSpectraGpuPreviewOnce();
        }
        ImGui::SameLine();
        float speed=ed.spectraGpuPreviewSpeed();
        ImGui::SetNextItemWidth(85.f);
        if(ImGui::SliderFloat("GPU speed",&speed,0.1f,2.f,"%.1fx"))
            ed.setSpectraGpuPreviewSpeed(speed);
        ImGui::EndDisabled();
        ImGui::SameLine();ImGui::TextDisabled("%s  v%lld (%d)",
            dirty_?"*":"",static_cast<long long>(doc_.getInt("version")),emitters_);
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
        Json candidate=doc_;
        std::string id,error;
        if(!spectra::authoring::addNode(candidate,type,id,error)) {
            message_=error;return;
        }
        remember();doc_=std::move(candidate);
        selectNode(int(doc_["nodes"].size())-1);
        refreshDiagnostics();
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
    void connectData(const std::string& from,const std::string& to,
                     const std::string& port) {
        Json candidate=doc_;
        std::string error;
        if(!spectra::authoring::connectData(candidate,from,to,port,error)) {
            message_=error;return;
        }
        remember();doc_=std::move(candidate);
        pendingDataFrom_.clear();
        message_="Подключён параметр "+port;
        refreshDiagnostics();
    }
    void link(const std::string& from,const std::string& to) {
        if(spectra::authoring::isData(nodeType(doc_,from)) &&
           doc_.getInt("version")==2) {
            pendingDataFrom_=from;
            for(size_t i=0;i<doc_["nodes"].size();++i)
                if(doc_["nodes"][i].getStr("id")==to) {selectNode(int(i));break;}
            const auto fields=spectra::authoring::inputPorts(nodeType(doc_,to));
            linkPort_=fields.empty()?"":fields.front();
            message_="Выберите входное поле для связи данных.";
            return;
        }
        Json candidate=doc_;
        std::string error;
        if(!spectra::authoring::connectExec(candidate,from,to,error)) {
            message_=error;return;
        }
        remember();doc_=std::move(candidate);
        refreshDiagnostics();
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
            const bool dataLink=e.has("toPort");
            p.x+=175;p.y+=24;q.y+=dataLink?52:24;
            const ImU32 color=ImGui::GetColorU32(
                dataLink?ImGuiCol_PlotHistogram:ImGuiCol_PlotLines);
            dl->AddBezierCubic(p,{p.x+70,p.y},{q.x-70,q.y},q,color,2.f);
            if(dataLink) {
                const std::string name=e.getStr("toPort");
                dl->AddText({q.x+4,q.y-7},color,name.c_str());
            }
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
            case spectra::FieldKind::Color: {
                fx::float4 color;
                if(fx::parseColor(current,color)) {
                    float rgba[4]={color.x,color.y,color.z,color.w};
                    changed=ImGui::ColorEdit4(field.label,rgba);
                    started=ImGui::IsItemActivated();
                    if(changed)updated=Json::string(fx::colorToHex(
                        {rgba[0],rgba[1],rgba[2],rgba[3]}));
                }
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
            // One undo checkpoint per active drag/text edit, not one per
            // frame, and only once an edit was actually applied: a rejected
            // value (or a click without change) must not push undo / dirty.
            if(started) {fieldCheckpoint_=doc_;fieldCheckpointValid_=true;}
            if(changed) {
                Json probe=doc_["nodes"][size_t(selected_)];
                if(spectra::applyTypedField(probe,field.key,updated,error)) {
                    if(fieldCheckpointValid_) {
                        fieldCheckpointValid_=false;
                        rememberSnapshot(std::move(fieldCheckpoint_));
                    } else if(!ImGui::IsItemActive())remember();
                    doc_.find("nodes")->at(size_t(selected_))=std::move(probe);
                    propertyText_=doc_["nodes"][size_t(selected_)]["properties"].dump(2);
                    refreshDiagnostics();
                } else message_=error;
            }
            if(ImGui::IsItemDeactivated())fieldCheckpointValid_=false;
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

    // Simple typed editors for v2 data literals. Complex values and exposed
    // functions remain in the advanced JSON field; never coerce a $node
    // reference or sampled expression into an editor scalar.
    void editValueNode() {
        if(selected_<0 || size_t(selected_)>=doc_["nodes"].size())return;
        const Json current=doc_["nodes"][size_t(selected_)];
        const std::string type=current.getStr("type");
        Json props=current["properties"];
        if(!props.isObject())props=Json::object();
        if(type=="Parameter") {
            std::string key=props.getStr("name",current.getStr("name"));
            if(ui::inputString("Параметр",key)) {
                remember();props.set("name",Json::string(key));
                doc_.find("nodes")->at(size_t(selected_)).set("properties",props);
                propertyText_=props.dump(2);refreshDiagnostics();
            }
            return;
        }
        const Json value=props["value"];
        if(value.isNull() || value.isObject())return; // preserves connections
        Json changed;
        bool update=false;
        if((type=="Float"||type=="Integer") && value.isNumber()) {
            if(type=="Float") {
                float number=float(value.toDouble());
                if(ImGui::DragFloat("Значение", &number,0.02f,-100000.f,100000.f)) {
                    changed=Json::number(double(number),6);update=true;
                }
            } else {
                int number=int(value.integer());
                if(ImGui::DragInt("Значение", &number,1,-100000,100000)) {
                    changed=Json::number(int64_t(number));update=true;
                }
            }
        } else if(type=="Bool" && value.type()==Json::Type::Bool) {
            bool yes=value.boolean();
            if(ImGui::Checkbox("Значение",&yes)) {
                changed=Json::fromBool(yes);update=true;
            }
        } else if((type=="Vec2"||type=="Vec3"||type=="Vec4") && value.isArray()) {
            const int n=type=="Vec2"?2:type=="Vec3"?3:4;
            if(value.size()==size_t(n)) {
                float parts[4]={0,0,0,0};
                for(int i=0;i<n;++i)parts[i]=float(value[size_t(i)].toDouble());
                bool edited=n==2?ImGui::DragFloat2("Вектор",parts,0.01f):
                    n==3?ImGui::DragFloat3("Вектор",parts,0.01f):
                          ImGui::DragFloat4("Вектор",parts,0.01f);
                if(edited) {
                    changed=Json::array();
                    for(int i=0;i<n;++i)changed.push(Json::number(double(parts[i]),6));
                    update=true;
                }
            }
        } else if(type=="Color" && value.isString()) {
            fx::float4 col;
            if(fx::parseColor(value,col)) {
                float rgba[4]={col.x,col.y,col.z,col.w};
                if(ImGui::ColorEdit4("Цвет",rgba)) {
                    changed=Json::string(fx::colorToHex({rgba[0],rgba[1],rgba[2],rgba[3]}));
                    update=true;
                }
            }
        }
        if(update) {
            remember();props.set("value",changed);
            doc_.find("nodes")->at(size_t(selected_)).set("properties",props);
            propertyText_=props.dump(2);
            refreshDiagnostics();
        }
    }

    void editCurveNode() {
        if(selected_<0 || size_t(selected_)>=doc_["nodes"].size())return;
        const Json current=doc_["nodes"][size_t(selected_)];
        const std::string type=current.getStr("type");
        if(type!="Curve" && type!="Gradient")return;
        const Json keys=current["properties"]["value"];
        if(!keys.isArray() || keys.size()<2 || keys.size()>64)return;
        ImGui::SeparatorText(type=="Curve"?"Кривая":"Градиент");
        if(type=="Curve") {
            std::vector<float> sampled;
            sampled.reserve(keys.size());
            for(const auto& key:keys.items()) {
                if(!key.isArray()||key.size()!=2||!key[size_t(1)].isNumber())return;
                sampled.push_back(float(key[size_t(1)].toDouble()));
            }
            ImGui::PlotLines("##spectra_curve_preview",sampled.data(),int(sampled.size()),
                             0,nullptr,FLT_MAX,FLT_MAX,ImVec2(-1,85));
        }
        Json changed=keys;
        bool edited=false;
        size_t removeIndex=keys.size();
        for(size_t i=0;i<keys.size();++i) {
            const Json& key=keys[i];
            if(!key.isArray()||key.size()!=2||!key[size_t(0)].isNumber())return;
            ImGui::PushID(int(i));
            float time=float(key[size_t(0)].toDouble());
            float minTime=i==0?0.f:float(keys[i-1][size_t(0)].toDouble()+0.0001);
            float maxTime=i+1==keys.size()?1.f:float(keys[i+1][size_t(0)].toDouble()-0.0001);
            if(ImGui::SliderFloat("t",&time,minTime,maxTime,"%.3f")) {
                changed.at(i).at(0)=Json::number(double(time),6);edited=true;
            }
            if(type=="Curve" && key[size_t(1)].isNumber()) {
                float amount=float(key[size_t(1)].toDouble());
                if(ImGui::DragFloat("v",&amount,0.02f,-100000.f,100000.f)) {
                    changed.at(i).at(1)=Json::number(double(amount),6);edited=true;
                }
            } else if(type=="Gradient") {
                fx::float4 color;
                if(fx::parseColor(key[size_t(1)],color)) {
                    float rgba[4]={color.x,color.y,color.z,color.w};
                    if(ImGui::ColorEdit4("rgba",rgba)) {
                        changed.at(i).at(1)=Json::string(
                            fx::colorToHex({rgba[0],rgba[1],rgba[2],rgba[3]}));
                        edited=true;
                    }
                }
            }
            ImGui::SameLine();
            if(keys.size()>2 && ImGui::SmallButton("-"))removeIndex=i;
            ImGui::PopID();
        }
        if(removeIndex<keys.size()) {
            changed.eraseAt(removeIndex);
            edited=true;
        }
        if(keys.size()<64 && ImGui::Button("+ Добавить точку")) {
            const size_t i=keys.size()-2;
            const double t=(keys[i][size_t(0)].toDouble()+
                            keys[i+1][size_t(0)].toDouble())*0.5;
            Json midpoint=Json::array();
            midpoint.push(Json::number(t,6));
            midpoint.push(keys[i][size_t(1)]);
            changed.itemsMut().insert(changed.itemsMut().begin()+std::ptrdiff_t(i+1),
                                      midpoint);
            edited=true;
        }
        if(edited) {
            remember();
            Json p=current["properties"];
            p.set("value",changed);
            doc_.find("nodes")->at(size_t(selected_)).set("properties",std::move(p));
            propertyText_=doc_["nodes"][size_t(selected_)]["properties"].dump(2);
            refreshDiagnostics();
        }
    }

    void properties(Editor& ed) {
        ImGui::TextUnformatted("Узлы");
        if(ImGui::BeginCombo("Добавить узел","Каталог SpectraFX")) {
            std::string group;
            for(const auto& info:spectra::authoring::nodeCatalog()) {
                if(info.v2Only && doc_.getInt("version")!=2)continue;
                if(group!=info.group) {
                    group=info.group;
                    ImGui::SeparatorText(info.group);
                }
                if(ImGui::Selectable(info.type))addNode(info.type);
            }
            ImGui::EndCombo();
        }
        if(!pendingFrom_.empty())ImGui::TextWrapped("Соединение: %s -> выберите вход",pendingFrom_.c_str());
        if(!pendingDataFrom_.empty() && selected_>=0 &&
           size_t(selected_)<doc_["nodes"].size()) {
            const std::string target=doc_["nodes"][size_t(selected_)].getStr("id");
            ImGui::SeparatorText("Связь данных");
            ImGui::TextWrapped("Выход %s → %s",pendingDataFrom_.c_str(),target.c_str());
            for(const auto& port:spectra::authoring::inputPorts(
                    doc_["nodes"][size_t(selected_)].getStr("type"))) {
                ImGui::PushID(port.c_str());
                if(ImGui::SmallButton(port.c_str()))linkPort_=port;
                ImGui::SameLine();
                ImGui::PopID();
            }
            ui::inputString("Вход/путь##spectra_port",linkPort_);
            if(ImGui::Button("Связать данные") && !linkPort_.empty())
                connectData(pendingDataFrom_,target,linkPort_);
            ImGui::SameLine();
            if(ImGui::SmallButton("Отмена##spectra_data_link"))
                pendingDataFrom_.clear();
        }
        ImGui::Separator();
        if(selected_>=0 && size_t(selected_)<doc_["nodes"].size()) {
            const Json& n=doc_["nodes"][size_t(selected_)];
            ImGui::Text("%s / %s",n.getStr("type").c_str(),n.getStr("id").c_str());
            if(spectra::authoring::isData(n.getStr("type"))) {
                editValueNode();
                editCurveNode();
            }
            if(n.getStr("type")=="FunctionCall") {
                const std::string active=n["properties"].getStr("function");
                if(ImGui::BeginCombo("Функция",active.c_str())) {
                    for(const auto& [name,definition]:doc_["functions"].members()) {
                        (void)definition;
                        if(ImGui::Selectable(name.c_str(),active==name)) {
                            remember();
                            Json props=doc_["nodes"][size_t(selected_)]["properties"];
                            props.set("function",Json::string(name));
                            doc_.find("nodes")->at(size_t(selected_)).set("properties",props);
                            propertyText_=props.dump(2);
                            refreshDiagnostics();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
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
        if(doc_.getInt("version")==2) {
            ImGui::SeparatorText("Параметры экземпляра (runtime)");
            if(ImGui::Button("Редактировать instanceParameters")) {
                instanceSchemaText_=(doc_["instanceParameters"].isObject()?
                    doc_["instanceParameters"]:Json::object()).dump(2);
                ImGui::OpenPopup("##spectra_instance_schema");
            }
            if(ImGui::BeginPopupModal("##spectra_instance_schema",nullptr,
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Имя -> {slot:0..7, default, min, max}. Значения меняются без перекомпиляции.");
                ui::inputMultiline("##spectra_instance_json",instanceSchemaText_,ImVec2(520,250));
                if(ImGui::Button("Применить instanceParameters")) {
                    try {
                        const Json schema=Json::parse(instanceSchemaText_);
                        spectra::InstanceParameterTable parsed;
                        std::vector<std::string> diagnostics;
                        if(!spectra::loadInstanceParameterTable(schema,parsed,diagnostics))
                            throw std::runtime_error(diagnostics.empty()?
                                "Invalid instance parameter schema":diagnostics.front());
                        remember();
                        doc_.set("instanceParameters",schema);
                        previewInstanceValues_=Json::object();
                        refreshDiagnostics();
                        message_="Схема параметров экземпляра обновлена.";
                        ImGui::CloseCurrentPopup();
                    } catch(const std::exception& e) {message_=e.what();}
                }
                ImGui::SameLine();
                if(ImGui::Button("Отмена##instance"))ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            spectra::InstanceParameterTable schema;
            std::vector<std::string> diagnostics;
            if(spectra::loadInstanceParameterTable(
                   doc_["instanceParameters"],schema,diagnostics)) {
                if(!schema.empty())ImGui::TextDisabled("Предпросмотр — значения данного экземпляра:");
                for(const auto& [name,definition]:schema) {
                    ImGui::PushID(name.c_str());
                    const Json& stored=previewInstanceValues_[name];
                    float value=stored.isNumber()?
                        float(stored.toDouble()):definition.initial;
                    if(ImGui::DragFloat(name.c_str(),&value,0.05f,
                                        definition.minimum,definition.maximum,
                                        "%.3f",ImGuiSliderFlags_AlwaysClamp)) {
                        value=std::clamp(value,definition.minimum,definition.maximum);
                        previewInstanceValues_.set(name,Json::number(double(value),4));
                    }
                    ImGui::PopID();
                }
                if(!schema.empty())ImGui::TextDisabled("Нажмите Предпросмотр, чтобы применить значения.");
            } else if(!diagnostics.empty())
                ImGui::TextWrapped("Ошибка instanceParameters: %s",diagnostics.front().c_str());
        }
        if(doc_.getInt("version")==2) {
            ImGui::SeparatorText("Переиспользуемые функции");
            if(ImGui::Button("Редактировать функции")) {
                functionText_=(doc_["functions"].isObject()?
                               doc_["functions"]:Json::object()).dump(2);
                ImGui::OpenPopup("##spectra_functions");
            }
            if(ImGui::BeginPopupModal("##spectra_functions",nullptr,
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
                ui::inputMultiline("##spectra_functions_json",functionText_,
                                   ImVec2(550,320));
                if(ImGui::Button("Применить определения")) {
                    try {
                        const Json functions=Json::parse(functionText_);
                        if(!functions.isObject())
                            throw std::runtime_error("functions must be a JSON object");
                        remember();
                        doc_.set("functions",functions);
                        refreshDiagnostics();
                        message_="Функции обновлены. Компилятор проверяет рекурсию и типы.";
                        ImGui::CloseCurrentPopup();
                    } catch(const std::exception& ex){message_=ex.what();}
                }
                ImGui::SameLine();
                if(ImGui::Button("Отмена##fn"))ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ui::inputString("Библиотека##spectra_fn_library",functionLibraryName_);
            if(ImGui::Button("Импортировать библиотеку") && !functionLibraryName_.empty()) {
                Json req=Json::object();
                req.set("library",Json::string(functionLibraryName_));
                req.set("graph",doc_);
                const Json result=ed.ws().call("fx_function_import",req);
                if(checkResult(result) && result["graph"].isObject()) {
                    remember();doc_=result["graph"];
                    refreshDiagnostics();
                    message_="Библиотека функций импортирована в граф.";
                }
            }
            if(ImGui::Button("Экспортировать функции") && !functionLibraryName_.empty()) {
                Json req=Json::object();
                req.set("library",Json::string(functionLibraryName_));
                req.set("graph",doc_);
                const Json result=ed.ws().call("fx_function_export",req);
                if(checkResult(result))
                    message_="Библиотека функций сохранена.";
            }
        }
        ImGui::Separator();
        if(ImGui::CollapsingHeader("Metal GPU timeline",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Visual simulation time: %.3f s",
                        ed.spectraGpuPreviewVisualSeconds());
            ImGui::Text("Queued forward steps: %u / 300",
                        ed.spectraGpuPreviewPendingSteps());
            ImGui::TextDisabled("Forward-only. No reverse GPU state or historical seek.");
            const auto& history=ed.spectraGpuPreviewHistory();
            if(!history.empty()) {
                std::vector<float> estimates;
                estimates.reserve(history.size());
                uint32_t failures=0;
                for(const auto& record:history) {
                    estimates.push_back(float(record.estimatedParticles));
                    if(!record.stepAccepted)++failures;
                }
                ImGui::PlotLines("Recent GPU particle estimates",
                    estimates.data(),int(estimates.size()),0,nullptr,
                    0.0f,FLT_MAX,{0,54});
                ImGui::Text("History: %zu / 180 frames, rejected steps: %u",
                            history.size(),failures);
                if(ImGui::Button("Copy GPU timeline JSON")) {
                    const std::string diagnostics=ed.spectraGpuPreviewHistoryJson();
                    ImGui::SetClipboardText(diagnostics.c_str());
                    message_="GPU preview diagnostics copied (not replayable snapshot)";
                }
            }
            ImGui::BeginDisabled(!ed.spectraGpuPreviewActive());
            ImGui::SetNextItemWidth(155.f);
            ImGui::SliderFloat("Advance GPU (+seconds)",&gpuAdvanceSeconds_,
                               0.1f,5.f,"%.2f s");
            ImGui::BeginDisabled(!ed.spectraGpuPreviewFrozen());
            if(ImGui::Button("Queue GPU advance")) {
                const uint32_t frames=uint32_t(std::lround(
                    gpuAdvanceSeconds_*60.f));
                if(!ed.queueSpectraGpuPreviewFrames(frames))
                    message_="GPU forward-step queue is full (300 frames)";
                else message_="Queued forward GPU frames; one step per rendered frame";
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Restart GPU") &&
               !ed.restartSpectraGpuPreview())
                message_="No active Metal GPU preview to restart";
            ImGui::EndDisabled();
            if(!ed.spectraGpuPreviewLastError().empty())
                ui::toneText(ui::Tone::Error,"%s",
                    ed.spectraGpuPreviewLastError().c_str());
        }
        if(ImGui::CollapsingHeader("Metal GPU checkpoint — live",ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto* pool=ed.spectraGpuTelemetry();
            if(pool!=gpuPrivacyPool_) {
                gpuPrivacyPool_=pool;
                gpuCheckpointGroup_=0;
                gpuReportIncludeBounds_=false;
                gpuReportIncludeIdentifiers_=false;
            }
            if(pool && pool->ready()) {
                const auto cp=pool->checkpointStats();
                const auto& ps=pool->stats();
                const auto& reset=pool->resetReport();
                if(reset.retryRequired || reset.deviceLost) {
                    ui::toneText(ui::Tone::Error,
                        "GPU reset blocked: %u/%u groups prepared, attempts %llu",
                        reset.groupsAccepted,reset.groupCount,
                        static_cast<unsigned long long>(reset.attempts));
                    if(reset.deviceLost)
                        ImGui::TextWrapped("Device lost: rebuild a new GPU pool from "
                            "CPU-side effect definitions. Live GPU particles cannot be recovered.");
                } else if(reset.attempts>0) {
                    ImGui::TextDisabled("Last reset accepted: %u/%u groups",
                        reset.groupsAccepted,reset.groupCount);
                }
                ImGui::Text("GPU groups: %u    Sources: %u",
                            ps.gpuGroups,ps.activeSources);
                ImGui::Text("Automatic VFX: budget %u, pressure %s",
                    ps.continuousFrameBudget,ps.pressureActive?"active":"off");
                ImGui::Text("Continuous births dropped under pressure: %llu",
                    static_cast<unsigned long long>(ps.pressureSkipped));
                ImGui::Text("Source handles: %u / %u",
                    ps.activeSources,ps.sourceCapacity);
                if(cp.gpuDeviceHealthKnown) {
                    ImGui::Text("Metal health: %s",
                                cp.gpuDeviceHealthy?"healthy":"UNHEALTHY");
                    if(!cp.gpuDeviceHealthy)
                        ui::toneText(ui::Tone::Error,
                            "GPU device reported unhealthy; do not trust live capture");
                } else ImGui::TextDisabled("Metal health: unavailable");
                ImGui::Text("GPU failures: kill %llu, staging %llu, compute %llu, reset %llu",
                    static_cast<unsigned long long>(cp.gpuKillUploadFailures),
                    static_cast<unsigned long long>(cp.gpuStagingUploadFailures),
                    static_cast<unsigned long long>(cp.gpuComputeBeginFailures),
                    static_cast<unsigned long long>(cp.gpuResetFailures));
                if(cp.stagingRecoveryGroups)
                    ui::toneText(ui::Tone::Warn,
                        "GPU input retry required in %u group(s)",
                        cp.stagingRecoveryGroups);
                ImGui::Text("GPU errors: pass-order %llu, device-unhealthy %llu",
                    static_cast<unsigned long long>(cp.gpuPassOrderRejections),
                    static_cast<unsigned long long>(cp.gpuDeviceUnhealthy));
                ImGui::Text("Fresh: %u  Stale: %u  Missing: %u",
                            cp.freshGroups,cp.staleGroups,cp.missingGroups);
                ImGui::Text("Pending: %u  Recoverable*: %u",
                            cp.pendingGroups,cp.restoreEligibleGroups);
                ImGui::Text("Binary export ready: %u / %zu group(s)",
                            cp.exportReadyGroups,pool->gpuGroupCount());
                const int groupCount=int(std::min<size_t>(pool->gpuGroupCount(),64));
                gpuCheckpointGroup_=groupCount>0?
                    std::clamp(gpuCheckpointGroup_,0,groupCount-1):0;
                ImGui::BeginDisabled(groupCount<=1);
                ImGui::SetNextItemWidth(95);
                if(ImGui::InputInt("GPU group index",&gpuCheckpointGroup_)) {
                    gpuCheckpointGroup_=groupCount>0?
                        std::clamp(gpuCheckpointGroup_,0,groupCount-1):0;
                    // Consent for revealing bounds/IDs is per selection.
                    // A different GPU group should start fully redacted.
                    gpuReportIncludeBounds_=false;
                    gpuReportIncludeIdentifiers_=false;
                }
                ImGui::EndDisabled();
                spectra::PoolGpuGroupSummary selected{};
                const bool selectedValid=pool->gpuGroupSummary(
                    size_t(gpuCheckpointGroup_),selected);
                const bool selectedReady=selectedValid && selected.exportReady;
                if(selectedValid) {
                    ImGui::Text("Selected group %zu: %u estimated particles, %.1f KiB",
                        selected.index,selected.estimatedAlive,
                        double(selected.allocatedGpuBytes)/1024.0);
                    ImGui::Text("Snapshot %s / %s",
                        selected.checkpoint.complete?
                            (selected.checkpoint.fresh?"fresh":"stale"):
                            "missing",
                        selected.checkpoint.pending?"pending readback":
                            (selectedReady?"export ready":"not exportable"));
                }
                if(const auto* recovery=ed.spectraGpuRecoveryHost()) {
                    const auto state=recovery->inspect(); // no Device mutation
                    const char* action="none";
                    switch(state.next) {
                    case spectra::PoolRecoveryAction::None:action="ready";break;
                    case spectra::PoolRecoveryAction::InitPool:action="initialize VFX pool";break;
                    case spectra::PoolRecoveryAction::RetryResetInGraphicsFrame:
                        action="retry visual reset at next safe host frame";break;
                    case spectra::PoolRecoveryAction::RecreateGraphicsDevice:
                        action="re-create graphics Device in native host";break;
                    case spectra::PoolRecoveryAction::InspectFailure:
                        action="inspect renderer failure";break;
                    }
                    ImGui::Text("Native recovery: %s (transition %llu)",
                        action,static_cast<unsigned long long>(state.transitionSequence));
                    if(state.consecutiveResetFailures)
                        ui::toneText(ui::Tone::Warn,
                            "GPU reset failures: %u; earliest owner frame: %llu",
                            state.consecutiveResetFailures,
                            static_cast<unsigned long long>(
                                state.retryNotBeforeOwnerFrame));
                    if(state.next==spectra::PoolRecoveryAction::RecreateGraphicsDevice)
                        ui::toneText(ui::Tone::Error,
                            "Native renderer must recreate a NEW graphics Device");
                    if(!state.reason.empty())
                        ImGui::TextWrapped("Recovery details: %s",state.reason.c_str());
                    ImGui::BeginDisabled(
                        state.next!=spectra::PoolRecoveryAction::None ||
                        pool->presentationBlocked());
                    if(ImGui::Button("Queue native VFX-only reset") &&
                       !ed.queueSpectraExternalVisualReset())
                        message_="Native renderer refused the visual reset intent";
                    ImGui::EndDisabled();
                    ImGui::TextDisabled(
                        "Native owner handles reset; editor never encodes GPU work.");
                }
                if(const auto* mailbox=ed.spectraGpuCaptureMailbox()) {
                    const auto& state=mailbox->status();
                    const char* phase="idle";
                    switch(state.phase) {
                    case spectra::GpuCaptureMailboxPhase::Idle:phase="idle";break;
                    case spectra::GpuCaptureMailboxPhase::Requested:phase="queued";break;
                    case spectra::GpuCaptureMailboxPhase::Claimed:phase="accepted by host";break;
                    case spectra::GpuCaptureMailboxPhase::InFlight:phase="GPU readback pending";break;
                    case spectra::GpuCaptureMailboxPhase::Ready:phase="ready";break;
                    case spectra::GpuCaptureMailboxPhase::Failed:phase="failed";break;
                    case spectra::GpuCaptureMailboxPhase::ReleaseRequested:phase="release requested";break;
                    }
                    ImGui::Text("Native renderer capture: %s (request %llu)",
                        phase,static_cast<unsigned long long>(state.sequence));
                    if(!state.reason.empty())
                        ImGui::TextWrapped("%s",state.reason.c_str());
                    ImGui::BeginDisabled(!selectedValid ||
                        !selected.gpuActive ||
                        (state.phase!=spectra::GpuCaptureMailboxPhase::Idle &&
                         state.phase!=spectra::GpuCaptureMailboxPhase::Failed));
                    if(ImGui::Button("Request selected group from render host") &&
                       !ed.queueSpectraExternalGpuCapture(size_t(gpuCheckpointGroup_)))
                        message_="Native render host declined or is not connected";
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(
                        state.phase!=spectra::GpuCaptureMailboxPhase::Ready &&
                        state.phase!=spectra::GpuCaptureMailboxPhase::Failed &&
                        state.phase!=spectra::GpuCaptureMailboxPhase::Requested &&
                        state.phase!=spectra::GpuCaptureMailboxPhase::InFlight);
                    if(ImGui::Button("Release native capture"))
                        (void)ed.releaseSpectraExternalGpuCapture();
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("UI queues intent only; native owner executes the GPU readback.");
                }
                ImGui::BeginDisabled(!ed.spectraGpuPreviewActive() ||
                                    gpuCheckpointGroup_!=0);
                if(ImGui::Button("Freeze & capture preview group 0")) {
                    if(!ed.requestSpectraGpuPreviewCheckpoint())
                        message_="Metal GPU preview is not active";
                }
                ImGui::EndDisabled();
                if(!ed.spectraGpuCaptureNote().empty())
                    ImGui::TextWrapped("%s",ed.spectraGpuCaptureNote().c_str());
                ImGui::Separator();
                ImGui::TextDisabled("Safe summary excludes raw particles, VM scalars, "
                                    "world coordinates and GPU IDs by default.");
                ImGui::Checkbox("Include world bounds (sensitive)",
                                &gpuReportIncludeBounds_);
                ImGui::SameLine();
                ImGui::Checkbox("Include GPU identifiers",
                                &gpuReportIncludeIdentifiers_);
                ImGui::BeginDisabled(!selectedReady);
                if(ImGui::Button("Save SAFE ZIP bundle")) {
                    std::string fileOrReason;
                    const bool ok=ed.saveSafeSpectraGpuBundle(
                        fileOrReason,size_t(gpuCheckpointGroup_));
                    message_=(ok?"Sanitized ZIP bundle saved: ":
                                "Safe ZIP unavailable: ")+fileOrReason;
                }
                ImGui::SameLine();
                if(ImGui::Button("Save safe JSON report")) {
                    std::string fileOrReason;
                    const bool ok=ed.saveSafeSpectraGpuReport(
                        fileOrReason,size_t(gpuCheckpointGroup_),
                        gpuReportIncludeBounds_,gpuReportIncludeIdentifiers_);
                    message_=(ok?"Redacted GPU report saved: ":
                                "GPU report unavailable: ")+fileOrReason;
                }
                ImGui::SameLine();
                if(ImGui::Button("Save RAW GPU checkpoint (sensitive)")) {
                    std::string fileOrReason;
                    const bool ok=ed.saveFreshSpectraGpuCheckpoint(
                        fileOrReason,size_t(gpuCheckpointGroup_));
                    message_=(ok?"RAW GPU diagnostic saved: ":
                                "GPU export unavailable: ")+fileOrReason;
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("SPFX V2 binary contains full positions/VM inputs. "
                                    "No GPU rewind or ECS save.");
                ImGui::Text("Readbacks: %llu / %llu complete, %llu failed",
                            static_cast<unsigned long long>(cp.requests),
                            static_cast<unsigned long long>(cp.completed),
                            static_cast<unsigned long long>(cp.failed));
                ImGui::Text("Denied: %llu  In flight: %zu KiB (%u chunks)",
                            static_cast<unsigned long long>(cp.budgetDenied),
                            cp.budget.bytesInFlight/1024,cp.budget.chunksInFlight);
                ImGui::Text("Payload returned: %zu KiB",cp.bytesReturned/1024);
                ImGui::Text("Adaptive backoffs: %llu  recoveries: %llu",
                            static_cast<unsigned long long>(cp.adaptiveBackoffs),
                            static_cast<unsigned long long>(cp.adaptiveRecoveries));
                ImGui::Text("Max active checkpoint interval: %u submissions",
                            cp.maximumCurrentIntervalFrames);
                ImGui::Text("GPU killed slots: %u waiting for completion, %llu reclaimed",
                            cp.killedSlotsAwaitingFence,
                            static_cast<unsigned long long>(cp.killedSlotsReclaimed));
                ImGui::Text("GPU scatter kills: %llu dispatches, %zu KiB indices",
                            static_cast<unsigned long long>(cp.killScatterDispatches),
                            cp.killIndexBytes/1024);
                ImGui::TextWrapped("* Recoverable means a complete current-revision "
                                   "snapshot exists; numerical CPU/Metal parity "
                                   "and uninterrupted visuals are not guaranteed.");
                if(cp.staleGroups || cp.budgetDenied)
                    ui::toneText(ui::Tone::Warn,
                        "Snapshots stale or readback budget exceeded");
                ImGui::TextDisabled("Telemetry from attached GPU pool, not CPU Scene preview.");
            } else {
                ImGui::TextDisabled("No live GPU pool attached.");
                ImGui::TextWrapped("Current editor Scene VFX preview runs on CPU. "
                                   "Metal host may attach a GpuPoolWorld via "
                                   "Editor::setSpectraGpuTelemetry().");
            }
        }
        ImGui::Text("CPU emitters: %d",emitters_);
        ImGui::Text("Metal GPU subset: %s",gpuEligible_?"compatible":"CPU fallback");
        if(!gpuReason_.empty())ImGui::TextWrapped("%s",gpuReason_.c_str());
        ImGui::Text("Metal scalar VM (opt-in): %s",gpuVmEligible_?"compatible":"CPU fallback");
        if(!gpuVmReason_.empty())ImGui::TextWrapped("%s",gpuVmReason_.c_str());
        ImGui::TextWrapped("V2: типизированные math/dataflow узлы, "
                           "модули FxEffect и функции компилируются в CPU. "
                           "Metal GPU доступен только для совместимого billboard-подмножества.");
    }

    float gpuAdvanceSeconds_=0.5f;
    int gpuCheckpointGroup_=0;
    bool gpuReportIncludeBounds_=false;
    bool gpuReportIncludeIdentifiers_=false;
    const spectra::GpuPoolWorld* gpuPrivacyPool_=nullptr; // non-owning identity only
    std::string project_,name_="new_spectra",message_,propertyText_,parametersText_,functionText_,instanceSchemaText_,functionLibraryName_="common_math",pendingFrom_,pendingDataFrom_,linkPort_;
    Json previewInstanceValues_=Json::object();
    std::vector<std::string> names_,draftNames_,errors_;
    Json doc_=defaultGraph();
    std::vector<Json> undo_,redo_;
    Json fieldCheckpoint_; // pre-edit doc of the active typed field widget
    bool fieldCheckpointValid_=false;
    std::map<std::string,std::pair<std::string,Json>> drafts_;
    std::map<std::string,bool> draftModes_;
    ImVec2 pan_{10,10};
    int selected_=-1,revision_=-1,emitters_=0;
    bool dirty_=false,valid_=false,dragging_=false,gpuEligible_=false,gpuVmEligible_=false,editingDraft_=false;
    std::string gpuReason_,gpuVmReason_;
};

AIVERSE_EDITOR_WINDOW(SpectraGraphWindow);
} // namespace aiverse::editor
