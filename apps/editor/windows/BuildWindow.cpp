// Build: everything about a build before it is made. Platform and configuration, quality
// settings (pack compression, texture atlases, sound profile, split/download), an estimate of the
// build size (rtsbuild --estimate: the real content pack, no app build), the heaviest files, and
// per-file settings: zstd level or store, image size per platform, leave a file out. Settings are
// stored in game.json ("packaging", "atlas"), so builds from the command line and CI match.
// Toolchains: for the selected platform the window checks in the background what the build needs
// (rtsbuild --check-toolchain, tools/rtsbuild/Toolchains.h), shows what is found / missing and
// installs it (rtsbuild --install-toolchain) through the same streamed runner as builds. A build
// that stops with RTS_MISSING_TOOLCHAIN offers to install and rebuild.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <future>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <filesystem>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "../core/BuildFixes.h"
#include "engine/i18n/Localization.h"
#include "engine/platform/WindowSettings.h"
#include "imgui_internal.h" // settings handler: editor prefs in editor_layout.ini

namespace fs = std::filesystem;

namespace rts::editor {

namespace {
struct Target { const char* id; const char* name; const char* audio; double engineMB; double storeLimitMB; const char* storeNote; };
// Engine/runtime size without content (measured Release builds, rounded); store limits are where
// the platform starts to make life harder (cellular download, base module, web first load).
const Target kTargets[] = {
    {"macos", "macOS (.app)", "desktop", 16, 0, ""},
    {"ios", "iOS", "mobile", 9, 200, "App Store: больше 200 МБ не качается по сотовой сети без подтверждения"},
    {"android", "Android", "mobile", 12, 200, "Google Play: базовый модуль AAB до 200 МБ (остальное — докачкой)"},
    {"web", "Web (HTML5)", "web", 6, 30, "Web: первая загрузка больше ~30 МБ заметно теряет игроков"},
    {"windows", "Windows (.exe)", "desktop", 9, 0, ""},
    {"linux", "Linux", "desktop", 9, 0, ""},
};
constexpr int kTargetCount = int(sizeof(kTargets) / sizeof(kTargets[0]));

std::string mb(double bytes) {
    char b[32];
    if (bytes >= 1048576) std::snprintf(b, sizeof b, "%.1f МБ", bytes / 1048576);
    else if (bytes >= 1024) std::snprintf(b, sizeof b, "%.0f КБ", bytes / 1024);
    else std::snprintf(b, sizeof b, "%.0f Б", bytes);
    return b;
}
std::string quote(const std::string& s) {
#if defined(_WIN32)
    return "\"" + s + "\"";
#else
    std::string r = "'";
    for (char c : s) r += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return r + "'";
#endif
}
bool isImage(const std::string& p) {
    auto dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = p.substr(dot);
    for (auto& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp";
}
} // namespace

class BuildWindow : public EditorWindow {
public:
    const char* id() const override { return "build"; }
    const char* title() const override { return "Сборка"; }
    const char* group() const override { return "publish"; }
    const char* dockArea() const override { return "center"; }
    int order() const override { return 5; }
    bool openByDefault() const override { return false; }

    BuildWindow() {
        // start on the platform the editor runs on (its build can be tried right away)
        const std::string host = platform::name();
        target_ = host == "macos" ? 0 : host == "windows" ? 4 : 5;
        // "Устанавливать недостающее автоматически" is a preference of this user, not of the game
        if (!ImGui::GetCurrentContext()) return;
        ImGuiSettingsHandler h;
        h.TypeName = "RtsBuild";
        h.TypeHash = ImHashStr("RtsBuild");
        h.UserData = this;
        h.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler* hh, const char* name) -> void* { return std::strcmp(name, "Prefs") == 0 ? hh->UserData : nullptr; };
        h.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line) {
            int v = 0;
            if (std::sscanf(line, "AutoInstall=%d", &v) == 1) static_cast<BuildWindow*>(entry)->autoInstall_ = v != 0;
        };
        h.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* hh, ImGuiTextBuffer* buf) {
            buf->appendf("[%s][Prefs]\nAutoInstall=%d\n\n", hh->TypeName, static_cast<BuildWindow*>(hh->UserData)->autoInstall_ ? 1 : 0);
        };
        ImGui::AddSettingsHandler(&h);
    }

    void update(Editor& ed, double dt) override {
        updateToolchains(ed);
        if (estimating_ && !ed.ws().building()) { estimating_ = false; loadReports(ed); }
        if (dirtyIn_ > 0 && (dirtyIn_ -= dt) <= 0 && autoEstimate_ && open) estimate(ed);
        // first look at the window: estimate right away, the numbers are the point of it
        if (open && !ed.ws().project.dir.empty() && reports_.find(kTargets[target_].id) == reports_.end() && !estimating_ && !ed.ws().building() && askedFor_ != target_ * 1000 + int(ed.ws().project.dir.size())) {
            askedFor_ = target_ * 1000 + int(ed.ws().project.dir.size());
            estimate(ed);
        }
    }

    void draw(Editor& ed) override {
        auto& ws = ed.ws();
        if (ws.project.dir.empty()) { ui::emptyStateHere(ICON_BUILD, "Игра не открыта", "Игра → Открыть"); return; }
        if (lastProject_ != ws.project.dir) { lastProject_ = ws.project.dir; reports_.clear(); }
        if (!fixLocLoaded_) {
            fixLoc_.load([](const std::string& name, std::string& out) { return platform::readContentFile(name, out); },
                         "ru", {ws.engineDir + "/engine/assets/lang"});
            fixLocLoaded_ = true;
        }
        header(ed);
        toolchainPanel(ed);
        missingPopup(ed);
        ImGui::Separator();
        if (ImGui::BeginTable("##build", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("settings", ImGuiTableColumnFlags_WidthFixed, std::max(ImGui::GetFontSize() * 24, ImGui::GetContentRegionAvail().x * 0.5f));
            ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::BeginChild("##settings", ImVec2(0, 0));
            settings(ed);
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("##report", ImVec2(0, 0));
            report(ed);
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }

private:
    // ------------------------------------------------------------ top bar
    void header(Editor& ed) {
        auto& ws = ed.ws();
        // platforms as tiles: the choice decides the estimate, the toolchain check and «Собрать»
        static const char* labels[] = {ICON_MONITOR "  macOS", ICON_SMARTPHONE "  iOS", ICON_SMARTPHONE "  Android", ICON_GLOBE "  Web",
                                       ICON_MONITOR "  Windows", ICON_MONITOR "  Linux"};
        static_assert(sizeof labels / sizeof *labels == kTargetCount);
        if (ui::choiceGrid("##target", target_, labels, kTargetCount, 104)) touched();
        // row 2: configuration, secondary actions on the left; the main action («Собрать») on the right edge
        static const char* configs[] = {"Debug", "Release"};
        ui::segmented("##cfg", release_, configs, 2, ImGui::GetFontSize() * 9);
        ImGui::SameLine(0, 12);
        ImGui::BeginDisabled(ws.building());
        if (ui::button(ICON_HARD_DRIVE, "Оценить размер")) estimate(ed);
        ImGui::SetItemTooltip("Собрать только пак контента и показать размер по файлам (секунды, без приложения)");
        ImGui::SameLine();
        if (ui::button(ICON_LAYOUT, "Все платформы")) build(ed, "all");
        ImGui::SetItemTooltip("Собрать игру для каждой платформы, для которой есть инструменты");
        ImGui::SameLine();
        const std::string main = "Собрать " + std::string(kTargets[target_].name);
        const float mainW = ImGui::CalcTextSize((std::string(ICON_BUILD) + "  " + main).c_str()).x + ImGui::GetStyle().FramePadding.x * 4;
        const float rx = ImGui::GetWindowContentRegionMax().x - mainW;
        if (rx > ImGui::GetCursorPosX() + 8) ImGui::SetCursorPosX(rx); else ImGui::NewLine();
        if (ui::button(ICON_BUILD, main.c_str(), true, ImVec2(mainW, 0))) build(ed, kTargets[target_].id);
        ImGui::EndDisabled();
        ui::checkbox("Пересчитывать размер после каждого изменения", &autoEstimate_);
        ImGui::SetItemTooltip("Собирается только пак контента, без приложения — секунды.");
        if (ws.building()) { ImGui::SameLine(); ui::toneText(ui::Tone::Warn, "%s %s", ICON_REFRESH, estimating_ ? "оценка…" : ws.runLabel().rfind("toolchain-", 0) == 0 ? "идёт установка…" : "идёт сборка…"); }
    }

    // ------------------------------------------------------------ game window on desktops (game.json "window")
    void windowSection(Editor& ed) {
        const std::string tid = kTargets[target_].id;
        ui::section(ICON_APP_WINDOW, "Окно игры");
        if (tid == "ios" || tid == "android") { ui::hint("На телефонах и планшетах игра всегда во весь экран. Ориентация — game.json \"orientation\"."); return; }
        if (tid == "web") { ui::hint("В браузере игра занимает страницу; полный экран — F11 или кнопкой игры (platform::setFullscreen)."); return; }
        Json& m = manifest(ed);
        const Json& all = m["window"];
        // scope: all desktops, or only the selected one (window.<platform> overrides)
        static const char* scopes[] = {"Все ПК и Mac", "Только эта платформа"};
        ui::segmented("##wscope", windowScope_, scopes, 2);
        ImGui::SetItemTooltip("Только эта платформа: настройки пишутся в window.%s и переопределяют общие.", tid.c_str());
        const Json eff = platform::windowBlockFor(all, tid);              // what this platform gets
        Json block = windowScope_ == 0 ? (all.isObject() ? all : Json::object()) : (all[tid].isObject() ? all[tid] : Json::object());
        const Json& view = windowScope_ == 0 ? block : eff;                // values shown in the widgets
        bool changed = false;
        static const char* modeIds[] = {"windowed", "fixed", "maximized", "fullscreen"};
        static const char* modeNames[] = {ICON_APP_WINDOW "  Окно", ICON_LOCK "  Фикс. размер", ICON_MAXIMIZE "  Развёрнуто", ICON_FULLSCREEN "  Полный экран"};
        static const char* modeHelp[] = {"Обычное окно: пользователь меняет размер и разворачивает его.",
                                         "Окно ровно заданного размера: без рамки изменения размера и кнопки «развернуть».",
                                         "Окно на весь рабочий стол (с заголовком и панелью задач / Dock).",
                                         "Сразу во весь экран: на Mac — отдельный экран (Space), на Windows и Linux — окно без рамки поверх монитора."};
        std::string mode = view.getStr("mode", "windowed");
        int mi = 0;
        for (int i = 0; i < 4; ++i) if (mode == modeIds[i]) mi = i;
        if (ui::choiceGrid("##wmode", mi, modeNames, 4, 120)) { block.set("mode", Json::string(modeIds[mi])); changed = true; }
        ui::hint(modeHelp[mi]);
        if (ui::beginProps("##window")) {
            int size[2] = {int(view["width"].integer(1280)), int(view["height"].integer(720))};
            ui::prop("Размер", mi >= 2 ? "Размер окна, когда игрок выйдет из полного экрана / развёрнутого окна" : "Размер области игры в точках (на Retina и 4K — умножается на масштаб экрана)");
            ImGui::InputInt2("##size", size);
            if (ImGui::IsItemDeactivatedAfterEdit()) { block.set("width", Json::number(int64_t(std::clamp(size[0], 320, 16384)))); block.set("height", Json::number(int64_t(std::clamp(size[1], 240, 16384)))); changed = true; }
            ui::prop("Быстрый выбор");
            static const int presets[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {1024, 768}, {960, 540}};
            for (auto& pr : presets) {
                char b[24]; std::snprintf(b, sizeof b, "%d×%d", pr[0], pr[1]);
                if (&pr != &presets[0]) ImGui::SameLine(0, 4);
                const bool cur = size[0] == pr[0] && size[1] == pr[1];
                if (cur) ImGui::PushStyleColor(ImGuiCol_Text, ui::accentColor());
                if (ImGui::SmallButton(b)) { block.set("width", Json::number(int64_t(pr[0]))); block.set("height", Json::number(int64_t(pr[1]))); changed = true; }
                if (cur) ImGui::PopStyleColor();
            }
            if (mi == 0 || mi == 2) {
                int mn[2] = {int(view["minWidth"].integer(0)), int(view["minHeight"].integer(0))};
                ui::prop("Мин. размер", "Меньше этого окно не сжать (0 — без ограничения)");
                ImGui::InputInt2("##min", mn);
                if (ImGui::IsItemDeactivatedAfterEdit()) { block.set("minWidth", Json::number(int64_t(std::max(0, mn[0])))); block.set("minHeight", Json::number(int64_t(std::max(0, mn[1])))); changed = true; }
                ui::propWide();
                bool aspect = view.getBool("keepAspect", false);
                if (ui::checkbox("Сохранять пропорции при изменении размера", &aspect)) { block.set("keepAspect", Json::fromBool(aspect)); changed = true; }
            }
            ui::propWide();
            bool fs = view.getBool("allowFullscreen", true);
            if (ui::checkbox("Разрешить полный экран (F11, Alt+Enter, зелёная кнопка Mac)", &fs)) { block.set("allowFullscreen", Json::fromBool(fs)); changed = true; }
            ui::endProps();
        }
        windowPreview(eff, tid);
        if (changed) {
            Json out = all.isObject() ? all : Json::object();
            if (windowScope_ == 0) { for (const char* p : {"macos", "windows", "linux"}) if (all.has(p)) block.set(p, all[p]); out = block; }
            else out.set(tid, block);
            save(ed, "window", out);
        }
        if (windowScope_ == 1 && all[tid].isObject() && ui::button(ICON_UNDO, "Убрать настройки этой платформы")) {
            Json out = all;
            out.erase(tid);
            save(ed, "window", out);
        }
        for (auto& pr : platform::windowSettingsProblems(all)) ui::callout(ui::Tone::Error, pr);
        ui::hint("game.json → \"window\". Тестовый запуск в редакторе и скриншоты всегда в окне.");
    }

    // A small picture: the screen and how the window sits on it.
    void windowPreview(const Json& b, const std::string& tid) {
        const float w = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 15), h = w * 9 / 16;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 frame = ImGui::GetColorU32(ImGuiCol_Border), screen = ImGui::GetColorU32(ImGuiCol_FrameBg);
        const ImVec4 acc = ui::accentColor();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), screen, 6);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), frame, 6, 0, 1.5f);
        const std::string mode = b.getStr("mode", "windowed");
        const float bar = tid == "macos" ? h * 0.06f : 0, task = tid == "windows" ? h * 0.08f : 0;
        if (bar > 0) dl->AddRectFilled(ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + w - 2, p.y + 2 + bar), ImGui::GetColorU32(ImGuiCol_TableHeaderBg), 4);
        if (task > 0) dl->AddRectFilled(ImVec2(p.x + 2, p.y + h - 2 - task), ImVec2(p.x + w - 2, p.y + h - 2), ImGui::GetColorU32(ImGuiCol_TableHeaderBg), 4);
        ImVec2 a, c;
        if (mode == "fullscreen") { a = ImVec2(p.x + 2, p.y + 2); c = ImVec2(p.x + w - 2, p.y + h - 2); }
        else if (mode == "maximized") { a = ImVec2(p.x + 2, p.y + 2 + bar); c = ImVec2(p.x + w - 2, p.y + h - 2 - task); }
        else {   // the window at its size on a 1920×1080 screen
            const float sw = float(b["width"].integer(1280)) / 1920.0f, sh = float(b["height"].integer(720)) / 1080.0f;
            const float ww = std::min(w - 8, w * sw), wh = std::min(h - 8 - bar - task, h * sh);
            a = ImVec2(p.x + (w - ww) * 0.5f, p.y + bar + (h - bar - task - wh) * 0.5f);
            c = ImVec2(a.x + ww, a.y + wh);
        }
        dl->AddRectFilled(a, c, ui::rgbaU32(acc, 0.25f), mode == "fullscreen" ? 4 : 3);
        dl->AddRect(a, c, ui::rgbaU32(acc, 0.9f), mode == "fullscreen" ? 4 : 3, 0, 1.5f);
        if (mode != "fullscreen") dl->AddRectFilled(a, ImVec2(c.x, a.y + std::min(8.0f, (c.y - a.y) * 0.2f)), ui::rgbaU32(acc, 0.6f), 3, ImDrawFlags_RoundCornersTop);
        char t[64];
        if (mode == "fullscreen") std::snprintf(t, sizeof t, "весь экран");
        else if (mode == "maximized") std::snprintf(t, sizeof t, "развёрнуто");
        else std::snprintf(t, sizeof t, "%lld×%lld%s", (long long)b["width"].integer(1280), (long long)b["height"].integer(720), mode == "fixed" ? " · фикс." : "");
        const ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2((a.x + c.x - ts.x) * 0.5f, (a.y + c.y - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), t);
        ImGui::Dummy(ImVec2(w, h));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("экран 1920×1080");
        ImGui::TextDisabled("%s", tid == "macos" ? "macOS" : tid == "windows" ? "Windows" : "Linux");
        ImGui::EndGroup();
    }

    void build(Editor& ed, const std::string& target) {
        ed.ws().startBuild(target, release_ ? "Release" : "Debug", buildFlags() + (autoInstall_ ? " --install-missing" : ""));
        lastBuildTarget_ = target;
        if (auto* c = ed.window("console")) c->open = true;
    }

    // ------------------------------------------------------------ toolchains
    struct Check { Json status; std::string error; };

    void requestCheck(Editor& ed, const std::string& target) {
        if (checking_.valid()) { recheck_ = true; return; }
        checkingTarget_ = target;
        Workspace* ws = &ed.ws();
        checking_ = std::async(std::launch::async, [ws, target] {
            Check c;
            try { c.status = ws->toolchainCheck(target); } catch (const std::exception& e) { c.error = e.what(); }
            return c;
        });
    }

    void updateToolchains(Editor& ed) {
        auto& ws = ed.ws();
        if (checking_.valid() && checking_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            checks_[checkingTarget_] = checking_.get();
            if (recheck_) { recheck_ = false; checks_.erase(kTargets[target_].id); }
        }
        const std::string tid = kTargets[target_].id;
        if (open && !checking_.valid() && !checks_.count(tid)) requestCheck(ed, tid);
        // a run of rtsbuild finished: after an install re-check (and rebuild); after a build look for missing tools
        if (ws.runCount() == seenRuns_ || ws.building()) return;
        seenRuns_ = ws.runCount();
        const std::string label = ws.runLabel();
        if (label.rfind("toolchain-", 0) == 0) {
            checks_.clear();
            if (ws.buildResult() != 0) { installQueue_.clear(); rebuildAfter_.clear(); installError_ = "установка не удалась — подробности в «Консоли»"; return; }
            installError_.clear();
            if (!installQueue_.empty()) { startInstall(ed, installQueue_.front()); installQueue_.pop_front(); return; }
            if (!rebuildAfter_.empty()) { std::string t = rebuildAfter_; rebuildAfter_.clear(); build(ed, t); }
            return;
        }
        if (label.rfind("build-", 0) == 0 && ws.buildResult() != 0) {
            missing_.clear();
            std::string log = ws.buildLog();
            for (size_t pos = 0; (pos = log.find("RTS_MISSING_TOOLCHAIN ", pos)) != std::string::npos;) {
                size_t e = log.find('\n', pos);
                std::string line = log.substr(pos + 22, e == std::string::npos ? std::string::npos : e - pos - 22);
                pos = e == std::string::npos ? log.size() : e;
                if (size_t c = line.find(':'); c != std::string::npos) missing_.push_back({line.substr(0, c), describe(ws, line.substr(0, c), line.substr(c + 1))});
            }
            if (!missing_.empty()) { showMissing_ = missingOpen_ = true; checks_.clear(); ed.focusWindow("build"); }
        }
    }

    // "emsdk,cmake" -> "Emscripten (emsdk) ≈ 400 МБ, CMake ≈ 50 МБ" from the last check of that target
    std::string describe(Workspace& ws, const std::string& target, const std::string& ids) {
        if (!checks_.count(target)) {   // the window was closed during the build: check now (a fraction of a second)
            Check c;
            try { c.status = ws.toolchainCheck(target); } catch (const std::exception& e) { c.error = e.what(); }
            checks_[target] = c;
        }
        auto it = checks_.find(target);
        std::string out, id;
        std::stringstream ss(ids);
        while (std::getline(ss, id, ',')) {
            while (!id.empty() && id[0] == ' ') id.erase(0, 1);
            std::string text = id;
            if (it != checks_.end())
                for (auto& r : it->second.status["requirements"].items())
                    if (r.getStr("id") == id) text = r.getStr("name") + (r.has("size") ? " (" + r.getStr("size") + ")" : "");
            out += (out.empty() ? "" : ", ") + text;
        }
        return out;
    }

    void startInstall(Editor& ed, const std::string& what) {
        try { ed.ws().startTool("--install-toolchain " + what, "toolchain-" + what, false); }
        catch (const std::exception& e) { installError_ = e.what(); return; }
        if (auto* c = ed.window("console")) c->open = true;
    }

    static std::string targetName(const std::string& id) {
        for (auto& t : kTargets) if (id == t.id) return t.name;
        return id;
    }

    void toolchainPanel(Editor& ed) {
        auto& ws = ed.ws();
        const Target& t = kTargets[target_];
        auto it = checks_.find(t.id);
        if (it == checks_.end()) { ImGui::TextDisabled("%s Проверяю инструменты для %s…", ICON_REFRESH, t.name); return; }
        if (!it->second.error.empty()) {
            ui::callout(ui::Tone::Error, std::string("Не удалось проверить инструменты сборки для ") + t.name + ": " + it->second.error);
            if (ui::button(ICON_REFRESH, "Проверить снова")) checks_.erase(t.id);
            return;
        }
        const Json& st = it->second.status;
        const ImVec4 red = ui::toneColor(ui::Tone::Error), green = ui::toneColor(ui::Tone::Ok), yellow = ui::toneColor(ui::Tone::Warn);
        ImGui::PushID("toolchain");
        if (!st.getBool("hostSupported", true)) {
            ImGui::TextColored(yellow, "%s %s — на этом компьютере собирается только набор иконок.", ICON_INFO, st.getStr("hostNote").c_str());
            ImGui::PopID();
            return;
        }
        const bool ok = st.getBool("ok", false);
        bool anyAuto = false;
        for (auto& r : st["requirements"].items()) if (!r.getBool("found") && r.getBool("required") && r.getBool("autoInstall")) anyAuto = true;
        if (ok) ImGui::TextColored(green, "%s Всё для сборки %s установлено", ICON_CHECK, t.name);
        else ImGui::TextColored(red, "%s Для сборки %s нужно:", ICON_WARNING, t.name);
        ImGui::SameLine();
        ImGui::BeginDisabled(ws.building() || checking_.valid());
        if (ImGui::SmallButton(ICON_REFRESH " Проверить снова")) { checks_.erase(t.id); requestCheck(ed, t.id); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::checkbox("Устанавливать недостающее автоматически", &autoInstall_)) ImGui::MarkIniSettingsDirty();
        ui::help("«Собрать» сначала поставит то, чего не хватает (rtsbuild --install-missing), в папку ~/.rtsengine/toolchains — без прав администратора. "
                 "Удалить всё установленное: удалите эту папку.");
        for (auto& r : st["requirements"].items()) {
            if (r.getBool("found") || (!r.getBool("required") && r.getStr("id") != "ninja")) continue;
            ImGui::PushID(r.getStr("id").c_str());
            const bool req = r.getBool("required");
            ImGui::Bullet();
            std::string line = r.getStr("name") + " — " + r.getStr("detail", "не установлен");
            if (r.getBool("autoInstall") && r.has("size")) line += "  (" + r.getStr("size") + ")";
            if (!req) line += ", необязательно";
            ImGui::PushStyleColor(ImGuiCol_Text, req ? red : yellow);
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
            if (r.getBool("autoInstall")) {
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Установщик: %s", r.getStr("install").c_str());
            } else if (r.has("manual")) {
                // read-only field: the command can be selected and copied
                std::string m = r.getStr("manual");
                ImGui::Indent();
                ImGui::TextDisabled("Вручную:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x - 110));
                ImGui::InputText("##manual", m.data(), m.size() + 1, ImGuiInputTextFlags_ReadOnly);
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_COPY " Копировать")) ImGui::SetClipboardText(m.c_str());
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (!ok && anyAuto) {
            ImGui::BeginDisabled(ws.building());
            if (ui::button(ICON_DOWNLOAD, "Установить автоматически", true)) { installQueue_.clear(); rebuildAfter_.clear(); startInstall(ed, t.id); }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("в %s", st.getStr("toolchainsDir").c_str());
        }
        if (ws.building() && ws.runLabel().rfind("toolchain-", 0) == 0) { ImGui::SameLine(); ImGui::TextColored(yellow, "%s идёт установка — ход в «Консоли»", ICON_REFRESH); }
        if (!installError_.empty()) ImGui::TextColored(red, "%s", installError_.c_str());
        int found = 0;
        for (auto& r : st["requirements"].items()) found += r.getBool("found") ? 1 : 0;
        if (found && ImGui::TreeNode("found", "Найдено: %d", found)) {
            for (auto& r : st["requirements"].items()) {
                if (!r.getBool("found")) continue;
                std::string v = r.getStr("version");
                ImGui::TextColored(green, "%s", ICON_CHECK);
                ImGui::SameLine();
                ImGui::Text("%s%s", r.getStr("name").c_str(), v.empty() ? "" : ("  " + v).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s", r.getStr("path").c_str());
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    // Not an ImGui modal: a floating window over the editor (a modal opened from a docked window
    // was closed again by the focus restore in NewFrame). It stays until a button is pressed.
    void missingPopup(Editor& ed) {
        if (!missingOpen_) return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (showMissing_) {
            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowFocus();
            showMissing_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Always);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin(ICON_WARNING "  Не хватает инструментов###toolchain-missing", &missingOpen_, f)) {
            ImGui::TextWrapped("Сборка остановлена: на этом компьютере не хватает инструментов.");
            for (auto& [target, ids] : missing_) ImGui::BulletText("%s: %s", targetName(target).c_str(), ids.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("Их можно установить автоматически в ~/.rtsengine/toolchains — без прав администратора и изменений в системе "
                               "(удалить: удалить эту папку). После установки сборка запустится снова.");
            ImGui::Spacing();
            auto install = [&](bool rebuild) {
                installQueue_.clear();
                for (size_t i = 1; i < missing_.size(); ++i) installQueue_.push_back(missing_[i].first);
                rebuildAfter_ = rebuild ? (lastBuildTarget_.empty() ? missing_[0].first : lastBuildTarget_) : std::string();
                startInstall(ed, missing_[0].first);
                missingOpen_ = false;
            };
            ImGui::BeginDisabled(ed.ws().building());
            if (ui::button(ICON_DOWNLOAD, "Установить и собрать", true)) install(true);
            ImGui::SameLine();
            if (ImGui::Button("Только установить")) install(false);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Закрыть")) missingOpen_ = false;
        }
        ImGui::End();
    }

    std::string buildFlags() const {
        std::string f;
        if (audio_ != 0) { static const char* a[] = {"auto", "desktop", "mobile", "web", "off"}; f += " --audio " + std::string(a[audio_]); }
        return f;
    }

    void estimate(Editor& ed) {
        auto& ws = ed.ws();
        if (ws.building()) { dirtyIn_ = 0.5; return; }
        outDir_ = (fs::path(ws.project.dir) / "Builds" / ".temp" / "estimate").string();   // hidden: not a finished build
        ws.startBuild(kTargets[target_].id, "Release", "--estimate --out " + quote(outDir_) + buildFlags());
        estimating_ = true;
        estimateManifest_ = manifest(ed).dump();
        estimateTarget_ = kTargets[target_].id;
        dirtyIn_ = 0;
    }

    void loadReports(Editor& ed) {
        for (auto& t : kTargets) {
            fs::path p = fs::path(outDir_) / t.id / "size-report.json";
            std::error_code ec;
            if (!fs::exists(p, ec)) continue;
            try { reports_[t.id] = Json::parseFile(p.string()); } catch (const std::exception&) {}
        }
        if (ed.ws().buildResult() == 0) reportManifests_[estimateTarget_] = estimateManifest_;
        if (ed.ws().buildResult() != 0) error_ = "оценка не удалась — подробности в «Консоли»"; else error_.clear();
    }

    void touched() { dirtyIn_ = 0.8; }

    // ------------------------------------------------------------ game.json helpers
    Json& manifest(Editor& ed) { return ed.ws().project.manifest; }
    void save(Editor& ed, const std::string& path, const Json& value) {
        Json p = Json::object();
        p.set("path", Json::string(path));
        p.set("value", value);
        Json r = ed.ws().call("set_manifest", p);
        if (r.has("error")) error_ = r.getStr("error");
        touched();
    }
    // packaging.files (all platforms) or packaging.targets.<t>.files (this platform)
    std::string rulesPath() const { return perTarget_ ? std::string("packaging.targets.") + kTargets[target_].id + ".files" : "packaging.files"; }
    Json rulesList(Editor& ed) {
        const Json* j = jsonPath(manifest(ed), rulesPath());
        return j && j->isArray() ? *j : Json::array();
    }
    const Json* ruleFor(const Json& list, const std::string& path) {
        for (auto& r : list.items()) if (r.getStr("match") == path) return &r;
        return nullptr;
    }
    void setRule(Editor& ed, const std::string& path, const char* key, const Json& value) {
        Json list = rulesList(ed);
        Json out = Json::array();
        bool found = false;
        for (auto& r : list.items()) {
            if (r.getStr("match") != path) { out.push(r); continue; }
            found = true;
            Json n = r;
            if (value.isNull()) n.erase(key); else n.set(key, value);
            if (n.size() > 1) out.push(n);      // only "match" left: the rule is gone
        }
        if (!found && !value.isNull()) {
            Json n = Json::object();
            n.set("match", Json::string(path));
            n.set(key, value);
            out.push(n);
        }
        save(ed, rulesPath(), out);
    }

    // ------------------------------------------------------------ left column
    void settings(Editor& ed) {
        Json& m = manifest(ed);
        windowSection(ed);
        ui::section(ICON_IMAGE, "Иконки и заставка");
        if (ui::wideButton(ICON_IMAGE, "Открыть «Иконки и заставка»")) ed.focusWindow("branding");
        ui::help("Иконки всех размеров для каждой платформы и экран загрузки (game.json \"icons\", \"splash\") — окно «Иконки и заставка».");
        ui::section(nullptr, "Сжатие пака");
        int level = int(m["packaging"]["level"].integer(19));
        ImGui::SetNextItemWidth(180);
        if (ImGui::SliderInt("zstd", &level, 1, 22)) {}
        if (ImGui::IsItemDeactivatedAfterEdit()) save(ed, "packaging.level", Json::number(int64_t(level)));
        ui::help("Уровень сжатия ресурс-пака для всех файлов (19 — хороший баланс; 22 — меньше, но сборка дольше; на скорость загрузки в игре почти не влияет). Отдельным файлам можно задать свой уровень в таблице справа.");

        ui::section(nullptr, "Атласы текстур");
        bool atlas = m["atlas"].getBool("enable", true);
        if (ui::checkbox("Собирать атласы", &atlas)) save(ed, "atlas.enable", Json::fromBool(atlas));
        ui::help("Мелкие 2D-картинки (UI, спрайты, тайлы) собираются в страницы-атласы: меньше переключений текстур и меньше пак.");
        const Json& mp = m["atlas"]["maxPage"];
        const char* tid = kTargets[target_].id;
        int page = int(mp.isNumber() ? mp.integer() : mp.has(tid) ? mp[tid].integer() : mp.has("default") ? mp["default"].integer()
                                                                  : (std::string(tid) == "web" || std::string(tid) == "android" ? 2048 : 4096));
        static const int pages[] = {1024, 2048, 4096, 8192};
        ImGui::SetNextItemWidth(120);
        if (ImGui::BeginCombo("Страница", std::to_string(page).c_str())) {
            for (int pg : pages)
                if (ImGui::Selectable(std::to_string(pg).c_str(), pg == page)) save(ed, std::string("atlas.maxPage.") + tid, Json::number(int64_t(pg)));
            ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::TextDisabled("для %s", kTargets[target_].name);

        ui::section(nullptr, "Звук");
        static const char* audios[] = {"по платформе", "desktop", "mobile", "web", "без пережатия"};
        ImGui::SetNextItemWidth(160);
        if (ImGui::Combo("Профиль", &audio_, audios, 5)) touched();
        ui::help("Звуки и музыка пережимаются при сборке по профилю платформы (audio.json -> build). Качество профилей задаётся там же.");
        ImGui::TextDisabled("по платформе: %s", kTargets[target_].audio);

        ui::section(nullptr, "Докачка (разбиение)");
        const Json& pt = m["packaging"]["targets"][tid];
        bool split = pt.has("split") ? pt.getBool("split", false) : m["packaging"].getBool("split", false);
        if (ui::checkbox("Делить на базу и докачку", &split)) save(ed, std::string("packaging.targets.") + tid + ".split", Json::fromBool(split));
        ui::help("В сборке остаётся только нужное для старта, остальное — части для CDN, игра докачивает их сама (packaging.sources).");
        if (split) {
            std::string base = pt.has("baseSize") ? pt.getStr("baseSize") : m["packaging"].getStr("baseSize", "");
            ImGui::SetNextItemWidth(100);
            if (ui::inputString("База, не больше", base, ImGuiInputTextFlags_EnterReturnsTrue)) save(ed, std::string("packaging.targets.") + tid + ".baseSize", Json::string(base));
            std::string chunk = pt.has("chunkSize") ? pt.getStr("chunkSize") : m["packaging"].getStr("chunkSize", "8MB");
            ImGui::SetNextItemWidth(100);
            if (ui::inputString("Часть", chunk, ImGuiInputTextFlags_EnterReturnsTrue)) save(ed, std::string("packaging.targets.") + tid + ".chunkSize", Json::string(chunk));
            ImGui::TextDisabled("размеры: 40MB, 512KB, 1GB (Enter — сохранить)");
        }

        ui::section(nullptr, "Правила файлов");
        ui::checkbox("Только для этой платформы", &perTarget_);
        ui::help("Выключено: настройки файлов в таблице действуют на все платформы (packaging.files). Включено: только на выбранную (packaging.targets.<платформа>.files) — например, уменьшить текстуры только на Android.");
        Json list = rulesList(ed);
        if (list.size() == 0) ImGui::TextDisabled("нет — всё по умолчанию");
        for (size_t i = 0; i < list.size(); ++i) {
            const Json& r = list[i];
            std::string d = r.getStr("match") + ":";
            if (r.getBool("exclude", false)) d += " исключён";
            if (r.has("level")) d += r["level"].integer() == 0 ? " без сжатия" : " zstd " + std::to_string(r["level"].integer());
            if (r.has("maxSize")) d += " до " + std::to_string(r["maxSize"].integer()) + " px";
            if (r.has("scale")) { char b[24]; std::snprintf(b, sizeof b, " x%.2g", r["scale"].toDouble()); d += b; }
            ImGui::PushID(int(i));
            if (ImGui::SmallButton("x")) {
                Json out = Json::array();
                for (size_t k = 0; k < list.size(); ++k) if (k != i) out.push(list[k]);
                save(ed, rulesPath(), out);
            }
            ImGui::SameLine();
            ImGui::TextWrapped("%s", d.c_str());
            ImGui::PopID();
        }
        if (!error_.empty()) { ImGui::Spacing(); ui::toneText(ui::Tone::Error, "%s", error_.c_str()); }
    }

    // ------------------------------------------------------------ right column
    void report(Editor& ed) {
        const Target& t = kTargets[target_];
        auto it = reports_.find(t.id);
        if (it == reports_.end()) {
            ImGui::TextWrapped("Размер ещё не оценён для %s. Нажмите «Оценить размер»: соберётся только пак контента с текущими настройками (атласы, звук, правила файлов), без сборки приложения.", t.name);
            return;
        }
        const Json& r = it->second;
        const Json& pk = r["pack"];
        const double pack = pk["packBytes"].toDouble(), raw = pk["rawBytes"].toDouble();
        const Json& sp = r["split"];
        const double shipped = sp.isObject() ? sp["baseBytes"].toDouble() : pack;
        const double engine = t.engineMB * 1048576;
        ui::section(ICON_HARD_DRIVE, (std::string("Оценка: ") + t.name).c_str());
        ImGui::Text("Сборка ~ %s", mb(shipped + engine).c_str());
        ImGui::SameLine(0, 16);
        ImGui::TextDisabled("контент %s + движок ~ %s", mb(shipped).c_str(), mb(engine).c_str());
        ImGui::Text("Исходные файлы %s -> в паке %s (%.0f%%), файлов %lld, повторов %lld", mb(raw).c_str(), mb(pack).c_str(),
                    raw > 0 ? pack / raw * 100 : 0, (long long)pk["entries"].integer(), (long long)pk["duplicates"].integer());
        if (sp.isObject())
            ImGui::Text("Докачка: %lld част., всего с докачкой %s", (long long)sp["chunks"].size(), mb(sp["totalBytes"].toDouble()).c_str());
        if (t.storeLimitMB > 0 && (shipped + engine) / 1048576 > t.storeLimitMB)
            ui::toneText(ui::Tone::Warn, "%s", t.storeNote);
        const Json& at = r["atlas"];
        if (at.getBool("enabled", false) && (at["pages"].integer() > 0 || at["excluded"].size())) {
            ui::section(nullptr, "Атласы");
            ImGui::Text("%lld текстур -> %lld стр., заполнение %.0f%%, экономия в паке %s", (long long)at["atlased"].integer(), (long long)at["pages"].integer(),
                        at["occupancyPercent"].toDouble(), mb(at["bytesSaved"].toDouble()).c_str());
            ImGui::Text("Видеопамять %s -> %s, переключений текстур %lld -> %lld", mb(at["gpuBytesBefore"].toDouble()).c_str(), mb(at["gpuBytesAfter"].toDouble()).c_str(),
                        (long long)at["textureBindsBefore"].integer(), (long long)at["textureBindsAfter"].integer());
            if (at["excluded"].size() && ImGui::TreeNodeEx("Не в атласе", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool fixable = false;
                for (auto& e : at["excluded"].items()) {
                    ImGui::BulletText("%s — %s", e.getStr("path").c_str(), e.getStr("reason").c_str());
                    if (e["fixMaxSize"].integer() > 0) {
                        fixable = true;
                        ImGui::Indent();
                        ImGui::TextUnformatted(fixLoc_.tr("editor.build.resize_plan", i18n::Args().num("size", e["fixMaxSize"].integer())).c_str());
                        ImGui::Unindent();
                    }
                }
                if (fixable) {
                    ImGui::TextWrapped("%s", fixLoc_.tr("editor.build.resize_help").c_str());
                    ImGui::BeginDisabled(ed.ws().building() || reportManifests_[t.id] != manifest(ed).dump());
                    if (ImGui::Button(fixLoc_.tr("editor.build.fix_atlas_sizes").c_str())) {
                        const std::string path = std::string("packaging.targets.") + t.id + ".files";
                        const Json* existing = jsonPath(manifest(ed), path);
                        Json params = Json::object();
                        params.set("path", Json::string(path));
                        params.set("value", atlasSizeFixRules(existing ? *existing : Json::array(), at["excluded"]));
                        Json result = ed.ws().call("set_manifest", params);
                        if (result.has("error")) error_ = result.getStr("error");
                        else { error_.clear(); estimate(ed); }
                    }
                    ImGui::EndDisabled();
                }
                ImGui::TreePop();
            }
        }
        if (r["fileRules"].size()) {
            ui::section(nullptr, "Что сделали правила файлов");
            for (auto& e : r["fileRules"].items())
                ImGui::BulletText("%s: %s%s", e.getStr("path").c_str(), e.getStr("what").c_str(),
                                  e["after"].integer() ? (" (" + mb(e["before"].toDouble()) + " -> " + mb(e["after"].toDouble()) + ")").c_str() : "");
        }
        files(ed, r);
    }

    void files(Editor& ed, const Json& r) {
        ui::section(nullptr, "Файлы (самые тяжёлые сверху)");
        ImGui::SetNextItemWidth(220);
        ui::inputString("##filefilter", filter_);
        if (filter_.empty()) { ImGui::SameLine(14); ImGui::TextDisabled("фильтр: sprites/, .ogg ..."); }
        struct Row { std::string path, codec; double raw, stored; };
        std::vector<Row> rows;
        for (auto& f : r["files"].items()) {
            std::string p = f.getStr("path");
            if (!filter_.empty() && p.find(filter_) == std::string::npos) continue;
            rows.push_back({p, f.getStr("codec"), f["rawBytes"].toDouble(), f["storedBytes"].toDouble()});
        }
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.stored > b.stored; });
        double total = 0;
        for (auto& rw : rows) total += rw.stored;
        Json list = rulesList(ed);
        const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("##files", 6, tf, ImVec2(0, 0))) return;
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Файл", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Исходный", ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("В паке", ImGuiTableColumnFlags_WidthFixed, 64);
        ImGui::TableSetupColumn("Сжатие", ImGuiTableColumnFlags_WidthFixed, 118);
        ImGui::TableSetupColumn("Размер картинки", ImGuiTableColumnFlags_WidthFixed, 108);
        ImGui::TableSetupColumn("Не включать", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(int(rows.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const Row& rw = rows[size_t(i)];
                const Json* rule = ruleFor(list, rw.path);
                ImGui::PushID(rw.path.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const float share = total > 0 ? float(rw.stored / total) : 0;
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + ImGui::GetContentRegionAvail().x * share, p0.y + ImGui::GetTextLineHeight()), IM_COL32(90, 140, 220, 60));
                ImGui::TextUnformatted(rw.path.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s -> %s (%s)", rw.path.c_str(), mb(rw.raw).c_str(), mb(rw.stored).c_str(), rw.codec.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(mb(rw.raw).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%s", mb(rw.stored).c_str());
                ImGui::TableNextColumn();
                {   // compression: default / store / level
                    int lv = rule && rule->has("level") ? int((*rule)["level"].integer()) : -1;
                    std::string cur = lv < 0 ? (rw.codec == "store" ? "как есть" : "по умолчанию") : lv == 0 ? "без сжатия" : "zstd " + std::to_string(lv);
                    ImGui::SetNextItemWidth(110);
                    if (ImGui::BeginCombo("##lv", cur.c_str())) {
                        if (ImGui::Selectable("по умолчанию", lv < 0)) setRule(ed, rw.path, "level", Json());
                        if (ImGui::Selectable("без сжатия", lv == 0)) setRule(ed, rw.path, "level", Json::number(int64_t(0)));
                        for (int l : {3, 9, 15, 19, 22})
                            if (ImGui::Selectable(("zstd " + std::to_string(l)).c_str(), lv == l)) setRule(ed, rw.path, "level", Json::number(int64_t(l)));
                        ImGui::EndCombo();
                    }
                }
                ImGui::TableNextColumn();
                if (isImage(rw.path) && rw.path.rfind("_atlas/", 0) != 0) {
                    std::string cur = "как есть";
                    if (rule && rule->has("maxSize")) cur = "до " + std::to_string((*rule)["maxSize"].integer()) + " px";
                    else if (rule && rule->has("scale")) cur = (*rule)["scale"].toDouble() >= 0.5 ? "50%" : "25%";
                    ImGui::SetNextItemWidth(100);
                    if (ImGui::BeginCombo("##sz", cur.c_str())) {
                        if (ImGui::Selectable("как есть")) { setRule(ed, rw.path, "maxSize", Json()); setRule(ed, rw.path, "scale", Json()); }
                        if (ImGui::Selectable("50%")) { setRule(ed, rw.path, "maxSize", Json()); setRule(ed, rw.path, "scale", Json::number(0.5, 2)); }
                        if (ImGui::Selectable("25%")) { setRule(ed, rw.path, "maxSize", Json()); setRule(ed, rw.path, "scale", Json::number(0.25, 2)); }
                        for (int s : {2048, 1024, 512, 256})
                            if (ImGui::Selectable(("до " + std::to_string(s) + " px").c_str())) { setRule(ed, rw.path, "scale", Json()); setRule(ed, rw.path, "maxSize", Json::number(int64_t(s))); }
                        ImGui::EndCombo();
                    }
                } else ImGui::TextDisabled("—");
                ImGui::TableNextColumn();
                bool ex = rule && rule->getBool("exclude", false);
                if (rw.path.rfind("engine/", 0) == 0 || rw.path == "game.json" || rw.path.rfind("_atlas/", 0) == 0) ImGui::TextDisabled("—");
                else if (ui::checkbox("##ex", &ex)) setRule(ed, rw.path, "exclude", ex ? Json::fromBool(true) : Json());
                ImGui::PopID();
            }
        ImGui::EndTable();
    }

    i18n::Localization fixLoc_;
    bool fixLocLoaded_ = false;
    int target_ = 3, release_ = 1, audio_ = 0, windowScope_ = 0;
    bool estimating_ = false, autoEstimate_ = true, perTarget_ = false;
    double dirtyIn_ = 0;
    int askedFor_ = -1;
    std::string outDir_, filter_, error_, lastProject_;
    std::map<std::string, Json> reports_;
    std::map<std::string, std::string> reportManifests_;
    std::string estimateManifest_, estimateTarget_;
    // toolchains
    bool autoInstall_ = false, recheck_ = false, showMissing_ = false, missingOpen_ = false;
    std::future<Check> checking_;
    std::string checkingTarget_, lastBuildTarget_, rebuildAfter_, installError_;
    std::map<std::string, Check> checks_;
    std::deque<std::string> installQueue_;
    std::vector<std::pair<std::string, std::string>> missing_;   // target -> missing ids
    int seenRuns_ = 0;
};

RTS_EDITOR_WINDOW(BuildWindow);

} // namespace rts::editor
