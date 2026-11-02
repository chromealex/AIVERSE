// Client UI: the "game" data model behind engine/assets/ui/{menu,hud,lobby}.rml.
// Everything here reads the simulation and turns clicks into Commands; layout and looks are in
// the documents, so games restyle or rearrange the HUD with their own ui/ files.
#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "GameClient.h"
#include "RegionSettings.h"
#include "modules/rts/view/ScriptHud.h"
#include "modules/rts/view/WorldCounters.h"
#include "engine/net/NetLocalize.h"
#include "modules/rts/RtsLocalize.h"
#include "engine/atlas/AtlasFormat.h"

namespace rts::client {

using i18n::Args;

namespace {

std::string clockText(int64_t seconds) {
    char b[32];
    std::snprintf(b, sizeof b, "%lld:%02lld", (long long)(seconds / 60), (long long)(seconds % 60));
    return b;
}

std::string fmt(const char* f, auto... args) {
    char b[256];
    std::snprintf(b, sizeof b, f, args...);
    return b;
}

void pushSection(UiState& s, const UiSection& sec) {
    UiAction h;
    h.header = true; h.label = sec.title; h.kind = sec.kind; h.id = -1;
    s.card.push_back(h);
    for (UiAction a : sec.actions) { a.kind = sec.kind; s.card.push_back(std::move(a)); }
}

std::string cssColor(float4 c) {
    return fmt("color: rgb(%d, %d, %d);", int(c.x * 255), int(c.y * 255), int(c.z * 255));
}

class MinimapListener final : public Rml::EventListener {
public:
    explicit MinimapListener(GameClient& c) : client_(c) {}
    void ProcessEvent(Rml::Event& ev) override {
        Rml::Element* el = ev.GetCurrentElement();
        const Rml::Vector2f o = el->GetAbsoluteOffset(Rml::BoxArea::Content);
        const Rml::Vector2f sz = el->GetBox().GetSize(Rml::BoxArea::Content);
        if (sz.x <= 0 || sz.y <= 0) return;
        const float x = ev.GetParameter("mouse_x", 0.0f), y = ev.GetParameter("mouse_y", 0.0f);
        client_.minimapPointer(std::clamp((x - o.x) / sz.x, 0.0f, 1.0f), std::clamp((y - o.y) / sz.y, 0.0f, 1.0f));
    }

private:
    GameClient& client_;
};

} // namespace

void GameClient::initUi() {
    if (!ui_.init(*dev_, "fonts/ui.ttf")) { error_ = "ui: init failed"; return; }
    initLua();   // before any document: <script> and onclick="..." need the plugin
    ui_.setKeyboardLanguage(loc_.language());
    // UI images the build packed into texture atlases are no longer plain files.
    auto atlasImages = std::make_shared<atlas::ImageSource>([](const std::string& p, std::string& o) { return platform::readContentFile(p, o); }, atlas::decodeImageFile);
    if (!atlasImages->manifest().empty())
        ui_.setImageSource([atlasImages](const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba) {
            atlas::Image img;
            if (!atlasImages->load(path, img)) return false;
            w = img.width; h = img.height; rgba = std::move(img.rgba);
            return true;
        });
    Rml::DataModelConstructor c = ui_.context()->CreateDataModel("game");
    if (!c) return;
    bindUiState(c, bound_);
    // Static text in documents:  {{ 'menu.map' | tr(lang) }}. The `lang` argument is unused by the
    // function but makes each expression depend on that variable, so all text follows a language
    // switch in place (no document reload: open screens keep their state).
    c.RegisterTransformFunc("tr", [this](const Rml::VariantList& a) -> Rml::Variant {
        return a.empty() ? Rml::Variant() : Rml::Variant(loc_.tr(a[0].Get<Rml::String>()));
    });
    // two-way bindings (menu controls write straight into the client)
    c.Bind("difficulty", &difficulty_);
    c.Bind("spectator", &spectator_);
    c.Bind("shadows", &shadowQuality_);
    c.Bind("materials", &matQuality_);
    c.Bind("particles", &fxQuality_);
    c.Bind("volume", &volume_);
    c.Bind("music", &musicVolume_);
    c.Bind("sfx", &sfxVolume_);          // sound effects (every bus but music), percent
    c.Bind("musicOn", &musicOn_);        // music on / off (1 / 0), keeps the volume
    c.Bind("sfxOn", &sfxOn_);
    c.Bind("netAddress", &netAddress_);
    c.Bind("netName", &netName_);
    c.Bind("chatInput", &chatInput_);
    c.Bind("roomName", &roomName_);

    using VArgs = const Rml::VariantList&;
    c.BindEventCallback("start", [this](Rml::DataModelHandle, Rml::Event&, VArgs a) { if (!a.empty()) startMatch(size_t(a[0].Get<int>())); });
    c.BindEventCallback("act", [this](Rml::DataModelHandle, Rml::Event&, VArgs a) {
        if (a.empty()) return;
        const int id = a[0].Get<int>(-1);
        if (id >= 0 && size_t(id) < actions_.size() && actions_[size_t(id)]) actions_[size_t(id)]();
    });
    c.BindEventCallback("skill_drag", [this](Rml::DataModelHandle, Rml::Event& event, VArgs a) {
        if(a.size()<2 || a[1].Get<float>(-1)<0 || event.GetParameter<int>("button",0)!=0)return;
        const int id=a[0].Get<int>(-1);
        if(id<0||size_t(id)>=actions_.size()||!actions_[size_t(id)])return;
        actions_[size_t(id)]();
        if(mode_==Mode::Skill){skillDragging_=true;skillOverWorld_=false;regionSwipe_.cancel();}
    });
    c.BindEventCallback("support_click", [this](Rml::DataModelHandle, Rml::Event&, VArgs a) {
        if(a.size()<2 || a[1].Get<float>(-1)>=0)return;
        const int id=a[0].Get<int>(-1);
        if(id>=0&&size_t(id)<actions_.size()&&actions_[size_t(id)])actions_[size_t(id)]();
    });
    c.BindEventCallback("pause", [this](Rml::DataModelHandle, Rml::Event&, VArgs) { if (screen_ == Screen::Playing) setScreen(Screen::Paused); });
    c.BindEventCallback("resume", [this](Rml::DataModelHandle, Rml::Event&, VArgs) { if (screen_ == Screen::Paused) setScreen(Screen::Playing); });
    c.BindEventCallback("howto", [this](Rml::DataModelHandle, Rml::Event&, VArgs) { howtoOpen_ = !howtoOpen_; });
    c.BindEventCallback("restart", [this](Rml::DataModelHandle, Rml::Event&, VArgs) { startMatch(mapIndex_); });
    c.BindEventCallback("tomenu", [this](Rml::DataModelHandle, Rml::Event&, VArgs) {
        if (netMatch_) { leaveNetMatch(); return; }
        toMenu();
    });
    // net('connect'), net('create', i) ... -> Lobby.cpp
    c.BindEventCallback("net", [this](Rml::DataModelHandle, Rml::Event&, VArgs a) {
        if (!a.empty()) netAction(a[0].Get<Rml::String>(), a.size() > 1 ? a[1].Get<int>(-1) : -1);
    });
    model_ = c.GetModelHandle();

    shadowQuality_ = boundShadowQuality_ = int(r3d_.shadowQuality());
    fxQuality_ = boundFxQuality_ = fx_.quality();
    matQuality_ = boundMatQuality_ = int(r3d_.materialQuality());
    ui_.setTranslator([this](const std::string& key) { return tr(key); });
    minimapListener_ = std::make_unique<MinimapListener>(*this);
    loadDocuments();
    runGameScript();   // ui/main.lua
}

// Loads every document (once; also usable to pick up edited documents). The data model survives
// a reload; documents rebind to it.
void GameClient::loadDocuments() {
    if (minimapEl_) {
        minimapEl_->RemoveEventListener(Rml::EventId::Mousedown, minimapListener_.get());
        minimapEl_->RemoveEventListener(Rml::EventId::Drag, minimapListener_.get());
        minimapEl_ = nullptr;
    }
    for (Rml::ElementDocument* d : {menuDoc_, hudDoc_, lobbyDoc_, howtoDoc_}) if (d) d->Close();
    menuDoc_ = ui_.loadDocument("menu.rml");
    lobbyDoc_ = ui_.loadDocument("lobby.rml");
    hudDoc_ = ui_.loadDocument("hud.rml");
    howtoDoc_ = ui_.loadDocument("howto.rml");   // last: above the menu and the pause modal
    if (!menuDoc_ || !hudDoc_ || !lobbyDoc_) error_ = "ui: documents not found (engine/ui/menu.rml, hud.rml, lobby.rml)";
    if (hudDoc_) minimapEl_ = hudDoc_->GetElementById("minimap-img");
    if (minimapEl_) {
        minimapEl_->AddEventListener(Rml::EventId::Mousedown, minimapListener_.get());
        minimapEl_->AddEventListener(Rml::EventId::Drag, minimapListener_.get());
    }
}

void GameClient::shutdownUi() {
    if (minimapEl_ && minimapListener_) {
        minimapEl_->RemoveEventListener(Rml::EventId::Mousedown, minimapListener_.get());
        minimapEl_->RemoveEventListener(Rml::EventId::Drag, minimapListener_.get());
    }
    minimapEl_ = nullptr;
    menuDoc_ = hudDoc_ = lobbyDoc_ = howtoDoc_ = nullptr;
    ui_.shutdown();
    shutdownLua();
    minimapListener_.reset();
}

void GameClient::setScreen(Screen s) {
    const bool changed = screen_ != s;
    screen_ = s;
    if (s != Screen::Playing) { boxing_ = panning_ = sendDrag_ = false; mode_ = Mode::Normal; }
    if (!changed) return;
    if (s == Screen::Over && sim_) {   // UI scripts: rewards, progress (game.on("matchEnd", fn))
        const int res = sim_->result();
        auto* me = sim_->player(human_);
        Json r = Json::object();
        r.set("won", Json::fromBool(!spectator_ && res > 0 && me && me->team == res));
        r.set("result", Json::number(int64_t(res)));
        r.set("seconds", Json::number(int64_t(sim_->tick() / std::max<uint32_t>(1, sim_->tickRate()))));
        r.set("map", Json::number(int64_t(mapIndex_ + 1)));
        r.set("mapTitle", Json::string(mapIndex_ < maps_.size() ? maps_[mapIndex_].title : ""));
        r.set("difficulty", Json::number(int64_t(difficulty_)));
        r.set("spectator", Json::fromBool(spectator_));
        r.set("online", Json::fromBool(netMatch_));
        // the simulation's variables at the end (game.set_var): how a game's scripts hand details of the ending to its
        // screens (e.g. "retry" chosen on an in-match result panel); integers stay integers
        Json vars = Json::object();
        for (const auto& [name, v] : mod().state(*sim_).trig.vars) {
            const double d = v.toDouble();
            if (d == double(int64_t(d))) vars.set(name, Json::number(int64_t(d))); else vars.set(name, Json::number(d, 6));
        }
        r.set("vars", vars);
        lastResult_ = r;
        luaEmit("matchEnd", r);
    }
    luaEmit("screen", Json::string(screenName()));
}

void GameClient::minimapPointer(float u, float v) {
    if (!sim_) return;
    if (planet()) { minimap_.planetPointer(view_, u, v); return; }   // MinimapPlanet.cpp
    const auto& g = mod().state(*sim_).grid;
    cam_.target = {u * float(g.w), 0, v * float(g.h)};
}

// ------------------------------------------------------------------ per-frame model

void GameClient::updateUi() {
    UiState s;
    s.screen = screen_ == Screen::Menu ? "menu" : screen_ == Screen::Lobby ? "lobby" : screen_ == Screen::Playing ? "playing"
             : screen_ == Screen::Paused ? "paused" : "over";
    s.gameName = gameName_;
    s.error = error_;
    s.platformInfo = std::string(platform::name()) + " · " + dev_->info().name;
    s.lang = loc_.language() + (loc_.pseudo() ? "-qps" : "");
    for (auto& m : maps_) s.maps.push_back(m.title);
    s.touch = platform::isTouchDevice();
    s.showCard = !spectator_;
    nextActions_.clear();
    if (langs_.available.size() > 1)
        for (size_t i = 0; i < langs_.available.size() && i < langNames_.size(); ++i) {
            UiAction a;
            a.label = langNames_[i];
            a.active = langs_.available[i] == loc_.language();
            const std::string tag = langs_.available[i];
            a.id = addAction([this, tag] { applyLanguage(tag, true); }, !a.active);
            s.languages.push_back(a);
        }
    if (screen_ == Screen::Lobby || net_) buildLobby(s);
    if (screen_ == Screen::Playing || screen_ == Screen::Lobby || screen_ == Screen::Over) howtoOpen_ = false;  // menu and pause only
    buildHowto(s);

    if (sim_) {
        cleanSelection();
        buildTopBar(s);
        if (!spectator_) { buildCommandCard(s); buildScriptCommands(s); buildTools(s); }
        buildOverlays(s);
        for (auto& n : notices_) if (n.until >= time_) s.notices.push_back(n.text);
        if (screen_ == Screen::Over) {
            const int res = sim_->result();
            auto* me = sim_->player(human_);
            const bool won = !spectator_ && res > 0 && me && me->team == res;
            s.overTitle = tr(spectator_ ? "pause.match_over" : won ? "pause.victory" : "pause.defeat");
            s.overTime = tr("pause.time", Args().str("time", clockText(sim_->tick() / sim_->tickRate())));
        }
        // minimap image
        auto& gs = mod().state(*sim_);
        const float W = platform::windowWidth(), H = platform::windowHeight();
        float3 q[4];
        if (planet()) minimap_.updatePlanet(*dev_, view_, scene_, rules_, !spectator_);
        else {
        const bool haveQuad = groundAt(0, 0, q[0]) && groundAt(W, 0, q[1]) && groundAt(W, H, q[2]) && groundAt(0, H, q[3]);
        minimap_.update(*dev_, gs, rules_, scene_, spectator_ ? nullptr : &gs.vision[human_], q, haveQuad, &view_.overlays().minimapMarks());
        }
        const float side = s.touch ? 150.0f : 190.0f;
        const float k = side / float(std::max(gs.grid.w, gs.grid.h));
        s.minimapStyle = fmt("width: %.0fdp; height: %.0fdp;", gs.grid.w * k, gs.grid.h * k);
    }

    if (menuDoc_ && engineMenu_ != engineMenuShown_) {   // game.engineMenu(false): the game's own screens
        if (engineMenu_) menuDoc_->Show(Rml::ModalFlag::None, Rml::FocusFlag::None); else menuDoc_->Hide();
        engineMenuShown_ = engineMenu_;
    }
    actions_.swap(nextActions_);
    // push only what changed: unchanged variables keep their elements (and hover/press state)
    if (model_) {
        syncUiState(model_, bound_, std::move(s));
        if (shadowQuality_ != boundShadowQuality_) { // changed from the pause menu
            r3d_.setShadowQuality(ShadowQuality(std::clamp(shadowQuality_, 0, 3)));
            boundShadowQuality_ = shadowQuality_;
            quality_.lock();   // the player chose: no adaptive changes any more
        }
        if (matQuality_ != boundMatQuality_) { // changed from the pause menu
            r3d_.setMaterialQuality(rts::render::MaterialQuality(std::clamp(matQuality_, 0, 2)));
            boundMatQuality_ = matQuality_;
        }
        if (fxQuality_ != boundFxQuality_) { // changed from the pause menu
            fx_.setQuality(fxQuality_);
            boundFxQuality_ = fxQuality_;
        }
    }
    ui_.update();
}

void GameClient::buildTopBar(UiState& s) {
    auto& gs = mod().state(*sim_);
    const auto& eco = gs.players[spectator_ ? 1 : human_];
    for (size_t r = 0; r < rules_.resources.size(); ++r) {
        const auto& rd = rules_.resources[r];
        UiItem it;
        if (rd.kind == ResourceKind::Capacity) {
            it.text = fmt("%s: %lld/%lld", rd.name.c_str(), (long long)eco.capUsed[r].toInt(), (long long)eco.capProvided[r].toInt());
            it.warn = eco.capUsed[r] > eco.capProvided[r];
        } else {
            it.text = fmt("%s: %lld", rd.name.c_str(), (long long)eco.stock[r].toInt());
            if ((rd.kind == ResourceKind::Flow || rd.capped) && eco.cap[r].raw > 0) it.sub = fmt("/%lld", (long long)eco.cap[r].toInt());
            if (rd.kind == ResourceKind::Flow) it.sub += " " + tr("hud.per_second", Args().str("value", fmt("%+.1f", eco.income[r].toDouble())));
        }
        s.top.push_back(it);
    }
    if (eco.supplyCap.raw > 0 || eco.supplyUsed.raw > 0)
        s.top.push_back({tr("hud.supply", Args().num("used", eco.supplyUsed.toInt()).num("cap", eco.supplyCap.toInt())), "", "", eco.supplyUsed >= eco.supplyCap});
    if (rules_.resources.empty()) {
        for (auto& p : sim_->players()) {
            int g = 0;
            sim_->world().each<Garrison, Owner>([&](Entity, Garrison& gg, Owner& o) { if (o.player == p.slot) g += gg.count; });
            sim_->world().each<Unit, Owner>([&](Entity en, Unit&, Owner& o) { if (o.player == p.slot && sim_->world().has<Mobile>(en)) ++g; });
            // online: names come from the lobby (bots and unnamed players as "@net.*" keys)
            const std::string name = netMatch_ ? net::displayName(loc_, p.name, p.slot)
                                   : mapIndex_ >= maps_.size() ? p.name : localizedPlayerName(loc_, maps_[mapIndex_].file, size_t(p.slot - 1), p.name);
            s.top.push_back({fmt("%s %d", name.c_str(), g) + (p.alive ? "" : tr("hud.eliminated")), "", cssColor(RtsView::playerColor(p.slot)), false});
        }
    }
    const auto& vic = gs.victory;
    if (vic.mode == VictoryMode::Tickets)
        for (size_t t = 1; t < gs.tickets.size(); ++t) s.top.push_back({tr("hud.team", Args().num("team", int64_t(t)).num("tickets", gs.tickets[t].toInt())), "", "", false});
    if (vic.mode == VictoryMode::Score)
        for (size_t t = 1; t < gs.score.size(); ++t)
            s.top.push_back({fmt("%lld", (long long)gs.score[t].toInt()), "", cssColor(RtsView::playerColor(uint8_t(t))), false});
    if (vic.mode == VictoryMode::Survive || vic.mode == VictoryMode::Score) {
        int64_t left = std::max<int64_t>(0, (vic.surviveSeconds * int64_t(sim_->tickRate())).toInt() - int64_t(sim_->tick())) / sim_->tickRate();
        s.top.push_back({tr("hud.time_left", Args().str("time", clockText(left))), "", "", left < 30});
    }
    if (uint32_t wt = mod().ticksUntilWave(*sim_)) s.top.push_back({tr("hud.wave_in", Args().num("count", wt / sim_->tickRate() + 1)), "", "", false});
    if (gs.regionCount > 0 && !spectator_) {
        int owned = 0;
        for (auto o : gs.regionOwner) if (o == human_) ++owned;
        s.top.push_back({tr("hud.regions", Args().num("count", owned)), "", "", false});
        UiAction a;
        a.label = tr("hud.claim"); a.active = mode_ == Mode::Claim;
        a.id = addAction([this] { mode_ = Mode::Claim; });
        s.topActions.push_back(a);
    }
    if (!rules_.autoBuildPool.empty() && !spectator_) {
        UiAction a;
        a.label = tr("hud.auto_build");
        for (auto& c : mod().autoBuildCost(*sim_, human_, rules_.autoBuildPool[0]))
            a.cost += (a.cost.empty() ? "" : " ") + std::to_string(c.value.toInt()) + " " + rules_.resources[c.res].name;
        a.id = addAction([this] { Command c; c.type = kCmdAutoBuild; c.param = -1; issue(c); });
        s.topActions.push_back(a);
    }
    s.clock = fmt("%u:%02u", sim_->tick() / sim_->tickRate() / 60, sim_->tick() / sim_->tickRate() % 60);
}

void GameClient::buildCommandCard(UiState& s) {
    if (!spectator_) for (const auto& panel : game::scriptHud(manifest_, mod().state(*sim_), rules_, human_, loc_)) {
        UiSection sec{panel.title, "list", {}};
        for (const auto& item : panel.items) {
            UiAction a; a.label = item.label; a.tip = item.description; a.enabled = item.enabled; a.image=item.image.rfind("ui/",0)==0?item.image.substr(3):item.image; a.frame=item.frameSprite.empty()?"none":"image("+item.frameSprite+" fill)"; a.hotkey=item.badge; a.cost=item.status; a.progress=item.progress;
            a.active = item.pointTarget && mode_ == Mode::Skill && abilityIndex_ == item.ability;
            if (item.ability >= 0) a.id = addAction([this, index=item.ability, pointTarget=item.pointTarget] {
                if(pointTarget){mode_=Mode::Skill;abilityIndex_=index;pairFirst_=false;}
                else {Command c; c.type=kCmdPlayerAbility; c.param=index; issue(c);}
            }, a.enabled);
            else { a.id=-1; a.enabled=false; }
            sec.actions.push_back(a);
        }
        if(panel.layout=="cards") {s.scriptTitle=panel.title;for(auto a:sec.actions)s.scriptCards.push_back(a);}
        else if(panel.layout=="status") {for(auto a:sec.actions)s.scriptStatus.push_back(a);}
        else if(panel.layout=="top") {for(auto a:sec.actions)s.scriptTop.push_back(a);}
        else if(panel.layout=="support") {for(auto a:sec.actions)s.scriptSupport.push_back(a);}
        else pushSection(s, sec);
    }
    if (regionSwipe_.enabled) {
        s.emptyHint=tr("controls.region_hint");
        const auto info=regionSwipe_.infoKey();
        if(!info.empty()){s.hasSelection=true;s.selTitle=tr(info+".title");s.selInfo.push_back(tr(info+".description"));}
        UiSection sec{tr("controls.region_ratio",Args().num("ratio",regionSwipe_.ratio)), "list", {}};
        for(int ratio:{35,50,100}) { UiAction a;a.label=std::to_string(ratio)+"%";a.active=regionSwipe_.ratio==ratio;
            a.id=addAction([this,ratio]{regionSwipe_.ratio=ratio;saveRegionRatio(ratio);});sec.actions.push_back(a); }
        pushSection(s,sec);buildToolCard(s);return;
    }
    World& w = sim_->world();
    if (selection_.empty()) {
        s.emptyHint = tr(platform::isTouchDevice() ? "card.hint_touch" : "card.hint_mouse");
        UiSection sec{"", "list", {}};
        UiAction a;
        a.label = tr("card.all_army");
        a.id = addAction([this] {
            for (auto& o : scene_.objects)
                if (o.owner == human_ && !o.isProjectile && !rules_.types[o.type].structure && !rules_.types[o.type].worker && !rules_.types[o.type].weapons.empty())
                    selection_.push_back(o.entity);
        });
        sec.actions.push_back(a);
        pushSection(s, sec);
        return;
    }
    s.hasSelection = true;
    std::map<uint16_t, int> counts;
    for (Entity e : selection_) counts[w.get<Unit>(e).type]++;
    for (auto& [t, n] : counts) {
        if (!s.selTitle.empty()) s.selTitle += ", ";
        s.selTitle += n > 1 ? fmt("%s ×%d", rules_.types[t].name.c_str(), n) : rules_.types[t].name;
    }
    const Entity e0 = selection_[0];
    const UnitType& t0 = rules_.types[w.get<Unit>(e0).type];
    if (selection_.size() == 1) {
        const Unit& u = w.get<Unit>(e0);
        if (!t0.invulnerable) s.selInfo.push_back(tr("card.health", Args().num("hp", u.hp.toInt()).num("max", t0.maxHp.toInt())));
        if (auto* rs = w.tryGet<ResourceStock>(e0)) s.selInfo.push_back(tr("card.stock", Args().num("amount", rs->amount.toInt())));
        if (auto* g = w.tryGet<Garrison>(e0)) s.selInfo.push_back(tr("card.garrison", Args().num("count", g->count).num("max", t0.garrisonMax)));
    }
    if (w.get<Owner>(e0).player != human_) return;
    s.ownSelection = true;

    auto costText = [&](uint16_t type) {
        std::string c;
        for (auto& x : rules_.types[type].cost) c += (c.empty() ? "" : "  ") + std::to_string(x.value.toInt()) + " " + rules_.resources[x.res].name;
        if (rules_.types[type].supplyUsed.raw > 0) c += (c.empty() ? "" : "  ") + tr("card.supply_cost", Args().num("count", rules_.types[type].supplyUsed.toInt()));
        return c;
    };
    auto reqText = [&](const std::vector<Requirement>& req) {
        std::string r;
        for (auto& q : req) r += (r.empty() ? "" : ", ") + (q.tech ? rules_.techs[q.index].name : rules_.types[q.index].name);
        return r.empty() ? r : tr("card.requires", Args().str("list", r));
    };
    auto simple = [&](uint16_t type, std::vector<Entity> units, int64_t param = 0) {
        return [this, type, units, param] { Command c; c.type = type; c.units = units; c.param = param; issue(c); };
    };

    if (selectionHasGarrison()) {
        UiSection sec{tr("card.send"), "row", {}};
        for (int p : {25, 50, 75, 100}) {
            UiAction a;
            a.label = fmt("%d%%", p); a.active = sendPercent_ == p; a.hotkey = std::to_string(p / 25);
            a.id = addAction([this, p] { sendPercent_ = p; });
            sec.actions.push_back(a);
        }
        pushSection(s, sec);
        s.selInfo.push_back(tr(platform::isTouchDevice() ? "ui.send_hint_touch" : "card.send_hint"));
    }
    bool anyMobile = false, anyWeapon = false;
    std::vector<uint16_t> buildable;
    for (Entity e : selection_) {
        const UnitType& t = rules_.types[w.get<Unit>(e).type];
        if (t.speed.raw > 0) anyMobile = true;
        if (!t.weapons.empty()) anyWeapon = true;
        for (uint16_t b : t.builds) if (std::find(buildable.begin(), buildable.end(), b) == buildable.end()) buildable.push_back(b);
    }
    if (anyMobile) {
        UiSection sec{"", "row", {}};
        auto add = [&](const std::string& label, const char* key, bool active, std::function<void()> fn) {
            UiAction a; a.label = label; a.hotkey = key; a.active = active; a.id = addAction(std::move(fn));
            sec.actions.push_back(a);
        };
        if (anyWeapon) add(tr("card.attack"), "A", mode_ == Mode::AttackMove, [this] { mode_ = Mode::AttackMove; });
        add(tr("card.stop"), "S", false, simple(kCmdStop, selection_));
        add(tr("card.hold"), "H", false, simple(kCmdHold, selection_));
        add(tr("card.patrol"), "P", mode_ == Mode::Patrol, [this] { mode_ = Mode::Patrol; });
        pushSection(s, sec);
    }
    if (!buildable.empty()) {
        UiSection sec{tr("card.build"), "grid", {}};
        for (uint16_t b : buildable) {
            UiAction a;
            a.label = rules_.types[b].name; a.cost = costText(b);
            a.enabled = mod().canTrain(*sim_, human_, b);
            if (!a.enabled) a.tip = reqText(rules_.types[b].requires_);
            a.active = mode_ == Mode::Place && placeType_ == b && !placeProducer_.valid();
            a.id = addAction([this, b] { mode_ = Mode::Place; placeType_ = b; placeProducer_ = kNullEntity; }, a.enabled);
            sec.actions.push_back(a);
        }
        pushSection(s, sec);
    }
    if (!t0.abilities.empty()) {
        UiSection sec{tr("ui.abilities"), "grid", {}};
        for (size_t i = 0; i < t0.abilities.size(); ++i) {
            const AbilityDef& ab = t0.abilities[i];
            if (ab.aura) { s.selInfo.push_back(tr("card.aura", Args().str("name", ab.name))); continue; }
            UiAction a;
            a.label = ab.name;
            if (ab.energyCost.raw > 0) a.cost = tr("card.energy_cost", Args().num("count", ab.energyCost.toInt()));
            a.enabled = mod().canCast(*sim_, e0, int(i));
            a.active = mode_ == Mode::Ability && abilityIndex_ == int(i);
            const bool instant = ab.target == TargetKind::None || ab.target == TargetKind::Self;
            a.id = addAction(instant ? simple(kCmdAbility, selection_, int64_t(i)) : std::function<void()>([this, i] { mode_ = Mode::Ability; abilityIndex_ = int(i); }), a.enabled);
            sec.actions.push_back(a);
        }
        if (!sec.actions.empty()) pushSection(s, sec);
    }
    if (auto* prod = w.tryGet<Production>(e0); prod && !(w.get<Unit>(e0).flags & kUnitUnderConstruction)) {
        UiSection sec{tr("ui.production"), "grid", {}};
        if (prod->ready != kNoType) {
            UiAction a;
            a.label = tr("card.place", Args().str("name", rules_.types[prod->ready].name));
            a.active = mode_ == Mode::Place && placeProducer_ == e0;
            uint16_t ready = prod->ready;
            a.id = addAction([this, ready, e0] { mode_ = Mode::Place; placeType_ = ready; placeProducer_ = e0; });
            sec.actions.push_back(a);
        }
        if (t0.chargesMax > 0) s.selInfo.push_back(tr("card.charges", Args().num("count", prod->charges).num("max", t0.chargesMax)));
        for (uint16_t p : t0.produces) {
            UiAction a;
            a.label = rules_.types[p].name; a.cost = costText(p);
            a.enabled = mod().canTrain(*sim_, human_, p);
            if (!a.enabled) a.tip = reqText(rules_.types[p].requires_);
            a.id = addAction([this, p, e0] {
                World& ww = sim_->world();
                if (!ww.alive(e0) || !ww.has<Unit>(e0)) return;
                // queue on the least busy producer of the same type in the selection
                Entity best = e0; size_t bestQ = SIZE_MAX;
                for (Entity e : selection_)
                    if (ww.alive(e) && ww.has<Unit>(e) && ww.get<Unit>(e).type == ww.get<Unit>(e0).type)
                        if (auto* pp = ww.tryGet<Production>(e); pp && pp->queue.size() < bestQ) { bestQ = pp->queue.size(); best = e; }
                Command c; c.type = kCmdTrain; c.param = p; c.units = {best};
                issue(c);
            }, a.enabled);
            sec.actions.push_back(a);
        }
        const auto& eco = mod().state(*sim_).players[human_];
        for (uint16_t k : t0.researches) {
            const TechDef& tech = rules_.techs[k];
            if (eco.researched[k]) continue;
            UiAction a;
            a.label = tr("card.research", Args().str("name", tech.name));
            for (auto& c : tech.cost) a.cost += (a.cost.empty() ? "" : "  ") + std::to_string(c.value.toInt()) + " " + rules_.resources[c.res].name;
            a.enabled = !eco.researching[k] && mod().requirementsMet(*sim_, human_, tech.requires_);
            if (!a.enabled && !eco.researching[k]) a.tip = reqText(tech.requires_);
            a.id = addAction(simple(kCmdResearch, {e0}, k), a.enabled);
            sec.actions.push_back(a);
        }
        for (uint16_t up : t0.upgrades) {
            UiAction a;
            a.label = tr("card.upgrade", Args().str("name", rules_.types[up].name)); a.cost = costText(up);
            a.id = addAction(simple(kCmdUpgrade, {e0}, up));
            sec.actions.push_back(a);
        }
        if (!t0.produces.empty()) {
            UiAction a;
            a.label = tr("card.rally"); a.active = mode_ == Mode::Rally;
            a.id = addAction([this] { mode_ = Mode::Rally; });
            sec.actions.push_back(a);
        }
        if (!sec.actions.empty()) pushSection(s, sec);
        if (!prod->queue.empty()) {
            UiSection q{tr("ui.queue"), "queue", {}};
            for (size_t i = 0; i < prod->queue.size(); ++i) {
                const ProdItem& it = prod->queue[i];
                UiAction a;
                a.label = it.item >= 0 ? rules_.types[size_t(it.item)].name : rules_.techs[size_t(~it.item)].name;
                if (i == 0 || t0.chargesMax > 0) a.progress = float(std::clamp(it.progress.toDouble(), 0.0, 1.0));
                a.id = addAction(simple(kCmdCancelTrain, {e0}, int64_t(i)));
                q.actions.push_back(a);
            }
            pushSection(s, q);
        }
    }
    UiSection special{"", "row", {}};
    auto addSpecial = [&](const std::string& label, std::function<void()> fn) {
        UiAction a; a.label = label; a.id = addAction(std::move(fn)); special.actions.push_back(a);
    };
    if (t0.structure && t0.sellable && !w.has<Construction>(e0)) addSpecial(tr("card.sell"), simple(kCmdSell, {e0}));
    if (t0.deploysTo != kNoType) addSpecial(tr("card.deploy"), simple(kCmdDeploy, {e0}));
    if (t0.members > 1) {
        s.selInfo.push_back(tr("card.squad", Args().num("alive", mod().membersAlive(*sim_, e0)).num("max", t0.members)));
        addSpecial(tr("card.reinforce"), simple(kCmdReinforce, selection_));
    }
    if (auto* h = w.tryGet<Hero>(e0)) s.selInfo.push_back(tr("card.hero", Args().num("level", h->level).num("xp", h->xp.toInt())));
    if (t0.maxEnergy.raw > 0) s.selInfo.push_back(tr("card.energy", Args().num("energy", w.get<Unit>(e0).energy.toInt()).num("max", t0.maxEnergy.toInt())));
    if (w.has<Construction>(e0)) addSpecial(tr("card.cancel_build"), simple(kCmdCancelTrain, {e0}));
    if (!special.actions.empty()) pushSection(s, special);
    buildToolCard(s);
}

void GameClient::buildOverlays(UiState& s) {
    const float k = platform::dpiScale(); // logical points -> context (framebuffer) pixels
    if (boxing_ && std::hypot(mouseX_ - downX_, mouseY_ - downY_) > 6) {
        const float x0 = std::min(downX_, mouseX_), y0 = std::min(downY_, mouseY_);
        s.boxStyle = fmt("left: %.0fpx; top: %.0fpx; width: %.0fpx; height: %.0fpx;", x0 * k, y0 * k,
                         std::abs(mouseX_ - downX_) * k, std::abs(mouseY_ - downY_) * k);
    }
    for(const auto& counter:game::worldCounters(mod().state(*sim_).trig.vars,manifest_["worldCounters"])) {
        render::float2 point;
        const float w=float(platform::windowWidth()),h=float(platform::windowHeight());
        if(!cam_.worldToScreen(counter.position,w,h,point)||point.x<0||point.y<0||point.x>=w||point.y>=h)continue;
        UiItem row;row.text=std::to_string(counter.value);
        row.style=fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx;",(point.x-32)*k,(point.y-14)*k,64*k);
        s.worldCounters.push_back(std::move(row));
    }
    if(regionSwipe_.enabled) {
        entityActions_.refresh(*sim_,mod(),spectator_?0:human_,manifest_["entityActionControls"]);
        for(const auto& box:entityActions_.screen(cam_,float(platform::windowWidth()),float(platform::windowHeight()))){
            const auto& item=entityActions_.items()[box.index];UiItem row;
            row.text=tr(item.label);row.warn=!item.enabled;row.progress=item.progress();
            row.sub=tr("entity_action.price",Args().num("cost",item.cost))+" / "+
                tr(item.remaining>0?"hud.cooldown_seconds":item.enabled?"hud.ready":"entity_action.unavailable",Args().num("seconds",item.remaining));
            row.style=fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx; height: %.1fpx;",box.x*k,box.y*k,box.w*k,box.h*k);
            s.entityActions.push_back(std::move(row));
        }
        const auto color=RtsView::playerColor(human_);
        for(auto q:regionSwipe_.controlFlags(cam_,float(platform::windowWidth()),float(platform::windowHeight()))){
            s.lines.push_back(fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx; height: %.1fpx; background-color: #f5f5eb; border: 1px #142332;",(q.x-1)*k,(q.y-26)*k,2*k,26*k));
            s.lines.push_back(fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx; height: %.1fpx; background-color: rgb(%d,%d,%d); border: 1px white; border-radius: 2px;",(q.x+1)*k,(q.y-25)*k,19*k,13*k,int(color.x*255),int(color.y*255),int(color.z*255)));
        }
        const auto strokes=regionSwipe_.strokes(cam_,float(platform::windowWidth()),float(platform::windowHeight()),time_,[&](float x,float z){return view_.heightAt(mod().state(*sim_).grid,x,z);});
        for(int layer=0;layer<2;++layer)for(const auto& stroke:strokes){
            if(stroke.shadow!=(layer==0))continue;
            const bool pass=layer==1;
            auto a=stroke.a,b=stroke.b;float dx=(b.x-a.x)*k,dy=(b.y-a.y)*k,width=stroke.width*k;
            const float light=.35f+.65f*stroke.highlight,tint=1-light;
            const int red=pass?int((light+tint*color.x)*255):18,green=pass?int((light+tint*color.y)*255):31,blue=pass?int((light+tint*color.z)*255):46;
            s.lines.push_back(fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx; height: %.1fpx; margin-top: %.1fpx; border-radius: %.1fpx; background-color: rgb(%d,%d,%d); opacity: %.3f; transform: rotate(%.2fdeg);",a.x*k,a.y*k,std::max(.5f,std::hypot(dx,dy)),width,-width*.5f,stroke.shadow?0:width*.5f,red,green,blue,stroke.alpha,std::atan2(dy,dx)*57.29578f));
        }
        for(const auto& u:regionSwipe_.accepted()){
            float2 q;if(cam_.worldToScreen({float(u.x.toDouble()),.12f,float(u.z.toDouble())},float(platform::windowWidth()),float(platform::windowHeight()),q))
                s.lines.push_back(fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx; height: %.1fpx; margin-top: 0px; border: 2px #64ebffff; border-radius: %.1fpx; background-color: #46dcff40;",(q.x-7)*k,(q.y-7)*k,14*k,14*k,9*k));
        }
        for(const auto& u:regionSwipe_.preview()){
            float2 q;if(cam_.worldToScreen({float(u.x.toDouble()),.12f,float(u.z.toDouble())},float(platform::windowWidth()),float(platform::windowHeight()),q))
                s.lines.push_back(fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx; height: %.1fpx; margin-top: 0px; border: 2px #fff5aaff; border-radius: %.1fpx; background-color: #ffe67840;",(q.x-7)*k,(q.y-7)*k,14*k,14*k,9*k));
        }
        if(regionSwipe_.dragging) {
            const bool valid=regionSwipe_.valid();
            s.dispatchCount=valid?std::to_string(regionSwipe_.count()):"—";
            s.dispatchCaption=tr(valid?"controls.region_send":"controls.region_invalid");
            s.dispatchRatio=std::to_string(regionSwipe_.previewRatio())+"%";
            const float x=std::clamp(mouseX_+22,8.0f,std::max(8.0f,float(platform::windowWidth())-204));
            const float y=std::clamp(mouseY_-102,8.0f,std::max(8.0f,float(platform::windowHeight())-94));
            s.dispatchStyle=fmt("left: %.0fpx; top: %.0fpx; color: %s;",x*k,y*k,!valid?"#ff9a90":regionSwipe_.favorable()?"#83e5ee":"#ffd58a");
        }
    }
    if (sendDrag_) {
        for (Entity e : selection_) {
            auto* t = sim_->world().tryGet<Transform>(e);
            float2 a;
            if (!t || !cam_.worldToScreen({float(t->pos.x.toDouble()), 0.5f, float(t->pos.z.toDouble())}, platform::windowWidth(), platform::windowHeight(), a))
                continue;
            const float dx = (mouseX_ - a.x) * k, dy = (mouseY_ - a.y) * k;
            s.lines.push_back(fmt("left: %.1fpx; top: %.1fpx; width: %.1fpx; transform: rotate(%.2fdeg);", a.x * k, a.y * k,
                                  std::hypot(dx, dy), std::atan2(dy, dx) * 57.29578f));
        }
    }
    if (screen_ == Screen::Playing && possessedUnit().valid() && mode_ == Mode::Normal) {
        s.modeHint = tr("mode.possessed");
        s.hintStyle = fmt("left: %.0fpx; top: %.0fpx;", (mouseX_ + 16) * k, (mouseY_ + 8) * k);
    }
    if (mode_ != Mode::Normal && screen_ == Screen::Playing) {
        std::string th = toolHint();
        if (th.empty()) th = scriptCommandHint();
        s.modeHint = !th.empty() ? th : tr(mode_ == Mode::AttackMove ? "mode.attack" : mode_ == Mode::Patrol ? "mode.patrol"
                      : mode_ == Mode::Rally ? "mode.rally" : mode_ == Mode::Ability ? "mode.ability"
                      : mode_ == Mode::Claim ? "mode.claim" : "mode.place");
        s.hintStyle = fmt("left: %.0fpx; top: %.0fpx;", (mouseX_ + 16) * k, (mouseY_ + 8) * k);
    }
}

} // namespace rts::client
