/** How a transcript line of the agent run is shown (lines come from AgentRun: prompts, texts, tool calls, status). */
export type AgentLineKind = 'user' | 'tool' | 'done' | 'error' | 'text';
export function agentLineKind(line: string): AgentLineKind {
  if (line.startsWith('› ')) return 'user';
  if (line.startsWith('→ ')) return 'tool';
  if (/^агент закончил$|^agent finished$/i.test(line)) return 'done';
  if (/^(ошибка агента|агент остановлен|не удалось запустить)/i.test(line) || /^error\b/i.test(line)) return 'error';
  return 'text';
}

/** Extensions "open_window" commands name legacy window ids; map them to Studio panels (null when there is none). */
const LEGACY_TO_PANEL: Record<string, string> = {
  inspector: 'inspector', hierarchy: 'hierarchy', scene: 'scene', game: 'game', console: 'console', assets: 'assets', rules: 'rules',
  terrain: 'terrain', water: 'water', roads: 'roads', tiles: 'tiles', fog: 'fog', generate: 'generate', fields: 'fields', physis: 'physis',
  postfx: 'postfx', effects: 'effects', lightbake: 'lightbake', models: 'models', scripts: 'scripts', input: 'input', overlays: 'overlays',
  branding: 'branding', studio_asset_audit: 'asset_audit', stats: 'stats', agent: 'agent', build: 'build', lighting: 'lighting', materials: 'materials',
  FpsWindow: 'actor', raid: 'raid', ModelingWindow: 'modeling', spectrafx_graph: 'fx_graph', spectrafx_gpu_assets: 'fx_functions',
  animation_assets: 'anim_assets', animation_graph: 'anim_graph', animation_rig: 'anim_rig', AnimationWindow: 'model_anim',
  project_import: 'project_import', studio_snapshot_diff: 'snapshot_diff', studio_frame_capture: 'frame_capture', texture_resources: 'texture_resources', animation_attachments: 'attachments', studio_extensions: 'extensions',
};
export function extensionPanelFor(id: string): string | null { return LEGACY_TO_PANEL[id] ?? null; }

export function extensionWindowFor(panel: string): string {
  return Object.keys(LEGACY_TO_PANEL).find(id => LEGACY_TO_PANEL[id] === panel) ?? panel;
}
