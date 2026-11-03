// "Make a game" (Сделать игру): a whole game project from a few answers. Takes art / music / content from a folder or a
// link, the user's idea (or none), and the choices of the wizard window, and writes a ready project with its .rtsproj:
// a template's game (picked by the idea when not given), a hard tutorial map (step by step, only the needed commands
// pass, the enemy waits), soft hints in every match, the meta game (levels with stars and rewards, upgrades, shop,
// daily reward, profile, settings, ads and purchases when asked), branding from the art, music and sounds, languages.
// Used by the editor window (EditorGameWizard.cpp) and MCP (game_make*, WizardOps.cpp). Format: docs/game-wizard.md.
// The generated runtime is data: templates/_wizard (Lua + RmlUi), configured by files the wizard writes.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "Workspace.h"
#include "engine/data/Json.h"

namespace rts::editor {

struct WizardSpec {
    // project
    std::string title = "Моя стратегия";
    std::string location;                    // parent folder ("" = ~/RTSProjects)
    std::string studio = "mystudio";         // bundle id: com.<studio>.<slug>
    std::string version = "0.1.0";
    // sources: a folder with art, music, models, fonts, texts (a GDD in .md / .txt is read as the idea too),
    // and / or a link to a file or an archive (downloaded into a staging folder first, fetchWizardArt)
    std::string artDir, artUrl;
    std::string idea;                        // what the game is about, any language; "" = the wizard decides
    // game
    std::string genre = "auto";              // template id or "auto" (chosen from the idea / features / art)
    std::vector<std::string> features;       // wanted: economy base army garrison lanes waves territory heroes spells
                                             // planets 2d casual (empty = whatever the template has)
    std::string orientation = "auto";        // auto | landscape | portrait
    std::vector<std::string> languages = {"ru", "en"};
    std::vector<std::string> platforms = {"android", "ios", "web", "desktop"};
    int seed = 0;                            // 0: from the title and the idea (same answers = same game)
    // tutorial
    std::string tutorial = "both";           // both | hard | soft | none
    bool tutorialForced = true;              // the first "Play" starts the tutorial
    bool tutorialSkippable = true;
    bool tutorialDim = true;                 // hard mode: dim the screen around the focus
    // levels
    int levels = 8;                          // 0 = only the template's own maps
    std::string mapPreset = "auto";          // map generator preset (gen_presets) or auto / none (copies of the base map)
    std::string mapSize = "auto";            // auto | small | medium | large
    int bossEvery = 4;                       // every N-th level is a hard "boss" level (0 = none)
    std::string difficultyCurve = "normal";  // easy | normal | hard
    bool keepTemplateMaps = true;            // the template's maps stay as extra skirmish maps
    bool verifyLevels = true;                // a quick bot match on every level (and the tutorial played by itself)
    // economy of the match
    int startResourcesPercent = 100;         // rules resources "start" scaled
    // meta
    std::vector<std::string> currencies = {"gold", "gems"};   // first = soft (rewards), second = hard (shop)
    bool upgrades = true, shop = true, daily = true, hints = true, profileScreen = true;
    int xpPerLevel = 100;
    // monetization
    bool ads = false;                        // rewarded (double reward, free currency) + interstitials
    int interstitialEvery = 3;               // matches between interstitials (0 = none)
    bool purchases = false;                  // currency packs + "no ads" for real money
    std::vector<std::string> stores;         // plugins: googleplay rustore appstore yandex-games steam (empty = by platform)
    // look
    bool paletteFromArt = true;              // UI accent colours from the key art
    bool brandingFromArt = true;             // icon + splash from the art
    // the agent step (WizardAgent.h): a connected AI agent rebuilds the starting project into the described game, 1 to 1
    bool agent = true;
    std::string agentClient;                 // claude-code | codex | "" (the first one connected and runnable)
    // the game's own texts per language (an agent may pass them to game_make), {tagline, intro, types, resources, currencies,
    // upgrades: {id: {lang: text}}, levels: [{lang: text}], reason, template}; empty = the wizard's own texts
    Json ai = Json::object();

    Json toJson() const;
    static WizardSpec fromJson(const Json& j);   // missing keys keep the defaults
};

// Everything the window and agents can choose from: templates with their mechanics, features, presets, defaults.
Json wizardCatalog(const std::string& engineDir);

// What the wizard would make (no files written): template + why, features, maps, tutorial archetype, meta.
Json planGame(const std::string& engineDir, const WizardSpec& spec);

// Downloads `url` (a file or a .zip / .tar.gz archive; http(s)) with the system curl into `dir` and unpacks archives.
// Blocking (the window runs it on a thread). Returns the folder with the content or "" + `err`.
std::string fetchWizardArt(const std::string& url, const std::string& dir, std::string* err);

// Makes the game step by step, so the window can show progress (one step per frame) and MCP can run it at once.
// The workspace opens the new project when the files are written (later steps use its ops: screens, branding, checks).
class GameMaker {
public:
    GameMaker(Workspace& ws, WizardSpec spec);
    bool step();                              // runs the next step; false when finished (or failed)
    Json runAll();                            // every step; the report
    bool done() const { return next_ >= steps_.size() || failed_; }
    bool failed() const { return failed_; }
    float progress() const { return steps_.empty() ? 1.0f : float(next_) / float(steps_.size()); }
    std::string current() const;              // what the next step does (Russian, for the window)
    const Json& report() const { return report_; }
    std::string projectDir() const { return dir_; }
    std::string projectFile() const { return file_; }

    struct Ctx;                               // working state shared by the steps (GameWizard*.cpp)
private:
    struct Step { std::string label; std::function<void()> fn; };
    Workspace& ws_;
    WizardSpec spec_;
    std::vector<Step> steps_;
    size_t next_ = 0;
    bool failed_ = false;
    Json report_ = Json::object();
    std::string dir_, file_;
    std::shared_ptr<Ctx> ctx_;
};

} // namespace rts::editor
