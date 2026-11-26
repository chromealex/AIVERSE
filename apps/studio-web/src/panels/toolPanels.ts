/** Tool panels migrated from the legacy editor windows: their op group (native StudioTools.h) and what they show first. */
export const TOOL_PANEL_IDS = ['terrain', 'water', 'roads', 'tiles', 'generate', 'postfx', 'effects', 'lightbake', 'models', 'overlays', 'branding',
  'fields', 'physis', 'actor', 'raid', 'balance', 'fx_graph', 'fx_functions', 'modeling', 'anim_assets', 'anim_graph', 'anim_rig', 'model_anim', 'project_import'] as const;
export type ToolPanelId = typeof TOOL_PANEL_IDS[number];
export interface ToolPanelSpec {
  /** Op name prefix dropped from card titles ("terrain_sculpt" -> "Sculpt"). */
  prefix: string;
  /** Read-only ops shown as the panel's current state (re-read after every change). */
  state: string[];
  /** Ops opened by default. */
  open: string[];
  intro: string;
}
export const TOOL_PANELS: Record<ToolPanelId, ToolPanelSpec> = {
  terrain: { prefix: 'terrain', state: ['map_info'], open: ['terrain_sculpt'],
    intro: 'Heights, levels and surface materials of the open map. Each change is one History step.' },
  water: { prefix: 'water', state: [], open: [],
    intro: 'Spline water: straits, rivers and open sea with their shores.' },
  roads: { prefix: 'road', state: ['road_network'], open: ['road_add'],
    intro: 'Spline road network: nodes, edges, road types, stops and driveways.' },
  tiles: { prefix: 'tile', state: ['tileset_list'], open: ['tile_paint'],
    intro: 'Tile layer of the map: tilesets and painted tiles.' },
  generate: { prefix: 'map', state: ['gen_presets'], open: ['map_generate'],
    intro: 'Generate, mirror or resize the map. Generating replaces the map content (undo brings it back).' },
  postfx: { prefix: 'postfx', state: ['postfx_get'], open: ['postfx_add'],
    intro: 'PRISMA post-processing chain with its cost per device class.' },
  effects: { prefix: 'fx', state: ['fx_list'], open: ['fx_set'],
    intro: 'Particle effects (fx/*.json) and what they are bound to.' },
  lightbake: { prefix: 'lightbake', state: ['lightbake_get'], open: ['lightbake_set'],
    intro: 'Baked lighting of the map.' },
  models: { prefix: 'model', state: ['geometry_catalog'], open: ['model_info'],
    intro: 'Model information, geometry analysis and proxy LODs.' },
  overlays: { prefix: 'overlay', state: ['overlay_list'], open: ['overlay_set'],
    intro: 'Map overlays (decals, markings, zones) and their templates.' },
  branding: { prefix: 'branding', state: ['branding_get'], open: ['branding_set'],
    intro: 'Game name, icons and splash screen used by builds.' },
  fields: { prefix: 'field', state: ['field_list'], open: ['field_paint'],
    intro: 'Resource fields, regions and territory painted on the map.' },
  physis: { prefix: 'physis', state: ['physis_get'], open: ['physis_set'],
    intro: 'PHYSIS nature simulation settings of the game (opt-in).' },
  actor: { prefix: 'actor', state: ['actor_get', 'lens_scene_get'], open: ['actor_set'],
    intro: 'First-person levels and LENS cameras.' },
  raid: { prefix: 'raid', state: ['raid_info'], open: ['raid_set'],
    intro: 'Extraction raid: mode, clock, extraction points, objectives; run a bot raid on the open map.' },
  balance: { prefix: 'balance', state: [], open: ['balance_duel'],
    intro: 'Balance checks: duels, matrices, batch matches and scenario runs (headless, nothing is changed).' },
  fx_graph: { prefix: 'fx_graph', state: ['fx_graph_list'], open: ['fx_graph_get'],
    intro: 'SpectraFX graphs: spawn, initialize, update and output stages; drafts and bake.' },
  fx_functions: { prefix: 'fx_function', state: ['fx_function_list'], open: ['fx_function_get'],
    intro: 'SpectraFX GPU emitter functions; import from Unity VFX.' },
  modeling: { prefix: 'modeling', state: ['modeling_list'], open: ['modeling_get'],
    intro: 'Procedural models: create, edit, LODs and export; generated textures.' },
  anim_assets: { prefix: 'animation', state: [], open: ['animation_asset_get'],
    intro: 'Animation clips: read, generate, bake and sample.' },
  anim_graph: { prefix: 'animation_graph', state: ['animation_graph_list'], open: ['animation_graph_get'],
    intro: 'Animation state graphs and montages.' },
  anim_rig: { prefix: 'animation', state: [], open: ['animation_avatar_suggest'],
    intro: 'Rigs: avatar mapping, skin weights, ragdoll and root motion.' },
  model_anim: { prefix: 'animation', state: [], open: ['model_info'],
    intro: 'Model animation: inspect a model and edit its clips.' },
  project_import: { prefix: 'port', state: [], open: ['port_scan'],
    intro: 'Import a project from another engine: scan, report, resolve and import.' },
};
export function isToolPanel(id: string): id is ToolPanelId { return (TOOL_PANEL_IDS as readonly string[]).includes(id); }
