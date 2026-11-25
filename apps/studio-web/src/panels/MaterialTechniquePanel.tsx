import {useEffect, useRef, useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert, StudioButton, StudioInput, StudioSection} from '../ui';

const capabilities = ['compute', 'rgba16f', 'texture2dArray'] as const;
type Rule = {source:string; use:string; minDistance?:number; maxDistance?:number; scheme?:string; requires?:string[]};
type Document = {rules:Rule[]; materials:string[]};

export default function MaterialTechniquePanel({projectPath}:{projectPath:string}) {
  const [model,setModel] = useState('');
  const [doc,setDoc] = useState<Document|null>(null);
  const [rules,setRules] = useState<Rule[]>([]);
  const [undo,setUndo] = useState<Rule[]|null>(null);
  const [redo,setRedo] = useState<Rule[]|null>(null);
  const [busy,setBusy] = useState(false);
  const [error,setError] = useState('');
  const live = useRef(true);
  useEffect(() => { live.current = true; return () => { live.current = false; }; }, []);
  const dirty = !!doc && JSON.stringify(rules) !== JSON.stringify(doc.rules);

  async function run(action:'load'|'save'|'undo'|'redo') {
    if (busy) return;
    setBusy(true);
    setError('');
    try {
      const next = action === 'undo' ? undo : action === 'redo' ? redo : rules;
      const response = await invoke('studio.op', {
        op: action === 'load' ? 'material_techniques_get' : 'material_techniques_set',
        expectedProject: projectPath,
        params: action === 'load' ? {model} : {model, rules:next, expected:doc?.rules}
      });
      if (!live.current) return;
      const result = response.result as Document;
      if (!Array.isArray(result?.rules) || !Array.isArray(result?.materials))
        throw new Error('Material techniques were unavailable');
      setUndo(action === 'load' || action === 'undo' ? null : doc?.rules ?? null);
      setRedo(action === 'undo' ? doc?.rules ?? null : null);
      setDoc(result);
      setRules(result.rules);
    } catch (e) {
      if (live.current) setError(e instanceof Error ? e.message : String(e));
    } finally {
      if (live.current) setBusy(false);
    }
  }
  function patch(i:number, changes:Partial<Rule>) {
    setRules(rows => rows.map((row,index) => index === i ? {...row,...changes} : row));
  }
  function move(i:number,delta:number) {
    const copy = [...rules];
    [copy[i],copy[i+delta]] = [copy[i+delta],copy[i]];
    setRules(copy);
  }
  return <StudioSection title="Material techniques">
    <fieldset disabled={busy} className="au-stack">
      <label>Model path<StudioInput placeholder="content/models/stone.glb" value={model} disabled={dirty}
        onChange={e => { setModel(e.target.value); setDoc(null); setRules([]); setUndo(null); setRedo(null); }}/></label>
      <StudioButton disabled={!model.trim() || dirty} onClick={() => void run('load')}>Load techniques</StudioButton>
      {doc && <>
        {rules.map((row,i) => <fieldset key={i} className="au-stack">
          <legend>Rule {i+1}</legend>
          {(['source','use'] as const).map(key => <label key={key}>{key === 'source' ? 'Source material' : 'Use material'}
            <select value={row[key]} onChange={e => patch(i,{[key]:e.target.value})}>
              {doc.materials.filter((m,idx,all)=>m && all.indexOf(m)===idx).map(m=><option key={m}>{m}</option>)}
            </select></label>)}
          {(['minDistance','maxDistance'] as const).map(key => <label key={key}>{key === 'minDistance' ? 'From distance' : 'Below distance'}
            <StudioInput type="number" min={0} step="any" value={row[key] ?? ''} placeholder={key === 'minDistance' ? '0' : 'Unlimited'}
              onChange={e=>patch(i,{[key]:e.target.value === '' ? undefined : Number(e.target.value)})}/></label>)}
          <label>Scheme<StudioInput value={row.scheme ?? ''} placeholder="Any" onChange={e=>patch(i,{scheme:e.target.value || undefined})}/></label>
          <fieldset><legend>Required GPU capabilities</legend>{capabilities.map(cap => <label key={cap}>
            <input type="checkbox" checked={row.requires?.includes(cap) ?? false} onChange={e=>patch(i,{requires:e.target.checked
              ? [...(row.requires ?? []),cap] : row.requires?.filter(value=>value!==cap)})}/>{cap}
          </label>)}</fieldset>
          <div className="au-row">
            <StudioButton disabled={i===0} onClick={()=>move(i,-1)}>Move up</StudioButton>
            <StudioButton disabled={i===rules.length-1} onClick={()=>move(i,1)}>Move down</StudioButton>
            <StudioButton onClick={()=>setRules(rules.filter((_,index)=>index!==i))}>Remove</StudioButton>
          </div>
        </fieldset>)}
        <div className="au-row">
          <StudioButton disabled={!doc.materials.some(Boolean) || rules.length>=256}
            onClick={()=>{const name=doc.materials.find(Boolean)!;setRules([...rules,{source:name,use:name}]);}}>Add rule</StudioButton>
          <StudioButton disabled={!dirty} onClick={()=>void run('save')}>Save rules</StudioButton>
          <StudioButton disabled={!dirty} onClick={()=>setRules(doc.rules)}>Discard edits</StudioButton>
          <StudioButton disabled={dirty || !undo} onClick={()=>void run('undo')}>Undo save</StudioButton>
          <StudioButton disabled={dirty || !redo} onClick={()=>void run('redo')}>Redo save</StudioButton>
        </div>
      </>}
    </fieldset>
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
  </StudioSection>;
}
