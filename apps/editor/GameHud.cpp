#include "GameHud.h"
#include "Editor.h"
#include "apps/client/RegionSettings.h"
#include "engine/i18n/GameLanguage.h"
#include "modules/rts/view/ScriptHud.h"
#include "modules/rts/view/WorldCounters.h"
#include <cstdio>

namespace rts::editor {
using client::UiAction;
using i18n::Args;

void GameHud::reset() {
    ui_.shutdown(); model_ = {}; bound_ = {}; project_.clear(); actions_.clear();
    blocked_ = false; pointAbility_ = -1; request_ = 0; howto_ = false;
    for (auto& p : pressed_) p = false;
}

int GameHud::action(std::function<void()> fn, bool enabled) {
    actions_.push_back(enabled ? std::move(fn) : std::function<void()>{});
    return int(actions_.size()) - 1;
}

void GameHud::init(Editor& ed) {
    reset(); project_ = ed.ws().project.dir;
    auto read = [project=project_, engine=ed.ws().engineDir](const std::string& path, std::string& out) {
        if (path.starts_with("engine/")) return platform::readContentFile(engine + "/engine/assets/" + path.substr(7), out);
        return platform::readContentFile(project + "/" + path, out);
    };
    const auto languages = i18n::readGameLanguages(ed.ws().project.manifest);
    i18n::loadGameLocalization(loc_, read, languages, i18n::chooseLanguage(languages, i18n::readSavedLanguage(platform::userDataDir())));
    if (!ui_.init(ed.device(), "fonts/ui.ttf", read)) return;
    ui_.setOnScreenKeyboard(rts::ui::UiLayer::OnScreenKeyboard::Never);
    ui_.setTranslator([this](const std::string& key) { return loc_.tr(key); });
    auto c = ui_.context()->CreateDataModel("game");
    client::bindUiState(c, bound_);
    c.RegisterTransformFunc("tr", [this](const Rml::VariantList& a) -> Rml::Variant {
        return a.empty() ? Rml::Variant() : Rml::Variant(loc_.tr(a[0].Get<Rml::String>()));
    });
    shadows_ = int(ed.renderer().shadowQuality()); materials_ = int(ed.renderer().materialQuality()); particles_ = int(ed.gameFx().quality());
    c.Bind("shadows", &shadows_); c.Bind("materials", &materials_); c.Bind("particles", &particles_);
    c.Bind("volume", &volume_); c.Bind("music", &music_);
    using ArgsList = const Rml::VariantList&;
    auto invoke = [this](ArgsList a) {
        int id = a.empty() ? -1 : a[0].Get<int>(-1);
        if (id >= 0 && size_t(id) < actions_.size() && actions_[size_t(id)]) actions_[size_t(id)]();
    };
    c.BindEventCallback("act", [invoke](Rml::DataModelHandle, Rml::Event&, ArgsList a) { invoke(a); });
    c.BindEventCallback("skill_drag", [invoke](Rml::DataModelHandle, Rml::Event& e, ArgsList a) {
        if (a.size() >= 2 && a[1].Get<float>(-1) >= 0 && e.GetParameter<int>("button", 0) == 0) invoke(a);
    });
    c.BindEventCallback("support_click", [invoke](Rml::DataModelHandle, Rml::Event&, ArgsList a) {
        if (a.size() >= 2 && a[1].Get<float>(-1) < 0) invoke(a);
    });
    c.BindEventCallback("pause", [&ed](Rml::DataModelHandle, Rml::Event&, ArgsList) { ed.playPaused = true; });
    c.BindEventCallback("resume", [&ed](Rml::DataModelHandle, Rml::Event&, ArgsList) { ed.playPaused = false; });
    c.BindEventCallback("restart", [this](Rml::DataModelHandle, Rml::Event&, ArgsList) { request_ = 1; });
    c.BindEventCallback("tomenu", [this](Rml::DataModelHandle, Rml::Event&, ArgsList) { request_ = 2; });
    c.BindEventCallback("howto", [this](Rml::DataModelHandle, Rml::Event&, ArgsList) { howto_ = !howto_; });
    model_ = c.GetModelHandle();
    if (auto* doc = ui_.loadDocument("hud.rml")) {
        // The editor play-test has no audio mixer. Keep project markup, but do not offer dead controls.
        Rml::ElementList buttons; doc->QuerySelectorAll(buttons, "button");
        for (auto* button : buttons) {
            const auto event = button->GetAttribute<Rml::String>("data-event-click", "");
            if (event.starts_with("volume =") || event.starts_with("music =")) {
                button->SetAttribute("disabled", true); button->SetClass("disabled", true);
            }
        }
    }
    ui_.loadDocument("howto.rml");
}

void GameHud::input(const Viewport& vp) {
    auto* c = ui_.context(); if (!c) return;
    const auto& io = ImGui::GetIO();
    const bool inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        io.MousePos.x >= vp.originX && io.MousePos.x < vp.originX + vp.pointsW &&
        io.MousePos.y >= vp.originY && io.MousePos.y < vp.originY + vp.pointsH;
    const int mods = (io.KeyCtrl ? Rml::Input::KM_CTRL : 0) | (io.KeyShift ? Rml::Input::KM_SHIFT : 0) |
        (io.KeyAlt ? Rml::Input::KM_ALT : 0) | (io.KeySuper ? Rml::Input::KM_META : 0);
    bool captured = false;
    if (inside || pressed_[0] || pressed_[1] || pressed_[2])
        c->ProcessMouseMove(int((io.MousePos.x - vp.originX) * vp.w / vp.pointsW), int((io.MousePos.y - vp.originY) * vp.h / vp.pointsH), mods);
    else c->ProcessMouseLeave();
    for (int b = 0; b < 3; ++b) {
        const int rmlButton = b;
        if (inside && ImGui::IsMouseClicked(b)) { pressed_[b] = true; captured |= !c->ProcessMouseButtonDown(rmlButton, mods); }
        if (pressed_[b] && ImGui::IsMouseReleased(b)) { captured |= !c->ProcessMouseButtonUp(rmlButton, mods); pressed_[b] = false; }
    }
    if (inside && (io.MouseWheel != 0 || io.MouseWheelH != 0)) c->ProcessMouseWheel(Rml::Vector2f(-io.MouseWheelH, -io.MouseWheel), mods);
    if (ImGui::IsWindowFocused()) {
        for (auto ch : io.InputQueueCharacters) c->ProcessTextInput(Rml::Character(ch));
        const std::pair<ImGuiKey, Rml::Input::KeyIdentifier> keys[] = {
            {ImGuiKey_Tab,Rml::Input::KI_TAB}, {ImGuiKey_Enter,Rml::Input::KI_RETURN}, {ImGuiKey_Space,Rml::Input::KI_SPACE},
            {ImGuiKey_Backspace,Rml::Input::KI_BACK}, {ImGuiKey_Delete,Rml::Input::KI_DELETE},
            {ImGuiKey_LeftArrow,Rml::Input::KI_LEFT}, {ImGuiKey_RightArrow,Rml::Input::KI_RIGHT},
            {ImGuiKey_UpArrow,Rml::Input::KI_UP}, {ImGuiKey_DownArrow,Rml::Input::KI_DOWN}};
        for (auto [key, rml] : keys) {
            if (ImGui::IsKeyPressed(key)) c->ProcessKeyDown(rml, mods);
            if (ImGui::IsKeyReleased(key)) c->ProcessKeyUp(rml, mods);
        }
    }
    blocked_ = captured || (inside && ui_.pointerOverUi()) || ui_.textInputFocused();
}

void GameHud::prepare(Editor& ed, const Viewport& vp, float density, bool touch, game::RegionSwipe& region, int pointAbility, const game::EntityActionControls& entityActions) {
    if (project_ != ed.ws().project.dir || !ui_.context()) init(ed);
    if (!model_) return;
    ui_.setViewport(vp.w, vp.h, density);
    client::UiState s;
    s.screen = ed.sim()->result() ? "over" : ed.playPaused ? "paused" : "playing";
    s.lang = loc_.language(); s.touch = touch; s.showCard = ed.humanSlot() > 0;
    for(const auto& box:entityActions.screen(ed.gameCam,vp.pointsW,vp.pointsH)){
        const auto& item=entityActions.items()[box.index];client::UiItem row;
        row.text=loc_.tr(item.label);row.warn=!item.enabled;row.progress=item.progress();
        row.sub=loc_.tr("entity_action.price",Args().num("cost",item.cost))+" / "+loc_.tr(item.remaining>0?"hud.cooldown_seconds":item.enabled?"hud.ready":"entity_action.unavailable",Args().num("seconds",item.remaining));
        const float k=float(vp.w)/vp.pointsW;
        row.style="left: "+std::to_string(box.x*k)+"px; top: "+std::to_string(box.y*k)+"px; width: "+std::to_string(box.w*k)+"px; height: "+std::to_string(box.h*k)+"px;";
        s.entityActions.push_back(std::move(row));
    }
    for(const auto& counter:game::worldCounters(ed.simModule()->state(*ed.sim()).trig.vars,ed.ws().project.manifest["worldCounters"])) {
        render::float2 point;
        if(!ed.gameCam.worldToScreen(counter.position,vp.pointsW,vp.pointsH,point)||point.x<0||point.y<0||point.x>=vp.pointsW||point.y>=vp.pointsH)continue;
        const float k=float(vp.w)/vp.pointsW;client::UiItem row;row.text=std::to_string(counter.value);
        row.style="left: "+std::to_string((point.x-32)*k)+"px; top: "+std::to_string((point.y-14)*k)+"px; width: "+std::to_string(64*k)+"px;";
        s.worldCounters.push_back(std::move(row));
    }
    s.minimapStyle = "display: none;"; // The editor's scene tools own its map navigation.
    s.howtoOpen = howto_; s.hasHowto = loc_.tr("howto.goal") != "howto.goal";
    s.howtoGoal = loc_.tr("howto.goal"); s.howtoLike = loc_.tr("howto.like"); s.howtoTip = loc_.tr("howto.tip");
    for (int i=1; i<=12; ++i) { auto key="howto.step"+std::to_string(i); auto value=loc_.tr(key); if(value!=key) s.howtoSteps.push_back(value); }
    auto& gs = ed.simModule()->state(*ed.sim()); const auto& rules = ed.simModule()->rules();
    const auto& eco = gs.players[std::max(1, ed.humanSlot())];
    for (size_t r=0; r<rules.resources.size(); ++r) {
        const auto& rd=rules.resources[r]; client::UiItem it;
        const auto name=rd.name.starts_with("@")?loc_.text(rd.name):loc_.trOr("resource."+rd.id,rd.name);
        it.text=name+": "+std::to_string(eco.stock[r].toInt());
        if(rd.kind==game::ResourceKind::Capacity) { it.text=name+": "+std::to_string(eco.capUsed[r].toInt())+"/"+std::to_string(eco.capProvided[r].toInt()); it.warn=eco.capUsed[r]>eco.capProvided[r]; }
        else if ((rd.kind==game::ResourceKind::Flow || rd.capped) && eco.cap[r].raw>0) it.sub="/"+std::to_string(eco.cap[r].toInt());
        s.top.push_back(it);
    }
    if(eco.supplyCap.raw>0||eco.supplyUsed.raw>0) s.top.push_back({loc_.tr("hud.supply",Args().num("used",eco.supplyUsed.toInt()).num("cap",eco.supplyCap.toInt())),"","",eco.supplyUsed>=eco.supplyCap});
    if(gs.regionCount>0 && ed.humanSlot()>0) { int owned=0; for(auto p:gs.regionOwner) if(p==ed.humanSlot())++owned; s.top.push_back({loc_.tr("hud.regions",Args().num("count",owned)),"","",false}); }
    char time[32]; const auto seconds=ed.sim()->tick()/ed.sim()->tickRate(); std::snprintf(time,sizeof time,"%u:%02u",seconds/60,seconds%60); s.clock=time;
    const auto* player = ed.sim()->player(uint8_t(ed.humanSlot()));
    s.overTitle=loc_.tr(ed.sim()->result()>0 && player && player->team==ed.sim()->result()?"pause.victory":"pause.match_over");
    s.overTime=loc_.tr("pause.time",Args().str("time",s.clock));
    actions_.clear();
    if(ed.humanSlot()>0) for(const auto& panel:game::scriptHud(ed.ws().project.manifest,gs,rules,uint8_t(ed.humanSlot()),loc_)) {
        std::vector<UiAction>* rows=&s.card;
        if(panel.layout=="cards") { rows=&s.scriptCards; s.scriptTitle=panel.title; }
        else if(panel.layout=="status") rows=&s.scriptStatus;
        else if(panel.layout=="top") rows=&s.scriptTop;
        else if(panel.layout=="support") rows=&s.scriptSupport;
        else { UiAction a; a.header=true; a.label=panel.title; a.kind="list"; a.id=-1; rows->push_back(a); }
        for(const auto& item:panel.items) {
            UiAction a; a.label=item.label; a.tip=item.description; a.enabled=item.enabled; a.kind="list";
            a.image=item.image.starts_with("ui/")?item.image.substr(3):item.image; a.frame=item.frameSprite.empty()?"none":"image("+item.frameSprite+" fill)";
            a.hotkey=item.badge; a.cost=item.status; a.progress=item.progress; a.active=item.pointTarget&&pointAbility==item.ability;
            a.id=item.ability<0?-1:action([this,&ed,index=item.ability,point=item.pointTarget] {
                if(point) pointAbility_=index; else { Command c; c.type=game::kCmdPlayerAbility; c.param=index; ed.issueCommand(c); }
            },item.enabled);
            if(item.ability<0)a.enabled=false;
            rows->push_back(a);
        }
    }
    if(region.enabled && ed.humanSlot()>0) {
        s.emptyHint=loc_.tr("controls.region_hint");
        auto info=region.infoKey(); if(!info.empty()) { s.hasSelection=true; s.selTitle=loc_.tr(info+".title"); s.selInfo.push_back(loc_.tr(info+".description")); }
        UiAction header; header.header=true; header.id=-1; header.kind="list"; header.label=loc_.tr("controls.region_ratio",Args().num("ratio",region.ratio)); s.card.push_back(header);
        for(int ratio:{35,50,100}) { UiAction a; a.label=std::to_string(ratio)+"%"; a.kind="list"; a.active=region.ratio==ratio; a.id=action([&region,ratio]{region.ratio=ratio;client::saveRegionRatio(ratio);}); s.card.push_back(a); }
        if(region.dragging) {
            s.dispatchCount=region.valid()?std::to_string(region.count()):"—"; s.dispatchCaption=loc_.tr(region.valid()?"controls.region_send":"controls.region_invalid"); s.dispatchRatio=std::to_string(region.ratio)+"%";
            const auto pos=ImGui::GetIO().MousePos; const float scale=float(vp.w)/vp.pointsW/density;
            s.dispatchStyle="left: "+std::to_string(std::clamp((pos.x-vp.originX+22)*scale,8.f,std::max(8.f,vp.w/density-204)))+"dp; top: "+std::to_string(std::clamp((pos.y-vp.originY-102)*scale,8.f,std::max(8.f,vp.h/density-94)))+"dp;";
        }
    } else if(ed.humanSlot()>0) {
        s.emptyHint=loc_.tr("card.hint_mouse");
        for(auto e:ed.gameSelection) if(ed.sim()->world().alive(e)) {
            if(auto* u=ed.sim()->world().tryGet<game::Unit>(e)) { s.hasSelection=true; if(!s.selTitle.empty())s.selTitle+=", "; s.selTitle+=rules.types[u->type].name; }
        }
    }
    if(pointAbility>=0)s.modeHint=loc_.tr("hud.choose_target");
    const int shadows = int(ed.renderer().shadowQuality()), materials = int(ed.renderer().materialQuality()), particles = int(ed.gameFx().quality());
    if (shadows_ != shadows) { shadows_ = shadows; model_.DirtyVariable("shadows"); }
    if (materials_ != materials) { materials_ = materials; model_.DirtyVariable("materials"); }
    if (particles_ != particles) { particles_ = particles; model_.DirtyVariable("particles"); }
    client::syncUiState(model_,bound_,std::move(s)); ui_.update(); input(vp);
    blocked_ = blocked_ || !bound_.scriptCards.empty() || ed.playPaused || ed.sim()->result()!=0;
    if (shadows_ != shadows) ed.renderer().setShadowQuality(render::ShadowQuality(std::clamp(shadows_,0,3)));
    if (materials_ != materials) ed.renderer().setMaterialQuality(render::MaterialQuality(std::clamp(materials_,0,2)));
    if (particles_ != particles) ed.gameFx().setQuality(fx::FxQuality(std::clamp(particles_,0,3)));
}

void GameHud::render(Editor& ed, const Viewport& vp) {
    if(!ui_.context())return;
    gfx::PassDesc pass; pass.colorTarget=vp.color; pass.depthTarget=vp.depth; pass.clearColorEnabled=false; pass.clearDepthEnabled=false;
    ed.device().beginPass(pass); ui_.render(); ed.device().endPass();
}

bool GameHud::finish(Editor& ed) {
    const int request=request_; request_=0;
    if(!request)return false;
    const int player=ed.humanSlot(); ed.stopPlay(); reset(); if(request==1)ed.startPlay(player); return true;
}
} // namespace rts::editor
