#include "../Editor.h"
#include "../EditorUtil.h"
#include "../EditorGenreUi.h"
namespace aiverse::editor {
class FpsWindow : public EditorWindow {
public:
    const char* id()const override{return "fps";}
    const char* title()const override{return "FPS";}
    const char* group()const override{return "logic";}
    const char* dockArea()const override{return "right-bottom";}
    bool openByDefault()const override{return false;}
    void draw(Editor& ed)override {
        auto text=[&](const char* key){return genreText(ed.ws(),key);};
        if(!ed.ws().project.isFps()){ImGui::TextWrapped("%s",text("fps.editor.only").c_str());return;}
        if(project_!=ed.ws().project.dir||path_!=ed.doc().path){project_=ed.ws().project.dir;path_=ed.doc().path;rules_=ed.ws().project.rulesJson.dump();map_=ed.doc().toJson().dump();error_.clear();}
        ImGui::TextWrapped("%s",text("fps.editor.hint").c_str());
        ImGui::BeginDisabled(ed.ws().building());
        if(ImGui::Button(text("fps.editor.play").c_str()))ed.startFpsPlay();
        ImGui::SameLine();if(ImGui::Button(text("fps.editor.listen").c_str()))ed.startFpsPlay(true);
        ui::inputString(text("fps.editor.address").c_str(),address_);
        if(ImGui::Button(text("fps.editor.connect").c_str()))ed.startFpsPlay(false,address_);
        ImGui::EndDisabled();
        ImGui::Separator();
        try {
            auto rules=Json::parse(rules_);auto map=Json::parse(map_);bool changed=false,mapChanged=false;
            auto integer=[&](Json& j,const char* field,const char* label,int fallback,int lo,int hi) {
                int value=int(j.getInt(field,fallback));if(ImGui::SliderInt(text(label).c_str(),&value,lo,hi)){j.set(field,Json::number(int64_t(value)));changed=true;}
            };
            auto& cfg=rules.member("fps");
            if(ImGui::CollapsingHeader(text("fps.settings.simulation").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
                integer(cfg,"tickRate","fps.settings.tick",60,20,120);
                integer(cfg,"snapshotRate","fps.settings.snapshots",20,1,int(cfg.getInt("tickRate",60)));
                integer(cfg,"maxPlayers","fps.settings.players",100,1,200);
            }
            if(ImGui::CollapsingHeader(text("fps.settings.weapon").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
                auto editWeapon=[&](Json& weapon) {
                    integer(weapon,"damage","fps.settings.damage",25,1,1000);
                    integer(weapon,"magazine","fps.settings.magazine",30,1,1000);
                };
                if(cfg["weapons"].isArray()) {
                    int index=0;
                    for(auto& weapon:cfg.member("weapons").itemsMut()) {
                        ImGui::PushID(index++);ImGui::TextUnformatted(text(weapon.getStr("nameKey","fps.weapon.rifle").c_str()).c_str());editWeapon(weapon);ImGui::PopID();
                    }
                }else editWeapon(cfg.member("weapon"));
            }
            if(ImGui::CollapsingHeader(text("fps.settings.movement").c_str())) {
                auto& motor=cfg.member("motor");float speed=float(motor.getFx("speed",5_fx).toDouble());
                if(ImGui::SliderFloat(text("fps.settings.speed").c_str(),&speed,1,30)){motor.set("speed",Json::number(double(speed)));if(motor.getFx("sprintSpeed",8_fx).toDouble()<speed)motor.set("sprintSpeed",Json::number(double(speed)));changed=true;}
            }
            if(ImGui::CollapsingHeader(text("fps.settings.geometry").c_str())) {
                auto& colliders=map.member("colliders");
                if(colliders.size()) {
                    collider_=std::clamp(collider_,0,int(colliders.size())-1);
                    ImGui::SliderInt(text("fps.settings.collider").c_str(),&collider_,0,int(colliders.size())-1);
                    auto& box=colliders.at(size_t(collider_));
                    for(const char* field:{"min","max"}) {
                        float xyz[3];for(size_t i=0;i<3;++i)xyz[i]=float(box[field][i].toDouble());
                        if(ImGui::DragFloat3(text(field==std::string("min")?"fps.settings.min":"fps.settings.max").c_str(),xyz,0.1f)) {
                            Json vector=Json::array();for(float coordinate:xyz)vector.push(Json::number(double(coordinate)));box.set(field,vector);mapChanged=true;
                        }
                    }
                }
            }
            if(changed)rules_=rules.dump();if(mapChanged)map_=map.dump();
        }catch(const std::exception&) { /* JSON editor below keeps malformed drafts editable. */ }
        if(ImGui::CollapsingHeader(text("fps.settings.advanced").c_str())) {
            ImGui::TextUnformatted(text("fps.editor.rules").c_str());ui::inputMultiline("##fpsRules",rules_,ImVec2(-1,170));
            ImGui::TextUnformatted(text("fps.editor.map").c_str());ui::inputMultiline("##fpsMap",map_,ImVec2(-1,200));
        }
        if(ImGui::Button(text("fps.editor.apply").c_str())) {
            try {
                Json args=Json::object();args.set("rules",Json::parse(rules_));args.set("map",Json::parse(map_));auto result=ed.ws().call("fps_set",args);
                error_=result.getStr("error");if(error_.empty()){rules_=ed.ws().project.rulesJson.dump();map_=ed.doc().toJson().dump();}
            }catch(const std::exception& e){error_=e.what();}
        }
        if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
    }
private:int collider_=0;std::string project_,path_,rules_,map_,error_,address_="udp://127.0.0.1:27100";
};
AIVERSE_EDITOR_WINDOW(FpsWindow);
}
