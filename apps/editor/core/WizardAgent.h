// The game wizard's agent step: after GameMaker made the starting project (a template = a starting point), a connected
// AI agent rebuilds it into the game the description / GDD asks for, 1 to 1, through the editor's own MCP tools (the
// same rtsmcp link as the Agent window: connect once, both use it). Agents that can run headless are launched here:
//   Claude Code  claude -p (stream-json), the rtsengine MCP tools allowed
//   Codex CLI    codex exec --full-auto
// Others (Cursor, Windsurf, Claude Desktop) get the task to paste. The task text is WIZARD_TASK.md in the project.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "GameWizard.h"

namespace rts::editor {

// The task for the agent (Russian + the description verbatim). `idea` = the user's text + texts of the content folder.
std::string wizardAgentTask(const WizardSpec& spec, const std::string& projectDir, const std::string& templ, const std::string& idea);

bool agentClientCanRun(const std::string& clientId);   // claude-code / codex with the CLI found

class AgentRun {
public:
    ~AgentRun();
    // Starts the client's CLI in the project folder with the task file on stdin. False + err when it can't start.
    bool start(const std::string& clientId, const std::string& projectDir, const std::string& taskFile, const std::string& logFile, std::string* err);
    void stop();                                  // kills the agent
    bool running() const { return running_; }
    bool started() const { return started_; }
    bool finished() const { return started_ && !running_; }
    int exitCode() const { return rc_; }
    std::vector<std::string> takeLines();         // new readable lines since the last call (tool calls, texts)
    std::vector<std::string> lastLines(size_t n); // the tail, for the window
    std::string result();                         // the agent's final message (Claude Code), when finished
    int toolCalls() const { return tools_; }
    std::string client() const { return client_; }

private:
    void reader(std::string cmd, std::string logFile);
    void add(const std::string& line);
    std::thread thread_;
    std::atomic<bool> running_{false}, started_{false};
    std::atomic<int> rc_{0}, tools_{0};
    std::mutex m_;
    std::vector<std::string> lines_;
    size_t taken_ = 0;
    std::string result_, client_, pidFile_;
};

} // namespace rts::editor
