import { copyText } from '../clipboard';
import { useEffect, useMemo, useRef, useState } from 'react';
import {
  Box, ChevronDown, ChevronRight, Info, Lightbulb, Lock, LockOpen, MoreVertical, Move3d, Puzzle, Search, Shield, User
} from 'lucide-react';
import type { StudioComponentCatalogItem, StudioInspectorComponent, StudioProperty } from '../types';
import { invoke } from '../bridge';
import PropertyField from '../components/PropertyField';
import GranularGridEditor from '../components/GranularGridEditor';
import { StudioContextMenu, StudioEmptyState, StudioIconButton, StudioLoader, type StudioMenuItem } from '../ui';
import { canRemove, canReset, copyValues, mergeInspectors, missingOption, pasteValues, resetValues, type CopiedComponent, type FieldValue } from './inspectorActions';
import type { StudioInspector } from '../types';
import { fuzzyScore } from '../commandSearch';
import type { StudioPanelContext } from './StudioPanelRenderer';
import './InspectorPanel.css';

const ICONS: Record<string, typeof Box> = {
  Transform: Move3d, MeshRenderer: Box, Light: Lightbulb, Unit: User,
  BoxCollider: Shield, OrientedBoxCollider: Shield, SpawnPoint: User
};
const COLLAPSE_KEY = 'aiverse.studio.inspector.collapsed';
// Copy/Paste values clipboard shared by every Inspector card (like Unity).
let componentClipboard: CopiedComponent | null = null;
const readCollapsed = (): Record<string, boolean> => {
  try { return JSON.parse(window.localStorage.getItem(COLLAPSE_KEY) ?? '{}') as Record<string, boolean>; }
  catch { return {}; }
};

/** Unity-like Transform rows: Position, Rotation and Scale, each X Y Z. */
const VECTOR_ROWS: Array<{ label: string; keys: Record<string, string> }> = [
  { label: 'Position', keys: { x: 'X', y: 'Y', height: 'Y', z: 'Z' } },
  { label: 'Rotation', keys: { rotationX: 'X', rotationY: 'Y', rotationZ: 'Z', yaw: 'Y', facing: 'Y' } },
  { label: 'Scale', keys: { scaleX: 'X', scaleY: 'Y', scaleZ: 'Z', scale: 'XYZ' } },
];
function splitVectors(properties: StudioProperty[]) {
  const used = new Set<StudioProperty>();
  const rows = VECTOR_ROWS.map(row => {
    const fields = properties.filter(p => p.type === 'number' && p.key in row.keys);
    fields.forEach(field => used.add(field));
    return { label: row.label, fields: fields.map(field => ({ field, axis: row.keys[field.key] })) };
  }).filter(row => row.fields.length > 0);
  // Only treat as vectors when this looks like a transform (has a position).
  if (!rows.some(row => row.label === 'Position' && row.fields.length >= 2)) return { rows: [], rest: properties };
  return { rows, rest: properties.filter(p => !used.has(p)) };
}

function ComponentCard({ component, entityId, busy, collapsed, onToggle, onChange, onMenu, onCreateMaterial }: {
  component: StudioInspectorComponent; entityId: string; busy: boolean; collapsed: boolean;
  onToggle: () => void; onChange: (key: string, value: FieldValue) => void;
  onMenu: (x: number, y: number) => void; onCreateMaterial: (name: string) => void;
}) {
  const Icon = ICONS[component.id] ?? Puzzle;
  const { rows, rest } = splitVectors(component.properties);
  const menuButton = useRef<HTMLButtonElement | null>(null);
  return <section className={'insp-comp' + (collapsed ? ' is-collapsed' : '')} aria-label={component.title}
    onContextMenu={event => { if ((event.target as HTMLElement).closest('.insp-comp__headrow')) { event.preventDefault(); onMenu(event.clientX, event.clientY); } }}>
    <div className="insp-comp__headrow">
      <button type="button" className="insp-comp__head" aria-expanded={!collapsed} onClick={onToggle}>
        {collapsed ? <ChevronRight size={12}/> : <ChevronDown size={12}/>}
        <Icon size={13} className="insp-comp__icon"/>
        <span className="insp-comp__title">{component.title}</span>
        {component.note && <span className="insp-comp__note" title={component.note}><Info size={12}/></span>}
      </button>
      <button type="button" ref={menuButton} className="insp-comp__menu" aria-label={component.title + ' menu'} aria-haspopup="menu"
        onClick={() => { const r = menuButton.current?.getBoundingClientRect(); if (r) onMenu(r.right - 180, r.bottom + 2); }}>
        <MoreVertical size={13}/></button>
    </div>
    {!collapsed && <div className="insp-comp__body">
      {component.id === 'Unit' && typeof component.properties.find(p => p.key === 'type')?.value === 'string' &&
        <button type="button" className="insp-link" title="Open this unit type in the Rules panel"
          onClick={() => { const type = String(component.properties.find(p => p.key === 'type')?.value);
            window.dispatchEvent(new CustomEvent('studio:show-panel', { detail: 'rules' }));
            window.setTimeout(() => window.dispatchEvent(new CustomEvent('studio:rules-type', { detail: type })), 0); }}>
          Edit type “{String(component.properties.find(p => p.key === 'type')?.value)}” in Rules →</button>}
      {component.id === 'GranularField' && <GranularGridEditor
        properties={component.properties} disabled={busy}
        onCommit={serialized => onChange('grid', serialized)}/>}
      {rows.map(row => <div key={row.label} className="insp-row insp-row--vector"
        title={row.fields.map(({field}) => field.description).filter(Boolean).join('\n') || undefined}>
        <span className="insp-row__label">{row.label}</span>
        <div className="insp-vector">
          {row.fields.map(({field, axis}) => <PropertyField key={entityId + component.id + field.key} variant="axis"
            className={'insp-axis--' + axis}
            property={{ ...field, label: axis, description: undefined }}
            disabled={busy || field.editable === false} onChange={value => onChange(field.key, value)}/>)}
        </div>
      </div>)}
      {rest.filter(property => component.id !== 'GranularField' ||
        !['width','height','mass','solid'].includes(property.key)).map(property => {
        const missing = missingOption(property);
        const shown = missing ? { ...property, options: [...(property.options ?? []), { label: String(property.value) + ' (missing)', value: String(property.value) }] } : property;
        return <div key={entityId + component.id + property.key} className={missing ? 'insp-field is-missing' : 'insp-field'}>
          <PropertyField property={shown} disabled={busy || property.editable === false} onChange={value => onChange(property.key, value)}/>
          {missing && <div className="insp-missing" role="alert">Not in materials.json ·
            <button type="button" disabled={busy} onClick={() => onCreateMaterial(String(property.value))}>Create “{String(property.value)}”</button>·
            <button type="button" disabled={busy} onClick={() => onChange(property.key, '')}>Use default</button></div>}
        </div>;
      })}
      {component.properties.length === 0 && <p className="insp-comp__hint">No editable fields.</p>}
    </div>}
  </section>;
}

/** Add Component: one search over engine components for this entity; unavailable ones explain why. */
function AddComponent({ entityId, busy, onPick }: { entityId: string; busy: boolean; onPick: (item: StudioComponentCatalogItem) => void }) {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [items, setItems] = useState<StudioComponentCatalogItem[]>([]);
  const [active, setActive] = useState(0);
  const root = useRef<HTMLDivElement | null>(null);
  useEffect(() => {
    if (!open) return;
    let live = true;
    setQuery(''); setActive(0);
    void invoke('studio.componentCatalog', { entityId }).then(list => { if (live) setItems(list); }).catch(() => { if (live) setItems([]); });
    const close = (event: MouseEvent) => { if (!root.current?.contains(event.target as Node)) setOpen(false); };
    window.addEventListener('mousedown', close);
    return () => { live = false; window.removeEventListener('mousedown', close); };
  }, [open, entityId]);
  const shown = useMemo(() => items
    .map(item => ({ item, s: Math.max(fuzzyScore(query, item.title), fuzzyScore(query, item.group) - 100) }))
    .filter(x => x.s >= 0).sort((a, b) => Number(b.item.available) - Number(a.item.available) || b.s - a.s).map(x => x.item), [items, query]);
  const groups = [...new Set(shown.map(i => i.group))];
  const pick = (item: StudioComponentCatalogItem | undefined) => { if (item?.available) { setOpen(false); onPick(item); } };
  let index = -1;
  return <div className="insp-add" ref={root}>
    <button type="button" className="insp-add__button" disabled={busy} aria-expanded={open} onClick={() => setOpen(v => !v)}>+ Add Component</button>
    {open && <div className="insp-add__pop" role="dialog" aria-label="Add Component"
      onKeyDown={event => {
        if (event.key === 'Escape') setOpen(false);
        else if (event.key === 'ArrowDown') { event.preventDefault(); setActive(a => Math.min(shown.length - 1, a + 1)); }
        else if (event.key === 'ArrowUp') { event.preventDefault(); setActive(a => Math.max(0, a - 1)); }
        else if (event.key === 'Enter') { event.preventDefault(); pick(shown[active]); }
      }}>
      <label className="insp-add__search"><Search size={13} aria-hidden="true"/>
        <input autoFocus value={query} placeholder="Search components" aria-label="Search components"
          onChange={event => { setQuery(event.target.value); setActive(0); }}/></label>
      {shown.length === 0 && <div className="insp-add__empty">No components match.</div>}
      {groups.map(group => <div key={group} role="group" aria-label={group}>
        <div className="insp-add__group">{group}</div>
        {shown.filter(i => i.group === group).map(item => { index += 1; const i = index;
          return <button key={item.id} type="button" className="insp-add__item" data-active={i === active}
            aria-disabled={!item.available} onMouseMove={() => setActive(i)} onClick={() => pick(item)}>
            <span><b>{item.title}</b><small>{item.available ? item.hint : item.reason ?? item.hint}</small></span>
          </button>; })}
      </div>)}
    </div>}
  </div>;
}

/** Unity-style Inspector: entity header + its document components. */
export default function InspectorPanel({ inspector: primary, selected, busy, inspectorPinned, toggleInspectorPin,
  changeProperty, setBusy, commitSnapshot, showPanel, reportError, snapshot }: StudioPanelContext) {
  // Multi-selection: read every selected object and show their common components.
  const ids = !inspectorPinned && snapshot?.selectedIds && snapshot.selectedIds.length > 1 ? snapshot.selectedIds.slice(-50) : null;
  const [others, setOthers] = useState<StudioInspector[] | null>(null);
  const idsKey = ids?.join('|') ?? '';
  useEffect(() => {
    if (!ids) { setOthers(null); return; }
    let live = true;
    void Promise.all(ids.map(entityId => invoke('studio.inspect', { entityId }))).then(list => { if (live) setOthers(list); })
      .catch(() => { if (live) setOthers(null); });
    return () => { live = false; };
  }, [idsKey, snapshot]); // eslint-disable-line react-hooks/exhaustive-deps
  const multi = !!ids && !!others && others.length > 1;
  const inspector = multi ? mergeInspectors(others!) : primary;
  const targetIds = multi ? ids! : null;
  const [collapsed, setCollapsed] = useState(readCollapsed);
  const [menu, setMenu] = useState<{ x: number; y: number; component: StudioInspectorComponent } | null>(null);
  const run = async (action: () => Promise<import('../types').StudioSnapshot>) => {
    setBusy(true);
    try { commitSnapshot(await action()); }
    catch (e) { reportError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const setMany = (component: StudioInspectorComponent, values: Record<string, FieldValue> | null) => {
    if (!inspector || !values || !Object.keys(values).length) return;
    void run(() => invoke('studio.setProperties', targetIds
      ? { entityIds: targetIds, componentId: component.id, values }
      : { entityId: inspector.id, componentId: component.id, values }));
  };
  const change = (component: StudioInspectorComponent, key: string, value: FieldValue) => {
    if (targetIds) setMany(component, { [key]: value });
    else void changeProperty(key, value, component.id);
  };
  const menuItems = (component: StudioInspectorComponent): StudioMenuItem[] => [
    { id: 'reset', label: 'Reset', disabled: busy || !canReset(component), onSelect: () => setMany(component, resetValues(component)) },
    { id: 'copy', label: 'Copy Values', separator: true, onSelect: () => {
      componentClipboard = copyValues(component);
      void copyText(JSON.stringify(componentClipboard));
    } },
    { id: 'paste', label: 'Paste Values', disabled: busy || !pasteValues(componentClipboard, component),
      onSelect: () => setMany(component, pasteValues(componentClipboard, component)) },
    { id: 'remove', label: 'Remove Component', danger: true, separator: true, disabled: busy || !inspector || !!targetIds || !canRemove(component),
      onSelect: () => { if (inspector) void run(() => invoke('studio.componentRemove', { entityId: inspector.id, componentId: component.id })); } },
  ];
  const addComponent = (item: StudioComponentCatalogItem) => {
    if (!inspector) return;
    if (item.action === 'native') void run(() => invoke('studio.componentAdd', { entityId: inspector.id, componentId: item.id }));
    else if (item.action === 'water') showPanel('nereid_flow');
    else if (item.action === 'light') {
      const t = inspector.components.find(c => c.id === 'Transform')?.properties ?? [];
      const num = (key: string) => { const v = t.find(p => p.key === key)?.value; return typeof v === 'number' ? v : undefined; };
      void run(async () => { await invoke('studio.lightAdd', { x: num('x'), z: num('z') }); return invoke('studio.snapshot', {}); });
    }
  };
  const createMaterial = (name: string) => {
    void run(async () => { await invoke('studio.materialCreate', { name, preset: 'matte' }); return invoke('studio.snapshot', {}); });
  };
  const [name, setName] = useState(inspector?.name ?? '');
  useEffect(() => setName(inspector?.name ?? ''), [inspector?.id, inspector?.name]);
  useEffect(() => {
    try { window.localStorage.setItem(COLLAPSE_KEY, JSON.stringify(collapsed)); } catch { /* storage optional */ }
  }, [collapsed]);
  const commitName = () => {
    if (inspector?.nameEditable && name.trim() && name !== inspector.name)
      void changeProperty('name', name.trim(), 'Entity');
    else setName(inspector?.name ?? '');
  };
  const lock = <StudioIconButton className="insp__lock"
    aria-label={inspectorPinned ? 'Unlock Inspector' : 'Lock Inspector to current object'}
    title={inspectorPinned ? 'Unlock Inspector' : 'Lock Inspector to current object'}
    aria-pressed={inspectorPinned} disabled={!selected && !inspectorPinned} onClick={toggleInspectorPin}
    icon={inspectorPinned ? <Lock size={12}/> : <LockOpen size={12}/>}/>;
  return <div className="insp">
    {!inspector ? <div className="insp__empty">
      {inspectorPinned && <div className="insp__empty-bar">{lock}</div>}
      <StudioEmptyState icon={selected ? <StudioLoader size={30}/> : undefined} title={selected ? 'Loading…' : 'Nothing selected'}
        description={selected ? 'Reading entity components.' : 'Select an entity in the Hierarchy or Scene.'}/>
    </div> : <div className="insp__scroll">
      <div className="insp__header">
        <Box size={16} className="insp__header-icon"/>
        <input className="au-input insp__name" aria-label="Entity name" value={name}
          readOnly={!inspector.nameEditable} disabled={busy}
          title={inspector.nameEditable ? 'Rename' : 'This entity is named by its type'}
          onChange={event => setName(event.target.value)} onBlur={commitName}
          onKeyDown={event => { if (event.key === 'Enter') event.currentTarget.blur();
            if (event.key === 'Escape') { setName(inspector.name); event.currentTarget.blur(); } }}/>
        {lock}
      </div>
      <div className="insp__meta"><span>{inspector.kind}</span><code title="Document entity ID">{inspector.id}</code></div>
      {inspector.components.filter(c=>c.id==='EvolveRegion').map(component=><EcologyPresetEditor key="evolve-preset" busy={busy} apply={values=>setMany(component,values)}/>)}
      {inspector.components.map(component => <ComponentCard key={inspector.id + ':' + component.id}
        component={component} entityId={inspector.id} busy={busy}
        collapsed={!!collapsed[component.id]}
        onToggle={() => setCollapsed(current => ({ ...current, [component.id]: !current[component.id] }))}
        onChange={(key, value) => change(component, key, value)}
        onMenu={(x, y) => setMenu({ x, y, component })} onCreateMaterial={createMaterial}/>)}
      {multi ? <p className="insp-comp__hint insp-multi-hint">Editing {ids!.length} objects · values that differ show “—”.</p>
        : <AddComponent entityId={inspector.id} busy={busy} onPick={addComponent}/>}
    </div>}
    {menu && <StudioContextMenu x={menu.x} y={menu.y} label={menu.component.title} items={menuItems(menu.component)} onClose={() => setMenu(null)}/>}
  </div>;
}

const ECO_KEYS=['durability','fertility','succession','pollution'] as const;
const ECO_BUILTINS:Record<string,number[]>={healthy:[100,90,80,0],industrial:[70,20,20,80],recovering:[80,35,10,50],desert:[85,10,10,0],young_forest:[90,70,20,0],old_growth:[100,95,95,0]};
function validateEcoPresets(input:unknown):Record<string,number[]>{
  if(!input||typeof input!=='object'||Array.isArray(input))throw new Error('Invalid preset document');
  const entries=Object.entries(input);
  if(entries.length>128)throw new Error('Too many presets');
  const out:Record<string,number[]>=Object.create(null);
  for(const [name,values] of entries){
    if(!/^[a-zA-Z0-9_-]{1,48}$/.test(name)||Object.prototype.hasOwnProperty.call(ECO_BUILTINS,name)||!Array.isArray(values)||values.length!==4||!values.every(v=>typeof v==='number'&&Number.isFinite(v)&&v>=0&&v<=100))throw new Error('Invalid preset: '+name);
    out[name]=values;
  }
  return out;
}
function EcologyPresetEditor({busy,apply}:{busy:boolean;apply:(values:Record<string,FieldValue>)=>void}){
  const [custom,setCustom]=useState<Record<string,number[]>>(()=>{try{return validateEcoPresets(JSON.parse(localStorage.getItem('aiverse.evolve.presets')??'{}'));}catch{return {};}});
  const [name,setName]=useState('');
  const [values,setValues]=useState([100,50,0,0]);
  const [error,setError]=useState('');
  const save=(next:Record<string,number[]>)=>{try{const checked=validateEcoPresets(next);localStorage.setItem('aiverse.evolve.presets',JSON.stringify(checked));setCustom(checked);setError('');}catch(e){setError(String(e));}};
  const all={...ECO_BUILTINS,...custom};
  return <div className="insp-comp__hint"><label className="ptp__row"><span>Ecology preset</span><select className="au-input" disabled={busy} value="" onChange={e=>{
    const p=all[e.target.value];if(p)apply(Object.fromEntries(ECO_KEYS.map((k,i)=>[k,Math.round(p[i]*1e7)])));
  }}><option value="">Apply preset…</option>{Object.keys(all).map(p=><option key={p} value={p}>{p}</option>)}</select></label>
  <details><summary>Custom presets</summary><input className="au-input" value={name} placeholder="Preset name" onChange={e=>setName(e.target.value)}/>
    {ECO_KEYS.map((k,i)=><label key={k} className="ptp__row">{k}<input type="number" min={0} max={100} value={values[i]} onChange={e=>setValues(old=>old.map((v,j)=>j===i?Number(e.target.value):v))}/></label>)}
    <button type="button" onClick={()=>save({...custom,[name]:values})}>Save locally</button>
    <button type="button" onClick={()=>{const next={...custom};delete next[name];save(next);}}>Delete named preset</button>
    <button type="button" onClick={()=>{const url=URL.createObjectURL(new Blob([JSON.stringify(custom,null,2)],{type:'application/json'}));const a=document.createElement('a');a.href=url;a.download='evolve-presets.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}}>Export</button>
    <label>Import JSON<input type="file" accept=".json,application/json" onChange={async e=>{try{const file=e.target.files?.[0];if(!file)return;if(file.size>65536)throw new Error('Preset file too large');save({...custom,...validateEcoPresets(JSON.parse(await file.text()))});}catch(err){setError(String(err));}}}/></label>
    {error&&<span role="alert">{error}</span>}
  </details></div>;
}
