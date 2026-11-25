import { useEffect, useRef, useState } from 'react';
import { invoke } from '../bridge';
import type { StudioResult, StudioSnapshot, WizardPlanInput } from '../types';
import { StudioLoader } from '../ui';

export default function GameWizard({ onOpened, onBusy }: { onOpened: (snapshot: StudioSnapshot) => void; onBusy: (busy: boolean) => void }) {
  const [creating, setCreating] = useState(false);
  const [options, setOptions] = useState<StudioResult['studio.wizardOptions']>();
  const [draft, setDraft] = useState<WizardPlanInput>();
  const [plan, setPlan] = useState<StudioResult['studio.wizardPlan']>();
  const [error, setError] = useState('');
  const [running, setRunning] = useState(false);
  const version = useRef(0);
  useEffect(() => {
    let active = true;
    void invoke('studio.wizardOptions', {}).then(catalog => {
      if (active) { setOptions(catalog); setDraft(catalog.defaults); }
    }).catch(e => { if (active) setError(String(e)); });
    return () => { active = false; version.current++; };
  }, []);
  function edit<K extends keyof WizardPlanInput>(key: K, value: WizardPlanInput[K]) {
    version.current++;
    setDraft(current => current && { ...current, [key]: value });
    setPlan(undefined); setError(''); setRunning(false);
  }
  async function preview() {
    if (!draft) return;
    const request = ++version.current;
    setRunning(true); setError('');
    try {
      const next = await invoke('studio.wizardPlan', draft);
      if (request === version.current) setPlan(next);
    } catch (e) { if (request === version.current) setError(String(e)); }
    finally { if (request === version.current) setRunning(false); }
  }
  async function chooseArtwork() {
    try {
      const { dir } = await invoke('studio.wizardArt', {});
      if (dir) edit('artDir', dir);
    } catch (e) { setError(String(e)); }
  }
  async function create() {
    if (!draft || !plan || creating) return;
    setCreating(true); onBusy(true); setError('');
    try {
      const result = await invoke('studio.wizardCreate', draft);
      if (result.snapshot) onOpened(result.snapshot);
    } catch (e) { setError(String(e)); }
    finally { setCreating(false); onBusy(false); }
  }
  if (!draft || !options) return error ? <p role="alert">{error}</p> : <StudioLoader label="Loading game wizard…" size={18}/>;
  return <div className="hub__workspace wizard">
    <form className="wizard__form" onSubmit={event => { event.preventDefault(); void preview(); }}>
      <fieldset className="wizard__fields" disabled={creating}><h3>Game concept</h3>
      <label>Game title<input className="au-input" required value={draft.title} onChange={e => edit('title', e.target.value)}/></label>
      <label>Game idea<textarea rows={3} placeholder="Describe what the player does" value={draft.idea} onChange={e => edit('idea', e.target.value)}/></label>
      <fieldset className="wizard__art"><legend>Artwork, models and audio</legend>
        {draft.artDir && <p>{draft.artDir}</p>}
        <button type="button" className="au-button au-button--sm" onClick={() => void chooseArtwork()}>Choose artwork folder…</button>
        {draft.artDir && <button type="button" className="au-button au-button--sm" onClick={() => edit('artDir','')}>Clear folder</button>}
      </fieldset>
      <label>Starting template<select value={draft.genre} onChange={e => edit('genre', e.target.value)}>
        <option value="auto">Match my idea and mechanics</option>
        {options.templates.map(t => <option key={t.id} value={t.id}>{t.name}</option>)}
      </select></label>
      <fieldset><legend>Mechanics</legend><div className="wizard__features">
        {options.features.map(f => <button type="button" key={f.id} title={f.description} aria-pressed={draft.features.includes(f.id)}
          onClick={() => edit('features', draft.features.includes(f.id) ? draft.features.filter(id => id !== f.id) : [...draft.features, f.id])}>{f.id}</button>)}
      </div></fieldset>
      <div className="wizard__grid">
        <label>Orientation<select value={draft.orientation} onChange={e => edit('orientation', e.target.value)}>
          {['auto','portrait','landscape'].map(id => <option key={id}>{id}</option>)}</select></label>
        <label>Tutorial<select value={draft.tutorial} onChange={e => edit('tutorial', e.target.value)}>
          {options.tutorialModes.map(id => <option key={id}>{id}</option>)}</select></label>
        <label>Level count<input className="au-input" type="number" min="0" max="40" required value={Number.isFinite(draft.levels) ? draft.levels : ''} onChange={e => edit('levels', e.target.valueAsNumber)}/></label>
        <label>Map preset<select value={draft.mapPreset} onChange={e => edit('mapPreset', e.target.value)}>
          {options.mapPresets.map(id => <option key={id}>{id}</option>)}</select></label>
      </div>
      <button className="au-button au-button--primary" disabled={running || !draft.title.trim()}>{running && <StudioLoader size={14}/>}Preview game plan</button>
      </fieldset>
      {error && <p className="hub__error" role="alert">{error}</p>}
    </form>
    <aside className="hub__details" aria-live="polite"><h3>{plan?.templateName || 'Game plan'}</h3>
      {plan ? <><p>{plan.reason}</p><dl className="wizard__result">
        <dt>Orientation</dt><dd>{plan.orientation}</dd><dt>Tutorial type</dt><dd>{plan.tutorialArchetype}</dd>
        <dt>Levels</dt><dd>{plan.levels}</dd><dt>Map preset</dt><dd>{plan.levelPreset}</dd>
        <dt>Meta features</dt><dd>{plan.meta.join(', ') || 'None'}</dd>
      </dl>
      <label className="wizard__verify"><input type="checkbox" checked={draft.verifyLevels} disabled={creating}
        onChange={e => setDraft({ ...draft, verifyLevels: e.target.checked })}/>Verify levels with bots</label>
      <button type="button" disabled={creating} className="au-button au-button--primary" onClick={() => void create()}>
        {creating && <StudioLoader size={14}/>}Create game in folder…</button></> : <p>Preview the plan to see the matching template, tutorial and levels.</p>}
    </aside>
  </div>;
}
