// Reference photo drawn semi-transparently over the Scene viewport while the
// artist blends or sculpts a face "to the person" (Character Creator).
export interface ReferenceView { src: string; opacity: number; scale: number; x: number; y: number; mirror: boolean; visible: boolean }
let state: ReferenceView = { src: '', opacity: .4, scale: .6, x: 0, y: 0, mirror: false, visible: true };
const listeners = new Set<(s: ReferenceView) => void>();
export function referenceView(): ReferenceView { return state; }
export function setReferenceView(patch: Partial<ReferenceView>): void { state = { ...state, ...patch }; for (const l of listeners) l(state); }
export function onReferenceView(listener: (s: ReferenceView) => void): () => void { listeners.add(listener); return () => { listeners.delete(listener); }; }
