// Studio tool panels (World, Look, Logic, Publish groups of the legacy editor): which Workspace ops
// each HTML panel may run through studio.op, which of them only read, and the two panels that have
// no op of their own (Input actions file, Asset Audit scan). Everything here goes through the same
// ops the MCP agent uses, so edits are validated and undoable exactly like in the old editor.
#pragma once
#include "engine/data/Json.h"
#include "engine/platform/EnhancedInputFileIO.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace aiverse::studio {

struct StudioToolGroup { const char* panel; std::vector<const char*> ops; };

inline const std::vector<StudioToolGroup>& studioToolGroups() {
    static const std::vector<StudioToolGroup> groups = {
        {"terrain", {"map_info", "terrain_sculpt", "terrain_level", "terrain_height", "terrain_heights", "terrain_paint",
                     "terrain_paint_material", "terrain_material", "terrain_detail", "heightmap_import", "heightmap_export"}},
        {"water", {"water_types", "water_network", "water_set", "water_delete", "water_shores", "water_bake", "nereid_water_state"}},
        {"roads", {"road_types", "road_network", "road_add", "road_delete", "road_node_set", "road_edge_set", "road_split",
                   "road_toggle", "road_type_set", "road_stop_set", "road_driveway_set"}},
        {"tiles", {"tileset_list", "tile_info", "tiles_init", "tile_paint", "tiles_remove"}},
        {"fog", {"rules_get", "rules_merge", "rules_delete"}},
        {"generate", {"gen_presets", "map_generate", "map_mirror", "map_resize", "map_validate", "map_ascii"}},
        {"postfx", {"postfx_get", "postfx_catalog", "postfx_add", "postfx_set", "postfx_enable", "postfx_remove",
                    "postfx_preset", "postfx_estimate", "postfx_lut_neutral"}},
        {"effects", {"fx_list", "fx_templates", "fx_get", "fx_set", "fx_bind", "fx_delete"}},
        {"lightbake", {"lightbake_get", "lightbake_set", "lightbake_clear"}},
        {"models", {"asset_list", "model_info", "geometry_catalog", "geometry_analyze", "strata_capabilities", "strata_model_analyze", "geometry_proxy_bake", "geometry_proxy_lod"}},
        {"scripts", {"script_api", "script_list", "script_read", "script_write", "script_check"}},
        {"overlays", {"overlay_templates", "overlay_list", "overlay_get", "overlay_set", "overlay_delete", "overlay_preview"}},
        {"branding", {"branding_get", "branding_set", "splash_preview", "icons_preview"}},
        {"fields", {"field_list", "field_get", "field_paint", "field_set_rules", "region_paint", "territory_paint"}},
        {"physis", {"physis_get", "physis_validate", "physis_set"}},
        {"actor", {"actor_get", "actor_set", "lens_scene_get", "lens_scene_validate", "lens_scene_set", "lens_camera_set", "lens_camera_delete"}},
        {"raid", {"raid_info", "raid_set", "raid_point", "raid_run"}},
        {"balance", {"balance_stats", "balance_duel", "balance_matrix", "balance_batch", "match_run", "scenario_run", "rules_lint"}},
        {"fx_graph", {"fx_graph_list", "fx_graph_node_catalog", "fx_graph_get", "fx_graph_validate", "fx_graph_set", "fx_graph_edit",
                      "fx_graph_bake", "fx_graph_delete", "fx_graph_draft_list", "fx_graph_draft_get", "fx_graph_draft_set", "fx_graph_draft_publish"}},
        {"fx_functions", {"fx_function_list", "fx_function_get", "fx_function_set", "fx_function_import", "fx_function_export", "fx_import_unity"}},
        {"modeling", {"modeling_catalog", "modeling_list", "modeling_get", "modeling_create", "modeling_edit", "modeling_validate", "strata_capabilities", "strata_model_analyze", "modeling_apply", "modeling_build",
                      "modeling_duplicate", "modeling_lod", "modeling_export", "modeling_graph_set", "modeling_graph_import", "modeling_graph_publish",
                      "texture_patterns", "texture_generate"}},
        {"anim_assets", {"modeling_list", "animation_get", "animation_asset_get", "animation_asset_save", "animation_generate", "animation_bake", "animation_sample"}},
        {"anim_graph", {"graph_catalog", "graph_list", "graph_get", "graph_node_catalog", "graph_validate", "graph_diagnostics", "graph_dependencies", "graph_property_schema", "graph_asset_dependencies", "animation_graph_list", "animation_graph_get", "animation_graph_validate", "animation_graph_set", "animation_graph_apply",
                        "animation_montage_validate"}},
        {"anim_rig", {"animation_avatar_suggest", "animation_avatar_validate", "rig_auto_weights", "rig_edit", "animation_ragdoll_generate",
                      "animation_root_motion_generate"}},
        {"model_anim", {"model_info", "animation_editor", "animation_edit"}},
        {"snapshot_diff", {"game_file_read"}},
        {"project_import", {"port_scan", "port_report", "port_item", "port_resolve", "port_import"}},
        // Package catalog (AIVERSE-PACKAGES): search and install content into the open project.
        {"catalog", {"package_search", "package_info", "package_status", "package_add", "package_remove", "package_install"}},
        // TerraWeave Generator panel (landscape heightfield + Arbor forest recipe).
        {"terraweave_generator", {"map_info", "terraweave_preview", "terraweave_generate", "terraweave_forest_get",
                                  "terraweave_forest_set"}},
    };
    return groups;
}

inline const StudioToolGroup* studioToolGroup(const std::string& panel) {
    for (const auto& g : studioToolGroups()) if (panel == g.panel) return &g;
    return nullptr;
}

inline bool studioToolAllowed(const std::string& op) {
    for (const auto& g : studioToolGroups())
        for (const char* o : g.ops) if (op == o) return true;
    return false;
}

// Ops that never change the project or the map: the panel runs them without an undo step or a
// document refresh.
inline bool studioToolReadOnly(const std::string& op) {
    if (op == "strata_capabilities" || op == "graph_diagnostics" || op == "graph_dependencies" || op == "graph_property_schema" || op == "graph_asset_dependencies") return true;
    static const char* suffixes[] = {"_get", "_list", "_info", "_types", "_network", "_catalog", "_templates",
                                     "_presets", "_validate", "_ascii", "_estimate", "_api", "_read", "_check",
                                     "_preview", "_analyze", "_state", "_export", "_run", "_scan", "_report", "_sample",
                                     "_stats", "_duel", "_matrix", "_batch", "_suggest", "_patterns", "_item", "_lint",
                                     "_search", "_status"};
    for (const char* s : suffixes) {
        const std::string suf = s;
        if (op.size() > suf.size() && op.compare(op.size() - suf.size(), suf.size(), suf) == 0) return true;
    }
    return false;
}

// "terrain_paint_material" -> "Terrain paint material" (History label).
inline std::string studioToolLabel(const std::string& op) {
    std::string out = op;
    std::replace(out.begin(), out.end(), '_', ' ');
    if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// ---- Input actions (input/actions.json, InputFlow asset) ----
inline Json studioInputNames() {
    namespace ec = platform::enhanced_config;
    Json out = Json::object(), keys = Json::array(), pad = Json::array(), axes = Json::array();
    for (size_t i = 1; i < ec::detail::kKeyNames.size(); ++i) keys.push(Json::string(ec::detail::kKeyNames[i]));
    for (const char* b : ec::detail::kPadButtons) pad.push(Json::string(b));
    for (const char* a : ec::detail::kPadAxes) axes.push(Json::string(a));
    out.set("keys", keys); out.set("padButtons", pad); out.set("padAxes", axes);
    Json devices = Json::array();
    for (const auto d : {ec::ei::Device::Keyboard, ec::ei::Device::MouseButton, ec::ei::Device::MouseAxis, ec::ei::Device::MouseWheel,
                         ec::ei::Device::GamepadButton, ec::ei::Device::GamepadAxis, ec::ei::Device::Touch})
        devices.push(Json::string(ec::detail::deviceName(d)));
    out.set("devices", devices);
    return out;
}

// ---- Asset Audit: quoted paths in data files that point at no file ----
struct StudioAuditRow { std::string source, reference; };
struct StudioAuditResult { std::vector<StudioAuditRow> rows; size_t scanned = 0; bool truncated = false; };

inline StudioAuditResult studioAssetAudit(const std::filesystem::path& project, const std::filesystem::path& engine) {
    namespace fs = std::filesystem;
    StudioAuditResult out;
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(project, ec);
    if (ec) return out;
    static const std::set<std::string> known = {".png", ".jpg", ".jpeg", ".webp", ".gltf", ".glb", ".obj", ".fbx", ".lua", ".rml",
                                                ".rcss", ".wav", ".ogg", ".mp3", ".json", ".rtex", ".ktx2"};
    static const std::set<std::string> inspected = {".json", ".lua", ".rml", ".rcss"};
    const std::regex quoted(R"RE(["']([^"'\r\n]{3,240})["'])RE");
    // Engine-provided content (shared materials, UI theme, fonts) is resolved by the runtime too.
    std::vector<fs::path> engineRoots;
    if (!engine.empty()) { engineRoots.push_back(engine); engineRoots.push_back(engine / "engine" / "assets"); }
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (name == ".git" || name == "node_modules" || name == "build" || name == "Builds" || name == "dist" || name == ".aiverse-cache")
                it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec) || !inspected.count(it->path().extension().string()) || it->file_size(ec) > 1024 * 1024) continue;
        if (++out.scanned > 3000) { out.truncated = true; break; }
        std::ifstream file(it->path(), std::ios::binary);
        if (!file) continue;
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        for (std::sregex_iterator match(content.begin(), content.end(), quoted), last; match != last; ++match) {
            const std::string path = (*match)[1].str();
            if (!known.count(fs::path(path).extension().string()) || path.find("://") != std::string::npos ||
                path.find("${") != std::string::npos || path.find('%') != std::string::npos)
                continue;
            const fs::path ref(path);
            if (ref.is_absolute() || path.find("..") != std::string::npos || path.find('\\') != std::string::npos) continue;
            if (fs::exists(root / ref, ec) || fs::exists(it->path().parent_path() / ref, ec)) continue;
            bool engineHas = false;
            for (const auto& e : engineRoots) if (fs::exists(e / ref, ec)) { engineHas = true; break; }
            if (engineHas) continue;
            out.rows.push_back({it->path().lexically_relative(root).generic_string(), path});
            if (out.rows.size() >= 1000) { out.truncated = true; return out; }
        }
    }
    return out;
}

} // namespace aiverse::studio
