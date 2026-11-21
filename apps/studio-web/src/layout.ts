/** HTML5 Studio window layout only; native scene/project state stays in C++. */
export type StudioEditorTab = 'scene' | 'game' | 'materials' | 'animation';
export type StudioSidebarTab = 'hierarchy' | 'assets';
export interface StudioLayout {
  selectedTab: StudioEditorTab;
  leftTab: StudioSidebarTab;
  leftSize: number;
  rightSize: number;
  showConsole: boolean;
}
export const DEFAULT_STUDIO_LAYOUT: StudioLayout = {
  selectedTab: 'scene',
  leftTab: 'hierarchy',
  leftSize: 260,
  rightSize: 300,
  showConsole: true
};
const KEY = 'aiverse.studio.layout.v1';

export function parseStudioLayout(input: unknown): StudioLayout {
  if (input === null || typeof input !== 'object' || Array.isArray(input)) return { ...DEFAULT_STUDIO_LAYOUT };
  const v = input as Record<string, unknown>;
  const width = (value: unknown, fallback: number) =>
    typeof value === 'number' && Number.isFinite(value) ? Math.round(Math.min(520, Math.max(200, value))) : fallback;
  const tab: StudioEditorTab = v.selectedTab === 'scene' || v.selectedTab === 'game' ||
    v.selectedTab === 'materials' || v.selectedTab === 'animation' ? v.selectedTab : DEFAULT_STUDIO_LAYOUT.selectedTab;
  const sidebar: StudioSidebarTab = v.leftTab === 'hierarchy' || v.leftTab === 'assets'
    ? v.leftTab : DEFAULT_STUDIO_LAYOUT.leftTab;
  return {
    selectedTab: tab,
    leftTab: sidebar,
    leftSize: width(v.leftSize, DEFAULT_STUDIO_LAYOUT.leftSize),
    rightSize: width(v.rightSize, DEFAULT_STUDIO_LAYOUT.rightSize),
    showConsole: typeof v.showConsole === 'boolean' ? v.showConsole : DEFAULT_STUDIO_LAYOUT.showConsole
  };
}
export function readStudioLayout(): StudioLayout {
  if (typeof window === 'undefined') return { ...DEFAULT_STUDIO_LAYOUT };
  try {
    const serialized = window.localStorage.getItem(KEY);
    return serialized ? parseStudioLayout(JSON.parse(serialized) as unknown) : { ...DEFAULT_STUDIO_LAYOUT };
  } catch { return { ...DEFAULT_STUDIO_LAYOUT }; }
}
export function saveStudioLayout(layout: StudioLayout): void {
  if (typeof window === 'undefined') return;
  try { window.localStorage.setItem(KEY, JSON.stringify(parseStudioLayout(layout))); }
  catch { /* Layout persistence is optional (private browser / restricted WebView). */ }
}
