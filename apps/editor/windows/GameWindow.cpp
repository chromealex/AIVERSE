// Game: the play-test as the player sees it. Can be framed as a device (phone simulator):
// resolution and aspect of the device, notch and safe area, orientation, touch-sized feedback.
// When the user plays a side, LMB selects (click / box), RMB gives smart orders, WASD/arrows pan.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include "../Devices.h"
#include "../Editor.h"
#include "../EditorGenreUi.h"
#include "../EditorUtil.h"
#include "../GameHud.h"
#include "modules/rts/view/RegionSwipe.h"
#include "modules/rts/view/ScriptHud.h"
#include "apps/client/RegionSettings.h"
#include "engine/i18n/Localization.h"
#include "modules/rts/RtsScript.h"

namespace aiverse::editor {

using namespace render;

class GameWindow : public EditorWindow {
public:
    const char* id() const override { return "game"; }
    const char* title() const override { return "Игра"; }
    const char* group() const override { return "scene"; }
    int order() const override { return 1; }
    const char* dockArea() const override { return "center"; }
    bool zeroPadding() const override { return true; }
    ImGuiWindowFlags flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

    void update(Editor& ed, double dt) override { dt_ = dt; if (!ed.playing()) gameHud_.reset(); }

    void draw(Editor& ed) override {
        if(ed.ws().project.isFps()) {
            ImGui::TextWrapped("%s",genreText(ed.ws(),"fps.editor.hint").c_str());
            ImGui::BeginDisabled(ed.ws().building());
            if(ImGui::Button(genreText(ed.ws(),"fps.editor.play").c_str()))ed.startFpsPlay();
            ImGui::SameLine();if(ImGui::Button(genreText(ed.ws(),"fps.editor.listen").c_str()))ed.startFpsPlay(true);
            ImGui::EndDisabled();
            if(ImGui::Button(genreText(ed.ws(),"fps.editor.open").c_str()))ed.focusWindow("fps");
            ed.gameViewport=nullptr;return;
        }
        toolbar(ed);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), ImGui::GetColorU32(ed.theme().palette().canvas)); // lighter than the device bezel
        if (!ed.playing() && !ed.clientPlaying()) {
            gameHud_.reset();
            const float bh = ImGui::GetFrameHeight() + 8;
            ui::emptyState(origin, ImVec2(origin.x + avail.x, origin.y + avail.y - bh * 2), ICON_GAME, "Тест не запущен",
                           ed.prefersClientPlay() ? "F5 — игра как в клиенте: меню, экраны и скрипты игры. Или тест карты: боты, игра за игрока."
                                                  : "F5 или кнопки ниже: боты против ботов, игра за одного из игроков или как в клиенте игры.");
            // start buttons under the message
            // -1: as in the game client (first for games with their own screens, ui/main.lua)
            std::vector<std::pair<std::string, int>> starts{{std::string(ICON_BOT "  Боты против ботов"), 0}};
            starts.insert(ed.prefersClientPlay() ? starts.begin() : starts.end(), {std::string(ICON_GAME "  Как в клиенте"), -1});
            for (size_t p = 0; p < ed.doc().data.players.size() && starts.size() < 4; ++p)
                if (!ed.doc().data.players[p].waves) starts.push_back({std::string(ICON_USER "  Играть за ") + ed.doc().data.players[p].name, int(p + 1)});
            float total = 0;
            for (auto& st : starts) total += ImGui::CalcTextSize(st.first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 + 8;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + std::max(8.0f, (avail.x - total) * 0.5f), origin.y + (avail.y - bh * 2) * 0.5f + ImGui::GetFontSize() * 4.2f));
            for (size_t i = 0; i < starts.size(); ++i) {
                if (i) ImGui::SameLine();
                ImGui::PushID(int(i));
                if (ui::button("", starts[i].first.c_str() , i == 0)) {
                    if (starts[i].second < 0) { gameHud_.reset(); ed.startClientPlay(); }
                    else ed.startPlay(starts[i].second);
                }
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(avail);
            ed.gameViewport = nullptr;
            return;
        }
        // screen rect: whole area, or the device screen fitted with a bezel
        const DeviceProfile* dev = findDevice(ed.gameDevice);
        float sw = avail.x, sh = avail.y, sx = origin.x, sy = origin.y, k = 1;
        float devW = 0, devH = 0;
        if (dev && dev->w > 0) {
            devW = float(dev->w); devH = float(dev->h);
            bool portraitNative = dev->w < dev->h;
            if (portraitNative == ed.gameLandscape) std::swap(devW, devH);
            const float bezel = dev->touch ? 14 : 6;
            k = std::min((avail.x - 2 * bezel - 8) / devW, (avail.y - 2 * bezel - 8) / devH);
            k = std::max(k, 0.05f);
            sw = devW * k; sh = devH * k;
            sx = origin.x + (avail.x - sw) * 0.5f; sy = origin.y + (avail.y - sh) * 0.5f;
            float r = dev->corner * k;
            dl->AddRectFilled(ImVec2(sx - bezel, sy - bezel), ImVec2(sx + sw + bezel, sy + sh + bezel), IM_COL32(12, 12, 14, 255), r + bezel);
        }
        if (sw <= 1 || sh <= 1) return;
        const float dpi = ImGui::GetIO().DisplayFramebufferScale.x;
        vp_.ensure(ed.device(), int(sw * dpi), int(sh * dpi));
        if (ed.clientPlaying()) { drawClient(ed, dev, dl, origin, avail, sx, sy, sw, sh, k, devW, devH); return; }
        ImGui::SetCursorScreenPos(ImVec2(sx, sy));
        vp_.originX = sx; vp_.originY = sy; vp_.pointsW = sw; vp_.pointsH = sh;
        // the image command goes first so the overlays drawn by renderGame (health bars, markers) end up on
        // top of it; the GPU pass itself runs now, before ImGui renders the frame
        vp_.image(ed.device(), sw, sh);
        dl->PushClipRect(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), true); // overlays of off-screen units stay inside
        ed.renderGame(vp_, ed.gameCam);
        dl->PopClipRect();
        vp_.drawnFrame = ImGui::GetFrameCount();
        ed.gameViewport = &vp_;
        if (dev && dev->w > 0) deviceChrome(dl, *dev, ed.gameLandscape, ed.gameSafeArea, sx, sy, sw, sh, k);
        region_.configure(ed.ws().project.manifest);
        if (region_.enabled) region_.refresh(*ed.sim(), *ed.simModule(), uint8_t(ed.humanSlot()));
        entityActions_.refresh(*ed.sim(),*ed.simModule(),uint8_t(ed.humanSlot()),ed.ws().project.manifest["entityActionControls"]);
        gameHud_.prepare(ed, vp_, dpi * k, dev && dev->touch, region_, scriptPointAbility_,entityActions_);
        if (int ability = gameHud_.takePointAbility(); ability >= 0) {
            scriptPointAbility_ = ability; scriptDragStart_ = ImGui::GetIO().MousePos; region_.cancel();
        }
        const bool cardClick = scriptCard(ed, sx + sw, sy + sh);
        if (!cardClick) input(ed);
        if (boxing_) dl->AddRect(boxA_, ImGui::GetIO().MousePos, IM_COL32(120, 255, 120, 220));
        gameHud_.render(ed, vp_);
        gameHud_.finish(ed);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + avail.y));
        ImGui::Dummy(ImVec2(0, 0));
    }

private:
    std::string filter_;
    double dt_ = 0;

    // The hosted game client (ClientHost.h): its frame into vp_, the device frame around it, a loading bar.
    void drawClient(Editor& ed, const DeviceProfile* dev, ImDrawList* dl, ImVec2 origin, ImVec2 avail, float sx, float sy, float sw, float sh,
                    float k, float devW, float devH) {
        auto& host = ed.clientHost();
        const bool device = dev && dev->w > 0;
        platform::Insets safe;
        if (device) {
            const bool rotated = (dev->w < dev->h) == ed.gameLandscape;
            const Insets4& in = rotated ? dev->landscape : dev->portrait;
            safe = {in.left, in.top, in.right, in.bottom};
        }
        vp_.originX = sx; vp_.originY = sy; vp_.pointsW = sw; vp_.pointsH = sh;
        host.place(sx, sy, device ? k : 1, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
        const bool loading = host.loading();
        host.frame(dt_, vp_.color, vp_.depth, vp_.w, vp_.h, device ? devW : sw, device ? devH : sh, device && dev->touch, safe);
        ImGui::SetCursorScreenPos(ImVec2(sx, sy));
        if (loading) {
            dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), IM_COL32(16, 18, 22, 255));
            const float bw = std::min(sw * 0.6f, 320.0f), by = sy + sh * 0.5f;
            dl->AddRectFilled(ImVec2(sx + (sw - bw) * 0.5f, by), ImVec2(sx + (sw + bw) * 0.5f, by + 6), IM_COL32(60, 64, 72, 255), 3);
            dl->AddRectFilled(ImVec2(sx + (sw - bw) * 0.5f, by), ImVec2(sx + (sw - bw) * 0.5f + bw * host.progress(), by + 6), IM_COL32(90, 160, 255, 255), 3);
            const char* label = "Загрузка игры…";
            const ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(sx + (sw - ts.x) * 0.5f, by - ts.y - 8), IM_COL32(220, 224, 230, 255), label);
            ImGui::Dummy(ImVec2(sw, sh));
        } else {
            vp_.image(ed.device(), sw, sh);
            // the screen is the game's: presses focus this window and never drag it or reach other items
            ImGui::SetCursorScreenPos(ImVec2(sx, sy));
            ImGui::InvisibleButton("##clientscreen", ImVec2(sw, sh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        }
        vp_.drawnFrame = ImGui::GetFrameCount();
        ed.gameViewport = &vp_;
        if (device) deviceChrome(dl, *dev, ed.gameLandscape, ed.gameSafeArea, sx, sy, sw, sh, k);
        if (host.quitRequested()) ed.stopClientPlay();   // the game's own "Quit"
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + avail.y));
        ImGui::Dummy(ImVec2(0, 0));
    }

    void toolbar(Editor& ed) {
        const float bottom = ui::beginToolbar();
        ui::beginSegment();
        if (ed.clientPlaying()) {
            if (ui::toolButton(ICON_GAME, "Идёт игра как в клиенте", true)) {}
            ImGui::SameLine();
            if (ui::toolButton(ICON_REFRESH, "Перезапустить игру (данные проекта перечитываются)")) { ed.stopClientPlay(); ed.startClientPlay(); }
            ImGui::SameLine();
            if (ui::toolButton(ICON_STOP, "Стоп")) ed.stopClientPlay();
        } else if (!ed.playing()) {
            if (ui::toolButton(ICON_GAME, "Играть как в клиенте игры, в этом окне: меню, экраны и скрипты игры, её ввод и звук")) { gameHud_.reset(); ed.startClientPlay(); }
            ImGui::SameLine();
            if (ui::toolButton(ICON_EXTERNAL, "Играть в клиенте игры: отдельное окно AiverseGame с меню, скриптами и вводом игры, как у игроков")) ed.launchClient();
            ImGui::SameLine();
            if (ui::toolButton(ICON_BOT, "Боты против ботов")) ed.startPlay(0);
            for (size_t p = 0; p < ed.doc().data.players.size(); ++p) {
                if (ed.doc().data.players[p].waves) continue;
                ImGui::SameLine();
                ImGui::PushID(int(p));
                if (ui::toolButton(ICON_USER, ("Играть за " + ed.doc().data.players[p].name).c_str())) ed.startPlay(int(p + 1));
                ImGui::PopID();
            }
        } else {
            if (ui::toolButton(ICON_PLAY, "Идёт тест", !ed.playPaused)) ed.playPaused = false;
            ImGui::SameLine();
            if (ui::toolButton(ICON_PAUSE, ed.playPaused ? "Продолжить" : "Пауза", ed.playPaused)) ed.playPaused = !ed.playPaused;
            ImGui::SameLine();
            if (ui::toolButton(ICON_STOP, "Стоп")) ed.stopPlay();
        }
        ui::endSegment();
        if (ed.playing()) {
            ImGui::SameLine(0, 10);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled(ICON_SPEED);
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(130);
            ImGui::SliderFloat("##speed", &ed.playSpeed, 0.25f, 16.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Скорость симуляции");
        }
        ui::toolbarDivider();
        const DeviceProfile* cur = findDevice(ed.gameDevice);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(cur && cur->w > 0 ? (cur->touch ? ICON_SMARTPHONE : ICON_MONITOR) : ICON_MONITOR);
        ImGui::SameLine(0, 4);
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##device", cur ? cur->name : "Свободный размер", ImGuiComboFlags_HeightLarge)) {
            if (ImGui::IsWindowAppearing()) { filter_.clear(); ImGui::SetKeyboardFocusHere(); }
            ui::searchField("##devfilter", filter_, "Устройство");
            auto lower = [](std::string v) { for (auto& c : v) c = char(std::tolower((unsigned char)c)); return v; };
            const std::string f = lower(filter_);
            const char* group = nullptr;
            for (auto& d : kDevices) {
                if (!f.empty() && lower(d.name).find(f) == std::string::npos && lower(d.id).find(f) == std::string::npos) continue;
                if (d.group[0] && (!group || std::string(group) != d.group)) { group = d.group; ImGui::SeparatorText(group); }
                char label[160];
                if (d.w > 0) std::snprintf(label, sizeof label, "%s##%s", d.name, d.id); else std::snprintf(label, sizeof label, "%s", d.name);
                if (ImGui::Selectable(label, ed.gameDevice == d.id)) ed.gameDevice = d.id;
                if (d.w > 0) {
                    ImGui::SameLine(250);
                    ImGui::TextDisabled("%dx%d @%.3gx", d.w, d.h, d.scale);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ui::beginSegment();
        if (ui::toolButton(ICON_LANDSCAPE, "Альбомная", ed.gameLandscape)) ed.gameLandscape = true;
        ImGui::SameLine();
        if (ui::toolButton(ICON_PORTRAIT, "Портретная", !ed.gameLandscape)) ed.gameLandscape = false;
        ui::endSegment();
        if (cur && cur->w > 0) {
            ImGui::SameLine(0, 8);
            if (ui::toolButton(ICON_SAFE_AREA, "Safe area: показать безопасную зону (вырез, статус-бар, полоска «домой»). HUD должен быть внутри рамки.", ed.gameSafeArea))
                ed.gameSafeArea = !ed.gameSafeArea;
        }
        ui::endToolbar(bottom);
    }

    // Device frame: rounded corners, the camera cut-out and (on demand) the safe area. Cut-outs are
    // described in the natural orientation; rotated devices map natural (x, y) to screen (y, W - x)
    // so the cut-out ends up on the left in landscape.
    void deviceChrome(ImDrawList* dl, const DeviceProfile& d, bool landscape, bool showSafe, float sx, float sy, float sw, float sh, float k) {
        const float r = d.corner * k;
        if (r > 0) { // mask the four screen corners outside the rounded display
            const float pi = 3.14159265f;
            struct C { float cx, cy, x, y, a0; } cs[] = {{sx + r, sy + r, sx, sy, pi}, {sx + sw - r, sy + r, sx + sw, sy, 1.5f * pi},
                                                       {sx + sw - r, sy + sh - r, sx + sw, sy + sh, 0}, {sx + r, sy + sh - r, sx, sy + sh, 0.5f * pi}};
            for (auto& c : cs) { // a ring from radius r outwards covers the corner (at most 0.42 r away)
                (void)c.x; (void)c.y;
                dl->PathArcTo(ImVec2(c.cx, c.cy), r * 1.22f, c.a0, c.a0 + 0.5f * pi, 16);
                dl->PathStroke(IM_COL32(12, 12, 14, 255), 0, r * 0.44f + 1.0f);
            }
        }
        const bool naturalPortrait = d.w < d.h;
        const bool rotated = naturalPortrait == landscape;
        const float W = float(d.w);
        auto box = [&](float x0, float y0, float x1, float y1, float round) {
            ImVec2 a, b;
            if (!rotated) { a = ImVec2(sx + x0 * k, sy + y0 * k); b = ImVec2(sx + x1 * k, sy + y1 * k); }
            else { a = ImVec2(sx + y0 * k, sy + (W - x1) * k); b = ImVec2(sx + y1 * k, sy + (W - x0) * k); }
            dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), round * k);
        };
        const float cx = W * 0.5f;
        switch (d.cutout) {
        case Cutout::None: break;
        case Cutout::Notch: box(cx - d.cutW * 0.5f, 0, cx + d.cutW * 0.5f, d.cutH, d.cutH * 0.45f); break;
        case Cutout::Island: box(cx - d.cutW * 0.5f, d.cutY, cx + d.cutW * 0.5f, d.cutY + d.cutH, d.cutH * 0.5f); break;
        case Cutout::PunchCenter: box(cx - d.cutW * 0.5f, d.cutY, cx + d.cutW * 0.5f, d.cutY + d.cutH, d.cutW * 0.5f); break;
        case Cutout::PunchLeft: box(d.cutY + 6, d.cutY, d.cutY + 6 + d.cutW, d.cutY + d.cutH, d.cutW * 0.5f); break;
        case Cutout::Waterdrop: box(cx - d.cutW * 0.5f, 0, cx + d.cutW * 0.5f, d.cutH, d.cutW * 0.5f); break;
        }
        if (!showSafe) return;
        const Insets4 in = deviceInsets(d, landscape);
        const float l = in.left * k, t = in.top * k, rr = in.right * k, bb = in.bottom * k;
        const ImU32 shade = IM_COL32(255, 70, 60, 70), edge = IM_COL32(255, 210, 80, 230);
        if (t > 0) dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + t), shade);
        if (bb > 0) dl->AddRectFilled(ImVec2(sx, sy + sh - bb), ImVec2(sx + sw, sy + sh), shade);
        if (l > 0) dl->AddRectFilled(ImVec2(sx, sy + t), ImVec2(sx + l, sy + sh - bb), shade);
        if (rr > 0) dl->AddRectFilled(ImVec2(sx + sw - rr, sy + t), ImVec2(sx + sw, sy + sh - bb), shade);
        dl->AddRect(ImVec2(sx + l, sy + t), ImVec2(sx + sw - rr, sy + sh - bb), edge, 0, 0, 1.5f);
        char lbl[96];
        std::snprintf(lbl, sizeof lbl, "safe area  L%.0f T%.0f R%.0f B%.0f pt", in.left, in.top, in.right, in.bottom);
        ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddRectFilled(ImVec2(sx + l + 4, sy + sh - bb - ts.y - 8), ImVec2(sx + l + ts.x + 12, sy + sh - bb - 4), IM_COL32(0, 0, 0, 170), 4);
        dl->AddText(ImVec2(sx + l + 8, sy + sh - bb - ts.y - 6), edge, lbl);
    }

    void input(Editor& ed) {
        region_.configure(ed.ws().project.manifest);
        if(region_.enabled){regionInput(ed);return;}
        if (gameHud_.blocked()) { boxing_ = false; return; }
        ImGuiIO& io = ImGui::GetIO();
        RTSCamera& cam = ed.gameCam;
        if (vp_.hovered && io.MouseWheel != 0) cam.distance = std::clamp(cam.distance * std::pow(0.88f, io.MouseWheel), cam.minDistance, cam.maxDistance);
        if (vp_.hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            float x0, z0, x1, z1;
            if (ed.groundAt(vp_, cam, io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y, x0, z0) && ed.groundAt(vp_, cam, io.MousePos.x, io.MousePos.y, x1, z1))
                cam.target += float3{x0 - x1, 0, z0 - z1};
        }
        if (ImGui::IsWindowFocused() && !io.WantTextInput) {
            float speed = cam.distance * io.DeltaTime;
            float3 fwd{-std::sin(cam.yaw), 0, -std::cos(cam.yaw)}, right{std::cos(cam.yaw), 0, -std::sin(cam.yaw)};
            if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) cam.target += fwd * speed;
            if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) cam.target += fwd * -speed;
            if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) cam.target += right * speed;
            if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) cam.target += right * -speed;
        }
        if (ed.humanSlot() <= 0) return;
        World& w = ed.sim()->world();
        const uint8_t me = uint8_t(ed.humanSlot());
        auto pickSim = [&](float mx, float my) {
            float x, z;
            Entity best;
            float bestD = 1e9f;
            if (!ed.groundAt(vp_, cam, mx, my, x, z)) return best;
            w.each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform& t) {
                float dx = float(t.pos.x.toDouble()) - x, dz = float(t.pos.z.toDouble()) - z;
                float r = ed.view().selectionRadius(u.type) + 0.2f, dd = std::sqrt(dx * dx + dz * dz);
                if (dd < r && dd < bestD) { bestD = dd; best = e; }
            });
            return best;
        };
        // a structure chosen on the command card: the next ground click places it (as the client's Place mode)
        if (const int place = gameHud_.placeType(); place >= 0) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { gameHud_.clearPlace(); return; }
            float x, z;
            if (vp_.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ed.groundAt(vp_, cam, io.MousePos.x, io.MousePos.y, x, z)) {
                Command c;
                c.type = game::kCmdBuild;
                c.param = place;
                if (io.KeyShift) c.flags = aiverse::kCmdQueue;
                for (Entity e : ed.gameSelection)
                    if (auto* u = w.tryGet<game::Unit>(e)) {
                        const auto& b = ed.rules().types[u->type].builds;
                        if (std::find(b.begin(), b.end(), uint16_t(place)) != b.end()) { c.units = {e}; break; }
                    }
                const float sx = ed.snapCoord(x, place), sz = ed.snapCoord(z, place);
                c.point = {Fixed::fromRatio(int64_t(sx * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(sz * 256), 256)};
                if (!c.units.empty()) ed.issueCommand(c);
                if (!io.KeyShift) gameHud_.clearPlace();
            }
            return;
        }
        if (vp_.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { boxing_ = true; boxA_ = io.MousePos; }
        if (boxing_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            boxing_ = false;
            ImVec2 b = io.MousePos;
            if (!io.KeyShift) ed.gameSelection.clear();
            if (std::hypot(b.x - boxA_.x, b.y - boxA_.y) < 6) {
                Entity e = pickSim(b.x, b.y);
                if (e.valid() && w.get<Owner>(e).player == me) ed.gameSelection.push_back(e);
            } else {
                float x0 = std::min(boxA_.x, b.x) - vp_.originX, x1 = std::max(boxA_.x, b.x) - vp_.originX;
                float y0 = std::min(boxA_.y, b.y) - vp_.originY, y1 = std::max(boxA_.y, b.y) - vp_.originY;
                w.each<game::Unit, Owner, Transform>([&](Entity e, game::Unit& u, Owner& o, Transform& t) {
                    if (o.player != me || ed.rules().types[u.type].structure) return;
                    float2 s;
                    if (!cam.worldToScreen({float(t.pos.x.toDouble()), 0.5f, float(t.pos.z.toDouble())}, vp_.pointsW, vp_.pointsH, s)) return;
                    if (s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1) ed.gameSelection.push_back(e);
                });
            }
        }
        if (vp_.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ed.gameSelection.empty()) {
            float x, z;
            if (ed.groundAt(vp_, cam, io.MousePos.x, io.MousePos.y, x, z)) {
                Command c;
                c.type = game::kCmdSmart;
                c.units = ed.gameSelection;
                c.point = {Fixed::fromRatio(int64_t(x * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(z * 256), 256)};
                c.target = pickSim(io.MousePos.x, io.MousePos.y);
                if (io.KeyShift) c.flags = aiverse::kCmdQueue;
                ed.issueCommand(c);
            }
        }
        ed.gameSelection.erase(std::remove_if(ed.gameSelection.begin(), ed.gameSelection.end(), [&](Entity e) { return !w.alive(e); }), ed.gameSelection.end());
    }

    GameHud gameHud_;
    game::RegionSwipe region_;
    game::EntityActionControls entityActions_;
    bool entityPointer_=false;
    ImVec2 entityStart_{};
    uint32_t regionTick_=UINT32_MAX;
    double emptyClick_=-10;
    bool regionPanning_=false;
    int scriptPointAbility_=-1;
    ImVec2 scriptDragStart_{};
    ImVec2 regionPanPoint_{};
    void regionInput(Editor& ed){
        auto& io=ImGui::GetIO();auto& cam=ed.gameCam;
        ed.scriptTargeting={};
        region_.refresh(*ed.sim(),*ed.simModule(),uint8_t(ed.humanSlot()));
        if(ed.sim()->tick()<regionTick_){region_.cancel();region_.arrows.clear();region_.ratio=client::loadRegionRatio();scriptPointAbility_=-1;entityActions_.cancel();entityPointer_=false;}
        region_.fit(cam,vp_.pointsW,vp_.pointsH,ed.sim()->tick()<regionTick_);regionTick_=ed.sim()->tick();
        auto point=[&]{float x=-1,z=-1;ed.groundAt(vp_,cam,io.MousePos.x,io.MousePos.y,x,z);return float3{x,.2f,z};};
        if(vp_.hovered&&!region_.dragging)region_.cursor=point();
        if(vp_.hovered&&!gameHud_.blocked()&&(ImGui::IsMouseClicked(ImGuiMouseButton_Right)||ImGui::IsMouseClicked(ImGuiMouseButton_Middle))){regionPanning_=true;regionPanPoint_=io.MousePos;region_.cancel();}
        if(regionPanning_){
            float3 a,b;
            if(cam.screenToGround(regionPanPoint_.x-vp_.originX,regionPanPoint_.y-vp_.originY,vp_.pointsW,vp_.pointsH,a)&&cam.screenToGround(io.MousePos.x-vp_.originX,io.MousePos.y-vp_.originY,vp_.pointsW,vp_.pointsH,b))cam.target+=a-b;
            regionPanPoint_=io.MousePos;
            if(!ImGui::IsMouseDown(ImGuiMouseButton_Right)&&!ImGui::IsMouseDown(ImGuiMouseButton_Middle))regionPanning_=false;
        }
        bool buttons=gameHud_.blocked();
        if(vp_.hovered&&!buttons&&io.MouseWheel!=0){auto before=point();cam.distance=std::clamp(cam.distance*std::exp(-.14f*io.MouseWheel),cam.minDistance,cam.maxDistance);auto after=point();if(before.x>=0&&after.x>=0)cam.target+=before-after;}
        const bool targetWorld=!gameHud_.pointerOverUi()&&!ed.playPaused&&io.MousePos.x>=vp_.originX&&io.MousePos.y>=vp_.originY&&io.MousePos.x<vp_.originX+vp_.pointsW&&io.MousePos.y<vp_.originY+vp_.pointsH&&ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if(entityPointer_ && (ed.playPaused || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))){entityActions_.cancel();entityPointer_=false;buttons=true;}
        if(entityPointer_){
            buttons=true;
            if(const auto* source=entityActions_.source()){auto p=point();ed.scriptTargeting={source->abilityId,p.x,p.z,true,targetWorld};ed.scriptTargeting.caster=source->entity.packed();}
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                auto cmd=entityActions_.release(point(),targetWorld,std::hypot(io.MousePos.x-entityStart_.x,io.MousePos.y-entityStart_.y)>=6);
                if(cmd)ed.issueCommand(*cmd);
                entityPointer_=false;
            }
        } else if(targetWorld && !buttons && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            entityActions_.press(cam,vp_.pointsW,vp_.pointsH,io.MousePos.x-vp_.originX,io.MousePos.y-vp_.originY)){
            entityPointer_=true;entityStart_=io.MousePos;region_.cancel();buttons=true;
        }
        if(scriptPointAbility_>=0){
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Right)||ImGui::IsKeyPressed(ImGuiKey_Escape))scriptPointAbility_=-1;
            else if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                auto p=point();
                if(targetWorld&&std::hypot(io.MousePos.x-scriptDragStart_.x,io.MousePos.y-scriptDragStart_.y)>=6){
                Command c;c.type=game::kCmdPlayerAbility;c.param=scriptPointAbility_;
                c.point={Fixed::fromRatio(int64_t(p.x*256),256),Fixed{},Fixed::fromRatio(int64_t(p.z*256),256)};
                ed.issueCommand(c);}
                scriptPointAbility_=-1;buttons=true;
            }
            else {
                auto p=point();ed.scriptTargeting={ed.simModule()->rules().playerAbilities[size_t(scriptPointAbility_)].id,p.x,p.z,true,targetWorld};
                buttons=true;
            }
        }
        if(ed.humanSlot()>0){
            if(vp_.hovered&&!buttons&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){boxA_=io.MousePos;region_.begin(point());}
            if(region_.dragging&&ImGui::IsMouseDown(ImGuiMouseButton_Left))region_.move(point());
            if(ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                auto p=point();if(vp_.hovered&&!buttons&&region_.region(p.x,p.z)<0&&std::hypot(io.MousePos.x-boxA_.x,io.MousePos.y-boxA_.y)<6){if(ed.time()-emptyClick_<.3)region_.fit(cam,vp_.pointsW,vp_.pointsH,true);emptyClick_=ed.time();}
                if(!buttons&&std::hypot(io.MousePos.x-boxA_.x,io.MousePos.y-boxA_.y)<6)region_.cancelFlow(*ed.sim(),*ed.simModule(),p,[&](Command c){ed.issueCommand(c);});
                if(buttons)region_.cancel();else{region_.cursor=p;region_.release(ed.time(),[&](Command c){ed.issueCommand(c);});}
            }
        }
        if (gameHud_.blocked()) return;
        auto* dl=ImGui::GetWindowDrawList();auto color=game::RtsView::playerColor(ed.humanSlot());
        auto packed=ImGui::ColorConvertFloat4ToU32(ImVec4(color.x,color.y,color.z,1));
        for(auto q:region_.controlFlags(cam,vp_.pointsW,vp_.pointsH)){
            const float x=q.x+vp_.originX,y=q.y+vp_.originY;
            dl->AddLine(ImVec2(x,y),ImVec2(x,y-26),IM_COL32(20,35,50,230),5);
            dl->AddLine(ImVec2(x,y),ImVec2(x,y-26),IM_COL32(245,245,235,255),2);
            dl->AddRectFilled(ImVec2(x+1,y-25),ImVec2(x+20,y-12),packed,2);
            dl->AddRect(ImVec2(x+1,y-25),ImVec2(x+20,y-12),IM_COL32(255,255,255,240),2,0,1.5f);
        }
        const auto strokes=region_.strokes(cam,vp_.pointsW,vp_.pointsH,ed.time(),[&](float x,float z){return ed.view().heightAt(ed.simModule()->state(*ed.sim()).grid,x,z);});
        for(int layer=0;layer<2;++layer)for(const auto& stroke:strokes){
            if(stroke.shadow!=(layer==0))continue;
            const bool pass=layer==1;
            const float light=.35f+.65f*stroke.highlight,tint=1-light;
            const auto c=pass?ImVec4(light+tint*color.x,light+tint*color.y,light+tint*color.z,stroke.alpha):ImVec4(.07f,.12f,.18f,stroke.alpha);
            dl->AddLine(ImVec2(vp_.originX+stroke.a.x,vp_.originY+stroke.a.y),ImVec2(vp_.originX+stroke.b.x,vp_.originY+stroke.b.y),ImGui::ColorConvertFloat4ToU32(c),stroke.width);
        }
        for(const auto& u:region_.accepted()){
            float2 q;if(cam.worldToScreen({float(u.x.toDouble()),.12f,float(u.z.toDouble())},vp_.pointsW,vp_.pointsH,q)){
                ImVec2 c(q.x+vp_.originX,q.y+vp_.originY);dl->AddCircleFilled(c,7,IM_COL32(70,220,255,65),16);dl->AddCircle(c,7,IM_COL32(100,235,255,255),16,2);
            }
        }
        for(const auto& u:region_.preview()){
            float2 q;if(cam.worldToScreen({float(u.x.toDouble()),.12f,float(u.z.toDouble())},vp_.pointsW,vp_.pointsH,q)){
                ImVec2 c(q.x+vp_.originX,q.y+vp_.originY);dl->AddCircleFilled(c,7,IM_COL32(255,230,120,65),16);dl->AddCircle(c,7,IM_COL32(255,245,170,255),16,2);
            }
        }
        if(region_.dragging){
            for(int r:region_.sources)if(region_.anchors.count(r)){float2 q;if(cam.worldToScreen(region_.anchors[r].point,vp_.pointsW,vp_.pointsH,q))dl->AddCircle(ImVec2(q.x+vp_.originX,q.y+vp_.originY),18,packed,24,3);}
        }
    }
    // rules "commands" (script commands) for the selection, bottom right: buttons + hotkeys; a command with
    // a point / unit argument waits for a left click in the view (like the game client, ClientScriptCmd.cpp)
    bool scriptCard(Editor& ed, float right, float bottom) {
        Simulation* sim = ed.sim();
        auto* m = ed.simModule();
        game::ScriptEngine* se = m ? m->scripts() : nullptr;
        if (!sim || !se || se->commands().empty() || ed.humanSlot() <= 0) return false;
        World& w = sim->world();
        const uint8_t me = uint8_t(ed.humanSlot());
        const auto& cs = se->commands();
        auto fits = [&](const game::ScriptCommandDef& d) {
            if (!d.card) return false;
            if (!d.needUnits) return true;
            for (Entity e : ed.gameSelection) {
                if (!w.alive(e) || !w.has<game::Unit>(e)) continue;
                const uint16_t t = w.get<game::Unit>(e).type;
                if ((d.types.empty() && !d.tags) || std::find(d.types.begin(), d.types.end(), t) != d.types.end() || (d.tags && (ed.rules().types[t].tags & d.tags))) return true;
            }
            return false;
        };
        auto send = [&](int i, Entity target, Vec3 pt, bool havePt) {
            const auto& d = cs[size_t(i)];
            Command c;
            c.type = game::kCmdScript; c.param = i; c.units = ed.gameSelection;
            for (size_t a = 0; a < d.args.size(); ++a) {
                const auto& ad = d.args[a];
                if (ad.kind == game::ScriptArgDef::Point) { if (int(a) == d.pick && !havePt) return; c.args.push_back(pt.x.raw); c.args.push_back(pt.z.raw); }
                else if (ad.kind == game::ScriptArgDef::Unit || ad.kind == game::ScriptArgDef::Entity) { if (int(a) == d.pick && !target.valid()) return; c.args.push_back(int64_t(target.packed())); }
                else c.args.push_back(ad.def);
            }
            ed.issueCommand(c);
        };
        auto start = [&](int i) { if (cs[size_t(i)].pick >= 0) pending_ = i; else send(i, kNullEntity, Vec3{}, false); };
        bool used = false;
        ImGuiIO& io = ImGui::GetIO();
        if (pending_ >= 0 && size_t(pending_) < cs.size() && vp_.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float x, z;
            Vec3 pt{};
            const bool have = ed.groundAt(vp_, ed.gameCam, io.MousePos.x, io.MousePos.y, x, z);
            if (have) pt = {Fixed::fromRatio(int64_t(x * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(z * 256), 256)};
            Entity t;
            float best = 1e9f;
            if (have) w.each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform& tr) {
                float dx = float(tr.pos.x.toDouble()) - x, dz = float(tr.pos.z.toDouble()) - z, dd = std::sqrt(dx * dx + dz * dz);
                if (dd < ed.view().selectionRadius(u.type) + 0.2f && dd < best) { best = dd; t = e; }
            });
            send(pending_, t, pt, have);
            if (!io.KeyShift) pending_ = -1;
            used = true;
        }
        if (pending_ >= 0 && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape))) { pending_ = -1; used = true; }
        // rules "mapClick": a plain left click on the map sends the game's tap command
        if (const int tap = game::mapClickCommand(cs); !used && pending_ < 0 && tap >= 0 && vp_.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float x, z;
            if (ed.groundAt(vp_, ed.gameCam, io.MousePos.x, io.MousePos.y, x, z)) {
                if (se->commandCooldown(*sim, me, tap).raw <= 0)
                    send(tap, kNullEntity, Vec3{Fixed::fromRatio(int64_t(x * 256), 256), Fixed{}, Fixed::fromRatio(int64_t(z * 256), 256)}, true);
                used = true;
            }
        }
        std::vector<int> shown;
        for (size_t i = 0; i < cs.size(); ++i) if (fits(cs[i])) shown.push_back(int(i));
        if (ImGui::IsWindowFocused() && !io.WantTextInput)
            for (int i : shown) {
                const std::string& h = cs[size_t(i)].hotkey;
                if (h.size() != 1 || !std::isalnum(static_cast<unsigned char>(h[0]))) continue;
                const char c = char(std::toupper(static_cast<unsigned char>(h[0])));
                const ImGuiKey key = c >= 'A' ? ImGuiKey(ImGuiKey_A + (c - 'A')) : ImGuiKey(ImGuiKey_0 + (c - '0'));
                if (ImGui::IsKeyPressed(key, false) && se->commandCooldown(*sim, me, i).raw <= 0) start(i);
            }
        if (shown.empty()) return used;
        const float bw = 170, bh = ImGui::GetFrameHeight();
        float y = bottom - 8 - float(shown.size()) * (bh + 4);
        for (int i : shown) {
            const auto& d = cs[size_t(i)];
            const Fixed cd = se->commandCooldown(*sim, me, i);
            std::string label = d.label + (d.hotkey.empty() ? "" : "  [" + d.hotkey + "]");
            if (cd.raw > 0) { char b[24]; std::snprintf(b, sizeof b, "  %.0f s", cd.toDouble() + 0.5); label += b; }
            if (pending_ == i) label = "> " + label;
            ImGui::SetCursorScreenPos(ImVec2(right - bw - 8, y));
            ImGui::PushID(i);
            ImGui::BeginDisabled(cd.raw > 0);
            if (ImGui::Button(label.c_str(), ImVec2(bw, bh))) { start(i); used = true; }
            ImGui::EndDisabled();
            if (!d.tip.empty()) ImGui::SetItemTooltip("%s", d.tip.c_str());
            if (ImGui::IsItemHovered()) used = true;   // no box selection under the card
            ImGui::PopID();
            y += bh + 4;
        }
        return used;
    }
    int pending_ = -1;

    Viewport vp_;
    bool boxing_ = false;
    ImVec2 boxA_{};
};

AIVERSE_EDITOR_WINDOW(GameWindow);

} // namespace aiverse::editor
