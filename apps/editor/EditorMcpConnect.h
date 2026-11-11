// "Connect an AI agent": one button per AI client found on this computer writes the editor's MCP server (aiversemcp)
// into its config (core/McpInstall.h). Shared by the Agent window and the game wizard: one connection serves both.
#pragma once
#include <string>
#include <vector>
#include "core/McpInstall.h"

namespace aiverse::editor {
class Editor;

struct McpConnectState {
    std::vector<McpClientInfo> clients;
    double nextScan = 0;
    std::string message;
    bool messageError = false;
};

McpServerSpec editorMcpSpec(Editor& ed);
void refreshMcpClients(Editor& ed, McpConnectState& st, bool force = false);
// The table of clients with Connect / Update / Disconnect, the result line and "copy the command".
void drawMcpConnect(Editor& ed, McpConnectState& st);
bool mcpConnectedAny(const McpConnectState& st);
} // namespace aiverse::editor
