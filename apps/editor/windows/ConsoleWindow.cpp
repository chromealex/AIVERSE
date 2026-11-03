// Console: build log (rtsbuild: builds, size estimates, toolchain installs). The log is a
// read-only text field, so a fragment can be selected and copied (Cmd/Ctrl+C); lines with errors
// and warnings are tinted. Every run is also written to <engine>/logs (Workspace::logsDir()).
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include "../Editor.h"
#include "../EditorUtil.h"
#include "imgui_internal.h" // the multiline field's child window: scroll + line tints

namespace rts::editor {

namespace {
enum class Kind { Plain, Error, Warning, Ok };
Kind lineKind(const char* b, const char* e) {
    std::string_view l(b, size_t(e - b));
    for (const char* k : {"ОШИБКА", "error:", "Error:", "FAILED", "RTS_MISSING_TOOLCHAIN", "не удал"}) if (l.find(k) != std::string_view::npos) return Kind::Error;
    for (const char* k : {"предупреждение", "warning:", "Warning:", "ВНИМАНИЕ"}) if (l.find(k) != std::string_view::npos) return Kind::Warning;
    for (const char* k : {"=== OK ===", "Готово", "Сборка завершена", "Установка завершена"}) if (l.find(k) != std::string_view::npos) return Kind::Ok;
    return Kind::Plain;
}

void openFolder(const std::string& dir) {
#if defined(_WIN32)
    std::string cmd = "explorer \"" + dir + "\"";
#elif defined(__APPLE__)
    std::string cmd = "open '" + dir + "' &";
#else
    std::string cmd = "xdg-open '" + dir + "' >/dev/null 2>&1 &";
#endif
    if (std::system(cmd.c_str()) != 0) {}   // nothing to do: the folder just does not open
}
} // namespace

class ConsoleWindow : public EditorWindow {
public:
    const char* id() const override { return "console"; }
    const char* title() const override { return "Консоль"; }
    const char* group() const override { return "service"; }
    int order() const override { return 3; }
    const char* dockArea() const override { return "bottom"; }

    void draw(Editor& ed) override {
        // Lua script errors of the running play-test (file:line); the full script log: window "Скрипты"
        if (auto* m = ed.simModule(); m && m->scripts() && m->scripts()->errorCount() > 0) {
            std::string errs;
            for (auto& e : m->scripts()->log())
                if (e.error) errs += (errs.empty() ? "" : "\n") + std::string("t=") + std::to_string(e.tick) + "  " + e.text;
            ui::callout(ui::Tone::Error, "Ошибки скриптов теста:\n" + errs);
        }
        auto& ws = ed.ws();
        const std::string& full = ws.buildLog();
        const bool running = ws.building();
        // toolbar: state, filter, counts, actions
        int errors = 0, warnings = 0;
        for (const char *b = full.c_str(), *end = b + full.size(); b < end;) {
            const char* e = static_cast<const char*>(std::memchr(b, '\n', size_t(end - b)));
            if (!e) e = end;
            Kind k = lineKind(b, e);
            errors += k == Kind::Error; warnings += k == Kind::Warning;
            b = e + 1;
        }
        if (running) ui::toneText(ui::Tone::Warn, "%s %s", ICON_REFRESH, ws.runLabel().rfind("toolchain-", 0) == 0 ? "Идёт установка…" : "Идёт сборка…");
        else if (full.empty()) ImGui::TextDisabled(ICON_CONSOLE "  Сборка ещё не запускалась (меню «Сборка»)");
        else if (ws.buildResult() != 0) ui::toneText(ui::Tone::Error, "%s Последний запуск завершился с ошибкой", ICON_ALERT);
        else ui::toneText(ui::Tone::Ok, "%s Последний запуск успешен", ICON_CIRCLE_CHECK);
        if (!full.empty()) {
            ImGui::SameLine(0, 16);
            static const char* filters[] = {"Всё", "Ошибки", "Предупреждения"};
            ui::segmented("##flt", filter_, filters, 3, ImGui::GetFontSize() * 18);
            ImGui::SameLine();
            if (errors) { ImGui::SameLine(); char t[32]; std::snprintf(t, sizeof t, "%s %d", ICON_ALERT, errors); ui::badge(t, ui::toneColor(ui::Tone::Error)); }
            if (warnings) { ImGui::SameLine(); char t[32]; std::snprintf(t, sizeof t, "%s %d", ICON_WARNING, warnings); ui::badge(t, ui::toneColor(ui::Tone::Warn)); }
        }
        // actions on the right
        const float bw = ImGui::GetFrameHeight();
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8, ImGui::GetWindowContentRegionMax().x - bw * 2 - 6));
        ImGui::BeginDisabled(full.empty());
        if (ui::toolButton(ICON_COPY, "Копировать журнал")) ImGui::SetClipboardText(full.c_str());
        ImGui::EndDisabled();
        ImGui::SameLine(0, 4);
        if (ui::toolButton(ICON_FOLDER_OPEN, ("Открыть папку логов\n" + ws.lastLogFile()).c_str())) openFolder(ws.logsDir());
        // the shown text: the whole log or only the matching lines
        if (filter_ == 0) log_ = full;
        else {
            log_.clear();
            for (const char *b = full.c_str(), *end = b + full.size(); b < end;) {
                const char* e = static_cast<const char*>(std::memchr(b, '\n', size_t(end - b)));
                if (!e) e = end;
                Kind k = lineKind(b, e);
                if ((filter_ == 1 && k == Kind::Error) || (filter_ == 2 && k == Kind::Warning)) log_.append(b, e).push_back('\n');
                b = e + 1;
            }
        }
        // Read-only multiline field over the log: selectable, Cmd/Ctrl+C, Cmd/Ctrl+A.
        const char* label = "##buildlog";
        ImGuiWindow* parent = ImGui::GetCurrentWindow();
        const ImGuiID id = ImGui::GetID(label);
        ImGui::InputTextMultiline(label, log_.data(), log_.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        // the field's scrolling child (name as BeginChildEx makes it) for tints and auto-scroll
        char name[512];
        ImFormatString(name, sizeof name, "%s/%s_%08X", parent->Name, label, id);
        ImGuiWindow* child = ImGui::FindWindowByName(name);
        if (!child) return;
        const float lineH = ImGui::GetFontSize();
        const float top = child->InnerRect.Min.y + ImGui::GetStyle().FramePadding.y - child->Scroll.y;
        const int first = std::max(0, int((child->Scroll.y - ImGui::GetStyle().FramePadding.y) / lineH) - 1);
        const int last = first + int(child->InnerRect.GetHeight() / lineH) + 2;
        int line = 0;
        for (const char *b = log_.c_str(), *end = b + log_.size(); b < end && line <= last; ++line) {
            const char* e = static_cast<const char*>(std::memchr(b, '\n', size_t(end - b)));
            if (!e) e = end;
            if (line >= first) {
                Kind k = lineKind(b, e);
                if (k != Kind::Plain) {
                    ImU32 c = k == Kind::Error ? IM_COL32(255, 80, 70, 60) : k == Kind::Warning ? IM_COL32(255, 200, 60, 50) : IM_COL32(80, 220, 110, 45);
                    float y = top + float(line) * lineH;
                    child->DrawList->AddRectFilled(ImVec2(child->InnerRect.Min.x, y), ImVec2(child->InnerRect.Max.x, y + lineH), c);
                }
            }
            b = e + 1;
        }
        // follow the end while the log grows, unless the user scrolled up or is selecting
        const bool grew = log_.size() != lastSize_;
        lastSize_ = log_.size();
        if (!grew) follow_ = child->Scroll.y >= child->ScrollMax.y - lineH;
        else if (follow_ && ImGui::GetActiveID() != id) ImGui::SetScrollY(child, 1e9f);
    }

private:
    std::string log_;
    size_t lastSize_ = 0;
    bool follow_ = true;
    int filter_ = 0;
};

RTS_EDITOR_WINDOW(ConsoleWindow);

} // namespace rts::editor
