// Agent: the MCP link (how to connect an AI agent) and the feed of everything agents did.
#include <filesystem>
#include "../Editor.h"
#include "../core/McpInstall.h"
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
    McpServerSpec spec(Editor& ed) const {
        McpServerSpec s;
        s.command = ed.options().mcpTool;
        if (s.command.empty()) s.command = (fs::path(ed.options().buildTool).parent_path() / "rtsmcp").string();
        s.args = {"--engine", ed.options().engineDir, "--port", std::to_string(ed.mcpPort())};
        if (!ed.options().editorExe.empty()) { s.args.push_back("--editor"); s.args.push_back(ed.options().editorExe); }
        return s;
    }

    // One click per AI client: writes the rtsmcp entry into its MCP config (see core/McpInstall.h).
    void connectSection(Editor& ed) {
        const McpServerSpec sp = spec(ed);
        if (clients_.empty() || ImGui::GetTime() > nextScan_) { clients_ = mcpClients(sp, ed.options().engineDir); nextScan_ = ImGui::GetTime() + 5; }
        if (ImGui::CollapsingHeader(ICON_PLUG "  Подключить ИИ-агента", connectedAny() ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Кнопка прописывает сервер rtsmcp в настройки клиента. После этого перезапустите клиент.");
            if (ImGui::BeginTable("##mcp", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                for (auto& c : clients_) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(c.name.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.configPath.c_str());
                    ImGui::TableNextColumn();
                    if (c.upToDate) ui::toneText(ui::Tone::Ok, ICON_CHECK " подключено");
                    else if (c.installed) ui::toneText(ui::Tone::Warn, "старый путь — обновите");
                    else if (c.present) ImGui::TextDisabled("найден");
                    else ImGui::TextDisabled("не установлен");
                    ImGui::TableNextColumn();
                    ImGui::PushID(c.id.c_str());
                    if (ImGui::SmallButton(c.upToDate ? "Переподключить" : c.installed ? "Обновить" : "Подключить")) run(ed, c, sp, false);
                    if (c.installed) { ImGui::SameLine(); if (ImGui::SmallButton(ICON_TRASH)) run(ed, c, sp, true); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Отключить"); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (!message_.empty()) ui::callout(messageError_ ? ui::Tone::Error : ui::Tone::Ok, message_);
            std::string cmd = "claude mcp add rtsengine -- \"" + sp.command + "\"";
            for (auto& a : sp.args) cmd += " \"" + a + "\"";
            ImGui::TextDisabled("Вручную:");
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_COPY " Копировать команду")) ImGui::SetClipboardText(cmd.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cmd.c_str());
        }
    }

    void run(Editor& ed, const McpClientInfo& c, const McpServerSpec& sp, bool remove) {
        std::string err;
        const bool ok = remove ? mcpUninstall(c.id, sp, ed.options().engineDir, &err) : mcpInstall(c.id, sp, ed.options().engineDir, &err);
        messageError_ = !ok;
        message_ = ok ? (remove ? c.name + ": отключено" : c.name + ": подключено. Перезапустите " + c.name + " — инструменты rtsengine появятся в списке.") : err;
        ed.ws().log("editor", ok ? message_ : "MCP " + c.name + ": " + err, !ok);
        clients_.clear();
    }

    bool connectedAny() const { for (auto& c : clients_) if (c.upToDate) return true; return false; }

    std::vector<McpClientInfo> clients_;
    double nextScan_ = 0;
    std::string filter_;
    bool errorsOnly_ = false;
    std::string message_;
    bool messageError_ = false;
};

RTS_EDITOR_WINDOW(AgentWindow);

} // namespace rts::editor
