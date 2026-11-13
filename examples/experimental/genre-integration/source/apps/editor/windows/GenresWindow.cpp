#include "../Editor.h"
#include "../EditorUtil.h"
#include "../EditorGenreUi.h"
#include "modules/genres/BoardModule.h"
#include "modules/genres/MotionModule.h"
namespace aiverse::editor {
class GenresWindow : public EditorWindow {
    std::string project_,rules_,map_,message_;
public:
    const char* id() const override{return "genres";}
    const char* title() const override{return "Genres";}
    const char* group() const override{return "logic";}
    const char* dockArea() const override{return "center";}
    bool openByDefault() const override{return false;}
    void draw(Editor& ed) override {
        auto& ws=ed.ws();auto tr=[&](const char* key){return genreText(ws,key);};
        if(!ws.project.isGenre()){ImGui::TextUnformatted(tr("genres.editor.only").c_str());return;}
        auto identity=ws.project.dir+"\n"+ed.doc().path;
        if(project_!=identity){project_=identity;rules_=ws.project.rulesJson.dump(2);map_=ed.doc().toJson().dump(2);message_.clear();}
        ImGui::TextWrapped("%s",tr("genres.editor.hint").c_str());
        if(ImGui::Button(tr("genres.editor.reload").c_str()))project_.clear();
        ImGui::SameLine();
        if(ImGui::Button(tr("genres.editor.apply").c_str())) {
            try {
                auto rules=Json::parse(rules_),map=Json::parse(map_);
                auto kind=ws.project.manifest.getStr("module");map.set("module",Json::string(kind));
                std::unique_ptr<GameModule> module;
                if(kind=="board")module=std::make_unique<genres::BoardModule>();else module=std::make_unique<genres::MotionModule>();
                Simulation check({17,60,0},std::move(module));check.setup(rules,map);
                std::string error;
                // Both documents are validated before either live document changes.
                if(!ed.doc().loadJson(map,&error))throw JsonError(error);
                ed.doc().dirty=true;ws.project.rulesJson=rules;ws.project.rulesDirty=true;
                if(!ws.project.saveRules(&error)||!ed.saveMap(ed.doc().path))throw JsonError(error);
                message_=tr("genres.editor.saved");
            }catch(const std::exception& e){message_=e.what();}
        }
        ImGui::SameLine();ImGui::BeginDisabled(ws.building());
        if(ImGui::Button(tr("genres.editor.play").c_str()))ed.startFpsPlay();
        ImGui::EndDisabled();ImGui::TextWrapped("%s",message_.c_str());
        if(ImGui::BeginTabBar("genre-data")) {
            if(ImGui::BeginTabItem(tr("genres.editor.rules").c_str())){ui::inputMultiline("##genre-rules",rules_,ImVec2(-1,-1));ImGui::EndTabItem();}
            if(ImGui::BeginTabItem(tr("genres.editor.map").c_str())){ui::inputMultiline("##genre-map",map_,ImVec2(-1,-1));ImGui::EndTabItem();}
            ImGui::EndTabBar();
        }
    }
};
AIVERSE_EDITOR_WINDOW(GenresWindow);
}
