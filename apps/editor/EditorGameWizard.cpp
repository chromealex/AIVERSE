// Project hub page «Сделать игру»: the game wizard (core/GameWizard.h). Layout: a stepper on the left (source, idea,
// project, tutorial, levels, meta, monetization, look), the current step as a card with a label / control grid in the
// middle, the summary on the right (what will be made, where, the big button, progress, result). Every answer is
// optional: the plan in the summary updates while the answers change. The making runs one step per frame (a link
// downloads on a thread first), then the new project opens.
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <thread>
#include <ctime>
#include <fstream>
#include <iterator>
#include "Editor.h"
#include "EditorHub.h"
#include "EditorIcons.h"
#include "EditorUtil.h"
#include "core/GameWizard.h"
#include "core/Projects.h"
#include "EditorMcpConnect.h"
#include "EditorSpeech.h"
#include "core/WizardAgent.h"
#include "core/WizardOps.h"

namespace fs = std::filesystem;

namespace rts::editor {

namespace {

struct WizardUi {
    WizardSpec spec;
    int page = 0;
    int source = 0;                    // 0 folder, 1 link, 2 nothing
    Json catalog;                      // wizardCatalog
    std::vector<std::string> genreIds, genreLabels;
    int genre = 0;
    std::string specKey;               // answers last planned
    Json plan;
    std::string planError;
    double planAt = -1;
    enum Phase { Idle, Fetching, Making, Done, Failed } phase = Idle;
    std::thread fetcher;
    std::atomic<bool> fetched{false};
    std::string fetchDir, fetchError;
    std::unique_ptr<GameMaker> maker;
    Json report;
    bool browsing = false;
    int browseFor = 0;                 // 0 content folder, 1 location
    std::string browseDir;
    char newFolder[128] = "";
    char soft[32] = "gold", hard[32] = "gems";
    // the agent step (core/WizardAgent.h): connect once (shared with the Agent window), then the agent rebuilds the game
    McpConnectState mcp;
    double runnableAt = -100;
    bool canClaude = false, canCodex = false;
    std::string taskFile;                    // WIZARD_TASK.md of the last made game
    bool agentOffered = false;               // made, agent asked for but nothing runnable: offer the task to copy
    ~WizardUi() { if (fetcher.joinable()) fetcher.join(); }
};
WizardUi g_wiz;
AgentRun g_agent;                     // outlives the hub page: the agent keeps working while the editor shows the game

struct Page { const char* icon; const char* title; const char* sub; };
const Page kPages[] = {
    {ICON_IMAGE, "Контент", "арт, музыка, тексты"},
    {ICON_ZAP, "Идея", "жанр и механики"},
    {ICON_PROJECT, "Проект", "название, платформы"},
    {ICON_HELP, "Обучение", "хард и софт"},
    {ICON_LEVELS, "Уровни", "карты и сложность"},
    {ICON_CASTLE, "Мета", "валюты, улучшения"},
    {ICON_PLUG, "Монетизация", "реклама, покупки"},
    {ICON_PALETTE, "Вид", "иконка и цвета"},
};
constexpr int kPageCount = int(sizeof kPages / sizeof kPages[0]);

ImVec4 mix(ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t); }
ImU32 col(ImGuiCol c, float alpha = 1) { ImVec4 v = ImGui::GetStyleColorVec4(c); v.w *= alpha; return ImGui::GetColorU32(v); }
ImU32 accent(float alpha = 1) { return ui::rgbaU32(ui::accentColor(), alpha); }

void hint(const char* text) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

// a form: label column + control column, every row the same height
bool beginForm(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) return false;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 165);
    ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthStretch);
    return true;
}
void row(const char* label, const char* tip = nullptr) {
    ImGui::TableNextRow(0, ImGui::GetFrameHeight() + 10);
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
    ImGui::TextUnformatted(label);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
    ImGui::SetNextItemWidth(-1);
}
void endForm() { ImGui::EndTable(); }

// a toggle pill (chips for features, languages, platforms, stores); returns true when toggled
bool pill(const char* label, bool on, const char* tip = nullptr) {
    ImGuiStyle& st = ImGui::GetStyle();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 size(ts.x + 26, ImGui::GetFrameHeight() + 2);
    if (ImGui::GetCursorPosX() > ImGui::GetStyle().WindowPadding.x + 1 &&
        ImGui::GetContentRegionAvail().x < size.x) ImGui::NewLine();
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##pill", size);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    if (on) dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), accent(hov ? 0.95f : 0.8f), r);
    else {
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), col(ImGuiCol_FrameBg, hov ? 1.4f : 1.0f), r);
        dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), col(ImGuiCol_Border, hov ? 1.0f : 0.6f), r);
    }
    dl->AddText(ImVec2(p.x + 13, p.y + (size.y - ts.y) * 0.5f), on ? IM_COL32(255, 255, 255, 255) : col(ImGuiCol_Text, 0.85f), label);
    if (tip && hov) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    ImGui::SameLine(0, st.ItemSpacing.x * 0.8f);
    return clicked;
}
void pillsEnd() { ImGui::NewLine(); }

bool pillList(std::vector<std::string>& v, const std::vector<std::string>& all) {
    bool changed = false;
    for (auto& a : all) {
        const bool on = std::find(v.begin(), v.end(), a) != v.end();
        if (pill(a.c_str(), on)) {
            changed = true;
            if (on) v.erase(std::remove(v.begin(), v.end(), a), v.end()); else v.push_back(a);
        }
    }
    pillsEnd();
    return changed;
}

// segmented choice (2-4 options), full width
bool segmented(const char* id, std::string& value, const std::vector<std::pair<std::string, std::string>>& items) {
    bool changed = false;
    ImGui::PushID(id);
    const float w = (ImGui::GetContentRegionAvail().x - (items.size() - 1) * 4) / float(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) ImGui::SameLine(0, 4);
        const bool on = items[i].first == value;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ui::accentColor() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ui::accentColor() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1, 1, 1, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        if (ImGui::Button(items[i].second.c_str(), ImVec2(w, ImGui::GetFrameHeight() + 4))) { value = items[i].first; changed = true; }
        ImGui::PopStyleColor(3);
    }
    ImGui::PopID();
    return changed;
}
bool combo(const char* id, std::string& value, const std::vector<std::pair<std::string, std::string>>& items) {
    int cur = 0;
    for (size_t i = 0; i < items.size(); ++i) if (items[i].first == value) cur = int(i);
    bool changed = false;
    if (ImGui::BeginCombo(id, items[size_t(cur)].second.c_str())) {
        for (size_t i = 0; i < items.size(); ++i)
            if (ImGui::Selectable(items[i].second.c_str(), int(i) == cur)) { value = items[i].first; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

// a switch: label on the left of the row, the toggle on the right
bool toggle(const char* id, bool& v) {
    const float h = ImGui::GetFrameHeight() * 0.85f, w = h * 1.9f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##t", ImVec2(w, h));
    if (clicked) v = !v;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), v ? accent(0.9f) : col(ImGuiCol_FrameBg, 1.6f), h * 0.5f);
    const float k = v ? 1.0f : 0.0f;
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + k * (w - h), p.y + h * 0.5f), h * 0.5f - 3, IM_COL32(255, 255, 255, 240));
    ImGui::PopID();
    return clicked;
}
void toggleRow(const char* label, bool& v, const char* tip = nullptr) {
    row(label, tip);
    toggle(label, v);
}

std::vector<std::string> stringsOf(const Json& j) {
    std::vector<std::string> o;
    for (auto& s : j.items()) o.push_back(s.str());
    return o;
}

// a card: rounded panel with padding; children draw inside
void beginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 18));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, mix(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg), ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_Border));
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
}
void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void bigTitle(const char* text) {
    if (ImFont* f = ui::headingFont()) ImGui::PushFont(f);
    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
    if (ui::headingFont()) ImGui::PopFont();
}

void cardTitle(const Page& p, int index) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::accentColor());
    ImGui::Text("ШАГ %d ИЗ %d", index + 1, kPageCount);
    ImGui::PopStyleColor();
    bigTitle(p.title);
    ImGui::Spacing();
}

// summary line in the right panel: icon, label, value
void fact(const char* icon, const char* label, const std::string& value) {
    const float x0 = ImGui::GetCursorPosX();
    ImGui::TextDisabled("%s", icon);
    ImGui::SameLine(x0 + 26);
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(x0 + 112);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

std::string tutorialText(const std::string& mode) {
    return mode == "both" ? "хард + софт" : mode == "hard" ? "только хард" : mode == "soft" ? "только подсказки" : "нет";
}

// Dictation into a text field: one field at a time listens; the live text shows under the button, the final text is
// appended to the field when the recognizer is done.
std::string* g_dictTarget = nullptr;
void dictate(const char* id, std::string& target, const WizardSpec& S) {
    ImGui::PushID(id);
    std::string done;
    if (g_dictTarget == &target && !speech::busy() && speech::takeResult(done)) {
        if (!done.empty()) target += (target.empty() || target.back() == '\n' ? "" : (target.back() == ' ' ? "" : " ")) + done;
        g_dictTarget = nullptr;
    }
    const bool mine = g_dictTarget == &target;
    const bool listening = mine && speech::listening();
    if (!speech::supported()) {
        ImGui::BeginDisabled();
        ImGui::Button("●  Наговорить");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", speech::error().c_str());
        ImGui::PopID();
        return;
    }
    ImGui::BeginDisabled(g_dictTarget && !mine);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (listening) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    const bool click = ImGui::Button(listening ? "■  Готово" : mine ? "…  Распознаю" : "      Наговорить");
    if (listening) ImGui::PopStyleColor(3);
    if (!listening && !mine) {   // the red dot of a record button
        const float r = ImGui::GetFontSize() * 0.28f;
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + ImGui::GetStyle().FramePadding.x + r + 2, p.y + ImGui::GetFrameHeight() * 0.5f), r, IM_COL32(230, 70, 70, 255));
    }
    ImGui::EndDisabled();
    if (click) {
        if (listening) speech::stop();
        else if (!mine) {
            const std::string lang = S.languages.empty() ? "ru" : S.languages[0];
            const std::string locale = lang == "ru" ? "ru-RU" : lang == "en" ? "en-US" : lang == "uk" ? "uk-UA" : lang == "kk" ? "kk-KZ"
                                     : lang == "de" ? "de-DE" : lang == "fr" ? "fr-FR" : lang == "es" ? "es-ES" : lang == "pt" ? "pt-BR"
                                     : lang == "it" ? "it-IT" : lang == "tr" ? "tr-TR" : lang == "zh" ? "zh-CN" : lang == "ja" ? "ja-JP"
                                     : lang == "ko" ? "ko-KR" : lang;
            g_dictTarget = &target;
            speech::start(locale);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Наговорите в микрофон — текст добавится в поле (язык: первый язык игры)");
    if (mine) {
        const std::string live = speech::transcript();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0);
        if (listening && live.empty()) ImGui::TextDisabled("Говорите…");
        else ImGui::TextDisabled("%s", live.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!speech::error().empty() && (mine || !g_dictTarget)) {
        if (mine && !speech::busy()) g_dictTarget = nullptr;
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), ICON_WARNING " %s", speech::error().c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::PopID();
}

// the agent's client: the one chosen, else the first connected one that can run headless
std::string agentClientFor(WizardUi& W, const WizardSpec& S) {
    auto connected = [&](const char* id) { for (auto& c : W.mcp.clients) if (c.id == id && c.upToDate) return true; return false; };
    auto ok = [&](const std::string& id) { return (id == "claude-code" && W.canClaude && connected("claude-code")) || (id == "codex" && W.canCodex && connected("codex")); };
    if (!S.agentClient.empty()) return ok(S.agentClient) ? S.agentClient : std::string();
    for (const char* id : {"claude-code", "codex"}) if (ok(id)) return id;
    return {};
}

// «ИИ-агент» on the idea page: the template is a starting point, the connected agent makes the described game
void drawAgent(Editor& ed, WizardUi& W, WizardSpec& S) {
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::accentColor());
    ImGui::TextUnformatted(ICON_AGENT "  ИИ-агент: игра 1 в 1 по описанию");
    ImGui::PopStyleColor();
    hint("Мастер соберёт стартовый проект из шаблона, а подключённый агент переделает его в игру ровно как в описании или ГДД: "
         "юниты, здания, механики, числа, карты, победа, тексты — и подгонит под неё обучение и мету. Работает через то же "
         "подключение, что окно Agent: подключите один раз.");
    ImGui::Spacing();
    if (beginForm("##fag")) {
        toggleRow("Доводка агентом", S.agent);
        endForm();
    }
    if (!S.agent) return;
    if (ImGui::GetTime() - W.runnableAt > 15) {   // which CLIs can run (a login shell each: not every frame)
        W.runnableAt = ImGui::GetTime();
        W.canClaude = agentClientCanRun("claude-code");
        W.canCodex = agentClientCanRun("codex");
    }
    drawMcpConnect(ed, W.mcp);
    ImGui::Spacing();
    const std::string who = agentClientFor(W, S);
    if (beginForm("##fag2")) {
        row("Кто доводит");
        auto connected = [&](const char* id) { for (auto& c : W.mcp.clients) if (c.id == id && c.upToDate) return true; return false; };
        std::vector<std::pair<std::string, std::string>> items = {{"", "авто"}};
        if (connected("claude-code") || W.canClaude) items.push_back({"claude-code", std::string("Claude Code") + (W.canClaude ? "" : " (CLI не найден)")});
        if (connected("codex") || W.canCodex) items.push_back({"codex", std::string("Codex CLI") + (W.canCodex ? "" : " (CLI не найден)")});
        combo("##agentc", S.agentClient, items);
        endForm();
    }
    if (!S.agentClient.empty() && who.empty())
        ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), ICON_WARNING " %s", S.agentClient == "codex" ? "Codex CLI не найден или не подключён" : "Claude Code не найден или не подключён");
    else if (who.empty())
        hint("Запустить из редактора можно Claude Code или Codex CLI (подключите и установите один). С Cursor, Windsurf или "
             "Claude Desktop мастер в конце даст задание — вставьте его в клиент.");
    else
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), ICON_CHECK " Доводить будет %s", who == "codex" ? "Codex CLI" : "Claude Code");
}

} // namespace

void Editor::drawGameWizard() {
    WizardUi& W = g_wiz;
    WizardSpec& S = W.spec;
    if (W.catalog.isNull() || W.catalog.size() == 0) {
        W.catalog = wizardCatalog(ws_.engineDir);
        W.genreIds = {"auto"};
        W.genreLabels = {"Авто — мастер выберет по идее, механикам и арту"};
        for (auto& t : W.catalog["templates"].items()) {
            W.genreIds.push_back(t.getStr("id"));
            W.genreLabels.push_back(t.getStr("name") + "  ·  " + t.getStr("description"));
        }
        if (S.location.empty()) S.location = defaultProjectsDir();
        if (const char* p = std::getenv("RTS_WIZARD_PAGE")) W.page = std::clamp(std::atoi(p), 0, kPageCount - 1);   // screenshots
    }
    const bool busy = W.phase == WizardUi::Fetching || W.phase == WizardUi::Making;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

    // ---------------------------------------------------------------- header
    bigTitle(ICON_ZAP "  Сделать игру");
    hint("Арт и идея → готовый проект .rtsproj: обучение, мета, уровни и сама игра. Любой шаг можно пропустить — мастер решит сам.");
    ImGui::Spacing();

    const float avail = ImGui::GetContentRegionAvail().x, height = ImGui::GetContentRegionAvail().y - 10;   // no page scrollbar
    const float navW = 200, sumW = std::clamp(avail * 0.28f, 260.f, 340.f), gap = 14;
    const float mainW = avail - navW - sumW - gap * 2;

    // ---------------------------------------------------------------- stepper
    ImGui::BeginChild("##wiznav", ImVec2(navW, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    for (int i = 0; i < kPageCount; ++i) {
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = 52;
        const bool sel = W.page == i;
        if (ImGui::InvisibleButton("##step", ImVec2(navW - 6, h)) && !busy) W.page = i;
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (sel) dl->AddRectFilled(p, ImVec2(p.x + navW - 6, p.y + h), accent(0.16f), 8);
        else if (hov) dl->AddRectFilled(p, ImVec2(p.x + navW - 6, p.y + h), col(ImGuiCol_FrameBg, 0.8f), 8);
        if (i + 1 < kPageCount) dl->AddLine(ImVec2(p.x + 22, p.y + 38), ImVec2(p.x + 22, p.y + h + 14), col(ImGuiCol_Border, 0.7f), 2);
        const ImVec2 c(p.x + 22, p.y + 24);
        dl->AddCircleFilled(c, 14, sel ? accent() : i < W.page ? accent(0.45f) : col(ImGuiCol_FrameBg, 1.8f));
        const std::string n = i < W.page && !sel ? ICON_CHECK : std::to_string(i + 1);
        const ImVec2 ns = ImGui::CalcTextSize(n.c_str());
        dl->AddText(ImVec2(c.x - ns.x * 0.5f, c.y - ns.y * 0.5f), sel || i < W.page ? IM_COL32(255, 255, 255, 255) : col(ImGuiCol_Text, 0.7f), n.c_str());
        dl->AddText(ImVec2(p.x + 46, p.y + 9), sel ? col(ImGuiCol_Text) : col(ImGuiCol_Text, 0.85f), kPages[i].title);
        dl->AddText(ImVec2(p.x + 46, p.y + 28), col(ImGuiCol_TextDisabled), kPages[i].sub);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine(0, gap);

    // ---------------------------------------------------------------- the step card
    ImGui::BeginGroup();
    const float footH = ImGui::GetFrameHeight() + 18;
    beginCard("##wizcard", ImVec2(mainW, height - footH));
    ImGui::BeginDisabled(busy);
    cardTitle(kPages[W.page], W.page);
    bool drawAiBlock = false;
    switch (W.page) {
    case 0: {
        hint("Откуда взять картинки, модели, музыку и звуки. Модели и спрайты встанут на юнитов с похожими именами, музыка — в меню и бой, "
             "главный арт — в заставку, меню и иконку, тексты .md / .txt (ГДД) мастер прочитает как идею.");
        ImGui::Spacing();
        std::string src = std::to_string(W.source);
        if (segmented("src", src, {{"0", ICON_FOLDER_OPEN "  Папка на диске"}, {"1", ICON_LINK "  Ссылка"}, {"2", ICON_CLOSE "  Без своего арта"}}))
            W.source = std::atoi(src.c_str());
        ImGui::Spacing(); ImGui::Spacing();
        if (W.source == 0) {
            ImGui::TextUnformatted("Папка с контентом");
            ImGui::SetNextItemWidth(-120);
            ui::inputString("##artdir", S.artDir);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FOLDER_OPEN " Выбрать", ImVec2(-1, 0))) { W.browsing = true; W.browseFor = 0; W.browseDir = S.artDir; }
        } else if (W.source == 1) {
            ImGui::TextUnformatted("Ссылка");
            ImGui::SetNextItemWidth(-1);
            ui::inputString("##arturl", S.artUrl);
        }
        ImGui::Spacing();
        if (W.source == 0) hint("png, jpg, tga · glb, gltf, obj, fbx · ogg, wav, mp3, flac · ttf, otf · md, txt");
        else if (W.source == 1) hint("Прямая ссылка на картинку или архив .zip / .tar.gz. Ссылки Dropbox и Google Drive на файл тоже подойдут.");
        else hint("Игра возьмёт модели и звуки шаблона, иконку и заставку движок сгенерирует.");
        break;
    }
    case 1: {
        hint("Опишите игру своими словами — по ним мастер выберет шаблон. Пусто — выберет сам.");
        ImGui::Spacing();
        ui::inputMultiline("##idea", S.idea, ImVec2(-1, 110));
        dictate("dict_idea", S.idea, S);
        ImGui::Spacing();
        drawAiBlock = true;
        if (beginForm("##f1")) {
            row("Жанр / шаблон");
            W.genre = std::clamp(W.genre, 0, int(W.genreIds.size()) - 1);
            if (ImGui::BeginCombo("##genre", W.genreLabels[size_t(W.genre)].c_str())) {
                for (size_t i = 0; i < W.genreIds.size(); ++i)
                    if (ImGui::Selectable(W.genreLabels[i].c_str(), int(i) == W.genre)) W.genre = int(i);
                ImGui::EndCombo();
            }
            endForm();
        }
        S.genre = W.genreIds[size_t(W.genre)];
        ImGui::Spacing();
        ImGui::TextUnformatted("Хочу механики");
        ImGui::Spacing();
        ImGui::BeginDisabled(S.genre != "auto");
        for (auto& f : W.catalog["features"].items()) {
            const std::string id = f.getStr("id");
            const bool on = std::find(S.features.begin(), S.features.end(), id) != S.features.end();
            if (pill(id.c_str(), on, f.getStr("description").c_str())) {
                if (on) S.features.erase(std::remove(S.features.begin(), S.features.end(), id), S.features.end()); else S.features.push_back(id);
            }
        }
        pillsEnd();
        ImGui::EndDisabled();
        if (S.genre != "auto") hint("Шаблон выбран вручную — механики берутся из него.");
        if (drawAiBlock) drawAgent(*this, W, S);
        break;
    }
    case 2: {
        if (beginForm("##f2")) {
            row("Название"); ui::inputString("##title", S.title);
            row("Студия", "для идентификатора приложения: com.<студия>.<игра>"); ui::inputString("##studio", S.studio);
            row("Версия"); ui::inputString("##ver", S.version);
            row("Где создать");
            ImGui::SetNextItemWidth(-120);
            ui::inputString("##loc", S.location);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FOLDER_OPEN " Выбрать##loc", ImVec2(-1, 0))) { W.browsing = true; W.browseFor = 1; W.browseDir = S.location; }
            row("Экран");
            segmented("or", S.orientation, {{"auto", "как в шаблоне"}, {"landscape", ICON_LANDSCAPE " горизонт."}, {"portrait", ICON_PORTRAIT " вертик."}});
            row("Языки");
            pillList(S.languages, stringsOf(W.catalog["languages"]));
            row("Платформы");
            pillList(S.platforms, stringsOf(W.catalog["platforms"]));
            row("Сид", "одинаковые ответы и сид — одинаковая игра; 0 — из названия и идеи");
            ImGui::InputInt("##seed", &S.seed);
            endForm();
        }
        break;
    }
    case 3: {
        hint("Хард — отдельная карта-обучение: шаг за шагом, проходят только нужные действия, враг ждёт. "
             "Софт — короткие подсказки в обычных матчах, каждая один раз.");
        ImGui::Spacing();
        segmented("tut", S.tutorial, {{"both", "Хард + софт"}, {"hard", "Только хард"}, {"soft", "Только софт"}, {"none", "Без обучения"}});
        ImGui::Spacing();
        if (beginForm("##f3")) {
            ImGui::BeginDisabled(S.tutorial == "soft" || S.tutorial == "none");
            toggleRow("Сначала обучение", S.tutorialForced, "первая кнопка «Играть» запускает обучение");
            toggleRow("Можно пропустить", S.tutorialSkippable);
            toggleRow("Затемнять вокруг цели", S.tutorialDim);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(S.tutorial == "hard" || S.tutorial == "none");
            toggleRow("Подсказки в бою", S.hints);
            ImGui::EndDisabled();
            endForm();
        }
        break;
    }
    case 4: {
        if (beginForm("##f4")) {
            row("Уровней"); ImGui::SliderInt("##lv", &S.levels, 0, 30);
            row("Босс каждый", "каждый N-й уровень — сложный, с повышенной наградой; 0 — без боссов"); ImGui::SliderInt("##boss", &S.bossEvery, 0, 10);
            row("Карты");
            {
                std::vector<std::pair<std::string, std::string>> presets;
                for (auto& p : W.catalog["mapPresets"].items()) {
                    const std::string id = p.str();
                    presets.push_back({id, id == "auto" ? "авто по жанру" : id == "none" ? "копии карты шаблона" : "генератор «" + id + "»"});
                }
                combo("##preset", S.mapPreset, presets);
            }
            row("Размер карт"); segmented("size", S.mapSize, {{"auto", "как есть"}, {"small", "меньше"}, {"large", "больше"}});
            row("Сложность"); segmented("diff", S.difficultyCurve, {{"easy", "мягкая"}, {"normal", "обычная"}, {"hard", "жёсткая"}});
            row("Стартовые ресурсы"); ImGui::SliderInt("##res", &S.startResourcesPercent, 25, 400, "%d%%");
            toggleRow("Карты шаблона — в схватку", S.keepTemplateMaps);
            toggleRow("Проверить ботами", S.verifyLevels, "бот проходит обучение и играет несколько уровней; ~10–30 с");
            endForm();
        }
        break;
    }
    case 5: {
        ImGui::TextUnformatted("Идеи для меты");
        hint("Что должно быть вокруг матчей: прогрессия, награды, магазин, события, кланы, боевой пропуск — своими словами. "
             "Агент сделает мету по этому списку; настройки ниже — основа мастера.");
        ui::inputMultiline("##metaidea", S.metaIdea, ImVec2(-1, 90));
        dictate("dict_meta", S.metaIdea, S);
        ImGui::Spacing();
        if (beginForm("##f5")) {
            row("Мягкая валюта", "награды за уровни, улучшения");
            ImGui::InputText("##soft", W.soft, sizeof W.soft, ImGuiInputTextFlags_CharsNoBlank);
            row("Твёрдая валюта", "магазин, покупки");
            ImGui::InputText("##hard", W.hard, sizeof W.hard, ImGuiInputTextFlags_CharsNoBlank);
            S.currencies.clear();
            if (W.soft[0]) S.currencies.push_back(W.soft);
            if (W.hard[0]) S.currencies.push_back(W.hard);
            toggleRow("Улучшения", S.upgrades, "покупаются в меню, дают бонусы в матче: здоровье, урон, скорость, ресурсы");
            toggleRow("Магазин", S.shop);
            toggleRow("Награда дня", S.daily, "7 дней подряд, на 3-й и 7-й — твёрдая валюта");
            toggleRow("Экран профиля", S.profileScreen);
            row("Опыт на уровень"); ImGui::InputInt("##xp", &S.xpPerLevel, 10, 100);
            endForm();
        }
        break;
    }
    case 6: {
        if (beginForm("##f6")) {
            toggleRow("Реклама", S.ads, "награда ×2 за просмотр, бесплатная валюта, межстраничная между матчами");
            ImGui::BeginDisabled(!S.ads);
            row("Межстраничная каждые"); ImGui::SliderInt("##inter", &S.interstitialEvery, 0, 10, S.interstitialEvery ? "%d матча" : "никогда");
            ImGui::EndDisabled();
            toggleRow("Покупки", S.purchases, "наборы твёрдой валюты и «без рекламы»");
            ImGui::BeginDisabled(!S.purchases);
            row("Магазины приложений");
            pillList(S.stores, stringsOf(W.catalog["stores"]));
            ImGui::EndDisabled();
            endForm();
        }
        ImGui::Spacing();
        hint("Пустой список магазинов — по платформам (Google Play, App Store, Яндекс Игры). Для теста ставятся демо-блоки рекламы Яндекса "
             "и заглушки для компьютера; свои id — в game.json → plugins.");
        break;
    }
    case 7: {
        if (beginForm("##f7")) {
            toggleRow("Иконка и заставка из арта", S.brandingFromArt);
            toggleRow("Цвета интерфейса из арта", S.paletteFromArt, "акцент и фон меню берутся из главного арта");
            endForm();
        }
        break;
    }
    }
    ImGui::EndDisabled();
    endCard();
    // back / next
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::BeginDisabled(busy || W.page == 0);
    if (ImGui::Button("Назад", ImVec2(130, 0)) && W.page > 0) --W.page;
    ImGui::EndDisabled();
    ImGui::SameLine(mainW - 130);
    ImGui::BeginDisabled(busy || W.page == kPageCount - 1);
    if (ImGui::Button("Далее " ICON_CHEVRON_RIGHT, ImVec2(130, 0)) && W.page < kPageCount - 1) ++W.page;
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::SameLine(0, gap);

    // ---------------------------------------------------------------- plan (recomputed when the answers change)
    if (W.source != 0) S.artDir.clear();
    if (W.source != 1) S.artUrl.clear();
    S.levels = std::clamp(S.levels, 0, 40);
    S.interstitialEvery = std::max(0, S.interstitialEvery);
    S.xpPerLevel = std::max(10, S.xpPerLevel);
    const std::string key = S.toJson().dump(0);
    if (!busy && key != W.specKey && ImGui::GetTime() - W.planAt > 0.3) {
        W.specKey = key;
        W.planAt = ImGui::GetTime();
        try { W.plan = planGame(ws_.engineDir, S); W.planError.clear(); }
        catch (const std::exception& e) { W.plan = Json(); W.planError = e.what(); }
    }

    // ---------------------------------------------------------------- run
    if (W.phase == WizardUi::Fetching && W.fetched) {
        if (W.fetcher.joinable()) W.fetcher.join();
        if (W.fetchDir.empty()) { W.phase = WizardUi::Failed; W.report = Json::object(); W.report.set("error", Json::string("ссылка: " + W.fetchError)); }
        else {
            WizardSpec s = S;
            s.artDir = W.fetchDir;
            s.artUrl.clear();
            W.maker = std::make_unique<GameMaker>(ws_, s);
            W.phase = WizardUi::Making;
        }
    }
    if (W.phase == WizardUi::Making && W.maker) {
        W.maker->step();   // one step per frame: the window stays alive and shows progress
        W.report = W.maker->report();
        if (W.maker->done()) {
            W.phase = W.maker->failed() ? WizardUi::Failed : WizardUi::Done;
            ws_.log("wizard", W.report.getStr("summary"), W.maker->failed());
            if (!W.maker->failed()) {
                W.taskFile = W.report.getStr("task");
                W.agentOffered = false;
                const std::string who = S.agent ? agentClientFor(W, S) : std::string();
                requestOpenProject(W.maker->projectFile());   // the editor's own open flow (views, layout)
                if (!who.empty() && !W.taskFile.empty()) {
                    std::string err;
                    const std::string log = (fs::path(ws_.logsDir()) / ("wizard-agent-" + std::to_string(std::time(nullptr)) + ".log")).string();
                    if (g_agent.start(who, W.maker->projectDir(), W.taskFile, log, &err)) {
                        ws_.log("wizard", std::string("Агент ") + (who == "codex" ? "Codex" : "Claude Code") + " доводит игру по описанию (лог: " + log + ")");
                        status = "Агент доводит игру по описанию — ход работы в окне Agent и в «Сделать игру»";
                    } else { ws_.log("wizard", "Агент не запустился: " + err, true); W.agentOffered = true; }
                } else if (S.agent) W.agentOffered = true;
            }
        }
    }

    // ---------------------------------------------------------------- summary
    beginCard("##wizsum", ImVec2(sumW, height));
    ImGui::PushStyleColor(ImGuiCol_Text, ui::accentColor());
    ImGui::TextUnformatted("ИТОГ");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    // the template: a tile with its initial and name
    {
        const std::string name = W.plan.isObject() ? W.plan.getStr("templateName") : std::string("—");
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + 52, p.y + 52), accent(0.85f), 12);
        const std::string letter = name.empty() ? "?" : name.substr(0, (unsigned char)name[0] >= 0xC0 ? 2 : 1);
        ImGui::SetWindowFontScale(1.5f);
        const ImVec2 ls = ImGui::CalcTextSize(letter.c_str());
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 26 - ls.x * 0.5f, p.y + 26 - ls.y * 0.5f), IM_COL32(255, 255, 255, 255), letter.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 64, p.y + 6));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(S.title.empty() ? "Без названия" : S.title.c_str());
        ImGui::TextDisabled("на основе «%s»", name.c_str());
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 62));
    }
    if (!W.planError.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), ICON_WARNING " %s", W.planError.c_str());
        ImGui::PopTextWrapPos();
    } else if (W.plan.isObject()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", W.plan.getStr("reason").c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    const std::string arche = W.plan.isObject() ? W.plan.getStr("tutorialArchetype") : "";
    const std::string archeRu = arche == "economy" ? "экономика" : arche == "garrison" ? "гарнизоны" : arche == "autobuild" ? "автостройка" : arche == "generic" ? "армия" : "—";
    fact(ICON_HELP, "Обучение", tutorialText(S.tutorial) + (arche.empty() || arche == "none" ? "" : " · " + archeRu));
    fact(ICON_LEVELS, "Уровни", S.levels ? std::to_string(S.levels) + (S.bossEvery ? ", босс каждый " + std::to_string(S.bossEvery) + "-й" : "") : "только карты шаблона");
    {
        std::string meta;
        auto add = [&](bool on, const char* t) { if (on) meta += (meta.empty() ? "" : ", ") + std::string(t); };
        add(S.upgrades, "улучшения"); add(S.shop, "магазин"); add(S.daily, "награда дня");
        fact(ICON_CASTLE, "Мета", meta.empty() ? "меню и уровни" : meta);
    }
    fact(ICON_PLUG, "Деньги", S.ads || S.purchases ? std::string(S.ads ? "реклама" : "") + (S.ads && S.purchases ? " + " : "") + (S.purchases ? "покупки" : "") : "нет");
    fact(ICON_IMAGE, "Контент", W.source == 0 ? (S.artDir.empty() ? "папка не выбрана" : fs::path(S.artDir).filename().string())
                                 : W.source == 1 ? (S.artUrl.empty() ? "ссылка не задана" : "по ссылке") : "арт шаблона");
    {
        std::string langs;
        for (auto& l : S.languages) langs += (langs.empty() ? "" : " ") + l;
        fact(ICON_GLOBE, "Языки", langs.empty() ? "—" : langs);
        const std::string who = S.agent ? agentClientFor(W, S) : std::string();
        fact(ICON_AGENT, "Агент", !S.agent ? "без доводки (шаблон как есть)" : g_agent.running() ? "работает: " + std::to_string(g_agent.toolCalls()) + " действий"
                                  : who.empty() ? "не подключён — задание для копирования" : (who == "codex" ? "Codex CLI" : "Claude Code") + std::string(" доведёт по описанию"));
    }
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    const std::string target = (fs::path(S.location) / projectFolderName(S.title)).string();
    ImGui::TextDisabled("Будет создан");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(target.c_str());
    ImGui::PopTextWrapPos();

    // bottom: progress / result / the button
    const float bottomH = 54 + (busy ? 46 : 0) + (W.phase == WizardUi::Done || W.phase == WizardUi::Failed ? 70 : 0);
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY() + 8, ImGui::GetWindowHeight() - bottomH - 18));
    if (busy) {
        const float pr = W.phase == WizardUi::Fetching ? 0.03f : W.maker ? W.maker->progress() : 0;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui::accentColor());
        ImGui::ProgressBar(pr, ImVec2(-1, 8), "");
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", W.phase == WizardUi::Fetching ? "Скачиваю по ссылке…" : W.maker->current().c_str());
        ImGui::Spacing();
    } else if (W.phase == WizardUi::Done || W.phase == WizardUi::Failed) {
        ImGui::PushTextWrapPos(0);
        if (W.phase == WizardUi::Failed) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), ICON_ERROR " %s", W.report.getStr("error").c_str());
        else ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), ICON_CHECK " Игра готова и открыта");
        ImGui::PopTextWrapPos();
        if (g_agent.started() || W.agentOffered) {
            if (g_agent.running()) {
                ImGui::TextColored(ui::accentColor(), ICON_AGENT " Агент работает: %d действий", g_agent.toolCalls());
                for (auto& l : g_agent.lastLines(4)) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", l.c_str()); ImGui::PopTextWrapPos(); }
                if (ImGui::SmallButton(ICON_STOP " Остановить агента")) g_agent.stop();
            } else if (g_agent.finished()) {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), ICON_CHECK " Агент закончил (%d действий) — итог в WIZARD.md", g_agent.toolCalls());
            }
            if (W.agentOffered && !W.taskFile.empty()) {
                if (ImGui::SmallButton(ICON_COPY " Скопировать задание агенту")) {
                    std::ifstream f(W.taskFile, std::ios::binary);
                    std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    ImGui::SetClipboardText(t.c_str());
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Вставьте в Claude Code, Cursor, Windsurf или Claude Desktop с подключённым rtsengine.\n%s", W.taskFile.c_str());
            }
        }
        const Json& warns = W.report["warnings"];
        if (warns.size()) {
            ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), ICON_WARNING " Проверить: %d (WIZARD.md)", int(warns.size()));
            if (ImGui::IsItemHovered()) {
                std::string all;
                for (auto& w : warns.items()) all += "• " + w.str() + "\n";
                ImGui::SetTooltip("%s", all.c_str());
            }
        }
        ImGui::Spacing();
    }
    const bool ready = !busy && !S.title.empty() && W.planError.empty() && (W.source != 1 || !S.artUrl.empty());
    ImGui::BeginDisabled(!ready);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
    ImGui::PushStyleColor(ImGuiCol_Button, ui::accentColor());
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, mix(ui::accentColor(), ImVec4(1, 1, 1, 1), 0.15f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, mix(ui::accentColor(), ImVec4(0, 0, 0, 1), 0.15f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool go = ImGui::Button(W.phase == WizardUi::Done ? ICON_ZAP "  Сделать ещё одну" : ICON_ZAP "  Сделать игру", ImVec2(-1, 48));
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    if (go) {
        if (ws_.map.dirty && !ws_.map.path.empty()) ws_.map.save(ws_.map.path, nullptr);   // the open project is left: keep its edits
        W.report = Json::object();
        W.maker.reset();
        if (W.source == 1 && !S.artUrl.empty()) {
            W.phase = WizardUi::Fetching;
            W.fetched = false;
            if (W.fetcher.joinable()) W.fetcher.join();
            const std::string url = S.artUrl, dir = wizardStagingDir();
            W.fetcher = std::thread([url, dir] {
                std::string err;
                g_wiz.fetchDir = fetchWizardArt(url, dir, &err);
                g_wiz.fetchError = err;
                g_wiz.fetched = true;
            });
        } else {
            WizardSpec s = S;
            if (W.source == 2) s.artDir.clear();
            W.maker = std::make_unique<GameMaker>(ws_, s);
            W.phase = WizardUi::Making;
        }
    }
    endCard();

    ImGui::PopStyleVar(3);
    std::string chosen;
    if (hubFolderBrowser(W.browsing, W.browseDir, W.newFolder, sizeof W.newFolder, false, chosen)) {
        if (W.browseFor == 0) S.artDir = chosen; else S.location = chosen;
    }
}

} // namespace rts::editor

namespace rts::editor {
// Every frame (Editor.cpp): the agent's progress into the activity feed and the status bar, also when the hub is closed.
void Editor::pollGameWizard() {
    for (auto& l : g_agent.takeLines()) ws_.log("agent", l);
    static bool wasRunning = false;
    if (g_agent.running()) status = "Агент доводит игру: " + std::to_string(g_agent.toolCalls()) + " действий";
    else if (wasRunning) status = "Агент закончил: " + std::to_string(g_agent.toolCalls()) + " действий, итог — в WIZARD.md проекта";
    wasRunning = g_agent.running();
}
} // namespace rts::editor
