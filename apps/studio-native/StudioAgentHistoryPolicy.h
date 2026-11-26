// Agent history policy is deliberately independent of the panel suffix heuristic.
// Unknown operations may run but must not be represented as proven reversible.
#pragma once
#include <string>
#include <set>
namespace aiverse::studio {
inline bool studioSceneViewAgentOperation(const std::string& op){
    return op=="view_ground"||op=="view_pick"||op=="view_focus"||op=="studio_planet_forest_live"||op=="studio_planet_scene_get"||op=="studio_planet_scene_body_set"||op=="studio_planet_scene_camera_set";
}
inline const char* studioAgentHistoryPolicy(const std::string& op){
    static const std::set<std::string> reads={
        "studio_runtime_entities","studio_runtime_inspect","studio_runtime_regions","studio_play_review_preview",
        "animation_editor","overlay_preview","overlay_plan_preview","overlay_placement_preview",
        "lightbake_status","lightbake_get","mcp_registry","simulation_budget_plan","game_info","rules_get","rules_list",
        "region_list","map_list","map_info","map_ascii","map_get","map_validate","entity_list","planet_forest_get",
        "view_ground","view_pick","studio_planet_forest_live","studio_planet_scene_get",
        "project_history_status","build_status","studio_asset_history","studio_asset_catalog",
        "studio_history_capabilities","studio_asset_move_preview",
        "studio_asset_dependencies","studio_asset_recovery_storage","studio_asset_trash_list",
        "studio_asset_trash_restore_review","studio_asset_trash_cleanup_review",
        "studio_agent_history_list","studio_agent_batch_preview",
        "studio_owner_history","studio_asset_history_reset_review"
    };
    static const std::set<std::string> map={
        "map_set","map_area","map_heightfield","map_resize","map_mirror","terrain_paint","terrain_level",
        "field_paint","region_name","region_paint","entity_place","entity_update","entity_delete",
        "road_toggle","player_set","player_remove","planet_forest_set","planet_forest_plant","planet_forest_move","planet_forest_remove"
    };
    static const std::set<std::string> project={"rules_set","rules_merge","rules_delete","set_manifest"};
    static const std::set<std::string> assets={"studio_asset_meta_patch","studio_asset_folder_create","studio_asset_move_commit","studio_asset_delete"};
    if(op=="studio_play_review_apply")return "map";
    if(op=="studio_runtime_edit"||op=="studio_runtime_profile"||op=="physis_nature_preview")return "runtime";
    if(reads.count(op))return "read";
    if(op=="view_postfx" || studioSceneViewAgentOperation(op))return "view";
    if(map.count(op))return "map";
    if(project.count(op))return "project";
    if(assets.count(op))return "assets";
    return "unverified";
}
}
