import { useEffect, useRef, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioSnapshot } from '../types';
import { StudioAlert, StudioButton, StudioField, StudioSection, StudioTextarea } from '../ui';

/** General manifest authoring. Workspace owns validation, persistence and Project history. */
export default function ProjectSettingsEditor({ project, disabled, updateSnapshot }: {
  project: string; disabled: boolean; updateSnapshot: (snapshot: StudioSnapshot) => void;
}) {
  const generation = useRef(0);
  const pending = useRef(false);
  const [busy, setBusy] = useState(false);
  const [original, setOriginal] = useState<string | null>(null);
  const [baseline, setBaseline] = useState('');
  const [text, setText] = useState('');
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  const dirty = original !== null && text !== baseline;
  useEffect(() => () => { ++generation.current; }, []);
  const run = async (save: boolean) => {
    if (pending.current || disabled) return;
    pending.current = true;
    const token = ++generation.current;
    setBusy(true); setError(''); setStatus('');
    try {
      if (save) {
        const value: unknown = JSON.parse(text);
        if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('Project settings must be a JSON object.');
        await invoke('studio.op', { op: 'set_manifest', expectedProject: project,
          params: { path: '', valueJson: text, expectedManifest: original } });
        if (token !== generation.current) return;
        setStatus('Saved. Reloading settings…');
      }
      const response = await invoke('studio.op', { op: 'game_info', expectedProject: project, params: {} });
      if (token !== generation.current) return;
      const result = response.result as { originalManifest?: unknown };
      if (typeof result.originalManifest !== 'string') throw new Error('The project reader returned no manifest.');
      const formatted = result.originalManifest;
      setOriginal(result.originalManifest); setBaseline(formatted); setText(formatted);
      setStatus(save ? 'Saved and reloaded.' : 'Loaded.');
      const snapshot = await invoke('studio.snapshot', {});
      if (token === generation.current && snapshot.project.path === project) updateSnapshot(snapshot);
    } catch (e) {
      if (token === generation.current) setError(e instanceof Error ? e.message : String(e));
    } finally {
      if (token === generation.current) { pending.current = false; setBusy(false); }
    }
  };
  return <StudioSection title="Project settings">
    <div className="au-stack">
      <div className="au-row">
        <StudioButton disabled={disabled || busy || dirty} onClick={() => void run(false)}>Load settings</StudioButton>
        <StudioButton variant="primary" disabled={disabled || busy || !dirty} onClick={() => void run(true)}>Save settings</StudioButton>
        <StudioButton disabled={disabled || busy || !dirty} onClick={() => { setText(baseline); setError(''); setStatus('Draft discarded.'); }}>Discard draft</StudioButton>
      </div>
      {original !== null && <StudioField label="game.json" htmlFor="project-settings-json">
        <StudioTextarea id="project-settings-json" rows={16} spellCheck={false} value={text} disabled={disabled || busy} onChange={e => setText(e.target.value)}/>
      </StudioField>}
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {status && <StudioAlert>{status}</StudioAlert>}
      <StudioAlert>Save applies project settings immediately. Use History → Project to undo compatible settings edits. Changing the rules source or module requires reopening the project; those edits and map-list changes are history barriers. Restart the game to apply runtime settings.</StudioAlert>
    </div>
  </StudioSection>;
}
