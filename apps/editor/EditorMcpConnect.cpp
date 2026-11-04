// "Connect an AI agent" panel (EditorMcpConnect.h).
#include "EditorMcpConnect.h"

#include <filesystem>
#include "Editor.h"
#include "EditorIcons.h"

namespace fs = std::filesystem;

namespace rts::editor {

McpServerSpec editorMcpSpec(Editor& ed) {
    McpServerSpec s;
    s.command = ed.options().mcpTool;
    if (s.command.empty()) s.command = (fs::path(ed.options().buildTool).parent_path() / "rtsmcp").string();
    s.args = {"--engine", ed.options().engineDir, "--port", std::to_string(ed.mcpPort())};
    if (!ed.options().editorExe.empty()) { s.args.push_back("--editor"); s.args.push_back(ed.options().editorExe); }
    return s;
}

void refreshMcpClients(Editor& ed, McpConnectState& st, bool force) {
    if (force || st.clients.empty() || ImGui::GetTime() > st.nextScan) {
        st.clients = mcpClients(editorMcpSpec(ed), ed.options().engineDir);
        st.nextScan = ImGui::GetTime() + 5;
    }
}

bool mcpConnectedAny(const McpConnectState& st) {
    for (auto& c : st.clients) if (c.upToDate) return true;
    return false;
}

static void run(Editor& ed, McpConnectState& st, const McpClientInfo& c, bool remove) {
    const McpServerSpec sp = editorMcpSpec(ed);
    std::string err;
    const bool ok = remove ? mcpUninstall(c.id, sp, ed.options().engineDir, &err) : mcpInstall(c.id, sp, ed.options().engineDir, &err);
    st.messageError = !ok;
    st.message = ok ? (remove ? c.name + ": отключено" : c.name + ": подключено. Если клиент открыт — перезапустите его, инструменты rtsengine появятся в списке.") : err;
    ed.ws().log("editor", ok ? st.message : "MCP " + c.name + ": " + err, !ok);
    st.clients.clear();
}

void drawMcpConnect(Editor& ed, McpConnectState& st) {
    refreshMcpClients(ed, st);
    const McpServerSpec sp = editorMcpSpec(ed);
    ImGui::BeginDisabled(ed.mcpPort() == 0);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float actionsW = ImGui::CalcTextSize("Переподключить").x + style.FramePadding.x * 2 + ImGui::GetFrameHeight() + style.ItemSpacing.x + 4;
    const float statusW = ImGui::CalcTextSize(ICON_CHECK " подключено").x + 8;
    if (ImGui::BeginTable("##mcp", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthFixed, statusW);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, actionsW);
        for (auto& c : st.clients) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(c.name.c_str());   // clipped by the column; the full name and path in the tooltip
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", c.name.c_str(), c.configPath.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (c.upToDate) ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.5f, 1), ICON_CHECK " подключено");
            else if (c.installed) { ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "обновите"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Записан старый путь к rtsmcp"); }
            else if (c.present) ImGui::TextDisabled("найден");
            else ImGui::TextDisabled("не установлен");
            ImGui::TableNextColumn();
            ImGui::PushID(c.id.c_str());
            const float trashW = c.installed ? ImGui::GetFrameHeight() + style.ItemSpacing.x : 0;
            if (ImGui::Button(c.upToDate ? "Переподключить" : c.installed ? "Обновить" : "Подключить", ImVec2(ImGui::GetContentRegionAvail().x - trashW, 0))) run(ed, st, c, false);
            if (c.installed) {
                ImGui::SameLine();
                if (ImGui::Button(ICON_TRASH, ImVec2(ImGui::GetFrameHeight(), 0))) run(ed, st, c, true);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Отключить");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    if (ed.mcpPort() == 0) ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), ICON_WARNING " Редактор запущен без связи с агентами (--listen 0)");
    if (!st.message.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(st.messageError ? ImVec4(1, 0.5f, 0.4f, 1) : ImVec4(0.4f, 0.85f, 0.5f, 1), "%s", st.message.c_str());
        ImGui::PopTextWrapPos();
    }
    std::string cmd = "claude mcp add rtsengine -- \"" + sp.command + "\"";
    for (auto& a : sp.args) cmd += " \"" + a + "\"";
    ImGui::TextDisabled("Вручную:");
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_COPY " Копировать команду")) ImGui::SetClipboardText(cmd.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", cmd.c_str());
}

} // namespace rts::editor
