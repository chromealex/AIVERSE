// Editor shell: main menu, game picker, status bar, agent toasts.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "Editor.h"
#include "core/Projects.h"
#include "EditorUtil.h"
#include "imgui.h"
#include "imgui_internal.h" // MenuItemEx / BeginMenuEx: icons in menus

namespace fs = std::filesystem;

namespace rts::editor {

namespace {
// Menu item with an icon in the icon column (imgui_internal MenuItemEx).
bool item(const char* icon, const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true) {
    return ImGui::MenuItemEx(label, icon, shortcut, selected, enabled);
}
bool menu(const char* icon, const char* label, bool enabled = true) { return ImGui::BeginMenuEx(label, icon, enabled); }

// Rounded "pill": a coloured dot + text; returns its width (drawn at the cursor).
float pillWidth(const char* text) { return ImGui::CalcTextSize(text).x + ImGui::GetFontSize() + 18; }
void pill(const char* text, ImVec4 dot, float pulse, const EditorTheme::Palette& pal) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = pillWidth(text), h = ImGui::GetFrameHeight() - 2;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(pal.frame), h * 0.5f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(pal.border), h * 0.5f);
    const ImVec2 c(p.x + 12, p.y + h * 0.5f);
    if (pulse > 0) { dot.w = 0.35f * pulse; dl->AddCircleFilled(c, 4 + 4 * pulse, ImGui::GetColorU32(dot)); dot.w = 1; }
    dl->AddCircleFilled(c, 4, ImGui::GetColorU32(dot));
    dl->AddText(ImVec2(p.x + 22, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(pal.text), text);
    ImGui::Dummy(ImVec2(w, h));
}

// Top menu groups of windows (EditorWindow::group()): menu title, icon, workspace name.
struct WindowGroup { const char* id; const char* menu; const char* icon; const char* workspace; };
const WindowGroup kGroups[] = {
    {"scene", "Сцена", ICON_SCENE, "Карта и объекты"},
    {"world", "Мир", ICON_TERRAIN, "Ландшафт"},
    {"look", "Графика", ICON_LIGHTING, "Графика и эффекты"},
    {"logic", "Логика", ICON_RULES, "Правила и тест"},
    {"publish", "Сборка", ICON_BUILD, "Выпуск"},
};
bool knownGroup(const std::string& g) {
    for (auto& k : kGroups) if (g == k.id) return true;
    return g == "service";
}
} // namespace

std::vector<EditorWindow*> Editor::windowsOf(const std::string& group) const {
    std::vector<EditorWindow*> out;
    for (auto& w : windows_)
        if (group == "other" ? !knownGroup(w->group()) : group == w->group()) out.push_back(w.get());
    std::stable_sort(out.begin(), out.end(), [](EditorWindow* a, EditorWindow* b) {
        if (a->openByDefault() != b->openByDefault()) return a->openByDefault();
        return a->order() < b->order();
    });
    return out;
}

void Editor::openWorkspace(const std::string& group) {
    // the default windows plus every window of the group, docked by their areas; the group's windows in front
    pendingFocus_.clear();
    for (auto& w : windows_) {
        const bool mine = group == w->group();
        w->open = w->openByDefault() || mine;
        if (mine) pendingFocus_.push_back(w->id());
    }
    resetLayout_ = true;
    pendingFocusFrames_ = 3;
    for (auto& g : kGroups) if (group == g.id) status = std::string("Раскладка «") + g.workspace + "»";
}

void Editor::drawWindowItems(const std::string& group) {
    for (EditorWindow* w : windowsOf(group)) {
        const char* icon = w->icon() ? w->icon() : windowIconFor(w->id());
        // click: open and bring to front; the check mark shows open windows, a second click on an open window hides it
        if (item(icon, w->title(), nullptr, w->open)) { if (w->open && isWindowInFront(w)) w->open = false; else focusWindow(w->id()); }
    }
}

bool Editor::isWindowInFront(EditorWindow* w) const {
    ImGuiWindow* iw = ImGui::FindWindowByName(w->imguiName());
    if (!iw) return false;
    if (iw->DockNode && iw->DockNode->TabBar) return iw->DockNode->TabBar->SelectedTabId == iw->TabId;
    return !iw->Hidden;
}

void Editor::drawMenuBar() {
    // a slim top bar: menus, breadcrumb, play controls in the centre, the agent link on the right
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, theme_.palette().appBg);
    const bool bar = ImGui::BeginMainMenuBar();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!bar) return;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 6));
    ImGui::TextColored(theme_.palette().accent, ICON_CASTLE);
    ImGui::PopStyleVar();
    // ---- Файл: project + map documents
    if (ImGui::BeginMenu("Файл")) {
        ImGui::SeparatorText("Проект");
        if (item(ICON_ZAP, "Сделать игру…")) showProjectHub(2);
        if (item(ICON_PLUS, "Новый проект…")) showProjectHub(1);
        if (item(ICON_FOLDER_OPEN, "Открыть проект…")) showProjectHub(0);
        for (auto& window : windows_) if (std::string(window->id()) == "project_import")
            if (item(ICON_FOLDER_OPEN, window->title())) focusWindow("project_import");
        if (menu(ICON_CLOCK, "Недавние проекты")) {
            int n = 0;
            for (auto& pr : recentProjects()) {
                if (!pr.exists || ++n > 12) continue;
                if (ImGui::MenuItem(pr.name.c_str(), nullptr, pr.dir == ws_.project.dir)) requestOpenProject(pr.file.empty() ? pr.dir : pr.file);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (pr.file.empty() ? pr.dir : pr.file).c_str());
            }
            if (n == 0) ImGui::TextDisabled("пока пусто");
            ImGui::EndMenu();
        }
        if (item(ICON_REFRESH, "Перечитать правила с диска")) {
            std::string err;
            if (ws_.project.load(ws_.project.dir, &err)) { reloadRules(); status = "Правила перечитаны"; } else status = "Ошибка: " + err;
        }
        ImGui::SeparatorText("Карта");
        if (menu(ICON_FILE_PLUS, "Новая карта")) {
            static int sz[2] = {64, 48};
            ImGui::TextDisabled("Размер в клетках (до %d)", game::kMaxMapSize);
            ImGui::SetNextItemWidth(160);
            ImGui::InputInt2("##newsize", sz);
            static const int presets[][2] = {{32, 32}, {64, 48}, {96, 96}, {128, 128}, {256, 256}};
            for (auto& pr : presets) {
                char b[24]; std::snprintf(b, sizeof b, "%d×%d", pr[0], pr[1]);
                if (&pr != &presets[0]) ImGui::SameLine();
                if (ImGui::SmallButton(b)) { sz[0] = pr[0]; sz[1] = pr[1]; }
            }
            if (ui::button(ICON_PLUS, "Создать", true)) { newMap(std::clamp(sz[0], 8, game::kMaxMapSize), std::clamp(sz[1], 8, game::kMaxMapSize)); ImGui::CloseCurrentPopup(); }
            ImGui::EndMenu();
        }
        if (menu(ICON_FOLDER_OPEN, "Открыть карту")) {
            std::error_code ec;
            std::vector<std::string> files;
            for (auto& e : fs::directory_iterator(ws_.mapsDir(), ec)) if (e.path().extension() == ".json") files.push_back(e.path().filename().string());
            std::sort(files.begin(), files.end());
            for (auto& f : files) if (item(ICON_MAP_PIN, f.c_str(), nullptr, fs::path(doc().path).filename() == f)) openMap(ws_.mapsDir() + "/" + f);
            if (files.empty()) ImGui::TextDisabled("в maps/ нет карт");
            ImGui::EndMenu();
        }
        if (item(ICON_SAVE, "Сохранить карту", "Ctrl+S", false, !doc().path.empty())) saveMap(doc().path);
        if (menu(ICON_FILE, "Сохранить карту как")) {
            static std::string name = "new_map.json";
            ImGui::TextDisabled("Файл в maps/");
            ImGui::SetNextItemWidth(220);
            ui::inputString("##saveas", name);
            if (ui::button(ICON_SAVE, "Сохранить", true)) {
                std::string fn = name;
                if (fs::path(fn).extension() != ".json") fn += ".json";
                saveMap(ws_.mapsDir() + "/" + fn);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }
        if (item(ICON_PLUS, "Добавить карту в список игры")) {
            Json p = Json::object(); p.set("addToGame", Json::fromBool(true));
            Json r = ws_.call("map_save", p);
            status = r.has("error") ? "Ошибка: " + r.getStr("error") : "Карта в списке игры";
        }
        ImGui::Separator();
        if (item(ICON_LINK, "Открывать .rtsproj этим редактором", nullptr, fileAssoc().registered, fileAssoc().supported))
            registerFileAssoc();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", fileAssoc().detail.c_str());
        ImGui::Separator();
        if (item(ICON_EXIT, "Выход")) quit_ = true;
        ImGui::EndMenu();
    }
    // ---- Правка
    if (ImGui::BeginMenu("Правка")) {
        if (item(ICON_UNDO, "Отменить", "Ctrl+Z", false, doc().canUndo() && !playing())) { doc().undo(); selection = {}; }
        if (item(ICON_REDO, "Повторить", "Ctrl+Y", false, doc().canRedo() && !playing())) { doc().redo(); selection = {}; }
        ImGui::Separator();
        if (item(ICON_TRASH, "Удалить выбранный объект", "Del", false, selectedEntity() >= 0)) { doc().removeEntity(selectedEntity()); selection = {}; }
        if (item(ICON_FIT, "Показать всю карту", "F")) fitScene();
        ImGui::SeparatorText("Симметрия");
        if (item(ICON_COPY, "Зеркально: левая половина → правая")) { doc().mirrorHorizontal(); status = "Отражено, игроки 1 и 2 поменяны в копии"; }
        if (item(ICON_ROTATE, "Центральная симметрия левой половины")) { doc().mirrorPoint(); status = "Отражено (поворот на 180°)"; }
        ImGui::SeparatorText("Рельеф");
        if (item(ICON_LEVELS, "Достроить рампы через край плато")) { doc().beginEdit(); doc().autoRamps(); }
        ImGui::Separator();
        if (item(ICON_CIRCLE_CHECK, "Проверить карту…")) { select({Selection::MapProps, -1, {}}); focusWindow("inspector"); }
        ImGui::EndMenu();
    }
    // ---- Вид: general windows, workspaces, theme
    if (ImGui::BeginMenu("Вид")) {
        drawWindowItems("service");
        if (!windowsOf("other").empty() && menu(ICON_WINDOW, "Другие окна")) { drawWindowItems("other"); ImGui::EndMenu(); }
        ImGui::SeparatorText("Раскладки");
        for (auto& g : kGroups) if (item(g.icon, g.workspace)) openWorkspace(g.id);
        if (item(ICON_LAYOUT, "Сбросить раскладку")) { for (auto& w : windows_) w->open = w->openByDefault(); resetLayout_ = true; }
        ImGui::SeparatorText("Сцена");
        ImGui::MenuItem("Уровни и рампы", nullptr, &showLevels);
        ImGui::MenuItem("Регионы", nullptr, &showRegions);
        ImGui::MenuItem("Зоны триггеров", nullptr, &showAreas);
        ImGui::MenuItem("Привязка к сетке", nullptr, &snap);
        ImGui::MenuItem("Камера следует за правками агента", nullptr, &followAgent);
        ImGui::Separator();
        if (menu(ICON_PALETTE, "Тема")) { theme_.drawMenu(); ImGui::EndMenu(); }
        ImGui::EndMenu();
    }
    // ---- window groups
    for (auto& g : kGroups) {
        if (std::string(g.id) == "publish") continue;   // in «Сборка» together with the build targets
        if (!ImGui::BeginMenu(g.menu)) continue;
        drawWindowItems(g.id);
        ImGui::Separator();
        if (item(ICON_LAYOUT, (std::string("Раскладка «") + g.workspace + "»").c_str())) openWorkspace(g.id);
        if (std::string(g.id) == "logic") {
            ImGui::SeparatorText("Тест");
            if (!playing()) {
                if (item(ICON_BOT, "Боты против ботов", "F5")) startPlay(0);
                for (size_t p = 0; p < doc().data.players.size(); ++p)
                    if (!doc().data.players[p].waves && item(ICON_USER, ("Играть за " + doc().data.players[p].name).c_str())) startPlay(int(p + 1));
            } else if (item(ICON_STOP, "Остановить тест", "F5")) stopPlay();
            if (menu(ICON_SMARTPHONE, "Запустить игру на устройстве")) {
                static const char* devices[][2] = {{"desktop-1080", "ПК 1920×1080"}, {"iphone-15", "iPhone 15"}, {"iphone-se", "iPhone SE"}, {"ipad", "iPad"},
                                                   {"pixel-8", "Pixel 8"}, {"android-low", "Слабый Android 720p"}};
                for (auto& d : devices)
                    if (ImGui::BeginMenu(d[1])) {
                        if (item(ICON_LANDSCAPE, "Альбомная")) launchOnDevice(d[0], true);
                        if (item(ICON_PORTRAIT, "Портретная")) launchOnDevice(d[0], false);
                        ImGui::EndMenu();
                    }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }
    // ---- Сборка: build windows and targets
    if (ImGui::BeginMenu("Сборка")) {
        drawWindowItems("publish");
        if (item(ICON_LAYOUT, "Раскладка «Выпуск»")) openWorkspace("publish");
        ImGui::SeparatorText("Собрать");
        ImGui::BeginDisabled(ws_.building());
        static const char* targets[][2] = {{"macos", "macOS (.app)"}, {"windows", "Windows (.exe)"}, {"linux", "Linux"}, {"web", "Web (HTML5)"},
                                           {"ios", "iOS (проект Xcode)"}, {"android", "Android (Gradle + APK)"}, {"all", "Все платформы"}};
        for (auto& t : targets) {
            const std::string id = t[0];
            if (item(id == "web" ? ICON_GLOBE : id == "all" ? ICON_LAYOUT : id == "ios" || id == "android" ? ICON_SMARTPHONE : ICON_MONITOR, t[1])) runBuild(t[0]);
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Конфигурация");
        if (ImGui::MenuItem("Release", nullptr, buildConfig == 1)) buildConfig = 1;
        if (ImGui::MenuItem("Debug", nullptr, buildConfig == 0)) buildConfig = 0;
        ImGui::Separator();
        if (item(ICON_APP_WINDOW, "Окно игры на ПК и Mac…")) focusWindow("build");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Размер, полный экран, изменение размера — окно «Сборка», раздел «Окно игры»");
        ImGui::EndMenu();
    }
    drawTopBarControls();
    ImGui::EndMainMenuBar();
}

void Editor::drawTopBarControls() {
    const auto& pal = theme_.palette();
    const float barW = ImGui::GetWindowWidth();
    const float menusEnd = ImGui::GetCursorPosX(), textY = ImGui::GetCursorPosY();
    // right: the agent link (MCP) — grey: listening, green: an agent is connected, pulsing: it is working
    float rightX = barW - 8;
    if (server_.listening()) {
        double lastAgent = -1e9;
        for (auto it = ws_.activity.rbegin(); it != ws_.activity.rend(); ++it) if (it->source != "editor") { lastAgent = it->time; break; }
        const bool busy = time_ - lastAgent < 2.5;
        const int clients = server_.clients();
        char text[64];
        std::snprintf(text, sizeof text, "%s · %d", busy ? "Агент работает" : clients > 0 ? "Агент подключён" : "MCP", server_.port());
        const float w = pillWidth(text);
        rightX -= w;
        ImGui::SetCursorPos(ImVec2(rightX, (ImGui::GetWindowHeight() - ImGui::GetFrameHeight() + 2) * 0.5f));
        const float pulse = busy ? float(0.5 + 0.5 * std::sin(time_ * 6.0)) : 0.0f;
        pill(text, busy ? pal.accent : clients > 0 ? pal.success : pal.textDim, pulse, pal);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Канал MCP для агентов: порт %d\nПодключено серверов rtsmcp: %d\nОкно «ИИ-агент» — как подключить и журнал действий", server_.port(), clients);
        if (ImGui::IsItemClicked()) focusWindow("agent");
        rightX -= 12;
    }
    // centre: play / pause / stop + what Play starts
    const float h = ImGui::GetFontSize() + 6; // drawPlayControls' button size
    const float playW = h * 4 + 2 * 3 + 4;
    float cx = (barW - playW) * 0.5f;
    // breadcrumb after the menus: game › map ● (dirty)
    std::string game = ws_.project.manifest.getStr("name", "без игры");
    std::string map = doc().path.empty() ? std::string("без имени") : fs::path(doc().path).filename().string();
    std::string crumb = game + "  " + ICON_CHEVRON_RIGHT + "  " + map;
    const float crumbX = menusEnd + 18, crumbW = ImGui::CalcTextSize(crumb.c_str()).x + (doc().dirty ? 14 : 0);
    if (crumbX + crumbW + 16 > cx) cx = crumbX + crumbW + 16; // narrow window: push the controls right
    if (cx + playW < rightX) {
        ImGui::SetCursorPos(ImVec2(crumbX, textY));
        ImGui::TextColored(pal.textDim, "%s", crumb.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", doc().path.empty() ? "Карта ещё не сохранена" : ws_.relative(doc().path).c_str());
        if (doc().dirty) {
            ImVec2 m = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(m.x + 9, m.y - ImGui::GetTextLineHeight() * 0.5f), 3.5f, ImGui::GetColorU32(pal.accent));
        }
        ImGui::SetCursorPos(ImVec2(cx, (ImGui::GetWindowHeight() - h) * 0.5f));
        drawPlayControls();
    }
}

void Editor::drawPlayControls() {
    // the menu bar lays items out horizontally on its own text line: place each button explicitly
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 3));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float step = ImGui::GetFrameHeight() + 2;
    int n = 0;
    auto at = [&] { ImGui::SetCursorScreenPos(ImVec2(origin.x + step * float(n++), origin.y)); };
    ui::beginSegment();
    const bool on = playing();
    std::string playTip = on ? "Остановить тест (F5)" : lastPlaySlot_ > 0 && size_t(lastPlaySlot_) <= doc().data.players.size()
                                                        ? "Играть за " + doc().data.players[size_t(lastPlaySlot_ - 1)].name + " (F5)"
                                                        : std::string("Тест: боты против ботов (F5)");
    at();
    if (ui::toolButton(ICON_PLAY, playTip.c_str(), on)) { if (on) stopPlay(); else startPlay(lastPlaySlot_); }
    at();
    if (ui::toolButton(ICON_PAUSE, playPaused ? "Продолжить" : "Пауза", on && playPaused, on)) playPaused = !playPaused;
    at();
    if (ui::toolButton(ICON_STOP, "Стоп", false, on)) stopPlay();
    at();
    if (ui::toolButton(ICON_CHEVRON_DOWN, "За кого играть")) ImGui::OpenPopup("##playas");
    ui::endSegment();
    ImGui::PopStyleVar();
    if (ImGui::BeginPopup("##playas")) {
        if (item(ICON_BOT, "Боты против ботов", nullptr, lastPlaySlot_ == 0)) { lastPlaySlot_ = 0; if (!on) startPlay(0); }
        for (size_t p = 0; p < doc().data.players.size(); ++p)
            if (!doc().data.players[p].waves && item(ICON_USER, ("Играть за " + doc().data.players[p].name).c_str(), nullptr, lastPlaySlot_ == int(p + 1))) {
                lastPlaySlot_ = int(p + 1);
                if (!on) startPlay(lastPlaySlot_);
            }
        ImGui::EndPopup();
    }
}

void Editor::drawStatusBar() {
    ImGuiViewport* mv = ImGui::GetMainViewport();
    const auto& pal = theme_.palette();
    const float h = statusBarHeight();
    ImGui::SetNextWindowPos(ImVec2(mv->WorkPos.x, mv->WorkPos.y + mv->WorkSize.y - h));
    ImGui::SetNextWindowSize(ImVec2(mv->WorkSize.x, h));
    ImGui::SetNextWindowViewport(mv->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal.appBg);
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    dl->AddLine(wp, ImVec2(wp.x + mv->WorkSize.x, wp.y), ImGui::GetColorU32(pal.border));
    const float ty = (h - ImGui::GetTextLineHeight()) * 0.5f;
    // left: the status line, coloured by what it says
    const bool bad = status.rfind("Ошибка", 0) == 0 || status.rfind("Нельзя", 0) == 0 || status.rfind("Не удалось", 0) == 0;
    const ImVec4 dot = bad ? pal.danger : playing() ? pal.warning : pal.success;
    dl->AddCircleFilled(ImVec2(wp.x + 14, wp.y + h * 0.5f), 3.5f, ImGui::GetColorU32(dot));
    ImGui::SetCursorPos(ImVec2(24, ty));
    ImGui::TextColored(bad ? pal.danger : pal.text, "%s", status.empty() ? "Готово" : status.c_str());
    // right, from the edge inwards: map file, agent activity, build progress
    float x = mv->WorkSize.x - 10;
    auto rightText = [&](const std::string& t, ImVec4 c) {
        x -= ImGui::CalcTextSize(t.c_str()).x;
        ImGui::SetCursorPos(ImVec2(x, ty));
        ImGui::TextColored(c, "%s", t.c_str());
        x -= 18;
    };
    rightText(std::string(ICON_MAP_PIN " ") + (doc().path.empty() ? "карта не сохранена" : ws_.relative(doc().path)) + (doc().dirty ? " •" : ""), pal.textDim);
    for (auto it = ws_.activity.rbegin(); it != ws_.activity.rend(); ++it) {
        if (it->source == "editor") continue;
        const double age = time_ - it->time;
        if (age > 20) break;
        std::string t = it->text.size() > 60 ? it->text.substr(0, 57) + "…" : it->text;
        ImVec4 c = age < 2.5 ? pal.accent : pal.textDim;
        rightText(std::string(ICON_AGENT " ") + t, c);
        break;
    }
    if (ws_.building()) {
        std::string log = ws_.buildLog();
        size_t e = log.find_last_not_of("\r\n ");
        std::string last;
        if (e != std::string::npos) { size_t b = log.find_last_of('\n', e); last = log.substr(b == std::string::npos ? 0 : b + 1, e - (b == std::string::npos ? 0 : b + 1) + 1); }
        if (last.size() > 48) last = "…" + last.substr(last.size() - 47);
        rightText(last, pal.textDim);
        // indeterminate progress: a sliding accent segment
        const float bw = 120, bh = 4;
        x -= bw;
        const ImVec2 a(wp.x + x, wp.y + h * 0.5f - bh * 0.5f), b(a.x + bw, a.y + bh);
        dl->AddRectFilled(a, b, ImGui::GetColorU32(pal.frame), 2);
        const float t = float(std::fmod(time_ * 0.8, 1.0)), seg = bw * 0.35f;
        const float s0 = std::max(a.x, a.x + (bw + seg) * t - seg), s1 = std::min(b.x, a.x + (bw + seg) * t);
        if (s1 > s0) dl->AddRectFilled(ImVec2(s0, a.y), ImVec2(s1, b.y), ImGui::GetColorU32(pal.accent), 2);
        x -= 8;
        rightText(std::string(ICON_BUILD) + " Сборка", pal.warning);
    }
    ImGui::End();
}

void Editor::drawActivityToasts() {
    // the last agent actions, fading out over the main viewport
    const auto& pal = theme_.palette();
    ImGuiViewport* mv = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetForegroundDrawList(mv);
    float y = mv->WorkPos.y + mv->WorkSize.y - statusBarHeight() - 44;
    int shown = 0;
    for (auto it = ws_.activity.rbegin(); it != ws_.activity.rend() && shown < 5; ++it) {
        if (it->source == "editor") continue;
        double age = time_ - it->time;
        if (age > 6) break;
        float a = float(std::clamp(1.0 - (age - 4) / 2, 0.0, 1.0));
        const std::string icon = it->error ? ICON_ALERT : ICON_AGENT;
        ImVec2 ts = ImGui::CalcTextSize(it->text.c_str()), is = ImGui::CalcTextSize(icon.c_str());
        float w = is.x + 8 + ts.x, x = mv->WorkPos.x + mv->WorkSize.x * 0.5f - w * 0.5f;
        ImVec4 bg = pal.panelRaised; bg.w = 0.96f * a;
        ImVec4 br = pal.border; br.w = a;
        dl->AddRectFilled(ImVec2(x - 12, y - 7), ImVec2(x + w + 12, y + ts.y + 7), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.25f * a)), 9); // soft shadow
        dl->AddRectFilled(ImVec2(x - 12, y - 8), ImVec2(x + w + 12, y + ts.y + 6), ImGui::GetColorU32(bg), 8);
        dl->AddRect(ImVec2(x - 12, y - 8), ImVec2(x + w + 12, y + ts.y + 6), ImGui::GetColorU32(br), 8);
        ImVec4 ic = it->error ? pal.danger : pal.accent; ic.w = a;
        ImVec4 tc = it->error ? pal.danger : pal.text; tc.w = a;
        dl->AddText(ImVec2(x, y), ImGui::GetColorU32(ic), icon.c_str());
        dl->AddText(ImVec2(x + is.x + 8, y), ImGui::GetColorU32(tc), it->text.c_str());
        y -= ts.y + 20;
        ++shown;
    }
}


} // namespace rts::editor
