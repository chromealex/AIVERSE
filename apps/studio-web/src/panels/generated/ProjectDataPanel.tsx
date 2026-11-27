import { useEffect, useRef, useState } from 'react';
import type { StudioSnapshot } from '../../types';
import { invoke } from '../../bridge';
import { StudioPanel, StudioField, StudioInput, StudioTextarea, StudioButton, StudioAlert, StudioEmptyState } from '../../ui';
import './ProjectDataPanel.css';

export default function ProjectDataPanel({ snapshot }: { snapshot: StudioSnapshot | null }) {
  const project = snapshot?.project.path ?? '';
  const generation = useRef(0);
  const [path, setPath] = useState('');
  const [loadedPath, setLoadedPath] = useState('');
  const [text, setText] = useState('');
  const [original, setOriginal] = useState('');
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  const dirty = Boolean(loadedPath) && text !== original;
  useEffect(() => {
    ++generation.current;
    setPath(''); setLoadedPath(''); setText(''); setOriginal(''); setBusy(false); setError(''); setStatus('');
    return () => { ++generation.current; };
  }, [project]);
  const run = async (save: boolean) => {
    const token = ++generation.current;
    const target = save ? loadedPath : path.trim();
    const candidate = text;
    setBusy(true); setError(''); setStatus('');
    try {
      if (save) await invoke('studio.op', { op: 'game_file_write', expectedProject: project,
        params: { path: target, text: candidate, expectedText: original } });
      const reply = await invoke('studio.op', { op: 'game_file_read', expectedProject: project, params: { path: target } });
      if (token !== generation.current) return;
      const result = reply.result as { text?: unknown };
      if (typeof result.text !== 'string') throw new Error('The file reader returned no text.');
      setLoadedPath(target); setPath(target); setText(result.text); setOriginal(result.text);
      setStatus(save ? 'Saved and reloaded.' : 'Loaded.');
    } catch (e) {
      if (token === generation.current) setError(e instanceof Error ? e.message : String(e));
    } finally { if (token === generation.current) setBusy(false); }
  };
  return <StudioPanel className="studio-panel-project_data" title="Project Data" subtitle={snapshot?.project.name ?? 'No project open'}>
    {!project ? <StudioEmptyState title="Open a project" description="Edit existing project text files after opening a project."/> : <div className="au-stack">
      <StudioField label="Project-relative file" htmlFor="project-data-path" hint="For example audio.json or lang/en.json. Use the dedicated editors for rules, game settings and maps.">
        <StudioInput id="project-data-path" value={path} disabled={busy || dirty} onChange={e => setPath(e.target.value)}/>
      </StudioField>
      <div className="au-row">
        <StudioButton disabled={busy || dirty || !path.trim()} onClick={() => void run(false)}>Load</StudioButton>
        <StudioButton variant="primary" disabled={busy || !dirty} onClick={() => void run(true)}>Save</StudioButton>
        <StudioButton disabled={busy || !dirty} onClick={() => { setText(original); setError(''); setStatus('Draft discarded.'); }}>Discard draft</StudioButton>
      </div>
      {loadedPath && <StudioField label={loadedPath} htmlFor="project-data-text">
        <StudioTextarea id="project-data-text" value={text} rows={18} spellCheck={false} disabled={busy} onChange={e => setText(e.target.value)}/>
      </StudioField>}
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {status && <StudioAlert>{status}</StudioAlert>}
      <StudioAlert>Save changes the file immediately. JSON syntax is checked before writing. File edits are outside Map and Project Undo; discard an unsaved draft here. Reload the game to apply saved data.</StudioAlert>
    </div>}
  </StudioPanel>;
}
