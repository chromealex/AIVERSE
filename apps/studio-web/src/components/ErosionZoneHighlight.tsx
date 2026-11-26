import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';

export default function ErosionZoneHighlight({ scope }: { scope: StudioGraphDocumentScope }) {
  const { projectPath, mapPath, epoch, revision } = scope;
  const [names, setNames] = useState<string[]>([]);
  const [index, setIndex] = useState(-2);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  useEffect(() => {
    let active = true;
    invoke('studio.viewportInput', { kind: 'erosionHighlight', projectPath, mapPath, epoch, revision })
      .then(result => {
        if (active) { setNames(result.zones ?? []); setIndex(result.highlight ?? -2); }
      }).catch(reason => { if (active) setError(String(reason)); });
    return () => { active = false; };
  }, [projectPath, mapPath, epoch, revision]);

  async function change(next: number) {
    setBusy(true);
    try {
      const result = await invoke('studio.viewportInput', { kind: 'erosionHighlight', ...scope, index: next });
      setIndex(result.highlight ?? -2);
      setError('');
    } catch (reason) { setError(String(reason)); }
    finally { setBusy(false); }
  }

  return <>
    {names.length > 0 && <label className="scene-toolbar__zone">Highlight
      <select aria-label="Highlight erosion zone" value={index} disabled={busy}
        onChange={event => void change(Number(event.currentTarget.value))}>
        <option value={-2}>None</option><option value={-1}>All zones</option>
        {names.map((name, ordinal) => <option key={ordinal} value={ordinal}>{name || `Zone ${ordinal + 1}`}</option>)}
      </select>
    </label>}
    {error && <small role="alert">{error}</small>}
  </>;
}
