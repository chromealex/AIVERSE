/**
 * Legacy (ImGui) editor windows that have no HTML5 implementation yet.
 * Source of truth: docs/studio-html5-parity.md. Shown disabled in Window menu
 * so the gap is visible; remove an entry when its HTML5 panel ships.
 */
export const LEGACY_WINDOWS_NOT_MIGRATED: ReadonlyArray<{ id: string; title: string }> = [
  { id: 'agent', title: 'AI Agent' }, { id: 'animation_assets', title: 'Animation Assets' },
  { id: 'animation_graph', title: 'Animation Graph' }, { id: 'animation_rig', title: 'Animation Rig' },
  { id: 'animation_attachments', title: 'Attachments' }, { id: 'AnimationWindow', title: 'Model Animation' },
  { id: 'branding', title: 'Branding' }, 
  { id: 'effects', title: 'Effects (FX)' }, { id: 'fields', title: 'Fields' }, { id: 'fog', title: 'Fog' },
  { id: 'FpsWindow', title: 'FPS Authoring' }, { id: 'generate', title: 'Generate' },
  { id: 'input', title: 'Input' }, { id: 'lightbake', title: 'Light Bake' },
  { id: 'models', title: 'Models' }, { id: 'ModelingWindow', title: 'Modeling' },
  { id: 'overlays', title: 'Overlays' }, { id: 'physis', title: 'PHYSIS' }, { id: 'postfx', title: 'PRISMA Post FX' },
  { id: 'project_import', title: 'Import Project' }, { id: 'studio_asset_audit', title: 'Project Audit' },
  { id: 'raid', title: 'Raid' }, { id: 'roads', title: 'Roads' },
  { id: 'scripts', title: 'Scripts' }, { id: 'spectrafx_gpu_assets', title: 'SpectraFX GPU Assets' },
  { id: 'spectrafx_graph', title: 'SpectraFX Graph' }, { id: 'stats', title: 'Statistics' },
  { id: 'studio_camera_bookmarks', title: 'Camera Bookmarks' }, { id: 'studio_extensions', title: 'Extensions' },
  { id: 'studio_frame_capture', title: 'Frame Capture' }, { id: 'studio_snapshot_diff', title: 'Snapshot Diff' },
  { id: 'terrain', title: 'Terrain' }, { id: 'texture_resources', title: 'Texture Resources' },
  { id: 'tiles', title: 'Tiles' }, { id: 'tools', title: 'Tools' }, { id: 'water', title: 'Water' },
];
