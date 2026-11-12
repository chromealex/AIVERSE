#pragma once
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../EditorGenreUi.h"
#include "../core/AnimationOps.h"
#include "../core/AnimationDocument.h"
#include "modules/rts/view/RtsPortrait.h"
#include "engine/render/AnimationGraph.h"
#include "engine/render/AnimationRig.h"
#include "engine/render/FullBodyIK.h"
#include <algorithm>
#include <map>
namespace aiverse::editor {
// Both windows edit the same runtime document; rig units remain an ordered execution graph.
class AnimationEditorPanel {
public:
    explicit AnimationEditorPanel(bool rig):rigMode_(rig){}
    ~AnimationEditorPanel(){release();}
    void update(Editor& ed,double dt){dt_=float(std::min(dt,.1));if(registered_)return;registered_=true;
      ed.ws().registerOp({rigMode_ ? "rig_editor_open" : "animation_editor_open",
                          "Open an animation asset in the visual editor; refuses unsaved replacement.",
                          Json::parse(R"({"properties":{"name":{"type":"string"},"graph":{"type":"object"}}})"),
                          [this, &ed](const Json &p) {
                            if (dirty_)
                              throw std::runtime_error("save or discard current edits");
                            Json g = p["graph"];
                            if (g.isNull()) {
                              auto r = ed.ws().call("animation_graph_get", p);
                              if (r.has("error"))
                                return r;
                              g = r["graph"];
                            }
                            animation::sort(g);
                            project_ = ed.ws().project.dir;
                            name_ = p.getStr("name", "untitled");
                            doc_ = saved_ = g;
                            reset();
                            ed.focusWindow(rigMode_ ? "animation_rig" : "animation_graph");
                            Json r = Json::object();
                            r.set("ok", Json::fromBool(true));
                            return r;
                          },
                          true});
        ed.ws().registerOp({rigMode_?"rig_runtime_status":"animation_runtime_status","Per-entity animation contact and climbing diagnostics from the running scene.",Json::object(),[&ed](const Json&){
            Json result=Json::array();if(!ed.entityFx)return result;std::vector<uint64_t> keys;for(const auto& entry:ed.entityFx->animations)keys.push_back(entry.first);std::sort(keys.begin(),keys.end());
            for(auto key:keys){if(result.size()>=256)break;const auto& entry=ed.entityFx->animations.at(key);const auto& c=entry.locomotion;Json row=Json::object();row.set("entity",Json::number(int64_t(key)));row.set("error",Json::string(entry.error));row.set("climbing",Json::fromBool(c.active));row.set("attached",Json::fromBool(c.attached));row.set("stanceFeet",Json::number(int64_t(c.supports)));row.set("steppingFeet",Json::number(int64_t(c.stepping)));row.set("footError",Json::number(double(c.footError),6));row.set("penetration",Json::number(double(c.penetration),6));Json position=Json::array(),normal=Json::array();for(float v:{c.position.x,c.position.y,c.position.z})position.push(Json::number(double(v),6));for(float v:{c.normal.x,c.normal.y,c.normal.z})normal.push(Json::number(double(v),6));row.set("bodyPosition",position);row.set("surfaceNormal",normal);result.push(row);}return result;
        },true});
      ed.ws().registerOp({rigMode_ ? "rig_editor_status" : "animation_editor_status",
                          "Current preview state and evaluated pose after a rendered frame.", Json::object(),
                          [this](const Json &) {
                            Json r = Json::object();
                            r.set("error", Json::string(error_));
                            r.set("ready", Json::fromBool(ready_));
                            r.set("dirty", Json::fromBool(dirty_));
                            r.set("graph", doc_);
                            Json a = Json::array();
                            for (const auto &p : pose_) {
                              Json v = Json::array();
                              v.push(Json::number(double(p.t.x)));
                              v.push(Json::number(double(p.t.y)));
                              v.push(Json::number(double(p.t.z)));
                              a.push(v);
                            }
                            r.set("translations", a);
                            auto climb=instance_.locomotionReport();r.set("climbing",Json::fromBool(climb.active));r.set("attached",Json::fromBool(climb.attached));r.set("stanceFeet",Json::number(int64_t(climb.supports)));r.set("steppingFeet",Json::number(int64_t(climb.stepping)));r.set("footError",Json::number(double(climb.footError)));r.set("climbPenetration",Json::number(double(climb.penetration)));Json pos=Json::array();for(float v:{climb.position.x,climb.position.y,climb.position.z})pos.push(Json::number(double(v)));r.set("bodyPosition",pos);
                            auto report = instance_.contactReport();
                            r.set("supported", Json::fromBool(report.supported));
                            r.set("stable", Json::fromBool(report.stable));
                            r.set("supports", Json::number(int64_t(report.supports)));
                            r.set("penetration", Json::number(double(report.penetration)));
                            r.set("balanceError", Json::number(double(report.balanceError)));
                            r.set("supportError", Json::number(double(report.supportError)));
                            return r;
                          },
                          true});
    }
    void draw(Editor& ed){
        ws_=&ed.ws();auto tr=[&](const char* k){return text(k);};
        if(ed.ws().project.dir.empty()){ui::hint(tr("openProject").c_str());return;}
        if(project_!=ed.ws().project.dir){if(!project_.empty())drafts_[project_]={doc_,saved_,name_,dirty_};project_=ed.ws().project.dir;if(drafts_.count(project_)){auto& draft=drafts_[project_];doc_=draft.document;saved_=draft.saved;name_=draft.name;dirty_=draft.dirty;}else{doc_=saved_=animation::makeGraph();name_="untitled";dirty_=false;}reset();}
        ImGui::SetNextItemWidth(150);ui::inputString("##asset",name_);ImGui::SameLine();
        if(ImGui::Button(tr("save").c_str()))save(ed);
        ImGui::SameLine();ImGui::BeginDisabled(dirty_);
        if(ImGui::Button(tr("load").c_str())){Json p=Json::object();p.set("name",Json::string(name_));auto r=ed.ws().call("animation_graph_get",p);if(check(r)){doc_=saved_=r["graph"];reset();}}
        ImGui::SameLine();if(ImGui::Button(tr("new").c_str())){doc_=saved_=animation::makeGraph();reset();}ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button(tr("undo").c_str())&&!undo_.empty()){redo_.push_back(doc_);doc_=undo_.back();undo_.pop_back();dirty_=true;}
        ImGui::SameLine();if(ImGui::Button(tr("redo").c_str())&&!redo_.empty()){undo_.push_back(doc_);doc_=redo_.back();redo_.pop_back();dirty_=true;}
        if(dirty_){ImGui::SameLine();if(ImGui::Button(tr("discard").c_str())){doc_=saved_;dirty_=false;reset();}}
        std::string model=doc_.getStr("model");ImGui::SetNextItemWidth(220);if(ui::inputString(tr("model").c_str(),model)){remember();doc_.set("model",Json::string(model));}
        ImGui::SameLine();if(ImGui::Button(tr("reload").c_str()))release();
        ImGui::SetNextItemWidth(180);if(ImGui::BeginCombo(tr("type").c_str(),target_.c_str())){for(const auto& t:ed.rules().types)if(ImGui::Selectable(t.id.c_str(),target_==t.id)){target_=t.id;auto path=t.view.getStr("model");if(!path.empty()){remember();doc_.set("model",Json::string(path));}}ImGui::EndCombo();}
        ImGui::SameLine();ImGui::BeginDisabled(!ready_||target_.empty());if(ImGui::Button(tr("apply").c_str())&&save(ed)){Json p=Json::object();p.set("name",Json::string(name_));p.set("target",Json::string(target_));check(ed.ws().call("animation_graph_apply",p));}ImGui::EndDisabled();
        ImGui::SetNextItemWidth(170);if(ImGui::BeginCombo(tr("scope").c_str(),scope_.empty()?tr("root").c_str():scope_.c_str())){if(ImGui::Selectable(tr("root").c_str(),scope_.empty())){scope_.clear();selected_.clear();}for(const auto& [name,g]:doc_["graphs"].members()){(void)g;if(ImGui::Selectable(name.c_str(),scope_==name)){scope_=name;selected_.clear();}}ImGui::EndCombo();}
        ImGui::SameLine();ImGui::SetNextItemWidth(110);ui::inputString("##subname",subName_);ImGui::SameLine();if(ImGui::Button(tr("addSubgraph").c_str())&&!subName_.empty()&&!doc_["graphs"].has(subName_)){remember();doc_.member("graphs").set(subName_,animation::makeGraph());scope_=subName_;}
        ImGui::SetNextItemWidth(170);if(ImGui::BeginCombo(tr("addNode").c_str(),"+")){for(auto kind:{"bind","clip","blend","additive","twoBoneIK","rig","stack","subgraph","motionMatching","native"})if(ImGui::Selectable(kind)){remember();auto& g=graph();int i=1;std::string id;do{id="node"+std::to_string(i++);}while(node(g,id));auto n=animation::makeNode(kind,id,g["nodes"][0].getStr("id"));g.member("nodes").push(n);selected_=id;}ImGui::EndCombo();}
        float width=ImGui::GetContentRegionAvail().x;float left=std::max(260.f,width*.58f);
        ImGui::BeginChild("canvasColumn",{left,0});
        ImGui::BeginChild("nodes",{0,std::max(180.f,ImGui::GetContentRegionAvail().y*.46f)},ImGuiChildFlags_Borders);canvas();ImGui::EndChild();
        preview(ed);ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("properties",{0,0});
        if(auto* n=node(graph(),selected_)){
            std::string name=n->getStr("id");if(ui::inputString(tr("nodeName").c_str(),name)){Json before=doc_;try{animation::rename(graph(),selected_,name);remember(before);selected_=name;}catch(const std::exception& e){error_=e.what();}}
            if(ImGui::Button(tr("output").c_str())){remember();graph().set("output",Json::string(selected_));}
            ImGui::SameLine();if(ImGui::Button(tr("delete").c_str())){Json before=doc_;try{animation::remove(graph(),selected_);remember(before);selected_.clear();}catch(const std::exception& e){error_=e.what();}}
        }
        Json before=doc_;if(auto* n=node(graph(),selected_)){
            if(n->getStr("kind")=="rig"){rigProperties(*n);if(rigMode_&&unit_>=0&&size_t(unit_)<(*n)["units"].size())editValue(n->member("units").at(size_t(unit_)),"unit",0);}
            if(ImGui::CollapsingHeader(tr("nodeProperties").c_str(),rigMode_?ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen))editValue(*n,"node",0);
        }
        if(ImGui::CollapsingHeader(tr("parameters").c_str(),ImGuiTreeNodeFlags_DefaultOpen)){editValue(graph().member("parameters"),"parameters",0);}
        if(ImGui::CollapsingHeader(tr("states").c_str())){if(!graph().has("states")){if(ImGui::Button(tr("addState").c_str()))graph().set("states",Json::parse(R"([{"id":"state","output":"bind","transitions":[]}])"));}else editValue(graph().member("states"),"states",0);std::string initial=graph().getStr("initial");if(ui::inputString("initial",initial))graph().set("initial",Json::string(initial));}
        if(!(before==doc_))remember(before);
        if (!error_.empty())
          ui::callout(ui::Tone::Error, error_);
        if (!message_.empty())
          ui::hint(message_.c_str());
        ui::hint(tr("help").c_str());ImGui::EndChild();
    }
private:
    std::string text(const char* key)const{return genreText(*ws_,("animationEditor."+std::string(key)).c_str());}
    Json& graph(){if(scope_.empty()||!doc_["graphs"].has(scope_)){scope_.clear();return doc_;}return doc_.member("graphs").member(scope_);}
    static Json* node(Json& g,const std::string& id){for(auto& n:g.member("nodes").itemsMut())if(n.getStr("id")==id)return &n;return nullptr;}
    void release(){instance_.reset({});if(model_&&dev_)render::releaseModel(*dev_,*model_);model_.reset();portrait_.shutdown();modelKey_.clear();programKey_.clear();ready_=false;}
    void reset(){release();undo_.clear();redo_.clear();scope_.clear();selected_.clear();error_.clear();controls_={};}
    void remember(){remember(doc_);}void remember(const Json& before){if(undo_.size()>=64)undo_.erase(undo_.begin());undo_.push_back(before);redo_.clear();dirty_=true;}
    bool check(const Json& r){if(r.has("error")){error_=r.getStr("error");return false;}error_.clear();return true;}
    bool save(Editor& ed){Json p=Json::object();p.set("name",Json::string(name_));p.set("graph",doc_);p.set("expected",saved_);auto r=ed.ws().call("animation_graph_set",p);if(!check(r))return false;doc_=saved_=r["graph"];dirty_=false;message_=text("savedDraft");return true;}
    void editValue(Json& value,const std::string& key,int depth){
      if (depth > 16)
        return;
      ImGui::PushID(key.c_str());
      if (value.isString()) {
        ImGui::TextUnformatted(key.c_str());
        ImGui::SetNextItemWidth(-1);
        auto s = value.str();
        if (ui::inputString("##value", s))
          value = Json::string(s);}
        else if(value.isNumber()){ImGui::TextUnformatted(key.c_str());ImGui::SetNextItemWidth(-1);float f=float(value.toDouble());if(ImGui::DragFloat("##value",&f,.01f,-1000000,1000000,"%.4f"))value=Json::number(double(f),6);}
        else if(value.type()==Json::Type::Bool){bool b=value.boolean();if(ImGui::Checkbox(key.c_str(),&b))value=Json::fromBool(b);}
        else if(value.isArray()&&value.size()==3&&value[0].isNumber()&&value[1].isNumber()&&value[2].isNumber()){ImGui::TextUnformatted(key.c_str());ImGui::SetNextItemWidth(-1);float v[]={float(value[0].toDouble()),float(value[1].toDouble()),float(value[2].toDouble())};if(ImGui::DragFloat3("##value",v,.01f)){value=Json::array();for(auto f:v)value.push(Json::number(double(f),6));}}
        else if(value.isArray()&&ImGui::TreeNodeEx(key.c_str(),ImGuiTreeNodeFlags_DefaultOpen)){
            int erase=-1;for(size_t i=0;i<value.size();++i){ImGui::PushID(int(i));if(ImGui::SmallButton("-"))erase=int(i);ImGui::SameLine();editValue(value.at(i),std::to_string(i),depth+1);ImGui::PopID();}if(erase>=0)value.eraseAt(size_t(erase));
            if(ImGui::SmallButton(text("addItem").c_str())){if(value.size())value.push(value[0]);else if(key=="states")value.push(Json::parse(R"({"id":"state","output":"bind","transitions":[]})"));else if(key=="transitions")value.push(Json::parse(R"({"to":"state","fade":0.2,"when":[]})"));else if(key=="when")value.push(Json::parse(R"({"parameter":"","op":"gt","value":0})"));else if(key=="joints")value.push(Json::parse(R"({"bone":"","stiffness":0,"min":[-3.14,-3.14,-3.14],"max":[3.14,3.14,3.14],"preferred":[0,0,0]})"));else if(key=="legs")value.push(Json::parse(R"({"hip":"","knee":"","foot":"","home":[0,-0.5,0],"pole":[0,0.5,0]})"));else if(key=="feet")value.push(animation::makeContacts("","")["feet"][0]);else if(key=="capsules")value.push(animation::makeContacts("","")["capsules"][0]);else if(key=="masses")value.push(animation::makeContacts("","")["balance"]["masses"][0]);else if(key=="obstacles")value.push(Json::parse(R"({"kind":"box","point":[0,0,0],"extent":[0.5,0.5,0.5],"radius":0.5,"normal":[0,1,0]})"));else if(key=="traits")value.push(Json::parse(R"({"trait":"","config":{}})"));else value.push(Json::string(""));}ImGui::TreePop();
        }else if(value.isObject()&&ImGui::TreeNodeEx(key.c_str(),ImGuiTreeNodeFlags_DefaultOpen)){
            std::string erased;for(auto& [k,v]:value.membersMut())if((k!="id"||key!="node")&&k!="position"){ImGui::PushID(k.c_str());if(ImGui::SmallButton("-"))erased=k;ImGui::SameLine();editValue(v,k,depth+1);ImGui::PopID();}if(!erased.empty())value.erase(erased);
            auto& field=fields_[ImGui::GetID("field")];ImGui::SetNextItemWidth(110);ui::inputString("##field",field);ImGui::SameLine();
            if(ImGui::BeginCombo("##newtype","+")){for(int t=0;t<5;++t){const char* keys[]={"number","vector","string","object","array"};if(ImGui::Selectable(text(keys[t]).c_str())&&!field.empty()&&!value.has(field)){value.set(field,t==0?Json::number(0.0):t==1?Json::parse("[0,0,0]"):t==2?Json::string(""):t==3?Json::object():Json::array());}}ImGui::EndCombo();}ImGui::TreePop();
        }ImGui::PopID();
    }
    void rigProperties(Json& n){auto& units=n.member("units");if(ImGui::BeginCombo(text("addUnit").c_str(),"+")){for(auto op:{"transform","copy","aim","twoBoneIK","limit","fullBodyIK","surfaceLocomotion"})if(ImGui::Selectable(op)){auto u=animation::makeUnit(op);if(std::string(op)=="surfaceLocomotion"&&!graph()["parameters"].has("velocity"))graph().member("parameters").set("velocity",Json::parse("[0,0,0]"));if(model_&&!model_->nodes.empty())u.set("bone",Json::string(model_->nodes[0].name));units.push(u);}ImGui::EndCombo();}for(size_t i=0;i<units.size();++i){ImGui::PushID(int(i));ImGui::Text("%zu: %s",i,units[i].getStr("op").c_str());if(units[i].getStr("op")=="fullBodyIK"&&!units[i].has("contacts")){ImGui::SameLine();if(ImGui::SmallButton(text("addContacts").c_str())){auto bone=units[i].getStr("bone"),foot=units[i]["effectors"][0].getStr("bone",bone);units.at(i).set("contacts",animation::makeContacts(bone,foot));}}if(i>0){ImGui::SameLine();if(ImGui::SmallButton("^"))std::swap(units.at(i),units.at(i-1));}ImGui::PopID();}}
    void canvas(){
      if (!ImGui::IsMouseDown(0))
        dragging_ = false;
      auto origin = ImGui::GetCursorScreenPos();
      auto avail = ImGui::GetContentRegionAvail();
      auto *dl = ImGui::GetWindowDrawList();
      if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        pan_.x += ImGui::GetIO().MouseDelta.x;
        pan_.y += ImGui::GetIO().MouseDelta.y;}
        if(ImGui::IsWindowHovered()&&ImGui::GetIO().MouseWheel!=0)zoom_=std::clamp(zoom_+ImGui::GetIO().MouseWheel*.1f,.5f,1.6f);
        if(rigMode_){auto* rig=node(graph(),selected_);if(!rig||rig->getStr("kind")!="rig"){for(auto& n:graph().member("nodes").itemsMut())if(n.getStr("kind")=="rig"){selected_=n.getStr("id");rig=&n;break;}}
            if(rig&&rig->getStr("kind")=="rig"){
                const auto& units=(*rig)["units"];int moveFrom=-1,moveTo=-1;for(size_t i=0;i<units.size();++i){ImVec2 p{origin.x+pan_.x+25+float(i%3)*180,origin.y+pan_.y+25+float(i/3)*90};dl->AddRectFilled(p,{p.x+160,p.y+60},ImGui::GetColorU32(ImGuiCol_FrameBg),6);dl->AddText({p.x+8,p.y+8},ImGui::GetColorU32(ImGuiCol_Text),units[i].getStr("op").c_str());dl->AddText({p.x+8,p.y+32},ImGui::GetColorU32(ImGuiCol_TextDisabled),units[i].getStr("bone").c_str());if(i+1<units.size()){ImVec2 next{origin.x+pan_.x+25+float((i+1)%3)*180,origin.y+pan_.y+25+float((i+1)/3)*90};dl->AddBezierCubic({p.x+160,p.y+30},{p.x+180,p.y+30},{next.x-20,next.y+30},{next.x,next.y+30},ImGui::GetColorU32(ImGuiCol_PlotLines),2);}ImGui::SetCursorScreenPos(p);ImGui::PushID(int(i));ImGui::InvisibleButton("unit",{160,60});if(ImGui::IsItemClicked())unit_=int(i);if(ImGui::BeginDragDropSource()){int index=int(i);ImGui::SetDragDropPayload("RIG_UNIT",&index,sizeof index);ImGui::TextUnformatted(units[i].getStr("op").c_str());ImGui::EndDragDropSource();}if(ImGui::BeginDragDropTarget()){if(auto* data=ImGui::AcceptDragDropPayload("RIG_UNIT")){moveFrom=*static_cast<const int*>(data->Data);moveTo=int(i);}ImGui::EndDragDropTarget();}ImGui::PopID();}
                if(moveFrom>=0&&moveTo>=0&&moveFrom!=moveTo){remember();auto& list=rig->member("units").itemsMut();auto item=list[size_t(moveFrom)];list.erase(list.begin()+moveFrom);list.insert(list.begin()+moveTo,item);unit_=moveTo;}
                ImGui::SetCursorScreenPos({origin.x+8,origin.y+avail.y-24});ImGui::TextUnformatted(text("rigOrder").c_str());return;
            }
        }
        for(float x=std::fmod(pan_.x,24.f);x<avail.x;x+=24)dl->AddLine({origin.x+x,origin.y},{origin.x+x,origin.y+avail.y},ImGui::GetColorU32(ImGuiCol_Border,.3f));
        auto position=[&](const Json& n){return ImVec2(origin.x+pan_.x+float(n["position"][0].toDouble())*zoom_,origin.y+pan_.y+float(n["position"][1].toDouble())*zoom_);};
        std::string from,to,port;
        for(auto& n:graph().member("nodes").itemsMut()){
            auto p=position(n);std::vector<std::string> inputs;auto k=n.getStr("kind");if(k=="blend"||k=="additive")inputs={"a","b"};else if(n.has("input"))inputs={"input"};
            for(size_t i=0;i<inputs.size();++i)if(auto* source=node(graph(),n.getStr(inputs[i]))){auto a=position(*source);a.x+=170*zoom_;a.y+=18*zoom_;ImVec2 b{p.x,p.y+(50+float(i)*22)*zoom_};dl->AddBezierCubic(a,{a.x+50,a.y},{b.x-50,b.y},b,ImGui::GetColorU32(ImGuiCol_PlotLines),2);}
            float h=(70+float(inputs.size())*22)*zoom_;dl->AddRectFilled(p,{p.x+170*zoom_,p.y+h},ImGui::GetColorU32(ImGuiCol_FrameBg),6);dl->AddRect(p,{p.x+170*zoom_,p.y+h},ImGui::GetColorU32(n.getStr("id")==selected_?ImGuiCol_ButtonActive:ImGuiCol_Border),6);dl->AddText({p.x+6,p.y+6},ImGui::GetColorU32(ImGuiCol_Text),n.getStr("id").c_str());dl->AddText({p.x+6,p.y+27*zoom_},ImGui::GetColorU32(ImGuiCol_TextDisabled),k.c_str());
            ImGui::PushID(n.getStr("id").c_str());ImGui::SetCursorScreenPos(p);ImGui::InvisibleButton("title",{145*zoom_,30*zoom_});if(ImGui::IsItemClicked()){selected_=n.getStr("id");}if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){if(!dragging_)remember();dragging_=true;Json pos=Json::array();pos.push(Json::number(n["position"][0].toDouble()+ImGui::GetIO().MouseDelta.x/zoom_));pos.push(Json::number(n["position"][1].toDouble()+ImGui::GetIO().MouseDelta.y/zoom_));n.set("position",pos);}
            ImGui::SetCursorScreenPos({p.x+150*zoom_,p.y+8*zoom_});ImGui::InvisibleButton("out",{20*zoom_,20*zoom_});dl->AddCircleFilled({p.x+160*zoom_,p.y+18*zoom_},5,ImGui::GetColorU32(ImGuiCol_PlotLines));if(ImGui::BeginDragDropSource()){auto id=n.getStr("id");ImGui::SetDragDropPayload("ANIM_NODE",id.c_str(),id.size()+1);ImGui::TextUnformatted(id.c_str());ImGui::EndDragDropSource();}
            for(size_t i=0;i<inputs.size();++i){ImGui::SetCursorScreenPos({p.x,p.y+(40+float(i)*22)*zoom_});ImGui::PushID(int(i));ImGui::InvisibleButton("in",{80*zoom_,20*zoom_});dl->AddCircleFilled({p.x+5,p.y+(50+float(i)*22)*zoom_},5,ImGui::GetColorU32(ImGuiCol_PlotLines));dl->AddText({p.x+14,p.y+(42+float(i)*22)*zoom_},ImGui::GetColorU32(ImGuiCol_Text),inputs[i].c_str());if(ImGui::BeginDragDropTarget()){if(auto* data=ImGui::AcceptDragDropPayload("ANIM_NODE")){from=static_cast<const char*>(data->Data);to=n.getStr("id");port=inputs[i];}ImGui::EndDragDropTarget();}ImGui::PopID();}ImGui::PopID();
        }
        if(!from.empty()){Json before=doc_;try{animation::connect(graph(),from,to,port);remember(before);}catch(const std::exception& e){error_=e.what();}}
        ImGui::SetCursorScreenPos({origin.x+8,origin.y+avail.y-22});ImGui::TextUnformatted(text("canvasHelp").c_str());
    }
    void preview(Editor& ed){
        ready_=false;std::string path=doc_.getStr("model");std::string modelKey=project_+"/"+path;
        try{
            if(modelKey_!=modelKey){release();modelKey_=modelKey;dev_=&ed.device();model_=loadAnimationModel(ed.ws(),path);if(!render::uploadModel(*dev_,*model_,true))throw std::runtime_error("model upload failed");if(!portrait_.init(*dev_,320))throw std::runtime_error("preview initialization failed");}
            if(!model_)return;
            std::string key=graph().dump(-1)+doc_["graphs"].dump(-1);if(programKey_!=key){programKey_=key;render::AnimationGraphs graphs;for(auto& [name,g]:doc_["graphs"].members())graphs[name]=g;auto previewGraph=graph();animation::sort(previewGraph);instance_.reset(render::AnimationGraph::compile(*model_,previewGraph,{}, {},graphs));controls_=instance_.parameters();error_.clear();}
            if(!instance_.graph())return;
            ImGui::Checkbox(text("play").c_str(),&playing_);ImGui::SameLine();if(ImGui::Button(text("reset").c_str())){programKey_.clear();}
            ImGui::SameLine();ImGui::SliderFloat("##yaw",&yaw_,-3.14f,3.14f);if(instance_.hasContacts()){ImGui::SetNextItemWidth(130);ImGui::DragFloat(text("previewGround").c_str(),&previewGround_,.01f);auto env=std::make_shared<render::AnimationEnvironment>();float ground=previewGround_;env->sphereContact=[ground](render::float3 p,float r,render::IKHit& h){return render::ikSpherePlane(p,r,{0,ground,0},{0,1,0},h);};env->raycast=[ground](render::float3 a,render::float3 b,render::IKHit& h){float d=b.y-a.y;if(std::abs(d)<1e-8f)return false;float t=(ground-a.y)/d;if(t<0||t>1)return false;h={a+(b-a)*t,{0,1,0},0,1};return true;};controls_.environment=env;}
            instance_.parameters()=controls_;if(playing_)instance_.update(dt_);instance_.evaluate(pose_);if(!instance_.healthy())error_=instance_.error();
            if(instance_.locomotionReport().active){const auto c=instance_.locomotionReport();ImGui::TextWrapped(text("climbReport").c_str(),c.supports,c.stepping,c.footError);ImGui::TextUnformatted(text(c.attached?"attached":"detached").c_str());}
            else if(instance_.hasContacts()){const auto r=instance_.contactReport();ImGui::TextUnformatted(text(r.stable?"stable":r.supported?"unstable":"unsupported").c_str());ImGui::TextWrapped(text("contactReport").c_str(),r.supports,r.supportError,r.penetration,r.balanceError);}
            auto displayPose=pose_;auto climb=instance_.locomotionReport();if(climb.active){auto center=(model_->boundsMin+model_->boundsMax)*.5f;auto shift=climb.position-center;for(size_t i=0;i<displayPose.size();++i)if(model_->nodes[i].parent<0)displayPose[i].t=displayPose[i].t-shift;}portrait_.renderPose(*model_,displayPose,yaw_);ready_=portrait_.texture().valid()&&instance_.healthy();
            float size=std::min({230.f,ImGui::GetContentRegionAvail().x*.45f,std::max(80.f,ImGui::GetContentRegionAvail().y-26.f)});auto flip=ed.device().info().renderTargetFlipY;
            ImGui::Image(ImTextureRef(ImTextureID(portrait_.texture().id)),{size,size},{0,flip?0.f:1.f},{1,flip?1.f:0.f});ImGui::SameLine();skeleton({std::max(80.f,ImGui::GetContentRegionAvail().x),size});
            const char* planes[]={"XY","XZ","YZ"};ImGui::SetNextItemWidth(100);ImGui::Combo("##plane",&plane_,planes,3);ImGui::SameLine();
            if(ImGui::Button(text("keepControls").c_str())){remember();auto& params=graph().member("parameters");for(auto& [name,v]:controls_.values)params.set(name,Json::number(double(v),6));for(auto& [name,v]:controls_.vectors){Json a=Json::array();for(float f:{v.x,v.y,v.z})a.push(Json::number(double(f),6));params.set(name,a);}}
            for (auto &[name, v] : controls_.values)
              ImGui::DragFloat(name.c_str(), &v, .01f);
            for (auto &[name, v] : controls_.vectors) {
              float f[] = {v.x, v.y, v.z};
              if (ImGui::DragFloat3(name.c_str(), f, .01f))
                v = {f[0], f[1], f[2]};
            }
        }catch(const std::exception& e){error_=e.what();instance_.reset({});}
    }
    void skeleton(ImVec2 size){
        auto origin=ImGui::GetCursorScreenPos();ImGui::BeginChild("skeleton",size,ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);auto* dl=ImGui::GetWindowDrawList();std::vector<render::mat4> world;render::worldMatrices(*model_,pose_,world);
        auto center=(model_->boundsMin+model_->boundsMax)*.5f;if(instance_.locomotionReport().active)center=instance_.locomotionReport().position;auto extent=model_->boundsMax-model_->boundsMin;float scale=std::min(size.x,size.y)*.7f/std::max({extent.x,extent.y,extent.z,.1f});
        auto uv=[&](render::float3 v){return ImVec2(plane_==2?v.y:v.x,plane_==0?v.y:v.z);};auto c=uv(center);auto point=[&](render::float3 v){auto a=uv(v);return ImVec2(origin.x+size.x*.5f+(a.x-c.x)*scale,origin.y+size.y*.5f-(a.y-c.y)*scale);};
        for(size_t i=0;i<world.size();++i){render::float3 p{world[i].at(0,3),world[i].at(1,3),world[i].at(2,3)};auto screen=point(p);int parent=model_->nodes[i].parent;if(parent>=0){auto& m=world[size_t(parent)];auto from=point({m.at(0,3),m.at(1,3),m.at(2,3)});dl->AddLine(from,screen,ImGui::GetColorU32(ImGuiCol_TextDisabled),2);}dl->AddCircleFilled(screen,3,ImGui::GetColorU32(ImGuiCol_Text));}
        if(instance_.hasContacts())dl->AddLine(point({-2,previewGround_,0}),point({2,previewGround_,0}),ImGui::GetColorU32(ImGuiCol_PlotHistogram),2);
        if(instance_.graph())for(const auto& node:instance_.graph()->nodes())if(node.rig)for(const auto& unit:node.rig->units())if(unit.locomotion||(unit.fullBody&&unit.fullBody->contacts())){
            const auto& contacts=unit.locomotion?unit.locomotion->surfaces():*unit.fullBody->contacts();const auto color=ImGui::GetColorU32(ImGuiCol_PlotHistogram);
            for(const auto& shape:contacts.shapes()){auto v=shape.point;auto it=controls_.vectors.find(shape.positionParameter);if(it!=controls_.vectors.end())v=it->second;
                if(shape.kind=="sphere")dl->AddCircle(point(v),shape.radius*scale,color,32,2);
                else if(shape.kind=="box"){render::float3 vertices[8];for(int i=0;i<8;++i)vertices[i]=v+render::float3{(i&1)?shape.extent.x:-shape.extent.x,(i&2)?shape.extent.y:-shape.extent.y,(i&4)?shape.extent.z:-shape.extent.z};for(int i=0;i<8;++i)for(int bit:{1,2,4})if(!(i&bit))dl->AddLine(point(vertices[i]),point(vertices[i|bit]),color,1);}
                else {v=center-shape.normal*render::dot(center-v,shape.normal);auto axis=render::normalize(render::cross(shape.normal,std::abs(shape.normal.z)<.9f?render::float3{0,0,1}:render::float3{1,0,0}));auto other=render::cross(shape.normal,axis);for(auto d:{axis,other})dl->AddLine(point(v-d*2),point(v+d*2),color,2);}
            }
            for(const auto& capsule:contacts.capsules()){auto start=world[size_t(capsule.bone)].transformPoint(capsule.offset),end=capsule.end>=0?world[size_t(capsule.end)].transformPoint({}):start;dl->AddLine(point(start),point(end),color,std::max(1.f,capsule.radius*scale*2));dl->AddCircle(point(start),capsule.radius*scale,color);dl->AddCircle(point(end),capsule.radius*scale,color);}
            for(const auto& foot:contacts.feet()){auto v=world[size_t(foot.bone)].transformPoint(foot.offset);dl->AddCircle(point(v),foot.radius*scale,ImGui::GetColorU32(ImGuiCol_PlotLines),16,2);}
        }
        for(auto& [name,v]:controls_.vectors){auto p=point(v);dl->AddCircle(p,7,ImGui::GetColorU32(ImGuiCol_PlotLines),12,2);dl->AddText({p.x+8,p.y},ImGui::GetColorU32(ImGuiCol_Text),name.c_str());ImGui::SetCursorScreenPos({p.x-8,p.y-8});ImGui::PushID(name.c_str());ImGui::InvisibleButton("control",{16,16});if(ImGui::IsItemActive()&&ImGui::IsMouseDragging(0)){auto d=ImGui::GetIO().MouseDelta;if(plane_==2)v.y+=d.x/scale;else v.x+=d.x/scale;if(plane_==0)v.y-=d.y/scale;else v.z-=d.y/scale;}ImGui::PopID();}ImGui::EndChild();
    }
    bool dragging_=false,rigMode_=false,registered_=false,dirty_=false,ready_=false,playing_=true;
    Workspace* ws_=nullptr;gfx::Device* dev_=nullptr;float previewGround_=0,dt_=0,yaw_=.5f,zoom_=1;int plane_=0,unit_=0;ImVec2 pan_{};
    Json doc_=animation::makeGraph(),saved_=doc_;std::vector<Json> undo_,redo_;struct Draft{Json document,saved;std::string name;bool dirty=false;};std::map<std::string,Draft> drafts_;std::map<ImGuiID,std::string> fields_;
    std::string project_,name_="untitled",scope_,subName_="subgraph",selected_,target_,modelKey_,programKey_,error_,message_;
    std::unique_ptr<render::Model> model_;render::AnimationGraphInstance instance_;render::AnimationParameters controls_;render::Pose pose_;game::Portrait portrait_;
};
}
