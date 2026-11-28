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
    const saved = JSON.parse(store?.getItem(key(project)) ?? '[]');
    const raw = Array.isArray(saved) ? saved : saved?.views;
    if (!Array.isArray(raw)) return [];
    return raw.filter(m => m && CAMERA_SLOTS.includes(m.slot) && ['x', 'y', 'z', 'distance', 'yaw', 'pitch'].every(k => finite(m[k])))
      .map(m => ({ slot: m.slot, name: String(m.name ?? 'View ' + m.slot).slice(0, 40), x: m.x, y: m.y, z: m.z,
        distance: m.distance, yaw: m.yaw, pitch: m.pitch,
        ...(finite(m.fov) && m.fov > 0 && m.fov < Math.PI ? { fov: m.fov } : {}), ortho: m.ortho === true }))
      .sort((a, b) => a.slot - b.slot);
  } catch { return []; }
}
function write(project: string, list: CameraBookmark[], store: Storage | null) {
  if (!project || !store) throw Error('Camera bookmark storage is unavailable.');
  const raw = store.getItem(key(project));
  let saved;
  try { saved = JSON.parse(raw ?? 'null'); } catch { /* replace invalid settings */ }
  store.setItem(key(project), JSON.stringify(saved?.importedV1 ? { importedV1: true, views: list } : list));
  return list;
}
export function importBookmarks(project: string, legacy: CameraBookmark[], store: Storage | null = storage()): CameraBookmark[] {
  const views = readBookmarks(project, store);
  if (!project || !store || !legacy.length) return views;
  try {
    if (JSON.parse(store.getItem(key(project)) ?? 'null')?.importedV1) return views;
    const occupied = new Set(views.map(view => view.slot));
    const conflicts = legacy.filter(view => occupied.has(view.slot));
    const imported = [...views, ...legacy.filter(view => !occupied.has(view.slot))];
    const free = CAMERA_SLOTS.filter(slot => !imported.some(view => view.slot === slot));
    conflicts.slice(0, free.length).forEach((view, index) => imported.push({ ...view, slot: free[index] }));
    imported.sort((a, b) => a.slot - b.slot);
    store.setItem(key(project), JSON.stringify({ importedV1: true, views: imported }));
    return imported;
  } catch { return views; }
}
export function saveBookmark(project: string, mark: CameraBookmark, store: Storage | null = storage()): CameraBookmark[] {
  return write(project, [...readBookmarks(project, store).filter(m => m.slot !== mark.slot), mark].sort((a, b) => a.slot - b.slot), store);
}
export function removeBookmark(project: string, slot: number, store: Storage | null = storage()): CameraBookmark[] {
  return write(project, readBookmarks(project, store).filter(m => m.slot !== slot), store);
}
