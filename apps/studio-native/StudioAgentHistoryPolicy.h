// Agent history policy is deliberately independent of the panel suffix heuristic.
// Unknown operations may run but must not be represented as proven reversible.
#pragma once
#include <string>
#include <set>
namespace aiverse::studio {
inline const char* studioAgentHistoryPolicy(const std::string& op){
    static const std::set<std::string> reads={
        "mcp_registry","simulation_budget_plan","game_info","rules_get","rules_list",
        "map_list","map_info","map_ascii","map_get","map_validate","entity_list",
        "project_history_status","build_status","studio_asset_history","studio_asset_catalog",
        "studio_history_capabilities","studio_asset_move_preview",
        "studio_asset_dependencies","studio_asset_recovery_storage","studio_asset_trash_list",
        "studio_asset_trash_restore_review","studio_asset_trash_cleanup_review"
    };
    static const std::set<std::string> map={
        "map_set","map_heightfield","map_resize","map_mirror","terrain_paint","terrain_level",
        "field_paint","region_paint","entity_place","entity_update","entity_delete",
        "road_toggle","player_set","player_remove"
    };
    static const std::set<std::string> project={"rules_set","rules_merge","rules_delete","set_manifest"};
    static const std::set<std::string> assets={"studio_asset_meta_patch","studio_asset_folder_create","studio_asset_move_commit","studio_asset_delete"};
    if(reads.count(op))return "read";
    if(map.count(op))return "map";
    if(project.count(op))return "project";
    if(assets.count(op))return "assets";
    return "unverified";
}
}
