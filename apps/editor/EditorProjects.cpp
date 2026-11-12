// Project hub (like Unity Hub): recent projects anywhere on disk, "new project" from a template into
// a folder the user picks, and a folder browser (ImGui, works the same on every desktop OS).
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include "Editor.h"
#include "EditorHub.h"
#include "EditorGenreUi.h"
#include "EditorIcons.h"
#include "EditorUtil.h"
#include "core/ProjectFile.h"
#include "core/Projects.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

namespace {

struct HubState {
    int tab = 0;                       // 0 projects, 1 new project, 2 make a game (EditorGameWizard.cpp)
    char search[128] = "";
    char title[128] = "Моя стратегия";
    char location[1024] = "";
    int genre = 0;
    int templ = 1;                     // index into listTemplates (0 = empty)
    std::string error;
    // folder browser
    bool browsing = false;
    int browseFor = 0;                 // 0: location of a new project, 1: open an existing project
    std::string browseDir;
    char newFolder[128] = "";
    std::vector<TemplateInfo> templates;
    double templatesAt = -1;
};
HubState g_hub;

std::string ago(long long t) {
    if (t <= 0) return "";
    const long long d = (long long)std::time(nullptr) - t;
    if (d < 120) return "только что";
    if (d < 3600) return std::to_string(d / 60) + " мин назад";
    if (d < 86400) return std::to_string(d / 3600) + " ч назад";
    return std::to_string(d / 86400) + " дн назад";
}

bool contains(const std::string& hay, const char* needle) {
    if (!*needle) return true;
    std::string h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return h.find(n) != std::string::npos;
}

ImU32 tint(const std::string& id) {   // stable colour per template for the card badge
    unsigned h = 2166136261u;
    for (unsigned char c : id) h = (h ^ c) * 16777619u;
    const float hue = float(h % 360) / 360.0f;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(hue, 0.45f, 0.75f, r, g, b);
    return ImGui::GetColorU32(ImVec4(r, g, b, 1));
}

} // namespace

void Editor::showProjectHub(int tab) { showPicker_ = true; g_hub.tab = tab; g_hub.error.clear(); }

// Folder browser as a modal: returns true when a folder was chosen (in `out`). Opening a project, it
// also lists .aiverse files (a click on one opens it, like a double-click in the OS file manager).
bool hubFolderBrowser(bool& open, std::string& dir, char* newFolder, size_t newFolderSize, bool needProject, std::string& out) {
    bool chosen = false;
    if (open) ImGui::OpenPopup("Выбор папки");
    ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Выбор папки", &open)) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec)) dir = defaultProjectsDir();
        if (!fs::is_directory(dir, ec)) { const char* h = std::getenv("HOME"); if (!h) h = std::getenv("USERPROFILE"); dir = h ? h : "/"; }
        if (ui::toolButton(ICON_FOLDER_UP, "Вверх", false, fs::path(dir).has_parent_path() && fs::path(dir).parent_path() != fs::path(dir)))
            dir = fs::path(dir).parent_path().string();
        ImGui::SameLine();
        if (ui::toolButton(ICON_HOME, "Домашняя папка", false, true)) { const char* h = std::getenv("HOME"); if (!h) h = std::getenv("USERPROFILE"); if (h) dir = h; }
        ImGui::SameLine();
        static char pathBuf[1024];
        std::snprintf(pathBuf, sizeof pathBuf, "%s", dir.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", pathBuf, sizeof pathBuf, ImGuiInputTextFlags_EnterReturnsTrue) && fs::is_directory(pathBuf, ec)) dir = pathBuf;
        ImGui::BeginChild("##dirs", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
        std::vector<fs::path> subs;
        for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2) && it->path().filename().string()[0] != '.') subs.push_back(it->path());
        }
        std::sort(subs.begin(), subs.end());
        if (needProject) {
            std::vector<fs::path> files;
            for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                std::error_code e2;
                if (it->is_regular_file(e2) && isProjectFilePath(it->path().string())) files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (auto& f : files) {
                std::string label = std::string(ICON_FILE) + "  " + f.filename().string();
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) { out = f.string(); chosen = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Файл проекта: открыть");
            }
        }
        for (auto& s : subs) {
            const bool isProject = fs::exists(s / "game.json", ec);
            std::string label = std::string(isProject ? ICON_GAME : ICON_PROJECT) + "  " + s.filename().string();
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (needProject && isProject && ImGui::IsMouseDoubleClicked(0)) { out = s.string(); chosen = true; }
                else if (ImGui::IsMouseDoubleClicked(0) || !needProject) dir = s.string();
                else dir = s.string();
            }
        }
        if (subs.empty() && !(needProject && !findProjectFile(dir).empty())) ImGui::TextDisabled("Пустая папка");
        ImGui::EndChild();
        if (!needProject) {
            ImGui::SetNextItemWidth(200);
            ImGui::InputTextWithHint("##nf", "новая папка", newFolder, newFolderSize);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FOLDER_PLUS " Создать") && newFolder[0]) {
                fs::create_directories(fs::path(dir) / newFolder, ec);
                if (!ec) dir = (fs::path(dir) / newFolder).string();
                newFolder[0] = 0;
            }
            ImGui::SameLine();
        }
        const bool here = !needProject || fs::exists(fs::path(dir) / "game.json", ec);
        ImGui::BeginDisabled(!here);
        if (ImGui::Button(needProject ? "Открыть этот проект" : "Выбрать эту папку", ImVec2(180, 0))) { out = dir; chosen = true; }
        ImGui::EndDisabled();
        if (needProject && !here) { ImGui::SameLine(); ImGui::TextDisabled("Выберите файл проекта (.aiverse, .rtsproj) или папку с game.json"); }
        ImGui::SameLine(ImGui::GetWindowWidth() - 110);
        if (ImGui::Button("Отмена", ImVec2(90, 0))) open = false;
        if (chosen) { open = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    return chosen;
}

void Editor::drawGamePicker() {
    ImGuiIO& io = ImGui::GetIO();
    HubState& H = g_hub;
    if (H.location[0] == 0) std::snprintf(H.location, sizeof H.location, "%s", defaultProjectsDir().c_str());
    if (H.templates.empty() || ImGui::GetTime() - H.templatesAt > 10) { H.templates = listTemplates(ws_.engineDir); H.templatesAt = ImGui::GetTime(); }
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    const bool wide = H.tab == 2;   // the game wizard has many answers: a bigger hub
    ImGui::SetNextWindowSize(ImVec2(std::min(wide ? 1240.f : 1160.f, io.DisplaySize.x - 40), std::min(wide ? 880.f : 780.f, io.DisplaySize.y - 40)));
    ImGui::Begin("Проекты", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar);

    // left: navigation
    ImGui::BeginChild("##nav", ImVec2(190, 0));
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), ICON_CASTLE "  AIVERSE");
    ImGui::Spacing(); ImGui::Spacing();
    if (ImGui::Selectable(ICON_PROJECT "  Проекты", H.tab == 0, 0, ImVec2(0, 30))) H.tab = 0;
    if (ImGui::Selectable(ICON_ZAP "  Сделать игру", H.tab == 2, 0, ImVec2(0, 30))) H.tab = 2;
    if (ImGui::Selectable(ICON_PLUS "  Новый проект", H.tab == 1, 0, ImVec2(0, 30))) H.tab = 1;
    {   // .aiverse double-click: does the OS know this editor?
        const AssocStatus& fa = fileAssoc();
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - (ws_.project.dir.empty() ? 40 : 80) - (fa.registered || !fa.supported ? 0 : 50));
        if (fa.registered) ImGui::TextDisabled(ICON_CHECK " .aiverse открываются здесь");
        else if (fa.supported) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("Открывать .aiverse двойным кликом в этом редакторе"); ImGui::PopTextWrapPos(); }
        else ImGui::TextDisabled(".aiverse: связь недоступна");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Двойной клик по файлу проекта .aiverse открывает его в редакторе.\n%s", fa.detail.c_str());
        if (!fa.registered && fa.supported && ImGui::Button(ICON_LINK " Связать", ImVec2(-1, 0))) registerFileAssoc();
    }
    if (!ws_.project.dir.empty()) {
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 40);
        if (ImGui::Button("Закрыть", ImVec2(-1, 0))) showPicker_ = false;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_Borders);

    std::string browsed;
    if (!status.empty() && status.rfind("Не удалось", 0) == 0)   // open failed: say why right here
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), ICON_WARNING " %s", status.c_str());
    if (H.tab == 0) {
        // «Новый проект» and «Сделать игру» live in the navigation on the left: the header keeps search + open
        auto projects = recentProjects();
        ui::heading("Проекты");
        const float openW = ImGui::CalcTextSize(ICON_FOLDER_OPEN "  Открыть…").x + ImGui::GetStyle().FramePadding.x * 2;
        const float searchW = 220;
        ImGui::SameLine(std::max(140.0f, ImGui::GetWindowContentRegionMax().x - openW - searchW - ImGui::GetStyle().ItemSpacing.x));
        if (!projects.empty()) {
            ImGui::SetNextItemWidth(searchW);
            ImGui::InputTextWithHint("##search", ICON_SEARCH " поиск", H.search, sizeof H.search);
            ImGui::SameLine();
        } else ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - openW);
        if (ui::button(ICON_FOLDER_OPEN, "Открыть…")) { H.browsing = true; H.browseFor = 1; H.browseDir = defaultProjectsDir(); }
        ImGui::SetItemTooltip("Открыть папку проекта (с game.json) или файл проекта (.aiverse, .rtsproj)");
        ImGui::Separator();
        ImGui::BeginChild("##list");
        int shown = 0;
        for (auto& pr : projects) {
            if (!contains(pr.name + " " + pr.dir, H.search)) continue;
            ++shown;
            ImGui::PushID(pr.dir.c_str());
            const bool current = pr.dir == ws_.project.dir;
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##p", current, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, 48)) &&
                ImGui::IsMouseDoubleClicked(0) && pr.exists)
                requestOpenProject(pr.file.empty() ? pr.dir : pr.file);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(p0.x + 6, p0.y + 8), ImVec2(p0.x + 38, p0.y + 40), tint(pr.name), 6);
            const std::string letter = pr.name.empty() ? "?" : pr.name.substr(0, (unsigned char)pr.name[0] >= 0xC0 ? 2 : 1);
            dl->AddText(ImVec2(p0.x + 16, p0.y + 15), IM_COL32(255, 255, 255, 230), letter.c_str());
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 50, p0.y + 6));
            const float right = ImGui::GetWindowContentRegionMax().x;
            const float textMax = ImGui::GetWindowPos().x + right - 260;   // keep clear of the date and buttons
            ImGui::PushClipRect(ImVec2(p0.x, p0.y), ImVec2(textMax, p0.y + 48), true);
            ImGui::BeginGroup();
            ImGui::TextUnformatted(pr.name.c_str());
            if (pr.exists) ImGui::TextDisabled("%s", ui::prettyPath(pr.dir).c_str());
            else ImGui::TextColored(ImVec4(1, 0.55f, 0.4f, 1), ICON_WARNING " папка не найдена: %s", pr.dir.c_str());
            ImGui::EndGroup();
            ImGui::PopClipRect();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (pr.file.empty() ? pr.dir : pr.file).c_str());
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + right - 250, p0.y + 14));
            ImGui::TextDisabled("%s", ago(pr.lastOpened).c_str());
            ImGui::SameLine(right - 140);
            ImGui::BeginDisabled(!pr.exists);
            if (ImGui::Button(current ? "Открыт" : "Открыть", ImVec2(90, 0))) requestOpenProject(pr.file.empty() ? pr.dir : pr.file);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::toolButton(ICON_CLOSE, "Убрать из списка (файлы не удаляются)", false, true)) forgetProject(pr.dir);
            ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + 52));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        if (shown == 0 && !projects.empty()) { ImGui::Spacing(); ImGui::TextDisabled("Ничего не найдено."); }
        if (projects.empty()) {
            // first start: two large choices instead of a line of text and duplicate buttons
            ImGui::Spacing(); ImGui::Spacing();
            ImGui::TextDisabled("Проектов пока нет. С чего начнём?");
            ImGui::Spacing();
            const float gap = 16, cw = std::min(340.0f, (ImGui::GetContentRegionAvail().x - gap) * 0.5f), ch = 150;
            auto card = [&](const char* id, const char* icon, const char* title, const char* text, bool primary) {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const bool hit = ImGui::InvisibleButton(id, ImVec2(cw, ch));
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const auto& pal = ui::pal();
                dl->AddRectFilled(p0, ImVec2(p0.x + cw, p0.y + ch), ImGui::GetColorU32(hov ? pal.frameHover : pal.panelRaised), 10);
                dl->AddRect(p0, ImVec2(p0.x + cw, p0.y + ch), ImGui::GetColorU32(primary || hov ? pal.accent : pal.border), 10, 0, primary ? 2.0f : 1.0f);
                dl->AddRectFilled(ImVec2(p0.x + 18, p0.y + 18), ImVec2(p0.x + 58, p0.y + 58), ImGui::GetColorU32(primary ? pal.accent : pal.frame), 8);
                const ImVec2 is = ImGui::CalcTextSize(icon);
                dl->AddText(ImVec2(p0.x + 38 - is.x * 0.5f, p0.y + 38 - is.y * 0.5f), IM_COL32(255, 255, 255, 240), icon);
                ImGui::PushFont(ui::headingFont(), 0.0f);
                dl->AddText(ImVec2(p0.x + 18, p0.y + 72), ImGui::GetColorU32(pal.text), title);
                ImGui::PopFont();
                dl->AddText(nullptr, 0, ImVec2(p0.x + 18, p0.y + 98), ImGui::GetColorU32(pal.textDim), text, nullptr, cw - 36);
                return hit;
            };
            if (card("##new", ICON_TEMPLATE, "Новый проект из шаблона", "Готовая игра-образец: правила, карта, юниты. Меняйте под себя.", false)) H.tab = 1;
            ImGui::SameLine(0, gap);
            if (card("##wiz", ICON_ZAP, "Сделать игру из арта", "Мастер соберёт проект из ваших картинок, моделей и идеи: обучение, уровни, меню.", true)) H.tab = 2;
        }
        ImGui::EndChild();
    } else if (H.tab == 2) {
        drawGameWizard();
    } else {
        ui::heading("Новый проект");
        ui::heading(genreText(ws_, "hub.genre").c_str());
        const float genreWidth=std::max(120.0f,(ImGui::GetContentRegionAvail().x-ImGui::GetStyle().ItemSpacing.x)*0.5f);
        for(int genre=0;genre<2;++genre) {
            if(genre)ImGui::SameLine();
            ImGui::PushID(genre+10000);
            if(ImGui::Selectable(genreText(ws_,genre?"hub.genre.fps":"hub.genre.rts").c_str(),H.genre==genre,0,ImVec2(genreWidth,42))) {
                H.genre=genre;H.templ=-1;
                if(std::string(H.title)=="Моя стратегия"||std::string(H.title)=="Мой шутер")std::snprintf(H.title,sizeof H.title,"%s",genre?"Мой шутер":"Моя стратегия");
            }
            ImGui::SetItemTooltip("%s",genreText(ws_,genre?"hub.genre.fps.detail":"hub.genre.rts.detail").c_str());
            ImGui::PopID();
        }
        ImGui::TextWrapped("%s",genreText(ws_,H.genre?"hub.genre.fps.detail":"hub.genre.rts.detail").c_str());
        ui::heading(genreText(ws_, "hub.template").c_str());
        ImGui::SetItemTooltip("Шаблоны: %s", (fs::path(ws_.engineDir) / "templates").string().c_str());
        ImGui::Separator();
        const float formH = 150;
        ImGui::BeginChild("##templates", ImVec2(0, -formH));
        const float cardH = 128, gapX = ImGui::GetStyle().ItemSpacing.x;
        const int cols = std::max(1, int((ImGui::GetContentRegionAvail().x + gapX) / (210 + gapX)));
        const float cardW = std::floor((ImGui::GetContentRegionAvail().x - gapX * float(cols - 1)) / float(cols));   // cards fill the row
        std::vector<size_t> visible;
        for(size_t i=0;i<H.templates.size();++i)if((H.templates[i].genre=="fps")==bool(H.genre))visible.push_back(i);
        if(std::find(visible.begin(),visible.end(),size_t(H.templ))==visible.end())H.templ=visible.empty()?-1:int(visible.front());
        size_t shown=0;
        for (size_t i = 0; i < H.templates.size(); ++i) {
            const TemplateInfo& t = H.templates[i];
            if((t.genre=="fps")!=bool(H.genre))continue;
            if (shown++ % size_t(cols)) ImGui::SameLine();
            ImGui::PushID(int(i));
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##t", H.templ == int(i), ImGuiSelectableFlags_AllowDoubleClick, ImVec2(cardW, cardH))) H.templ = int(i);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRect(p0, ImVec2(p0.x + cardW, p0.y + cardH), ImGui::GetColorU32(H.templ == int(i) ? ImGuiCol_CheckMark : ImGuiCol_Border), 6, 0, H.templ == int(i) ? 2.0f : 1.0f);
            dl->AddRectFilled(ImVec2(p0.x + 10, p0.y + 10), ImVec2(p0.x + 42, p0.y + 42), t.id == "empty" ? ImGui::GetColorU32(ImGuiCol_FrameBg) : tint(t.id), 6);
            dl->AddText(ImVec2(p0.x + 18, p0.y + 17), IM_COL32(255, 255, 255, 230), t.id == "empty" ? ICON_PLUS : ICON_TEMPLATE);
            dl->PushClipRect(p0, ImVec2(p0.x + cardW - 6, p0.y + cardH), true);
            dl->AddText(ImVec2(p0.x + 52, p0.y + 12), ImGui::GetColorU32(ImGuiCol_Text), t.name.c_str());
            const std::string sub = t.id == "empty" ? std::string("с нуля") : t.id + (t.maps ? " · карт: " + std::to_string(t.maps) : "");
            dl->AddText(ImVec2(p0.x + 52, p0.y + 32), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub.c_str());
            if (!t.description.empty()) dl->AddText(nullptr, 0, ImVec2(p0.x + 10, p0.y + 52), ImGui::GetColorU32(ImGuiCol_TextDisabled), t.description.c_str(), nullptr, cardW - 20);
            dl->PopClipRect();
            if (ImGui::IsItemHovered() && !t.description.empty()) ImGui::SetTooltip("%s", t.description.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::Separator();
        const std::string target = (fs::path(H.location) / projectFolderName(H.title)).string();
        if (ui::beginProps("##newform", 120)) {
            ui::prop("Название");
            ImGui::InputText("##title", H.title, sizeof H.title);
            ui::prop("Где создать", "Папка, в которой появится папка проекта");
            const float bw = ImGui::CalcTextSize(ICON_FOLDER_OPEN "  Выбрать…").x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
            ImGui::InputText("##loc", H.location, sizeof H.location);
            ImGui::SameLine();
            if (ui::button(ICON_FOLDER_OPEN, "Выбрать…")) { H.browsing = true; H.browseFor = 0; H.browseDir = H.location; }
            ui::propWide();
            ImGui::TextDisabled("Папка проекта: %s", ui::prettyPath(target).c_str());
            ui::endProps();
        }
        if (!H.error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", H.error.c_str());
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 190);
        ImGui::BeginDisabled(H.templ<0);
        if (ui::button(ICON_PLUS, "Создать проект", true, ImVec2(180, 0))) {
            std::string err;
            const std::string dir = createProject(ws_.engineDir, H.templates[size_t(H.templ)].id, H.title, H.location, "", &err);
            if (dir.empty()) H.error = err;
            else { H.error.clear(); requestOpenProject(dir); }
        }
        ImGui::EndDisabled();
    }
    if (hubFolderBrowser(H.browsing, H.browseDir, H.newFolder, sizeof H.newFolder, H.browseFor == 1, browsed)) {
        if (H.browseFor == 0) std::snprintf(H.location, sizeof H.location, "%s", browsed.c_str());
        else requestOpenProject(browsed);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace aiverse::editor
