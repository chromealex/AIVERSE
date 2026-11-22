// Projects live anywhere on the user's disk (like Unity projects); the engine ships templates in
// <engine>/templates/<id>/ (sample games). A new project is a copy of a template (or the empty
// template) in a folder the user picks. Recently opened projects are remembered in
// ~/.aiverse/projects.json, shared by the editor and aiversemcp.
#pragma once
#include <string>
#include <vector>

namespace aiverse::editor {

struct TemplateInfo {
    std::string id, dir, name, description, genre, preview;   // preview: image path (may be empty)
    int maps = 0;
    // "engine": templates/<id> of the engine; "catalog": a template package of AIVERSE-PACKAGES, id
    // "pkg:<package id>" (created by tools/pkg/aiverse_pkg.py create), dir empty.
    std::string source = "engine";
    bool official = false;          // catalog: made by the AIVERSE authors
    std::string version;            // catalog: latest version
    std::string engineTemplate;     // catalog: the engine template it came from (replaces it in the hub)
    long long size = 0;             // catalog: archive bytes
    std::string previewUrl;         // catalog: https URL of the first preview (preview is its cached copy)
};

struct ProjectInfo {
    std::string dir, name;
    std::string file;           // <dir>/<Title>.aiverse when known (ProjectFile.h)
    long long lastOpened = 0;   // unix seconds
    bool exists = false;
};

std::vector<TemplateInfo> listTemplates(const std::string& engineDir);   // "empty" first, then templates/ sorted by name

// Template packages of the catalog: official ones (all = every template package), filtered by query words.
// cachedOnly never touches the network (instant, empty before the first online call); refresh ignores
// the six-hour cache. On failure returns {} and sets *error.
std::vector<TemplateInfo> catalogTemplates(const std::string& engineDir, bool all, const std::string& query,
                                           bool cachedOnly, bool refresh, std::string* error = nullptr);
// The start screen: "empty", the official catalog templates, then the engine templates the catalog does
// not replace (all of them when the catalog is unavailable).
std::vector<TemplateInfo> hubTemplates(const std::string& engineDir, bool cachedOnly, std::string* catalogError = nullptr);
// A template id for createProject: "empty", a templates/ folder, or "pkg:<package id>".
bool isCatalogTemplate(const std::string& templateId);

std::string projectsRegistry();                         // ~/.aiverse/projects.json
std::string defaultProjectsDir();                       // ~/AiverseProjects
std::vector<ProjectInfo> recentProjects();              // most recent first
void rememberProject(const std::string& dir, const std::string& name, const std::string& file = "");
void forgetProject(const std::string& dir);

// Folder name for a project title: "Моя стратегия" -> "Моя стратегия" (keeps letters, digits, spaces, - _).
std::string projectFolderName(const std::string& title);
// Creates <parentDir>/<folder> from a template id ("empty", a templates/ folder or "pkg:<package id>") with its
// <Title>.aiverse (the template's own .aiverse is not copied). Also ensures
// a lower-case content/ directory for project assets without modifying template
// resources or the game's existing rules/maps. Returns the project folder or ""
// with a readable error.
std::string createProject(const std::string& engineDir, const std::string& templateId, const std::string& title,
                          const std::string& parentDir, const std::string& folder, std::string* error);

} // namespace aiverse::editor
