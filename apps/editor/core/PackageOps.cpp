// Package catalog (AIVERSE-PACKAGES) for the editor and MCP: package_search / info / add / remove /
// status / install / skill run tools/pkg/aiverse_pkg.py --json and return its result, so agents and the
// Studio Catalog window install content into the open project without a shell.
#include "PackageOps.h"
#include "PackageTool.h"
#include <filesystem>
#include <stdexcept>

namespace aiverse::editor {
namespace {
std::string requireId(const Json& p) {
    std::string id = p.getStr("id");
    if (id.empty()) throw std::runtime_error("id is required (package id, optionally id@range)");
    return id;
}

std::string projectDir(Workspace& ws, const Json& p) {
    std::string dir = p.getStr("project");
    if (dir.empty()) dir = ws.project.dir;
    if (dir.empty()) throw std::runtime_error("no project is open (or pass project)");
    return dir;
}
}

void registerPackageOps(Workspace& ws) {
    const Json idSchema = Json::parse(R"({"properties":{"id":{"type":"string"},"project":{"type":"string"},"force":{"type":"boolean"}},"required":["id"]})");
    const Json projSchema = Json::parse(R"({"properties":{"project":{"type":"string"}}})");
    ws.registerOp({"package_search",
        "Search the package catalog (AIVERSE-PACKAGES): words match id, titles, summaries, tags and provides. "
        "Optional category (materials, textures, models, templates, audio, vfx, ui, scripts, plugins, tools) and kind "
        "(content, template, source, script, plugin, tool). Returns [{id, kind, category, title, latest, size, cached}].",
        Json::parse(R"({"properties":{"query":{"type":"string"},"category":{"type":"string"},"kind":{"type":"string"}}})"),
        [&ws](const Json& p) {
            std::vector<std::string> args{"search"};
            if (p.has("category")) { args.push_back("--category"); args.push_back(p.getStr("category")); }
            if (p.has("kind")) { args.push_back("--kind"); args.push_back(p.getStr("kind")); }
            const std::string q = p.getStr("query");
            size_t i = 0;
            while (i < q.size()) {
                size_t j = q.find(' ', i);
                if (j == std::string::npos) j = q.size();
                if (j > i) args.push_back(q.substr(i, j - i));
                i = j + 1;
            }
            return runPackageTool(ws.engineDir, args);
        }, false});
    ws.registerOp({"package_info",
        "Full catalog entry of a package: manifest (title, summary, description and their -<lang> translations, licence, "
        "minEngine, provides, dependencies, install.mount), versions with sizes and SHA-256, previews, readme, viewer URL.",
        idSchema, [&ws](const Json& p) { return runPackageTool(ws.engineDir, {"info", requireId(p)}); }, false});
    ws.registerOp({"package_add",
        "Install a content package into the open project (or project): copies it to install.mount, installs its content "
        "dependencies first, merges its material library, records it in game.json packages and aiverse.lock. "
        "id may carry a range (terrain-pbr@^1.0). force overwrites differing files and ids.",
        idSchema, [&ws](const Json& p) {
            std::vector<std::string> args{"add", requireId(p), "--project", projectDir(ws, p)};
            if (p.getBool("force")) args.push_back("--force");
            Json r = runPackageTool(ws.engineDir, args);
            ws.note({Change::Rules, 0, 0, -1, -1, -1, "Пакет: " + p.getStr("id")});   // materials and models reload
            return r;
        }, false});
    ws.registerOp({"package_remove",
        "Remove an installed package from the open project: deletes its files and merged ids, drops it from game.json "
        "and aiverse.lock. Dependencies stay installed.",
        idSchema, [&ws](const Json& p) {
            Json r = runPackageTool(ws.engineDir, {"remove", requireId(p), "--project", projectDir(ws, p)});
            ws.note({Change::Rules, 0, 0, -1, -1, -1, "Пакет удалён: " + p.getStr("id")});
            return r;
        }, false});
    ws.registerOp({"package_status",
        "Packages of the open project: wanted range (game.json), locked version (aiverse.lock), installed files, "
        "missing and changed files. mismatch > 0 means install or verify is needed.",
        projSchema, [&ws](const Json& p) { return runPackageTool(ws.engineDir, {"verify", "--project", projectDir(ws, p)}); }, false});
    ws.registerOp({"package_install",
        "Restore every package of the open project from aiverse.lock (after cloning the project).",
        projSchema, [&ws](const Json& p) {
            Json r = runPackageTool(ws.engineDir, {"install", "--project", projectDir(ws, p)});
            ws.note({Change::Rules, 0, 0, -1, -1, -1, "Пакеты восстановлены"});
            return r;
        }, false});
    ws.registerOp({"package_skill",
        "Packages an engine skill needs (frontmatter packages:). With install=true installs them into the open project.",
        Json::parse(R"({"properties":{"skill":{"type":"string"},"install":{"type":"boolean"},"project":{"type":"string"}},"required":["skill"]})"),
        [&ws](const Json& p) {
            std::vector<std::string> args{"skill", p.getStr("skill")};
            if (p.getBool("install")) { args.push_back("--project"); args.push_back(projectDir(ws, p)); }
            Json r = runPackageTool(ws.engineDir, args);
            if (p.getBool("install")) ws.note({Change::Rules, 0, 0, -1, -1, -1, "Пакеты навыка " + p.getStr("skill")});
            return r;
        }, false});
}
}
