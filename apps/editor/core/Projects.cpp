#include "Projects.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include "engine/data/Json.h"
#include "engine/pack/FileCopy.h"
#include "PackageTool.h"
#include "ProjectFile.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

namespace {

fs::path home() {
    if (const char* h = std::getenv("HOME"); h && *h) return h;
    if (const char* h = std::getenv("USERPROFILE"); h && *h) return h;
    std::error_code ec;
    return fs::current_path(ec);
}

bool writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    if (!f) return false;
    f << text;
    return bool(f);
}

std::string slug(const std::string& s) {   // bundle id part: ascii letters/digits only
    std::string o;
    for (unsigned char c : s) if (std::isalnum(c) && c < 128) o += char(std::tolower(c));
    return o.empty() ? "game" : o;
}

Json loadRegistry() {
    try { Json j = Json::parseFile(projectsRegistry()); if (j["projects"].isArray()) return j; } catch (...) {}
    Json j = Json::object();
    j.set("projects", Json::array());
    return j;
}

void saveRegistry(const Json& j) { writeFile(projectsRegistry(), j.dump(2) + "\n"); }

std::string canonical(const std::string& dir) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(dir, ec), ec);
    return p.lexically_normal().string();
}

// folders of a project that are outputs or editor state, never copied into a new project
bool skipped(const fs::path& rel) {
    const std::string top = rel.begin() == rel.end() ? std::string() : rel.begin()->string();
    if (std::distance(rel.begin(), rel.end()) == 1 && isProjectFilePath(top)) return true;   // the new project gets its own
    return top == "Builds" || top == "dist" || top == "logs" || top == "Logs" || top == ".aiverse" || top == ".aiverse";
}

const char* kEmptyRules = R"({
  "resources": {"gold": {"name": "Золото", "start": 500}},
  "types": {
    "hq": {"name": "Штаб", "key": true, "structure": {"footprint": [3, 3]}, "health": {"max": 1500}, "produces": ["worker"], "dropoff": ["gold"], "supply": 10},
    "worker": {"name": "Рабочий", "move": {"speed": 2.5}, "health": {"max": 40}, "cost": {"gold": 50, "time": 8, "supply": 1}, "gather": {"gold": {"carry": 8, "time": 2}}, "builds": []},
    "mine": {"name": "Рудник", "structure": {"footprint": [2, 2]}, "invulnerable": true, "resourceNode": {"resource": "gold", "amount": 5000}}
  }
}
)";
const char* kEmptyMap = R"({"name": "Start", "size": [48, 32], "players": [{"name": "Blue", "team": 1}, {"name": "Red", "team": 2}],
 "terrain": {"rows": []}, "entities": [{"type": "hq", "owner": 1, "pos": [8.5, 16.5]}, {"type": "mine", "owner": 0, "pos": [4, 10]},
 {"type": "hq", "owner": 2, "pos": [39.5, 16.5]}, {"type": "mine", "owner": 0, "pos": [44, 22]}]}
)";

} // namespace

std::vector<TemplateInfo> listTemplates(const std::string& engineDir) {
    std::vector<TemplateInfo> out;
    TemplateInfo empty;
    empty.id = "empty";
    empty.name = "Пустой проект";
    empty.description = "Один тип юнита, рудник и пустая карта: начать с нуля.";
    empty.genre = "rts";
    empty.maps = 1;
    out.push_back(empty);
    std::error_code ec;
    std::vector<TemplateInfo> found;
    for (auto& e : fs::directory_iterator(fs::path(engineDir) / "templates", ec)) {
        if (!e.is_directory() || !fs::exists(e.path() / "game.json")) continue;
        TemplateInfo t;
        t.id = e.path().filename().string();
        t.dir = e.path().string();
        try {
            Json m = Json::parseFile((e.path() / "game.json").string());
            t.name = m.getStr("name", t.id);
            t.description = m.getStr("description", "");
            t.genre = m.getStr("genre", m.getStr("module", "game"));
            t.maps = int(m["maps"].size());
            std::string pv = m.getStr("preview", "");
            if (pv.empty() && m["icons"].isObject()) pv = m["icons"].getStr("default", "");
            if (pv.empty()) pv = m.getStr("icon", "");
            for (const char* c : {"preview.png", "branding/icon.png", "icon.png"}) if (pv.empty() && fs::exists(e.path() / c)) pv = c;
            if (!pv.empty() && fs::exists(e.path() / pv)) t.preview = (e.path() / pv).string();
        } catch (...) { t.name = t.id + " (ошибка game.json)"; }
        found.push_back(t);
    }
    std::sort(found.begin(), found.end(), [](const TemplateInfo& a, const TemplateInfo& b) { return a.name < b.name; });
    out.insert(out.end(), found.begin(), found.end());
    return out;
}

bool isCatalogTemplate(const std::string& templateId) { return templateId.rfind("pkg:", 0) == 0; }

std::vector<TemplateInfo> catalogTemplates(const std::string& engineDir, bool all, const std::string& query,
                                           bool cachedOnly, bool refresh, std::string* error) {
    std::vector<TemplateInfo> out;
    std::vector<std::string> args = {"templates"};
    if (all) args.push_back("--all");
    if (cachedOnly) args.push_back("--cached");
    else if (refresh) args.push_back("--refresh");
    std::string word;
    for (char c : query + " ") {   // query words are separate arguments
        if (c == ' ' || c == '\t') { if (!word.empty()) args.push_back(word); word.clear(); }
        else word += c;
    }
    try {
        const Json list = runPackageTool(engineDir, args);
        for (const auto& e : list.items()) {
            TemplateInfo t;
            t.source = "catalog";
            t.id = "pkg:" + e.getStr("id");
            t.name = e.getStr("title", e.getStr("id"));
            t.description = e.getStr("summary");
            t.genre = e.getStr("module");
            t.maps = int(e["maps"].size());
            t.official = e.getBool("official");
            t.version = e.getStr("latest");
            t.engineTemplate = e.getStr("engineTemplate");
            t.size = (long long)e["size"].integer(0);
            t.preview = e.getStr("previewFile");
            if (e["previews"].isArray() && e["previews"].size()) t.previewUrl = e["previews"][size_t(0)].str("");
            out.push_back(std::move(t));
        }
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
    }
    return out;
}

std::vector<TemplateInfo> hubTemplates(const std::string& engineDir, bool cachedOnly, std::string* catalogError) {
    auto engine = listTemplates(engineDir);
    const auto catalog = catalogTemplates(engineDir, false, "", cachedOnly, false, catalogError);
    std::vector<TemplateInfo> out;
    out.push_back(engine.front());   // "empty"
    out.insert(out.end(), catalog.begin(), catalog.end());
    for (size_t i = 1; i < engine.size(); ++i) {
        const bool replaced = std::any_of(catalog.begin(), catalog.end(),
                                          [&](const TemplateInfo& c) { return c.engineTemplate == engine[i].id; });
        if (!replaced) out.push_back(engine[i]);
    }
    return out;
}

std::string projectsRegistry() {
    const auto current = home() / ".aiverse" / "projects.json";
    const auto legacy = home() / ".aiverse" / "projects.json";
    std::error_code ec;
    if (!fs::exists(current, ec) && fs::exists(legacy, ec)) return legacy.string();
    return current.string();
}
std::string defaultProjectsDir() { return (home() / "AiverseProjects").string(); }

std::vector<ProjectInfo> recentProjects() {
    std::vector<ProjectInfo> out;
    Json j = loadRegistry();
    for (auto& p : j["projects"].items()) {
        ProjectInfo info;
        info.dir = p.getStr("dir");
        info.name = p.getStr("name", fs::path(info.dir).filename().string());
        info.file = p.getStr("file");
        info.lastOpened = (long long)p["opened"].integer(0);
        std::error_code ec;
        info.exists = fs::exists(fs::path(info.dir) / "game.json", ec);
        if (!info.dir.empty()) out.push_back(info);
    }
    std::stable_sort(out.begin(), out.end(), [](const ProjectInfo& a, const ProjectInfo& b) { return a.lastOpened > b.lastOpened; });
    return out;
}

void rememberProject(const std::string& dir, const std::string& name, const std::string& file) {
    const std::string d = canonical(dir);
    Json j = loadRegistry();
    Json list = Json::array();
    Json me = Json::object();
    me.set("dir", Json::string(d));
    me.set("name", Json::string(name));
    if (!file.empty()) me.set("file", Json::string(canonical(file)));
    me.set("opened", Json::number(int64_t(std::time(nullptr))));
    list.push(me);
    for (auto& p : j["projects"].items()) if (canonical(p.getStr("dir")) != d && list.size() < 50) list.push(p);
    j.set("projects", list);
    saveRegistry(j);
}

void forgetProject(const std::string& dir) {
    const std::string d = canonical(dir);
    Json j = loadRegistry();
    Json list = Json::array();
    for (auto& p : j["projects"].items()) if (canonical(p.getStr("dir")) != d) list.push(p);
    j.set("projects", list);
    saveRegistry(j);
}

std::string projectFolderName(const std::string& title) {
    std::string o;
    for (unsigned char c : title) {
        if (c >= 128 || std::isalnum(c) || c == ' ' || c == '-' || c == '_') o += char(c);   // keeps UTF-8 letters
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    return o.empty() ? "NewGame" : o;
}

std::string createProject(const std::string& engineDir, const std::string& templateId, const std::string& title,
                          const std::string& parentDir, const std::string& folder, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return std::string(); };
    if (title.empty()) return fail("укажите название проекта");
    if (parentDir.empty()) return fail("укажите папку, где создать проект");
    const std::string name = folder.empty() ? projectFolderName(title) : folder;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || name == "." || name == "..")
        return fail("имя папки не должно содержать / или \\");
    std::error_code ec;
    const fs::path dst = fs::absolute(fs::path(parentDir) / name, ec);
    if (fs::exists(dst, ec) && !fs::is_empty(dst, ec)) return fail("папка уже существует и не пуста: " + dst.string());
    if (isCatalogTemplate(templateId)) {
        // a template package: aiverse_pkg.py downloads (or takes from the cache), verifies and unpacks it,
        // then installs the packages its game.json lists
        try {
            runPackageTool(engineDir, {"create", templateId.substr(4), dst.string()});
        } catch (const std::exception& e) {
            return fail("шаблон из каталога '" + templateId.substr(4) + "': " + e.what());
        }
    }
    fs::create_directories(dst / "maps", ec);
    if (ec) return fail("не удалось создать " + dst.string() + ": " + ec.message());
    try {
        if (templateId != "empty" && !isCatalogTemplate(templateId)) {
            const fs::path src = fs::path(engineDir) / "templates" / templateId;
            if (!fs::exists(src / "game.json")) return fail("шаблон '" + templateId + "' не найден");
            // per file: MinGW's recursive fs::copy fails into an existing folder
            for (auto it = fs::recursive_directory_iterator(src); it != fs::recursive_directory_iterator(); ++it) {
                const fs::path rel = fs::relative(it->path(), src);
                if (skipped(rel)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
                const fs::path to = dst / rel;
                if (it->is_directory()) fs::create_directories(to);
                else aiverse::pack::copyFileReplaceOrThrow(it->path(), to);
            }
        } else if (templateId == "empty") {   // a catalog template is already unpacked above
            writeFile(dst / "rules.json", kEmptyRules);
            writeFile(dst / "maps" / "start.json", kEmptyMap);
            writeFile(dst / "game.json", "{}");
        }
        // Every newly created game project has a dedicated content/ asset root.
        // Templates may already contain content/; preserve their authored files.
        // Never accept a template-provided symlink that redirects assets outside
        // the new project directory.
        const fs::path content = dst / "content";
        ec.clear();
        const fs::file_status contentStatus = fs::symlink_status(content, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
            throw std::runtime_error("cannot inspect content/: " + ec.message());
        if (fs::is_symlink(contentStatus) ||
            (fs::exists(contentStatus) && !fs::is_directory(contentStatus)))
            throw std::runtime_error("content/ must be a real project directory");
        ec.clear();
        fs::create_directories(content, ec);
        if (ec) throw std::runtime_error("cannot create content/: " + ec.message());

        Json m = Json::parseFile((dst / "game.json").string());
        m.set("name", Json::string(title));
        m.set("id", Json::string("com.aiverse." + slug(name)));
        m.set("version", Json::string("0.1.0"));
        if (!m.has("module")) m.set("module", Json::string("game"));
        if (!m.has("rules")) m.set("rules", Json::string("rules.json"));
        if (!m.has("maps") || templateId == "empty") {
            Json maps = Json::array();
            Json e = Json::object(); e.set("file", Json::string("maps/start.json")); e.set("title", Json::string("Start"));
            maps.push(e);
            m.set("maps", maps);
        }
        if (templateId != "empty") m.set("template", Json::string(isCatalogTemplate(templateId) ? templateId.substr(4) : templateId));
        if (templateId == "empty") {
            // New blank projects start with a neutral scale reference, never procedural grass.
            // Copy runtime assets into the project so packaging stays self-contained. The engine
            // ships only the checkerboard; the full library is the catalog package terrain-pbr
            // (tools/pkg/aiverse_pkg.py add terrain-pbr). Only textures/ is copied, so that package
            // installs over it without conflicts (identical checkerboard files and material id).
            const fs::path pack = fs::path(engineDir) / "engine/assets/materials/terrain";
            if (!fs::exists(pack / "default-terrain.json"))
                return fail("bundled terrain PBR materials are missing: " + pack.string());
            fs::create_directories(dst / "materials/terrain/textures");
            for (const auto& entry : fs::recursive_directory_iterator(pack / "textures")) {
                const fs::path target = dst / "materials/terrain/textures" / fs::relative(entry.path(), pack / "textures");
                if (entry.is_directory()) fs::create_directories(target);
                else aiverse::pack::copyFileReplaceOrThrow(entry.path(), target);
            }
            const Json terrain = Json::parseFile((pack / "default-terrain.json").string());
            const Json library = Json::parseFile((pack / "materials.json").string());
            m.set("terrain", terrain);
            if (!writeFile(dst / "materials.json", library.dump(2) + "\n"))
                return fail("cannot write bundled PBR material library");
        }
        writeFile(dst / "game.json", m.dump() + "\n");
        ProjectFile pf;
        pf.name = title;
        std::string perr;
        if (!writeProjectFile((dst / projectFileName(title)).string(), pf, &perr)) return fail(perr);
    } catch (const std::exception& e) {
        return fail(std::string("не удалось создать проект: ") + e.what());
    }
    rememberProject(dst.string(), title, (dst / projectFileName(title)).string());
    return dst.lexically_normal().string();
}

} // namespace aiverse::editor
