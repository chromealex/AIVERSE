// The game wizard's agent step (WizardAgent.h).
#include "WizardAgent.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "GameWizardImpl.h"

#if !defined(_WIN32)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rts::editor {

namespace {

std::string q(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

// The user's login shell: GUI apps on macOS don't get the PATH where npm / brew put claude and codex.
std::string loginShell() {
    const char* s = std::getenv("SHELL");
    return s && *s ? s : "/bin/zsh";
}

// The CLI's absolute path. A GUI app on macOS starts with a short PATH (no npm / brew / ~/.local), so look in PATH,
// then where installers put claude / codex, then ask the user's shell (login + interactive: .zprofile and .zshrc).
std::string findCli(const std::string& name) {
#if defined(_WIN32)
    return std::system(("where " + name + " >NUL 2>NUL").c_str()) == 0 ? name : std::string();
#else
    std::error_code ec;
    auto exe = [&](const fs::path& p) { return fs::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0; };
    if (const char* path = std::getenv("PATH")) {
        std::string ps = path;
        size_t a = 0;
        while (a <= ps.size()) {
            const size_t b = ps.find(':', a);
            const std::string d = ps.substr(a, b == std::string::npos ? std::string::npos : b - a);
            if (!d.empty() && exe(fs::path(d) / name)) return (fs::path(d) / name).string();
            if (b == std::string::npos) break;
            a = b + 1;
        }
    }
    const char* h = std::getenv("HOME");
    const fs::path home = h ? h : "/";
    std::vector<fs::path> dirs = {home / ".local/bin", home / ".claude/local", home / ".npm-global/bin", home / ".bun/bin",
                                  home / ".volta/bin", home / ".yarn/bin", "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"};
    for (auto it = fs::directory_iterator(home / ".nvm/versions/node", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        dirs.push_back(it->path() / "bin");
    for (auto& d : dirs) if (exe(d / name)) return (d / name).string();
    // CLIs that come inside the desktop apps: Claude Code in the Claude app's data folder (newest version), Codex in ChatGPT
    std::vector<fs::path> bundled;
    if (name == "claude") {
        std::vector<fs::path> found;
        for (auto v = fs::directory_iterator(home / "Library/Application Support/Claude/claude-code", ec); !ec && v != fs::directory_iterator(); v.increment(ec)) {
            std::error_code e2;
            for (auto h2 = fs::directory_iterator(v->path(), e2); !e2 && h2 != fs::directory_iterator(); h2.increment(e2))
                found.push_back(h2->path() / "claude.app/Contents/MacOS/claude");
        }
        std::sort(found.begin(), found.end());   // version folders: the newest last
        bundled.insert(bundled.end(), found.rbegin(), found.rend());
    } else if (name == "codex") {
        for (const fs::path& app : {fs::path("/Applications/ChatGPT.app"), home / "Applications/ChatGPT.app", fs::path("/Applications/Codex.app")}) {
            bundled.push_back(app / "Contents/Resources/codex-cli/bin/codex");
            bundled.push_back(app / "Contents/Resources/codex");
        }
    }
    for (auto& b : bundled) if (exe(b)) return b.string();
    FILE* p = popen((loginShell() + " -ilc " + q("command -v " + name) + " 2>/dev/null </dev/null").c_str(), "r");
    std::string out;
    if (p) { char buf[1024]; while (std::fgets(buf, sizeof buf, p)) out += buf; pclose(p); }
    size_t nl = out.rfind('/');                                   // the last line that is a path
    if (nl != std::string::npos) {
        const size_t a = out.rfind('\n', nl) == std::string::npos ? 0 : out.rfind('\n', nl) + 1;
        std::string path = out.substr(a);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' ')) path.pop_back();
        if (exe(path)) return path;
    }
    return {};
#endif
}

std::string clip(const std::string& s, size_t n) {
    std::string o = s.substr(0, n);
    for (char& c : o) if (c == '\n' || c == '\r') c = ' ';
    return s.size() > n ? o + "…" : o;
}

} // namespace

std::string agentCliPath(const std::string& id) {
    if (id == "claude-code") return findCli("claude");
    if (id == "codex") return findCli("codex");
    return {};
}

bool agentClientCanRun(const std::string& id) { return !agentCliPath(id).empty(); }

std::string wizardAgentTask(const WizardSpec& s, const std::string& dir, const std::string& templ, const std::string& idea) {
    std::string t;
    t += "# Задание: игра «" + s.title + "» точно по описанию\n\n";
    t += "Ты делаешь игру в RTSEngine через MCP-инструменты rtsengine (сервер rtsmcp). В редакторе уже открыт проект " + dir +
         " — все правки идут через инструменты и сразу видны в редакторе.\n\n";
    t += "Мастер «Сделать игру» собрал стартовый проект из шаблона `" + templ + "` (что сделано — WIZARD.md, прочитай через "
         "game_file_read). Шаблон — только точка старта. Цель: игра должна быть ровно такой, как в описании ниже — 1 в 1: каждый "
         "юнит, здание, ресурс, фракция, механика, правило, числа, карты, условия победы, сеттинг и названия. Всё лишнее из шаблона "
         "удали или переделай, всего недостающего добейся.\n\n";
    t += "## Как работать\n\n"
         "1. Прочитай справочники: rules_schema (формат правил), script_api (Lua), ui_api (экраны), gen_presets. Посмотри rules_list, "
         "game_file_read rules.json, WIZARD.md.\n"
         "2. Составь список всего, что есть в описании, и сверь с проектом.\n"
         "3. Типы, ресурсы, фракции, техи, победа, экономика — rules_merge / rules_set / rules_delete. Механики, которых нет в "
         "блоках данных — Lua: script_write {register: true} + script_check (в симуляции только целые и fx(), без float).\n"
         "4. Карты — map_new / map_generate / entity_place / player_set / map_save {addToGame: true}; уровни мастера — "
         "maps/wiz_level_*.json (генерируются по `generate`; поменяй пресет, если нужно), обучение — maps/wiz_tutorial.json.\n"
         "5. Проверяй всё: rules_lint, map_validate, match_run на каждой карте (матч должен заканчиваться), balance_* для баланса.\n"
         "6. Не ломай то, что сделал мастер, а приведи под новую игру:\n"
         "   - обучение: scripts/wizard/tutorial_steps.lua — шаги ссылаются на id типов и команды; формат описан в начале "
         "scripts/wizard/tutorial.lua; тексты шагов — lang/*.json `wiz.tut.<id>`;\n"
         "   - подсказки: scripts/wizard/hints_config.lua (`wiz.hint.*`);\n"
         "   - мета: ui/wizard.json (валюты, уровни, награды, улучшения: stat maxHp / damage / speed, resource), "
         "scripts/wizard/meta_config.lua, ui/shop.json, ui/profile.json;\n"
         "   - тексты: lang/<язык>.json — имена всех типов (`type.<id>`), ресурсов (`resource.<id>`), карт, `game.name` на всех языках игры (";
    for (size_t i = 0; i < s.languages.size(); ++i) t += (i ? ", " : "") + s.languages[i];
    t += ").\n"
         "7. Меняй только данные игры, никогда код движка.\n"
         "8. В конце допиши в WIZARD.md раздел «## Агент: что сделано» (game_file_write): что из описания реализовано, что "
         "реализовано приближённо и почему, чего нет. Ответь тем же кратким итогом по-русски.\n\n";
    t += "## Описание игры (как дал пользователь)\n\n";
    t += idea.empty() ? std::string("(описания нет — сделай цельную игру на основе шаблона и названия «" + s.title + "»)\n") : idea + "\n";
    return t;
}

AgentRun::~AgentRun() {
    if (running_) stop();
    if (thread_.joinable()) thread_.join();
}

bool AgentRun::start(const std::string& clientId, const std::string& dir, const std::string& taskFile, const std::string& logFile, std::string* err) {
#if defined(_WIN32)
    if (err) *err = "запуск агента из редактора на Windows пока не поддержан: скопируйте задание в клиент";
    (void)clientId; (void)dir; (void)taskFile; (void)logFile;
    return false;
#else
    if (running_) { if (err) *err = "агент уже работает"; return false; }
    if (thread_.joinable()) thread_.join();
    const std::string cli = agentCliPath(clientId);
    if (cli.empty()) { if (err) *err = std::string(clientId == "codex" ? "codex" : "claude") + " не найден: установите CLI или скопируйте задание"; return false; }
    // the CLI's own folder first in PATH: npm-installed CLIs start through `env node`, which lives next to them
    const std::string pathPrefix = "PATH=" + q(fs::path(cli).parent_path().string()) + ":\"$PATH\" ";
    std::string inner;
    if (clientId == "claude-code")
        inner = pathPrefix + q(cli) + " -p --output-format stream-json --verbose --allowedTools mcp__rtsengine --max-turns 400 < " + q(taskFile);
    else if (clientId == "codex")
        inner = pathPrefix + q(cli) + " exec --full-auto - < " + q(taskFile);
    else { if (err) *err = "этот клиент нельзя запустить из редактора: скопируйте задание"; return false; }
    client_ = clientId;
    pidFile_ = taskFile + ".pid";
    // sh records its pid, then becomes the login shell running the agent: stop() kills that process group
    const std::string cmd = "cd " + q(dir) + " && sh -c " + q("echo $$ > " + q(pidFile_) + "; exec " + loginShell() + " -lc " + q(inner)) + " 2>&1";
    {
        std::lock_guard<std::mutex> lk(m_);
        lines_.clear(); taken_ = 0; result_.clear();
    }
    tools_ = 0; rc_ = 0;
    started_ = true;
    running_ = true;
    thread_ = std::thread(&AgentRun::reader, this, cmd, logFile);
    return true;
#endif
}

void AgentRun::add(const std::string& line) {
    std::lock_guard<std::mutex> lk(m_);
    lines_.push_back(line);
    if (lines_.size() > 4000) { lines_.erase(lines_.begin(), lines_.begin() + 1000); taken_ = taken_ > 1000 ? taken_ - 1000 : 0; }
}

void AgentRun::reader(std::string cmd, std::string logFile) {
#if !defined(_WIN32)
    std::ofstream lf(logFile);
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { add("не удалось запустить агента"); rc_ = -1; running_ = false; return; }
    std::string line;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) {
        line += buf;
        if (line.back() != '\n' && line.size() < (1u << 20)) continue;
        lf << line; lf.flush();
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        // Claude Code stream-json: assistant messages (text, tool_use), the final result
        if (client_ == "claude-code" && !line.empty() && line[0] == '{') {
            try {
                Json j = Json::parse(line);
                const std::string type = j.getStr("type");
                if (type == "assistant") {
                    for (auto& c : j["message"]["content"].items()) {
                        if (c.getStr("type") == "text" && !c.getStr("text").empty()) add(clip(c.getStr("text"), 300));
                        else if (c.getStr("type") == "tool_use") {
                            ++tools_;
                            std::string name = c.getStr("name");
                            if (name.rfind("mcp__rtsengine__", 0) == 0) name = name.substr(16);
                            add("→ " + name + " " + clip(c["input"].dump(0), 120));
                        }
                    }
                } else if (type == "result") {
                    std::lock_guard<std::mutex> lk(m_);
                    result_ = j.getStr("result");
                    if (j.getBool("is_error")) lines_.push_back("ошибка агента: " + clip(result_, 400));
                }
            } catch (...) { add(clip(line, 300)); }
        } else if (!line.empty()) {
            add(clip(line, 300));
        }
        line.clear();
    }
    int rc = pclose(p);
    rc_ = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : rc;
    std::error_code ec;
    fs::remove(pidFile_, ec);
    add(rc_ == 0 ? "агент закончил" : "агент остановлен (код " + std::to_string(int(rc_)) + ")");
    running_ = false;
#else
    (void)cmd; (void)logFile;
#endif
}

void AgentRun::stop() {
#if !defined(_WIN32)
    const std::string pid = wiz::readText(pidFile_);
    const int n = std::atoi(pid.c_str());
    if (n > 1) { ::kill(-n, SIGTERM); ::kill(n, SIGTERM); }
#endif
}

std::vector<std::string> AgentRun::takeLines() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<std::string> out(lines_.begin() + long(std::min(taken_, lines_.size())), lines_.end());
    taken_ = lines_.size();
    return out;
}

std::vector<std::string> AgentRun::lastLines(size_t n) {
    std::lock_guard<std::mutex> lk(m_);
    const size_t a = lines_.size() > n ? lines_.size() - n : 0;
    return std::vector<std::string>(lines_.begin() + long(a), lines_.end());
}

std::string AgentRun::result() { std::lock_guard<std::mutex> lk(m_); return result_; }

} // namespace rts::editor
