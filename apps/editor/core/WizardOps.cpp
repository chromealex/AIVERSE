// MCP / editor operations of the game wizard (GameWizard.h, docs/game-wizard.md):
//   game_make_options  what can be chosen: templates and their mechanics, features, presets, stores, defaults
//   game_make_plan     what would be made for these answers (template + why, tutorial shape, levels, meta); no files
//   game_make          makes the whole project (downloads an art link first) and opens it; returns the report
#include "WizardOps.h"

#include <ctime>
#include <filesystem>
#include "GameWizard.h"
#include "Projects.h"

namespace fs = std::filesystem;

namespace rts::editor {

namespace {
const char* kSpecSchema = R"J({"properties": {
  "title": {"type": "string", "description": "game title (also the project folder and the .rtsproj name)"},
  "location": {"type": "string", "description": "parent folder of the project (default ~/RTSProjects)"},
  "artDir": {"type": "string", "description": "folder with art, models, music, sounds, fonts and .md/.txt texts (a GDD is read as the idea)"},
  "artUrl": {"type": "string", "description": "link to an image / archive (.zip, .tar.gz) with the content; downloaded with curl"},
  "idea": {"type": "string", "description": "what the game is about, any language; empty = the wizard decides"},
  "genre": {"type": "string", "description": "template id (game_make_options) or 'auto'"},
  "features": {"type": "array", "items": {"type": "string"}, "description": "wanted mechanics: economy base army garrison lanes waves territory heroes spells survival planets 2d casual portrait"},
  "orientation": {"type": "string", "enum": ["auto", "landscape", "portrait"]},
  "languages": {"type": "array", "items": {"type": "string"}}, "platforms": {"type": "array", "items": {"type": "string"}},
  "tutorial": {"type": "string", "enum": ["both", "hard", "soft", "none"], "description": "hard = a step-by-step tutorial map, soft = one-time hints in matches"},
  "tutorialForced": {"type": "boolean"}, "tutorialSkippable": {"type": "boolean"}, "tutorialDim": {"type": "boolean"},
  "levels": {"type": "integer", "description": "number of levels (0..40)"},
  "mapPreset": {"type": "string", "description": "map generator preset for levels, 'auto' or 'none' (copies of the base map)"},
  "mapSize": {"type": "string", "enum": ["auto", "small", "medium", "large"]},
  "bossEvery": {"type": "integer"}, "difficultyCurve": {"type": "string", "enum": ["easy", "normal", "hard"]},
  "keepTemplateMaps": {"type": "boolean"}, "verifyLevels": {"type": "boolean", "description": "bot matches on the tutorial and a sample of levels"},
  "startResourcesPercent": {"type": "integer"},
  "currencies": {"type": "array", "items": {"type": "string"}, "description": "soft currency first, hard second"},
  "upgrades": {"type": "boolean"}, "shop": {"type": "boolean"}, "daily": {"type": "boolean"}, "hints": {"type": "boolean"},
  "xpPerLevel": {"type": "integer"},
  "ads": {"type": "boolean"}, "interstitialEvery": {"type": "integer"}, "purchases": {"type": "boolean"},
  "stores": {"type": "array", "items": {"type": "string"}},
  "paletteFromArt": {"type": "boolean"}, "brandingFromArt": {"type": "boolean"},
  "seed": {"type": "integer", "description": "0 = from the title and the idea"},
  "studio": {"type": "string"}, "version": {"type": "string"},
  "ai": {"type": "object", "description": "the game's own texts per language (an agent writes it): {tagline, intro: {lang: text}, types / resources / currencies / upgrades: {id: {lang: text}}, levels: [{lang: text}]}"}}})J";

std::string stagingDir() {
    const char* h = std::getenv("HOME");
    if (!h) h = std::getenv("USERPROFILE");
    char b[32];
    std::time_t t = std::time(nullptr);
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", std::localtime(&t));
    return (fs::path(h ? h : ".") / ".rtsengine" / "wizard" / b).string();
}
} // namespace

std::string wizardStagingDir() { return stagingDir(); }

void registerWizardOps(Workspace& ws) {
    Workspace* w = &ws;
    ws.registerOp({"game_make_options",
        "The game wizard (\"Сделать игру\"): what can be chosen for game_make — templates with their mechanics and orientation, "
        "features, tutorial modes, map presets and sizes, stores, platforms, languages, and the default answers.",
        Json::parse(R"({"properties": {}})"),
        [w](const Json&) {
            Json r = wizardCatalog(w->engineDir);
            r.set("summary", Json::string(std::to_string(r["templates"].size()) + " шаблонов для мастера"));
            return r;
        }});
    ws.registerOp({"game_make_plan",
        "What game_make would make for these answers, without writing files: the template and why (from the idea's words, the "
        "features, the orientation; the seed breaks ties), the tutorial shape (economy / garrison / generic), the level generator, meta parts.",
        Json::parse(kSpecSchema),
        [w](const Json& p) {
            WizardSpec s = WizardSpec::fromJson(p);
            return planGame(w->engineDir, s);
        }});
    ws.registerOp({"game_make",
        "Make a whole game project from a few answers and open it: a template picked by the idea, the user's art / models / music / "
        "sounds / fonts from artDir or artUrl (models and sprites on the types they are named after, music by name into playlists, "
        "the key art into the splash, an icon, UI colours), a step-by-step hard tutorial map built from the rules (only the needed "
        "commands pass, the enemy waits), one-time hints in matches, generated levels with stars and rewards, the meta screens "
        "(home, levels, upgrades that buff the match, shop, daily reward, profile, settings, result), purchases / ads when asked, "
        "language files, a check with bots, WIZARD.md and the .rtsproj. Takes up to a minute (bot checks); verifyLevels false is faster.",
        Json::parse(kSpecSchema),
        [w](const Json& p) {
            WizardSpec s = WizardSpec::fromJson(p);
            if (!s.artUrl.empty() && s.artDir.empty()) {
                std::string err;
                s.artDir = fetchWizardArt(s.artUrl, stagingDir(), &err);
                if (s.artDir.empty()) throw std::runtime_error(err);
            }
            GameMaker maker(*w, s);
            Json r = maker.runAll();
            if (!r.getBool("ok")) throw std::runtime_error(r.getStr("error", "не получилось"));
            w->log("wizard", "Сделана игра: " + maker.projectFile());
            w->note({Change::Project, 0, 0, -1, -1, -1, "новая игра мастером"});
            return r;
        }});
}

} // namespace rts::editor
