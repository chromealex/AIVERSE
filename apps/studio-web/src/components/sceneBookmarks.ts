/** Scene camera bookmarks, per project (browser storage; editor state, not game data). */
export interface CameraBookmark { slot: number; name: string; x: number; y: number; z: number; distance: number; yaw: number; pitch: number; fov?: number; ortho: boolean }
export const CAMERA_SLOTS = [1, 2, 3, 4, 5, 6, 7, 8, 9];
type Storage = Pick<globalThis.Storage, 'getItem' | 'setItem'>;
const key = (project: string) => 'aiverse.studio.cameraMarks.' + project;
function storage(): Storage | null { try { return typeof window === 'undefined' ? null : window.localStorage; } catch { return null; } }
const finite = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);
export function readBookmarks(project: string, store: Storage | null = storage()): CameraBookmark[] {
  if (!project) return [];
  try {
    const raw = JSON.parse(store?.getItem(key(project)) ?? '[]');
    if (!Array.isArray(raw)) return [];
    return raw.filter(m => m && CAMERA_SLOTS.includes(m.slot) && ['x', 'y', 'z', 'distance', 'yaw', 'pitch'].every(k => finite(m[k])))
      .map(m => ({ slot: m.slot, name: String(m.name ?? 'View ' + m.slot).slice(0, 40), x: m.x, y: m.y, z: m.z,
        distance: m.distance, yaw: m.yaw, pitch: m.pitch,
        ...(finite(m.fov) && m.fov > 0 && m.fov < Math.PI ? { fov: m.fov } : {}), ortho: m.ortho === true }))
      .sort((a, b) => a.slot - b.slot);
  } catch { return []; }
}
function write(project: string, list: CameraBookmark[], store: Storage | null) {
  try { if (project) store?.setItem(key(project), JSON.stringify(list)); } catch { /* quota */ }
  return list;
}
export function saveBookmark(project: string, mark: CameraBookmark, store: Storage | null = storage()): CameraBookmark[] {
  return write(project, [...readBookmarks(project, store).filter(m => m.slot !== mark.slot), mark].sort((a, b) => a.slot - b.slot), store);
}
export function removeBookmark(project: string, slot: number, store: Storage | null = storage()): CameraBookmark[] {
  return write(project, readBookmarks(project, store).filter(m => m.slot !== slot), store);
}
