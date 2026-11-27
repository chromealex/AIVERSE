import { STUDIO_PANEL_TITLES, type StudioPanel } from '../nestedDock.ts';

/** How a transcript line of the agent run is shown (lines come from AgentRun: prompts, texts, tool calls, status). */
export type AgentLineKind = 'user' | 'tool' | 'done' | 'error' | 'text';
export function agentLineKind(line: string): AgentLineKind {
  if (line.startsWith('› ')) return 'user';
  if (line.startsWith('→ ')) return 'tool';
  if (/^агент закончил$|^agent finished$/i.test(line)) return 'done';
  if (/^(ошибка агента|агент остановлен|не удалось запустить)/i.test(line) || /^error\b/i.test(line)) return 'error';
  return 'text';
}

// For renamed windows, keep the ImGui registration id in project menu callbacks.
const windowForPanel: Partial<Record<StudioPanel,string>> = {
  anim_assets: 'animation_assets', anim_graph: 'animation_graph', anim_rig: 'animation_rig',
  animation: 'animation_authoring', attachments: 'animation_attachments', asset_audit: 'studio_asset_audit',
  extensions: 'studio_extensions', frame_capture: 'studio_frame_capture', snapshot_diff: 'studio_snapshot_diff',
  static_geometry: 'static-geometry', fx_functions: 'spectrafx_gpu_assets', fx_graph: 'spectrafx_vfx_graph',
};
const panelForWindow = new Map<string,StudioPanel>([
  ...Object.entries(windowForPanel).map(([panel,id]) => [id,panel as StudioPanel] as const),
  ['spectrafx_graph','fx_graph'], ['lens_cameras','actor'], ['tools','scene'], ['studio_camera_bookmarks','scene'],
  ['FpsWindow','actor'], ['ModelingWindow','modeling'], ['AnimationWindow','model_anim'],
]);

export function extensionPanelFor(id: string): StudioPanel | null {
  return panelForWindow.get(id) ?? (Object.hasOwn(STUDIO_PANEL_TITLES,id) ? id as StudioPanel : null);
}
export function extensionWindowFor(panel: StudioPanel): string {
  return windowForPanel[panel] ?? panel;
}
