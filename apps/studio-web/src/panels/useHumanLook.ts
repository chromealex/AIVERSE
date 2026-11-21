// Shared state + native calls for editing an imported character's look
// (VIVID Appearance window and the Human Creator "model" source).
import { useCallback, useEffect, useRef, useState } from 'react';
import type { HumanFaceShape, HumanLook, HumanLookInfo } from '../types';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';

export const emptyLook = (): HumanLook => ({ version: 1, values: {}, materials: {} });

export interface Snapshot { look: HumanLook; face: HumanFaceShape | null }

export function useHumanLook(project: string) {
  const [info, setInfo] = useState<HumanLookInfo | null>(null);
  const [face, setFace] = useState<HumanFaceShape | null>(null);
  const [draft, setDraft] = useState<HumanLook>(emptyLook);
  const [guid, setGuid] = useState('');
  const [saved, setSaved] = useState<string[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  // One history for look AND face shape (blend, proportions, sculpt).
  const [undo, setUndo] = useState<Snapshot[]>([]);
  const [redo, setRedo] = useState<Snapshot[]>([]);
  const lastRecord = useRef(0);
  const current = useRef<Snapshot>({ look: emptyLook(), face: null });
  const generation = useRef(0);

  useEffect(() => {
    const gen = ++generation.current;
    setInfo(null); setFace(null); setGuid(''); setDraft(emptyLook()); setUndo([]); setRedo([]);
    if (!project || !nativeConnected()) return;
    setBusy(true);
    void Promise.all([invoke('studio.humanAppearanceInfo', {}), invoke('studio.humanAppearanceList', {})])
      .then(([i, list]) => { if (gen !== generation.current) return; setInfo(i); setFace(i.face); setDraft(i.look); setGuid(i.preview.guid === 'vivid-creator' ? '' : i.preview.guid); setSaved(list.looks); setError(''); })
      .catch(e => { if (gen === generation.current) setError(String(e)); })
      .finally(() => { if (gen === generation.current) setBusy(false); });
    return () => { ++generation.current; };
  }, [project]);

  current.current = { look: draft, face };
  // Slider drags fire many edits: one history step per gesture (500 ms).
  const record = useCallback(() => {
    const now = Date.now();
    if (now - lastRecord.current > 500) { const snap = current.current; setUndo(u => [...u.slice(-63), snap]); setRedo([]); }
    lastRecord.current = now;
  }, []);
  const edit = useCallback((next: HumanLook) => {
    record(); setDraft(next); setStatus('Есть неприменённые изменения');
  }, [record]);
  const editFace = useCallback((next: HumanFaceShape | null) => { record(); setFace(next); }, [record]);
  /** Returns the restored state so the caller can re-apply it to the preview. */
  const undoLook = (): Snapshot | null => {
    if (!undo.length) return null; const snap = undo[undo.length - 1];
    setRedo(r => [...r, current.current]); setUndo(undo.slice(0, -1)); setDraft(snap.look); setFace(snap.face); lastRecord.current = 0; return snap;
  };
  const redoLook = (): Snapshot | null => {
    if (!redo.length) return null; const snap = redo[redo.length - 1];
    setUndo(u => [...u, current.current]); setRedo(redo.slice(0, -1)); setDraft(snap.look); setFace(snap.face); lastRecord.current = 0; return snap;
  };

  const run = useCallback(async (action: () => Promise<void>) => {
    setBusy(true); setError('');
    try { await action(); } catch (e) { setError(String(e)); } finally { setBusy(false); }
  }, []);
  const loadAsset = () => run(async () => {
    const result = await invoke('studio.humanAppearanceFromAsset', { guid });
    edit(result.look); setStatus('Профиль загружен — нажмите «Показать»');
  });
  const apply = (nextFace: HumanFaceShape | null = face, look: HumanLook = draft) => run(async () => {
    const current = await invoke('studio.humanAppearanceInfo', {});
    const result = await invoke('studio.humanAppearanceApply', { guid, revision: current.preview.revision, look, face: nextFace });
    setInfo(result); setDraft(result.look); setStatus('Применено');
  });
  // Live preview while a slider is dragged: never toggles busy (that would
  // disable the slider mid-drag) and coalesces to the latest value while the
  // native side is still applying the previous one.
  const liveJob = useRef<{ busy: boolean; next: { look: HumanLook; face: HumanFaceShape | null } | null }>({ busy: false, next: null });
  const live = useCallback((look: HumanLook, nextFace: HumanFaceShape | null) => {
    const job = liveJob.current;
    if (job.busy) { job.next = { look, face: nextFace }; return; }
    job.busy = true;
    void (async () => {
      let item: { look: HumanLook; face: HumanFaceShape | null } | null = { look, face: nextFace };
      while (item) {
        try {
          const current = await invoke('studio.humanAppearanceInfo', {});
          setInfo(await invoke('studio.humanAppearanceApply', { guid, revision: current.preview.revision, look: item.look, face: item.face }));
        } catch { /* the committed apply reports errors */ }
        item = job.next; job.next = null;
      }
      job.busy = false;
    })();
  }, [guid]);
  const refresh = useCallback(async () => {
    const next = await invoke('studio.humanAppearanceInfo', {});
    setInfo(next); setFace(next.face);
    return next;
  }, []);

  // Native preview status polling while a model is bound.
  useEffect(() => {
    if (!info?.preview.bound || !project || !nativeConnected()) return;
    const gen = generation.current;
    const timer = setInterval(() => {
      void invoke('studio.humanAppearanceInfo', {}).then(next => {
        if (gen !== generation.current) return;
        setInfo(old => old?.preview.revision === next.preview.revision ? next : old);
      }).catch(() => {});
    }, 1500);
    return () => clearInterval(timer);
  }, [project, info?.preview.revision, info?.preview.bound]);

  // Viewport sculpt strokes change the native face shape: pull it back.
  useEffect(() => onNativeEvent((event, payload) => {
    if (event === 'human.sculpt' && (payload as { hit?: boolean } | null)?.hit) { record(); void refresh().catch(() => {}); }
  }), [refresh, record]);

  return { info, setInfo, face, setFace, draft, setDraft, edit, guid, setGuid, saved, setSaved, busy, error, setError,
    status, setStatus, undo, redo, undoLook, redoLook, run, loadAsset, apply, live, refresh, editFace };
}
export type HumanLookState = ReturnType<typeof useHumanLook>;
