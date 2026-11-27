import { useEffect, useMemo, useRef, useState } from 'react';
import { RotateCcw, Search } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioButton, StudioField, StudioSelect } from '../ui';
import type { StudioRules } from '../types';
import { RULE_GROUPS, parseRuleInput, typeValues, type RuleValue } from './rulesSchema';
import { fuzzyScore } from '../commandSearch';
import './RulesPanel.css';
import PlanetForestPanel from './PlanetForestPanel';

/** Rules: unit types as a form of known fields (validated by the game loader on every write). */
export default function RulesPanel({ projectPath, historyVersion, onChanged, openActorSettings }: { projectPath: string; openActorSettings?: () => void; historyVersion?: string; onChanged: () => void }) {
  const [forestOpen,setForestOpen]=useState(false);
  const [rules, setRules] = useState<StudioRules | null>(null);
  const [typeId, setTypeId] = useState('');
  const [query, setQuery] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [section,setSection]=useState<'types'|'triggers'|'damageTable'|'global'|'document'>(openActorSettings ? 'document' : 'types');
  const [raw, setRaw] = useState<string | null>(null);
 const syntaxError=useMemo(()=>{
  if(raw===null)return '';
  try{JSON.parse(raw);return '';}catch(e){return String(e);}
 },[raw]);
  const typeDraftDirty = section === 'types' && raw !== null && raw !== JSON.stringify(rules?.types[typeId], null, 2);
  const draftProject=useRef<string|null>(null);
  useEffect(()=>{
    draftProject.current=raw!==null&&(section==='global'||section==='document') ? projectPath : null;
  },[raw,section]);
  useEffect(() => {
    let live = true;
    if(draftProject.current!==projectPath){
      setRules(null); setError(''); setRaw(null);
    }
    if (!projectPath) return;
    void invoke('studio.rules', {}).then(r => { if (live) { setRules(r); setTypeId(id => id && id in r.types ? id : Object.keys(r.types)[0] ?? ''); } })
      .catch(e => { if (live) setError(e instanceof Error ? e.message : String(e)); });
    return () => { live = false; };
  }, [projectPath, historyVersion]);
  const ids = useMemo(() => {
    const all = Object.keys(rules?.types ?? {});
    if (!query.trim()) return all;
    return all.map(id => ({ id, s: Math.max(fuzzyScore(query, id), fuzzyScore(query, String((rules!.types[id] as { name?: unknown })?.name ?? ''))) }))
      .filter(x => x.s >= 0).sort((a, b) => b.s - a.s).map(x => x.id);
  }, [rules, query]);
  // Inspector → "Edit type in Rules" selects that type.
  useEffect(() => {
    const pick = (e: Event) => { const id = String((e as CustomEvent<unknown>).detail ?? ''); if (id && !typeDraftDirty && !busy) { setSection('types');setForestOpen(false);setTypeId(id); setQuery(''); setRaw(null); } };
    const forest=()=>{if (!typeDraftDirty && !busy) setForestOpen(true);};
    window.addEventListener('studio:rules-type', pick);
    window.addEventListener('studio:rules-forest',forest);
    return () => {window.removeEventListener('studio:rules-type', pick);window.removeEventListener('studio:rules-forest',forest);};
  }, [typeDraftDirty,busy]);
  const values = useMemo(() => rules && typeId ? typeValues(typeId, rules.types[typeId], rules.resolved[typeId], rules.resources, rules.bodies?.[typeId]) : [], [rules, typeId]);
  const write = async (path: string, value: unknown) => {
    setBusy(true); setError('');
    try { setRules(await invoke('studio.rulesSet', { path, value })); onChanged(); return true; }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); return false; }
    finally { setBusy(false); }
  };
  const commit = (v: RuleValue, input: string | boolean) => {
    const parsed = parseRuleInput(v.field, input);
    if (!parsed.ok) { setError(v.field.label + ': ' + parsed.error); return; }
    if (parsed.value === null && v.own === undefined) return;
    if (parsed.value === v.own) return;
    void write(v.path, parsed.value);
  };
  if (!projectPath) return <div className="rules__empty">Open a project to edit its rules.</div>;
  const extendsBase = typeId && rules ? (rules.types[typeId] as { extends?: unknown })?.extends : undefined;
  return <div className="rules-container"><div className="rules">
    <aside className="rules__list">
      {openActorSettings && <div className="rules__section">
        <strong>Actor settings</strong>
        <span>Simulation, movement and loadout</span>
        <StudioButton onClick={openActorSettings}>Open Actors &amp; Cameras</StudioButton>
      </div>}
      <label className="rules__section">Rule section
        <select value={section} disabled={busy||typeDraftDirty||(section!=='types'&&raw!==null)} onChange={event=>{
          setForestOpen(false);setSection(event.target.value as typeof section); setRaw(null);setError('');
        }}>
          <option value="types">Unit types</option><option value="triggers">Triggers</option>
          <option value="damageTable">Damage matrix</option><option value="global">Global settings</option>
          <option value="document">Complete rules.json</option>
        </select>
      </label>
      {section==='types'&&<>
      <button type="button" className={'rules__type'+(forestOpen?' is-active':'')} disabled={busy||typeDraftDirty} onClick={()=>setForestOpen(true)}>Planet forest</button>
      {rules && <StudioField label="Auto-target order" htmlFor="rules-acquire-order">
        <StudioSelect id="rules-acquire-order" value={rules.acquireOrder} disabled={busy}
          onChange={e => void write('targeting.acquireOrder', e.target.value)}>
          <option value="nearest">Nearest</option>
          <option value="rowMajor">By map row</option>
        </StudioSelect>
      </StudioField>}
      <label className="rules__search"><Search size={12} aria-hidden="true"/>
        <input value={query} onChange={e => setQuery(e.target.value)} placeholder="Search types" aria-label="Search unit types"/></label>
      <div role="listbox" aria-label="Unit types" className="rules__types">
        {ids.map(id => { const name = String((rules!.types[id] as { name?: unknown })?.name ?? id);
          return <button key={id} type="button" role="option" disabled={busy||typeDraftDirty} aria-selected={id === typeId} draggable
            title="Drag into the Scene to place a new object of this type"
            onDragStart={e => { e.dataTransfer.setData('text/plain', 'aiverse-type:' + id); e.dataTransfer.effectAllowed = 'copy'; }}
            className={'rules__type' + (id === typeId && !forestOpen ? ' is-active' : '')} onClick={() => { setForestOpen(false);setTypeId(id); setRaw(null); }}>
            <b>{name}</b>{name !== id && <small>{id}</small>}</button>; })}
        {rules && !ids.length && <p className="rules__empty">No types.</p>}
      </div>
      </>}
    </aside>
    {forestOpen ? <PlanetForestPanel key={projectPath} historyVersion={historyVersion} onChanged={onChanged}/> : <section className="rules__form" aria-label={section==='types'?'Type '+typeId:section}>
      {syntaxError&&section!=='global'&&section!=='document'&&<div className="rules__error" role="alert">{syntaxError}</div>}
      {error && <div className="rules__error" role="alert">{error}<button type="button" aria-label="Dismiss" onClick={() => setError('')}>×</button></div>}
      {!rules ? <p className="rules__empty">Loading rules…</p> : section==='global'||section==='document' ? <RulesDocumentEditor key={projectPath+section}
        section={section} projectPath={projectPath} syntaxError={syntaxError} raw={raw} setRaw={setRaw} onChanged={async()=>{
          setRules(await invoke('studio.rules',{})); onChanged();
        }}/> : section!=='types' ? <>
        <h3>{section==='triggers'?'Triggers':'Damage matrix'}</h3>
        {section==='damageTable'&&!!rules.armorTypes?.length&&<div className="rules__matrix"><table>
          <thead><tr><th>Attack / armor</th>{rules.armorTypes.map(armor=><th key={armor}>{armor}</th>)}</tr></thead>
          <tbody>{Object.entries(rules.damageTable??{}).map(([attack,row])=><tr key={attack}>
            <th scope="row">{attack}</th>{rules.armorTypes!.map(armor=>{
              const multiplier = typeof row?.[armor] === 'number' ? row[armor] : 1;
              const effect = multiplier > 1.001 ? 'increased' : multiplier < 0.999 ? 'reduced' : 'normal';
              return <td key={armor} data-effect={effect}>{multiplier}×</td>;
            })}
          </tr>)}</tbody>
        </table></div>}
        <div className="rules__rawbox">
          <textarea disabled={busy} aria-invalid={!!syntaxError} aria-label={section==='triggers'?'Trigger definitions':'Damage multipliers'} spellCheck={false}
            value={raw??JSON.stringify(rules[section]??(section==='triggers'?[]:{}),null,2)}
            onChange={event=>{setError('');setRaw(event.target.value);}}/>
          <button disabled={busy||raw===null||!!syntaxError} onClick={()=>{
            try{const value=JSON.parse(raw!);void write(section,value).then(saved=>{if(saved)setRaw(null);});}
            catch(e){setError(String(e));}
          }}>Validate &amp; save</button>
          {raw!==null&&<button disabled={busy} onClick={()=>{setRaw(null);setError('');}}>Discard changes</button>}
        </div>
      </> : !typeId ? <p className="rules__empty">rules.json has no types.</p> : <>
        <div className="rules__head">
          <h3>{typeId}</h3>
          {typeof extendsBase === 'string' && <span className="rules__extends">extends {extendsBase}</span>}
          <span className="rules__spacer"/>
          <button type="button" className="rules__raw-toggle" disabled={busy||typeDraftDirty} onClick={() => setRaw(r => r === null ? JSON.stringify(rules.types[typeId], null, 2) : null)}>
            {raw === null ? 'Edit JSON' : 'Form'}</button>
        </div>
        <p className="rules__note">Changes are validated by the game loader and saved to rules.json at once. Blank = inherit / default.</p>
        {raw !== null ? <div className="rules__rawbox">
          <textarea disabled={busy} aria-invalid={!!syntaxError} value={raw} onChange={e => {setError('');setRaw(e.target.value);}} spellCheck={false} aria-label={'JSON of ' + typeId}/>
          <button type="button" disabled={busy||!typeDraftDirty||!!syntaxError} onClick={() => { try { void write('types.' + typeId, JSON.parse(raw)).then(saved=>{if(saved)setRaw(null);}); } catch (e) { setError('JSON: ' + (e instanceof Error ? e.message : String(e))); } }}>Validate &amp; save</button>
          {typeDraftDirty&&<button type="button" disabled={busy} onClick={()=>{setRaw(null);setError('');}}>Discard changes</button>}
        </div> : RULE_GROUPS.map(group => {
          const rows = values.filter(v => v.field.group === group);
          if (!rows.length) return null;
          return <fieldset key={group} className="rules__group"><legend>{group}</legend>
            {rows.map(v => <RuleRow key={v.path} value={v} busy={busy} onCommit={input => commit(v, input)} onReset={() => void write(v.path, null)}/>)}
          </fieldset>;
        })}
      </>}
    </section>}
  </div></div>;
}

function RulesDocumentEditor({section,projectPath,syntaxError,raw,setRaw,onChanged}:{
 section:'global'|'document';projectPath:string;syntaxError:string;raw:string|null;setRaw:(v:string|null)=>void;onChanged:()=>Promise<void>;
}){
 const [original,setOriginal]=useState<string|null>(null);
 const [busy,setBusy]=useState(false);
 const [error,setError]=useState('');
 const [revision,reload]=useState(0);
 useEffect(()=>{
  let live=true;
  setOriginal(null);setError('');
  invoke('studio.rulesDocument',{section}).then(r=>{if(live)setOriginal(r.text);})
   .catch(e=>{if(live)setError(String(e));});
  return ()=>{live=false;};
 },[section,revision]);
 async function save(){
  if(original===null||raw===null||busy||syntaxError)return;
  setBusy(true);setError('');
  try{
   const result=await invoke('studio.rulesDocumentSave',{section,projectPath,original,value:JSON.parse(raw)});
   setOriginal(result.text);setRaw(null);await onChanged();
  }catch(e){setError(String(e));}finally{setBusy(false);}
 }
 const title=section==='global'?'Global settings':'Complete rules.json';
 return <div className="rules__rawbox"><h3>{title}</h3>
  {(syntaxError||error)&&<div role="alert" className="rules__error">{syntaxError||error}</div>}
  {original!==null&&<textarea value={raw??original} aria-label={title} spellCheck={false}
   disabled={busy} aria-invalid={!!syntaxError} onChange={e=>{setError('');setRaw(e.target.value===original?null:e.target.value);}}/>}
  <button disabled={busy||raw===null||!!syntaxError} onClick={()=>void save()}>Validate &amp; save</button>
  <button disabled={busy} onClick={()=>{setRaw(null);reload(v=>v+1);}}>{raw===null?'Reload':'Discard & reload'}</button>
 </div>;
}

function RuleRow({ value: v, busy, onCommit, onReset }: { value: RuleValue; busy: boolean; onCommit: (input: string | boolean) => void; onReset: () => void }) {
  const shown = v.own ?? v.resolved ?? v.field.defaultValue;
  const [draft, setDraft] = useState(v.own === undefined ? '' : String(v.own));
  useEffect(() => setDraft(v.own === undefined ? '' : String(v.own)), [v.own]);
  const id = 'rule-' + v.path.replace(/[^a-z0-9]/gi, '-');
  return <div className={'rules__row' + (v.inherited ? ' is-inherited' : '')}>
    <label htmlFor={id}>{v.field.label}{v.field.hint && <small> · {v.field.hint}</small>}</label>
    {v.field.type === 'boolean'
      ? <input id={id} type="checkbox" checked={Boolean(shown)} disabled={busy} onChange={e => onCommit(e.target.checked)}/>
      : <input id={id} className="au-input" type={v.field.type === 'number' ? 'number' : 'text'} value={draft} disabled={busy}
          step={v.field.step ?? 'any'} min={v.field.min} placeholder={shown === undefined ? 'default' : String(shown) + (v.inherited ? ' (inherited)' : '')}
          onChange={e => setDraft(e.target.value)} onBlur={() => onCommit(draft)}
          onKeyDown={e => { if (e.key === 'Enter') e.currentTarget.blur(); if (e.key === 'Escape') { setDraft(v.own === undefined ? '' : String(v.own)); } }}/>}
    {v.own !== undefined && <button type="button" className="rules__reset" title="Remove own value (inherit / default)" aria-label={'Reset ' + v.field.label}
      disabled={busy} onClick={onReset}><RotateCcw size={11}/></button>}
  </div>;
}
