import type { StudioInputAsset, StudioInputTrigger } from '../types';

const kinds = ['down', 'pressed', 'released', 'hold', 'tap', 'doubleTap', 'holdAndRelease', 'pulse', 'chord', 'notChord'] as const;
const timed = new Set(['hold', 'tap', 'doubleTap', 'holdAndRelease', 'pulse']);

export default function InputTriggers({items = [], actions, onChange}: {
  items?: StudioInputTrigger[];
  actions: StudioInputAsset['actions'];
  onChange: (items: StudioInputTrigger[]) => void;
}) {
  const replace = (index: number, trigger: StudioInputTrigger) =>
    onChange(items.map((current, i) => i === index ? trigger : current));
  return <details className="input-triggers">
    <summary>Triggers ({items.length})</summary>
    {items.map((trigger, i) => <fieldset key={i}>
      <legend>Trigger {i + 1}</legend>
      <label>Type <select className="au-input" value={trigger.type} onChange={e => {
        const type = e.target.value as StudioInputTrigger['type'];
        if (timed.has(type)) replace(i, {type, seconds: 0.25});
        else if (type === 'chord' || type === 'notChord') replace(i, {type, action: actions[0]?.name});
        else replace(i, {type});
      }}>{kinds.map(kind => <option key={kind}>{kind}</option>)}</select></label>
      {timed.has(trigger.type) && <label>Seconds <input className="au-input" type="number"
        min={trigger.type === 'pulse' ? 0.01 : 0} max={120} step="any" value={trigger.seconds}
        onChange={e => replace(i, {...trigger, seconds: Number(e.target.value)})}/></label>}
      {(trigger.type === 'chord' || trigger.type === 'notChord') && <label>Referenced action
        <select className="au-input" value={trigger.action} onChange={e => replace(i, {...trigger, action: e.target.value})}>
          {actions.map((action, n) => <option key={n}>{action.name}</option>)}
        </select></label>}
      <button type="button" className="au-button au-button--sm" onClick={() => onChange(items.filter((_, n) => n !== i))}>Remove trigger</button>
    </fieldset>)}
    <button type="button" className="au-button au-button--sm" disabled={items.length >= 16}
      onClick={() => onChange([...items, {type: 'down'}])}>Add trigger</button>
  </details>;
}
