import type { StudioResult, WizardPlanInput } from '../types';

export const wizardPages = ['Concept', 'Project', 'Levels', 'Economy', 'Distribution'] as const;
export type WizardPage = typeof wizardPages[number];
type Keys<T> = { [K in keyof WizardPlanInput]: WizardPlanInput[K] extends T ? K : never }[keyof WizardPlanInput];
interface Props {
  page: WizardPage; draft: WizardPlanInput; options: StudioResult['studio.wizardOptions'];
  edit: <K extends keyof WizardPlanInput>(key: K, value: WizardPlanInput[K]) => void;
}

export function GameWizardSettings({ page, draft, options, edit }: Props) {
  const number = (key: Keys<number>, label: string, min: number, max: number, disabled = false) =>
    <label>{label}<input className="au-input" type="number" min={min} max={max} required disabled={disabled}
      value={Number.isFinite(draft[key]) ? draft[key] : ''} onChange={e => edit(key, e.target.valueAsNumber)}/></label>;
  const toggle = (key: Keys<boolean>, label: string, disabled = false) =>
    <label className="wizard__check"><input type="checkbox" checked={draft[key]} disabled={disabled}
      onChange={e => edit(key, e.target.checked)}/>{label}</label>;
  const select = (key: Keys<string>, label: string, values: string[]) =>
    <label>{label}<select value={draft[key]} onChange={e => edit(key, e.target.value)}>
      {values.map(v => <option key={v}>{v}</option>)}</select></label>;
  const multi = (key: 'platforms' | 'languages' | 'stores', label: string, values: string[]) =>
    <fieldset><legend>{label}</legend><div className="wizard__features">
      {values.map(id => <button key={id} type="button" aria-pressed={draft[key].includes(id)}
        disabled={key === 'languages' && draft.languages.length === 1 && draft.languages[0] === id}
        onClick={() => edit(key, draft[key].includes(id) ? draft[key].filter(value => value !== id) : [...draft[key], id])}>{id}</button>)}
    </div></fieldset>;
  if (page === 'Project') return <>
    <h3>Project settings</h3><div className="wizard__grid">
      <label>Studio name<input className="au-input" value={draft.studio} onChange={e => edit('studio', e.target.value)}/></label>
      <label>Game version<input className="au-input" value={draft.version} onChange={e => edit('version', e.target.value)}/></label>
    </div>
    {multi('languages', 'Game languages', options.languages)}
    <small>Primary language: {draft.languages[0]}</small>
    {multi('platforms', 'Target platforms', options.platforms)}
    {number('seed', 'Generation seed (0 for automatic)', -2147483648, 2147483647)}
    {toggle('brandingFromArt', 'Use artwork for the icon and splash')}
    {toggle('paletteFromArt', 'Use artwork colors in menus')}
  </>;
  if (page === 'Levels') return <>
    <h3>Levels and tutorial</h3><div className="wizard__grid">
      {number('levels', 'Level count', 0, 40)}
      {select('mapPreset', 'Map preset', options.mapPresets)}
      {select('mapSize', 'Map size', options.mapSizes)}
      {select('difficultyCurve', 'Difficulty', ['easy','normal','hard'])}
      {number('bossEvery', 'Boss interval (levels)', 0, 40)}
      {number('startResourcesPercent', 'Starting resources (%)', 10, 1000)}
    </div>
    {toggle('keepTemplateMaps', 'Keep template maps for skirmishes')}
    {select('tutorial', 'Tutorial', options.tutorialModes)}
    {toggle('tutorialForced', 'Start with the tutorial', ['soft','none'].includes(draft.tutorial))}
    {toggle('tutorialSkippable', 'Allow skipping the tutorial', ['soft','none'].includes(draft.tutorial))}
    {toggle('tutorialDim', 'Dim the area outside the tutorial target', ['soft','none'].includes(draft.tutorial))}
    {toggle('hints', 'Show hints during matches', ['hard','none'].includes(draft.tutorial))}
  </>;
  if (page === 'Economy') return <>
    <h3>Progression and rewards</h3>
    <label>Meta game idea<textarea rows={3} value={draft.metaIdea} onChange={e => edit('metaIdea', e.target.value)}/></label>
    <div className="wizard__grid">{['Reward currency', 'Premium currency'].map((label, i) =>
      <label key={label}>{label}<input className="au-input" pattern="\S+" value={draft.currencies[i] ?? ''}
        onChange={e => { const values = [...draft.currencies]; values[i] = e.target.value; edit('currencies', values); }}/></label>)}
    </div>
    {number('xpPerLevel', 'XP per level', 10, 100000)}
    {toggle('upgrades', 'Upgrades')}{toggle('shop', 'Shop')}{toggle('daily', 'Daily rewards')}{toggle('profileScreen', 'Player profile')}
  </>;
  if (page === 'Distribution') return <>
    <h3>Ads and purchases</h3>
    {toggle('ads', 'Enable ads')}{number('interstitialEvery', 'Matches between interstitials', 0, 100, !draft.ads)}
    {toggle('purchases', 'In-app purchases')}
    {multi('stores', 'Stores', options.stores)}
  </>;
  return null;
}
