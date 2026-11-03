// Agent: the MCP link (how to connect an AI agent) and the feed of everything agents did.
#include <filesystem>
#include "../Editor.h"
#include "../EditorMcpConnect.h"

namespace fs = std::filesystem;

namespace rts::editor {

class AgentWindow : public EditorWindow {
public:
    const char* id() const override { return "agent"; }
    const char* title() const override { return "Agent"; }
    int order() const override { return 1; }
    const char* dockArea() const override { return "bottom"; }

    void draw(Editor& ed) override {
        auto& ws = ed.ws();
        ImGui::BeginDisabled(ed.mcpPort() == 0);
        connectSection(ed);
        ImGui::EndDisabled();
        ImGui::Checkbox("Камера Scene следует за правками агента", &ed.followAgent);
        ImGui::SeparatorText("Действия");
        ImGui::BeginChild("##feed", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (auto it = ws.activity.rbegin(); it != ws.activity.rend(); ++it) {
            int sec = int(it->time);
            ImGui::TextDisabled("%02d:%02d %-6s", sec / 60, sec % 60, it->source.c_str());
            ImGui::SameLine();
            if (it->error) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", it->text.c_str());
            else ImGui::TextWrapped("%s", it->text.c_str());
        }
        ImGui::EndChild();
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
};

RTS_EDITOR_WINDOW(AgentWindow);

} // namespace rts::editor
