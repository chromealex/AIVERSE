import { useEffect, useMemo, useState } from 'react';
import { RotateCcw, Search } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioRules } from '../types';
import { RULE_GROUPS, parseRuleInput, typeValues, type RuleValue } from './rulesSchema';
import { fuzzyScore } from '../commandSearch';
import './RulesPanel.css';

/** Rules: unit types as a form of known fields (validated by the game loader on every write). */
export default function RulesPanel({ projectPath, onChanged }: { projectPath: string; onChanged: () => void }) {
  const [rules, setRules] = useState<StudioRules | null>(null);
  const [typeId, setTypeId] = useState('');
  const [query, setQuery] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [section,setSection]=useState<'types'|'triggers'|'damageTable'>('types');
  const [raw, setRaw] = useState<string | null>(null);
  useEffect(() => {
    let live = true;
    setRules(null); setError(''); setRaw(null); setSection('types');
    if (!projectPath) return;
    void invoke('studio.rules', {}).then(r => { if (live) { setRules(r); setTypeId(id => id && id in r.types ? id : Object.keys(r.types)[0] ?? ''); } })
      .catch(e => { if (live) setError(e instanceof Error ? e.message : String(e)); });
    return () => { live = false; };
  }, [projectPath]);
  const ids = useMemo(() => {
    const all = Object.keys(rules?.types ?? {});
    if (!query.trim()) return all;
    return all.map(id => ({ id, s: Math.max(fuzzyScore(query, id), fuzzyScore(query, String((rules!.types[id] as { name?: unknown })?.name ?? ''))) }))
      .filter(x => x.s >= 0).sort((a, b) => b.s - a.s).map(x => x.id);
  }, [rules, query]);
  // Inspector → "Edit type in Rules" selects that type.
  useEffect(() => {
    const pick = (e: Event) => { const id = String((e as CustomEvent<unknown>).detail ?? ''); if (id) { setSection('types'); setTypeId(id); setQuery(''); setRaw(null); } };
    window.addEventListener('studio:rules-type', pick);
    return () => window.removeEventListener('studio:rules-type', pick);
  }, []);
  const values = useMemo(() => rules && typeId ? typeValues(typeId, rules.types[typeId], rules.resolved[typeId], rules.resources) : [], [rules, typeId]);
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
  return <div className="rules">
    <aside className="rules__list">
      <label className="rules__section">Rule section
        <select value={section} disabled={busy||(section!=='types'&&raw!==null)} onChange={event=>{
          setSection(event.target.value as typeof section); setRaw(null);setError('');
        }}>
          <option value="types">Unit types</option><option value="triggers">Triggers</option>
          <option value="damageTable">Damage matrix</option>
        </select>
      </label>
      {section==='types'&&<>
      <label className="rules__search"><Search size={12} aria-hidden="true"/>
        <input value={query} onChange={e => setQuery(e.target.value)} placeholder="Search types" aria-label="Search unit types"/></label>
      <div role="listbox" aria-label="Unit types" className="rules__types">
        {ids.map(id => { const name = String((rules!.types[id] as { name?: unknown })?.name ?? id);
          return <button key={id} type="button" role="option" aria-selected={id === typeId} draggable
            title="Drag into the Scene to place a new object of this type"
            onDragStart={e => { e.dataTransfer.setData('text/plain', 'aiverse-type:' + id); e.dataTransfer.effectAllowed = 'copy'; }}
            className={'rules__type' + (id === typeId ? ' is-active' : '')} onClick={() => { setTypeId(id); setRaw(null); }}>
            <b>{name}</b>{name !== id && <small>{id}</small>}</button>; })}
        {rules && !ids.length && <p className="rules__empty">No types.</p>}
      </div>
      </>}
    </aside>
    <section className="rules__form" aria-label={section==='types'?'Type '+typeId:section}>
      {error && <div className="rules__error" role="alert">{error}<button type="button" aria-label="Dismiss" onClick={() => setError('')}>×</button></div>}
      {!rules ? <p className="rules__empty">Loading rules…</p> : section!=='types' ? <>
        <h3>{section==='triggers'?'Triggers':'Damage matrix'}</h3>
        {section==='damageTable'&&!!rules.armorTypes?.length&&<div className="rules__matrix"><table>
          <thead><tr><th>Attack / armor</th>{rules.armorTypes.map(armor=><th key={armor}>{armor}</th>)}</tr></thead>
          <tbody>{Object.entries(rules.damageTable??{}).map(([attack,row])=><tr key={attack}>
            <th scope="row">{attack}</th>{rules.armorTypes!.map(armor=><td key={armor}>{typeof row?.[armor]==='number'?row[armor]:1}</td>)}
          </tr>)}</tbody>
        </table></div>}
        <div className="rules__rawbox">
          <textarea disabled={busy} aria-label={section==='triggers'?'Trigger definitions':'Damage multipliers'} spellCheck={false}
            value={raw??JSON.stringify(rules[section]??(section==='triggers'?[]:{}),null,2)}
            onChange={event=>setRaw(event.target.value)}/>
          <button disabled={busy||raw===null} onClick={()=>{
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
          <button type="button" className="rules__raw-toggle" onClick={() => setRaw(r => r === null ? JSON.stringify(rules.types[typeId], null, 2) : null)}>
            {raw === null ? 'Edit JSON' : 'Form'}</button>
        </div>
        <p className="rules__note">Changes are validated by the game loader and saved to rules.json at once. Blank = inherit / default.</p>
        {raw !== null ? <div className="rules__rawbox">
          <textarea value={raw} onChange={e => setRaw(e.target.value)} spellCheck={false} aria-label={'JSON of ' + typeId}/>
          <button type="button" disabled={busy} onClick={() => { try { void write('types.' + typeId, JSON.parse(raw)); } catch (e) { setError('JSON: ' + (e instanceof Error ? e.message : String(e))); } }}>Validate &amp; save</button>
        </div> : RULE_GROUPS.map(group => {
          const rows = values.filter(v => v.field.group === group);
          if (!rows.length) return null;
          return <fieldset key={group} className="rules__group"><legend>{group}</legend>
            {rows.map(v => <RuleRow key={v.path} value={v} busy={busy} onCommit={input => commit(v, input)} onReset={() => void write(v.path, null)}/>)}
          </fieldset>;
        })}
      </>}
    </section>
  </div>;
}

function RuleRow({ value: v, busy, onCommit, onReset }: { value: RuleValue; busy: boolean; onCommit: (input: string | boolean) => void; onReset: () => void }) {
  const shown = v.own ?? v.resolved;
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
