/** Pure browser presentation contract for the native Scene viewport. */
export type StudioSceneMode = 'welcome' | 'renderer' | 'demo';

/** Avoid showing an unauthored default MapDoc as a loaded user project. */
export function studioSceneMode(nativeConnected: boolean, projectPath?: string | null): StudioSceneMode {
  if (!nativeConnected) return 'demo';
  return typeof projectPath === 'string' && projectPath.trim().length > 0 ? 'renderer' : 'welcome';
}
