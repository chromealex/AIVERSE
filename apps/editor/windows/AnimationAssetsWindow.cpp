#include "../Editor.h"
#include "../EditorUtil.h"
#include "libraries/include/aiverselib/motionweave/MotionWeave.h"
#include "libraries/include/aiverselib/animation/HumanoidProfile.h"
#include <algorithm>
#include <map>
#include <string>
namespace aiverse::editor {
// GUI and MCP both use the same Workspace operations, so generated assets
// share validation, project containment and optimistic-concurrency rules.
class AnimationAssetsWindow final : public EditorWindow {
public:
    const char* id() const override{return "animation_assets";}
    const char* title() const override{return "MotionWeave — Аватары и монтаж";}
    const char* group() const override{return "look";}
    const char* dockArea() const override{return "right";}
    bool openByDefault() const override{return false;}
    void update(Editor& ed,double) override {
        if(registered_)return;
        registered_=true;
        ed.ws().registerOp({"animation_assets_open",
            "Open Avatar, Montage, Ragdoll and Root Motion asset editor.",
            Json::object(),[&ed](const Json&){
                ed.focusWindow("animation_assets");
                Json result=Json::object();
                result.set("ok",Json::fromBool(true));return result;
            },true});
    }
    void draw(Editor& ed) override {
        if(ed.ws().project.dir.empty()){
            ImGui::TextUnformatted("Откройте проект для работы с анимациями.");
            return;
        }
        if(project_!=ed.ws().project.dir) {
            project_=ed.ws().project.dir;
            avatar_=ragdoll_=root_=montage_=Json{};
            saved_.clear();message_.clear();
        }
        ImGui::TextUnformatted("Модель (относительно проекта)");
        ui::inputString("##animation-model",model_);
        ImGui::TextUnformatted("Имя ассета");
        ui::inputString("##animation-name",name_);
        if(ImGui::BeginTabBar("AnimationAuthorTabs")) {
            if(ImGui::BeginTabItem("Humanoid Avatar")) {
                active_="avatar";drawAvatar(ed);ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Ragdoll")) {
                active_="ragdoll";drawRagdoll(ed);ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Root Motion")) {
                active_="root_motion";drawRoot(ed);ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Montage")) {
                active_="montage";drawMontage(ed);ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if(ImGui::Button("Открыть"))load(ed);
        ImGui::SameLine();
        if(ImGui::Button("Сохранить"))save(ed);
        if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
        auto* doc=activeDocument();
        if(doc && doc->isObject() && ImGui::CollapsingHeader("JSON-предпросмотр")) {
            const auto preview=doc->dump(2);
            ImGui::BeginChild("animation-json-preview",ImVec2(0,220),ImGuiChildFlags_Borders);
            ImGui::TextUnformatted(preview.c_str());
            ImGui::EndChild();
        }
    }
private:
    void call(Editor& ed,const std::string& op,Json p,Json* result=nullptr){
        Json r=ed.ws().call(op,p);
        if(r.has("error")){message_=r.getStr("error");return;}
        if(result)*result=r["document"];
        message_="Готово";
    }
    Json* activeDocument(){
        if(active_=="avatar")return &avatar_;
        if(active_=="ragdoll")return &ragdoll_;
        if(active_=="root_motion")return &root_;
        if(active_=="montage")return &montage_;
        return nullptr;
    }
    void load(Editor& ed){
        Json req=Json::object();
        req.set("kind",Json::string(active_));
        req.set("name",Json::string(name_));
        Json result=ed.ws().call("animation_asset_get",req);
        if(result.has("error")){message_=result.getStr("error");return;}
        if(auto* doc=activeDocument()){
            *doc=result["document"];
            saved_[active_]=*doc;
            if(active_=="avatar"||active_=="montage")
                model_=doc->getStr("model",model_);
        }
        message_="Ассет загружен";
    }
    void save(Editor& ed){
        Json* doc=activeDocument();
        if(!doc||!doc->isObject()){message_="Сначала сгенерируйте ассет";return;}
        Json req=Json::object();
        req.set("kind",Json::string(active_));
        req.set("name",Json::string(name_));
        req.set("document",*doc);
        if(const auto it=saved_.find(active_);it!=saved_.end())
            req.set("expected",it->second);
        Json r=ed.ws().call("animation_asset_save",req);
        if(r.has("error")){message_=r.getStr("error");return;}
        saved_[active_]=*doc;message_="Ассет сохранён в animations/"+active_+"/";
    }
    void drawAvatar(Editor& ed){
        if(ImGui::Button("Предложить соответствия костей")) {
            Json req=Json::object();req.set("model",Json::string(model_));
            call(ed,"animation_avatar_suggest",req,&avatar_);
        }
        if(!avatar_.isObject()){
            ImGui::TextWrapped("Загрузите скелет и проверьте предложенные соответствия.");
            return;
        }
        avatar_.set("model",Json::string(model_));
        ImGui::TextWrapped("Автоматические имена проверяйте вручную; для Humanoid нужны Hips и Head.");
        ImGui::BeginChild("avatar-bones",ImVec2(0,320),ImGuiChildFlags_Borders);
        for(const auto* role:motionweave::kHumanoidRoleNames) {
            ImGui::PushID(role);
            ImGui::TextUnformatted(role);
            std::string value=avatar_["bones"].getStr(role);
            if(ui::inputString("##avatar-role",value)) {
                if(value.empty())avatar_.member("bones").erase(role);
                else avatar_.member("bones").set(role,Json::string(value));
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if(ImGui::Button("Проверить аватар")) {
            Json req=Json::object();req.set("document",avatar_);
            call(ed,"animation_avatar_validate",req);
        }
    }
    void drawRagdoll(Editor& ed){
        ImGui::TextWrapped("Генерация Box3D рэгдолла по текущему Humanoid Avatar.");
        ImGui::SliderFloat("Радиус частей", &radius_,.05f,.5f);
        ImGui::SliderFloat("Плотность",&density_,50.f,1800.f);
        ImGui::SliderFloat("Трение",&friction_,0.f,2.f);
        ImGui::SliderFloat("Лимит шарнира",&limit_,.1f,2.5f);
        if(ImGui::Button("Сгенерировать физический риг")){
            Json options=Json::object();
            options.set("radiusRatio",Json::number(double(radius_)));
            options.set("density",Json::number(double(density_)));
            options.set("friction",Json::number(double(friction_)));
            options.set("jointLimit",Json::number(double(limit_)));
            Json p=Json::object();p.set("profile",avatar_);p.set("options",options);
            call(ed,"animation_ragdoll_generate",p,&ragdoll_);
        }
        if(ragdoll_.isObject()){
            ImGui::Text("Частей: %zu, шарниров: %zu",
                ragdoll_["bodies"].size(),ragdoll_["joints"].size());
        }
    }
    void drawRoot(Editor& ed){
        ImGui::TextUnformatted("Клип");ui::inputString("##root-clip",clip_);
        ImGui::TextUnformatted("Корневая кость (полный путь)");
        ui::inputString("##root-bone",rootBone_);
        ImGui::InputInt("ID клипа",&clipId_);
        ImGui::SliderInt("Тиков/сек",&rate_,10,240);
        ImGui::Checkbox("Зациклить",&loop_);
        if(ImGui::Button("Запечь фиксированные кадры")) {
            Json req=Json::object();
            req.set("model",Json::string(model_));
            req.set("clip",Json::string(clip_));
            req.set("root",Json::string(rootBone_));
            req.set("id",Json::number(int64_t(std::max(1,clipId_))));
            req.set("rate",Json::number(int64_t(rate_)));
            req.set("loop",Json::fromBool(loop_));
            call(ed,"animation_root_motion_generate",req,&root_);
        }
        if(root_.isObject()&&root_["tracks"].size())
            ImGui::Text("Кадров: %zu",root_["tracks"][0]["frames"].size());
    }
    void drawMontage(Editor& ed){
        if(!montage_.isObject()){
            if(ImGui::Button("Новый Montage")) {
                montage_=Json::object();
                montage_.set("model",Json::string(model_));
                montage_.set("slot",Json::string("UpperBody"));
                montage_.set("blendIn",Json::number(.1));
                montage_.set("blendOut",Json::number(.1));
                montage_.set("sections",Json::array());
            }
            return;
        }
        montage_.set("model",Json::string(model_));
        std::string slot=montage_.getStr("slot","UpperBody");
        if(ui::inputString("Слот",slot))montage_.set("slot",Json::string(slot));
        std::string mask=montage_.getStr("maskRoot");
        if(ui::inputString("Маска кости",mask))montage_.set("maskRoot",Json::string(mask));
        float fadeIn=float(montage_["blendIn"].toDouble(.1));
        float fadeOut=float(montage_["blendOut"].toDouble(.1));
        if(ImGui::DragFloat("Blend In",&fadeIn,.01f,0.f,10.f))
            montage_.set("blendIn",Json::number(double(fadeIn),4));
        if(ImGui::DragFloat("Blend Out",&fadeOut,.01f,0.f,10.f))
            montage_.set("blendOut",Json::number(double(fadeOut),4));
        if(ImGui::Button("+ Секция")){
            Json s=Json::object();
            s.set("name",Json::string("Section"+std::to_string(montage_["sections"].size())));
            s.set("clip",Json::string(clip_));
            s.set("begin",Json::number(0.));
            s.set("end",Json::number(1.));
            s.set("next",Json::number(int64_t(-1)));
            s.set("notifies",Json::array());
            montage_.member("sections").push(s);
        }
        ImGui::BeginChild("montage-sections",ImVec2(0,310),ImGuiChildFlags_Borders);
        auto& sections=montage_.member("sections");
        for(size_t i=0;i<sections.size();++i){
            ImGui::PushID(int(i));
            auto& s=sections.at(i);
            ImGui::Separator();
            std::string sectionName=s.getStr("name");
            if(ui::inputString("Секция",sectionName))
                s.set("name",Json::string(sectionName));
            std::string clip=s.getStr("clip");
            if(ui::inputString("Клип",clip))s.set("clip",Json::string(clip));
            float start=float(s["begin"].toDouble());
            float end=float(s["end"].toDouble(1.));
            if(ImGui::DragFloat("Начало",&start,.01f,0.f,600.f))
                s.set("begin",Json::number(double(start),4));
            if(ImGui::DragFloat("Конец",&end,.01f,0.f,600.f))
                s.set("end",Json::number(double(end),4));
            int next=int(s.getInt("next",-1));
            if(ImGui::InputInt("Следующая (-1 = конец)",&next))
                s.set("next",Json::number(int64_t(next)));
            if(ImGui::Button("+ Notify")) {
                Json n=Json::object();
                n.set("name",Json::string("event"));
                n.set("time",Json::number(0.));
                s.member("notifies").push(n);
            }
            auto& notifies=s.member("notifies");
            for(size_t n=0;n<notifies.size();++n) {
                ImGui::PushID(int(n));
                auto& item=notifies.at(n);
                std::string label=item.getStr("name");
                if(ui::inputString("Notify",label))item.set("name",Json::string(label));
                float time=float(item["time"].toDouble());
                if(ImGui::DragFloat("Время",&time,.01f,0.f,600.f))
                    item.set("time",Json::number(double(time),4));
                ImGui::PopID();
            }
            if(ImGui::Button("Удалить секцию")){
                sections.eraseAt(i);
                for(auto& x:sections.itemsMut())x.set("next",Json::number(int64_t(-1)));
                ImGui::PopID();break;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if(ImGui::Button("Проверить Montage")){
            Json request=Json::object();request.set("document",montage_);
            call(ed,"animation_montage_validate",request);
        }
    }
    bool registered_=false;
    std::string project_,model_="models/hero.glb",name_="character";
    std::string active_="avatar",message_,clip_="Walk",rootBone_="root";
    Json avatar_,ragdoll_,root_,montage_;
    std::map<std::string,Json> saved_;
    float radius_=.17f,density_=750.f,friction_=.65f,limit_=.85f;
    int clipId_=1,rate_=60;bool loop_=false;
};
AIVERSE_EDITOR_WINDOW(AnimationAssetsWindow);
} // namespace aiverse::editor
