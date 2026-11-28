import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import { StudioButton, StudioSection } from '../ui';

const fields = [
  { key: 'damage', label: 'Damage', fallback: 25 },
  { key: 'magazine', label: 'Magazine capacity', fallback: 30 },
] as const;
type Weapon = { nameKey?: string; damage?: number; magazine?: number };
type Patch = Partial<Pick<Weapon, 'damage' | 'magazine'>>;
const section = 'actorLoadout';

export function ActorLoadout({ project }: { project: string }) {
  const [original, setOriginal] = useState('');
  const [patches, setPatches] = useState<Patch[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [message, setMessage] = useState('');
  const [revision, setRevision] = useState(0);
  const doc: { weapons?: Weapon[]; weapon?: Weapon } = original ? JSON.parse(original) : {};
  const weapons = original ? doc.weapons ?? [doc.weapon ?? {}] : [];
  const dirty = patches.some(patch => Object.keys(patch).length > 0);

  useEffect(() => {
    let live = true;
    setBusy(true);
    setError('');
    setMessage('');
    invoke('studio.rulesDocument', { section }).then(({ text }) => {
      if (live) { setOriginal(text); setPatches([]); }
    }).catch(e => { if (live) setError(String(e)); })
      .finally(() => { if (live) setBusy(false); });
    return () => { live = false; };
  }, [project, revision]);

  async function save() {
    if (!dirty || busy) return;
    setBusy(true);
    setError('');
    try {
      const slots = weapons.map((_, i) => patches[i] ?? {});
      const { text } = await invoke('studio.rulesDocumentSave', { section, projectPath: project, original, value: { slots } });
      setOriginal(text);
      setPatches([]);
      setMessage('Loadout saved.');
    } catch (e) { setError(String(e)); }
    finally { setBusy(false); }
  }

  return <form className="actor-simulation" aria-label="Actor loadout" onSubmit={e => { e.preventDefault(); void save(); }}>
    {weapons.map((weapon, index) => <StudioSection key={index} title={`Slot ${index + 1} · ${(weapon?.nameKey ?? 'actor.weapon.rifle').split('.').pop()}`}>
      {fields.map(({ key, label, fallback }) => {
        const baseline = weapon?.[key] ?? fallback;
        const value = patches[index]?.[key] ?? baseline;
        return <label key={key}>
          <span>{label}<output>{value}</output></span>
          <input aria-label={`Slot ${index + 1} ${label}`} type="range" min={1} max={1000} value={value} disabled={busy}
            onChange={event => {
              const next = [...patches];
              const patch = { ...next[index] };
              const number = event.target.valueAsNumber;
              if (number === baseline) delete patch[key]; else patch[key] = number;
              next[index] = patch;
              setPatches(next);
              setMessage('');
            }}/>
        </label>;
      })}
    </StudioSection>)}
    {busy && !original && <p role="status">Reading loadout…</p>}
    {error && <p role="alert">{error}</p>}
    {message && <p role="status">{message}</p>}
    <div className="insp-map__actions">
      <StudioButton type="submit" variant="primary" disabled={busy || !dirty}>Save loadout</StudioButton>
      <StudioButton disabled={busy} onClick={() => setRevision(n => n + 1)}>{dirty ? 'Discard and reload' : 'Reload'}</StudioButton>
    </div>
  </form>;
}
