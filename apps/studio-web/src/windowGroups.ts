/** One exhaustive category per panel, independent of dock layout and workspace. */
import { STUDIO_PANELS, STUDIO_PANEL_TITLES, type StudioPanel } from './nestedDock.ts';

export const WINDOW_GROUPS = [
  ['scene', 'Scene & Editing'],
  ['world', 'World & Terrain'],
  ['render', 'Rendering & Materials'],
  ['fx', 'Effects & Environment'],
  ['animation', 'Animation & Rigging'],
  ['character', 'Characters'],
  ['assets', 'Assets & Modeling'],
  ['gameplay', 'Gameplay & Logic'],
  ['runtime', 'Runtime & Diagnostics'],
  ['project', 'Project & Publishing'],
] as const;
export type WindowGroup = typeof WINDOW_GROUPS[number][0];

// Adding a panel to STUDIO_PANELS requires choosing its category here.
export const PANEL_WINDOW_GROUP: Record<StudioPanel, WindowGroup> = {
  scene: 'scene', hierarchy: 'scene', inspector: 'scene', history: 'scene',
  terrain: 'world', water: 'world', nereid_flow: 'world', roads: 'world',
  tiles: 'world', fog: 'world', generate: 'world', fields: 'world',
  building_generator: 'world', feature_readiness: 'project', terraweave_generator: 'world', caverns: 'world', world_explorer: 'world', world_persistence: 'world',
  materials: 'render', lighting: 'render', shader_graph: 'render',
  postfx: 'render', lightbake: 'render', texture_resources: 'render',
  effects: 'fx', fx_graph: 'fx', fx_library: 'fx', fx_functions: 'fx', wind: 'fx',
  animation: 'animation', anim_assets: 'animation', anim_graph: 'animation',
  anim_rig: 'animation', model_anim: 'animation', attachments: 'animation',
  facial_rig: 'character', human_creator: 'character', human_appearance: 'character',
  assets: 'assets', models: 'assets', modeling: 'assets', asset_audit: 'assets', catalog: 'assets',
  rules: 'gameplay', scripts: 'gameplay', input: 'gameplay', overlays: 'gameplay',
  physis: 'gameplay', actor: 'gameplay', raid: 'gameplay', balance: 'gameplay',
  game: 'runtime', runtime: 'runtime', console: 'runtime', stats: 'runtime',
  frame_capture: 'runtime', simulation_profiler: 'runtime', snapshot_diff: 'runtime', agent: 'runtime',
  project: 'project', build: 'project', branding: 'project', project_import: 'project', extensions: 'project',
};

export function windowPanelGroups() {
  return WINDOW_GROUPS.map(([id, title]) => ({
    id, title,
    panels: STUDIO_PANELS.filter(panel => PANEL_WINDOW_GROUP[panel] === id)
      .sort((a, b) => STUDIO_PANEL_TITLES[a].localeCompare(STUDIO_PANEL_TITLES[b], 'en')),
  }));
}
