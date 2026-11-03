// Agent: the MCP link (how to connect an AI agent) and the feed of everything agents did.
#include <filesystem>
#include "../Editor.h"
#include "../EditorMcpConnect.h"
#include "../EditorUtil.h"

namespace fs = std::filesystem;

namespace rts::editor {

class AgentWindow : public EditorWindow {
public:
    const char* id() const override { return "agent"; }
    const char* title() const override { return "ИИ-агент"; }
    const char* group() const override { return "service"; }
    int order() const override { return 1; }
    const char* dockArea() const override { return "bottom"; }

    void draw(Editor& ed) override {
        auto& ws = ed.ws();
        if (ed.mcpPort() == 0) ui::callout(ui::Tone::Warn, "Канал для агентов выключен (редактор запущен с --listen 0).");
        else {
            char t[96];
            std::snprintf(t, sizeof t, "Канал MCP слушает порт %d", ed.mcpPort());
            ui::toneText(ui::Tone::Ok, "%s %s", ICON_PLUG_ZAP, t);
        }
        ImGui::BeginDisabled(ed.mcpPort() == 0);
        connectSection(ed);
        ImGui::EndDisabled();
        ui::section(ICON_ACTIVITY, "Действия");
        ImGui::SetNextItemWidth(-1);
        ui::searchField("##ff", filter_, "Фильтр действий", ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Только ошибки").x - ImGui::GetFrameHeight() * 2 - 12);
        ImGui::SameLine();
        ImGui::Checkbox("Только ошибки", &errorsOnly_);
        ImGui::Checkbox("Камера «Сцены» следует за правками агента", &ed.followAgent);
        if (ImGui::BeginTable("##feed", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_SizingFixedFit, ImVec2(0, 0))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Время", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00:00 ").x);
            ImGui::TableSetupColumn("Кто", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("editor  ").x);
            ImGui::TableSetupColumn("Что", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (auto it = ws.activity.rbegin(); it != ws.activity.rend(); ++it) {
                if (errorsOnly_ && !it->error) continue;
                if (!ui::contains(it->text, filter_) && !ui::contains(it->source, filter_)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                int sec = int(it->time);
                ImGui::TextDisabled("%02d:%02d", sec / 60, sec % 60);
                ImGui::TableNextColumn();
                ui::toneText(it->source == "editor" ? ui::Tone::Dim : ui::Tone::Accent, "%s", it->source.c_str());
                ImGui::TableNextColumn();
                if (it->error) { ImGui::PushStyleColor(ImGuiCol_Text, ui::toneColor(ui::Tone::Error)); ImGui::TextWrapped("%s %s", ICON_ALERT, it->text.c_str()); ImGui::PopStyleColor(); }
                else ImGui::TextWrapped("%s", it->text.c_str());
            }
            ImGui::EndTable();
        }
        if (ws.activity.empty()) ui::hint("Здесь появятся правки, которые агент делает через MCP: правила, карты, матчи, скриншоты.");
    }

private:
    // One click per AI client (EditorMcpConnect.h, shared with the game wizard: one connection for both)
    void connectSection(Editor& ed) {
        refreshMcpClients(ed, mcp_);
        if (ImGui::CollapsingHeader(ICON_PLUG "  Подключить ИИ-агента", mcpConnectedAny(mcp_) ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Кнопка прописывает сервер rtsmcp в настройки клиента. После этого перезапустите клиент.");
            drawMcpConnect(ed, mcp_);
        }
    }

    McpConnectState mcp_;
    std::string filter_;
    bool errorsOnly_ = false;
};

RTS_EDITOR_WINDOW(AgentWindow);

} // namespace rts::editor
