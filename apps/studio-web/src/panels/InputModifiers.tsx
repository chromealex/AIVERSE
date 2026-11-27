import type { StudioInputModifier } from '../types';

const axes = ['x', 'y', 'z'] as const;
const kinds = ['scale', 'negate', 'deadzone', 'swizzle', 'normalize'] as const;

export default function InputModifiers({ items = [], onChange }: {
  items?: StudioInputModifier[];
  onChange: (items: StudioInputModifier[]) => void;
}) {
  const replace = (index: number, item: StudioInputModifier) =>
    onChange(items.map((old, i) => i === index ? item : old));
  return <details className="input-modifiers">
    <summary>Modifiers ({items.length})</summary>
    {items.map((item, i) => <fieldset key={i}>
      <legend>Modifier {i + 1}</legend>
      <label>Type <select className="au-input" value={item.type} onChange={e => {
        const type = e.target.value as StudioInputModifier['type'];
        replace(i, type === 'swizzle' ? { type, order: [0, 1, 2] } : { type });
      }}>{kinds.map(kind => <option key={kind}>{kind}</option>)}</select></label>
      {item.type === 'scale' && axes.map(axis => <label key={axis}>{axis.toUpperCase()}
        <input className="au-input" type="number" min={-10000} max={10000} step="any" value={item[axis] ?? 1}
          onChange={e => replace(i, { ...item, [axis]: Number(e.target.value) })}/>
      </label>)}
      {item.type === 'deadzone' && <label>Threshold {item.threshold ?? 0.1}
        <input type="range" min={0} max={0.999} step={0.001} value={item.threshold ?? 0.1}
          onChange={e => replace(i, { ...item, threshold: Number(e.target.value) })}/>
      </label>}
      {item.type === 'swizzle' && axes.map((axis, n) => <label key={axis}>Source for {axis.toUpperCase()}
        <select className="au-input" value={item.order?.[n] ?? n} onChange={e =>
          replace(i, { ...item, order: (item.order ?? [0, 1, 2]).map((v, j) => j === n ? Number(e.target.value) : v) })}>
          {axes.map((name, j) => <option key={name} value={j}>{name.toUpperCase()}</option>)}
        </select>
      </label>)}
      <button type="button" className="au-button au-button--sm" onClick={() => onChange(items.filter((_, j) => i !== j))}>Remove modifier</button>
    </fieldset>)}
    <button type="button" className="au-button au-button--sm" disabled={items.length >= 16}
      onClick={() => onChange([...items, { type: 'scale' }])}>Add modifier</button>
  </details>;
}
