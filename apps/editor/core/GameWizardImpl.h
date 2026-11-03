// Internals of the game wizard (GameWizard.h): the working state of one GameMaker run and the steps, split by topic:
//   GameWizard.cpp         spec, catalog, template choice (planGame), download, the step list
//   GameWizardContent.cpp  sources: scan the folder, import art / models / sounds / music / fonts / texts, branding, palette
//   GameWizardTutorial.cpp tutorial map + steps from the rules, soft hints
//   GameWizardMeta.cpp     levels, meta screens and their config, shop, monetization, languages, report
#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "GameWizard.h"

namespace rts::editor::wiz {

// What the template's rules offer, read from rules.json (not the parsed Rules: names / ids are enough).
struct TypeInfo {
    std::string id, name;
    bool structure = false, mobile = false, combat = false, worker = false, node = false;
    bool garrison = false, entersGarrison = false, key = false, waveSpawn = false, air = false;
    int supply = 0, supplyCost = 0;
    std::vector<std::string> produces, builds, gathers;   // gathers: resources
    std::vector<std::string> needs, factions;             // factions: empty = every faction
    std::string nodeResource, deploysTo, buildMode;
    bool powerField = false;
    bool forFaction(const std::string& f) const { return factions.empty() || f.empty() || std::find(factions.begin(), factions.end(), f) != factions.end(); }
    std::vector<std::pair<std::string, int64_t>> cost;    // resource -> amount (no time / supply)
};
struct RulesInfo {
    std::vector<std::string> resources;
    std::vector<TypeInfo> types;
    bool sprites2d = false, planets = false;
    const TypeInfo* find(const std::string& id) const;
    static RulesInfo read(const Json& rules);
};

struct TemplateTraits {
    const char* id;
    const char* tags;        // space separated features
    const char* keywords;    // space separated words of ideas that point here (ru / en, lower case, stems)
    const char* preset;      // map generator preset for levels ("" = copies of the base map)
};
const std::vector<TemplateTraits>& templateTraits();

std::string lower(const std::string& s);                  // ASCII + Cyrillic (UTF-8) lower case
std::vector<std::string> words(const std::string& s);     // lower-case word tokens (letters / digits, UTF-8 kept)
uint32_t hashText(const std::string& s);
std::string luaLiteral(const Json& j, int indent = 0);     // Lua table constructor for a JSON value
Json readJsonFile(const std::string& path);                // comments allowed; null when missing / broken
bool writeText(const std::string& path, const std::string& text, std::string* err = nullptr);
std::string readText(const std::string& path);
std::string slugOf(const std::string& title);

} // namespace rts::editor::wiz

namespace rts::editor {

struct GameMaker::Ctx {
    std::string engineDir, kitDir;           // engine checkout, templates/_wizard
    std::string dir;                         // the new project
    WizardSpec spec;
    Json plan = Json::object();
    std::string templ;                       // template id
    std::string sourceDir;                   // art folder (spec.artDir or the download)
    std::string idea;                        // spec.idea + texts found in the folder
    Json manifest = Json::object(), rules = Json::object();
    wiz::RulesInfo info;
    std::string baseMapFile;                 // project-relative, the template's first map
    Json baseMap = Json::object();
    std::string mainLang = "ru";
    // results of the content step
    std::string keyArt, iconArt, menuArt;    // project-relative images
    std::vector<std::string> images, models, music, sounds, fonts;
    Json assigned = Json::array();           // {file, to, why}
    // maps
    int tutorialMap = 0;                     // 1-based index in game.json maps (0 = none)
    std::vector<Json> levels;                // ui/wizard.json levels
    std::string archetype;                   // economy | garrison | generic
    Json tutorialSteps = Json::array();
    // texts: key -> {lang -> text}
    std::map<std::string, std::map<std::string, std::string>> texts;
    Json warnings = Json::array(), done = Json::array();

    void warn(const std::string& w) { warnings.push(Json::string(w)); }
    void did(const std::string& d) { done.push(Json::string(d)); }
    void text(const std::string& key, const std::string& ru, const std::string& en) { texts[key]["ru"] = ru; texts[key]["en"] = en; }
    std::string tr(const std::string& ru, const std::string& en) const { return mainLang == "ru" ? ru : en; }
    std::string path(const std::string& rel) const { return dir + "/" + rel; }
    std::string typeName(const std::string& id, const std::string& lang) const;   // from the template's lang files
    void saveManifest();
    void saveRules();
};

namespace wiz {
// GameWizardContent.cpp
void scanSources(GameMaker::Ctx& c);
void importContent(GameMaker::Ctx& c);
void applyBranding(Workspace& ws, GameMaker::Ctx& c);
void applyPalette(GameMaker::Ctx& c);
void applyAudio(GameMaker::Ctx& c);
// GameWizardTutorial.cpp
void makeTutorial(GameMaker::Ctx& c);
void makeHints(GameMaker::Ctx& c);
// GameWizardMeta.cpp
void makeLevels(GameMaker::Ctx& c);
void makeMeta(GameMaker::Ctx& c);
void makeMonetization(GameMaker::Ctx& c);
void writeLanguages(GameMaker::Ctx& c);
void verifyGame(Workspace& ws, GameMaker::Ctx& c);
void writeReport(GameMaker::Ctx& c);
void copyKit(GameMaker::Ctx& c);
} // namespace wiz

} // namespace rts::editor
