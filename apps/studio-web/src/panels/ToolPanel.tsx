import { useCallback, useEffect, useMemo, useState } from 'react';
import { ChevronDown, ChevronRight, Play, RefreshCw, Search } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioSnapshot } from '../types';
import { buildParams, fieldDraft, opLabel, resultLine, schemaFields, stateOps, type ToolField, type ToolOpInfo } from './toolSchema';
import { TOOL_PANELS, type ToolPanelId } from './toolPanels';
import JsonTree from './JsonTree';
import SceneBrushBar, { type BrushKind } from './SceneBrushBar';
import './ProjectToolPanels.css';
import './ToolPanel.css';
import { StudioLoader, StudioButton } from '../ui';
import NativeGraphPanel from '../graphs/NativeGraphPanel';
import ModelGraphPanel from '../graphs/ModelGraphPanel';

/** Panels with a Scene brush and the brush tools each offers. */
const BRUSHES: Partial<Record<ToolPanelId, BrushKind[]>> = {
  terrain: ['terrain', 'paint', 'objects'], tiles: ['tiles'], fields: ['field'], roads: ['road'], water: ['river'],
};

const message = (e: unknown) => e instanceof Error ? e.message : String(e);

function FieldInput({ field, value, error, onChange }: { field: ToolField; value: string; error?: string; onChange: (v: string) => void }) {
  const id = 'tool-' + field.key;
  let input;
  if (field.kind === 'boolean')
    input = <select id={id} className="au-input" value={value} onChange={e => onChange(e.target.value)}>
      <option value="">{field.required ? 'Choose…' : 'Default'}</option><option value="true">true</option><option value="false">false</option></select>;
  else if (field.kind === 'enum')
    input = <select id={id} className="au-input" value={value} onChange={e => onChange(e.target.value)}>
      <option value="">{field.required ? 'Choose…' : 'Default'}</option>{field.options!.map(o => <option key={o} value={o}>{o}</option>)}</select>;
  else if (field.kind === 'json')
    input = <textarea id={id} className="au-input tool__json" rows={2} spellCheck={false} value={value}
      placeholder={field.required ? 'JSON value' : 'JSON (optional)'} onChange={e => onChange(e.target.value)}/>;
  else
    input = <input id={id} className="au-input" value={value} spellCheck={false}
      inputMode={field.kind === 'string' ? undefined : 'decimal'}
      placeholder={field.kind === 'string' ? '' : field.min !== undefined || field.max !== undefined ? `${field.min ?? ''}…${field.max ?? ''}` : field.kind}
      onChange={e => onChange(e.target.value)}/>;
  return <label className={'tool__field' + (field.kind === 'json' ? ' tool__field--wide' : '')} title={field.hint || undefined}>
    <span>{field.label}{field.required && <i>*</i>}</span>{input}
    {error && <small className="tool__fielderr">{error}</small>}
  </label>;
}

function OpCard({ op, prefix, initiallyOpen, onDone }: { op: ToolOpInfo; prefix: string; initiallyOpen: boolean; onDone: (changed: boolean) => void }) {
  const fields = useMemo(() => schemaFields(op.schema), [op.schema]);
  const [open, setOpen] = useState(initiallyOpen);
  const [more, setMore] = useState(false);
  const [drafts, setDrafts] = useState<Record<string, string>>(() => Object.fromEntries(fields.map(f => [f.key, fieldDraft(f)])));
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<{ ok: boolean; text: string; value?: unknown } | null>(null);
  const run = async () => {
    const built = buildParams(fields, drafts);
    setErrors(built.errors);
    if (Object.keys(built.errors).length) return;
    setRunning(true);
    try {
      const r = await invoke('studio.op', { op: op.name, params: built.params });
      setResult({ ok: true, text: resultLine(r.result), value: r.result });
      onDone(r.changed);
    } catch (e) { setResult({ ok: false, text: message(e) }); }
    finally { setRunning(false); }
  };
  return <section className={'tool__card' + (open ? ' is-open' : '')}>
    <button type="button" className="tool__cardhead" onClick={() => setOpen(!open)} aria-expanded={open}>
      {open ? <ChevronDown size={13}/> : <ChevronRight size={13}/>}
      <b>{opLabel(op.name, prefix)}</b>
      {op.readOnly && <span className="tool__tag">read</span>}
      <code>{op.name}</code>
    </button>
    {open && <div className="tool__cardbody">
      {op.description && <p className={'tool__desc' + (more ? ' is-full' : '')} onClick={() => setMore(!more)}>{op.description}</p>}
      {fields.length > 0 && <div className="tool__fields">
        {fields.map(f => <FieldInput key={f.key} field={f} value={drafts[f.key] ?? ''} error={errors[f.key]}
          onChange={v => setDrafts(d => ({ ...d, [f.key]: v }))}/>)}
      </div>}
      <div className="tool__run">
        <button type="button" className="au-button au-button--sm au-button--primary" disabled={running} onClick={() => void run()}>
          {running ? <StudioLoader size={16}/> : <Play size={13}/>}{op.readOnly ? 'Read' : 'Apply'}</button>
        {result && <span className={'tool__result ' + (result.ok ? 'is-ok' : 'is-error')} role={result.ok ? 'status' : 'alert'}>{result.text}</span>}
      </div>
      {result?.ok && result.value !== undefined && <div className="tool__answer"><JsonTree value={result.value} name="answer"/></div>}
    </div>}
  </section>;
}

/** A legacy editor window as an HTML panel: its current state on top, then one card per operation. */
export default function ToolPanel({ panel, snapshot, commitSnapshot }: {
  panel: ToolPanelId; snapshot: StudioSnapshot | null; commitSnapshot: (s: StudioSnapshot) => void;
}) {
  const spec = TOOL_PANELS[panel];
  const graphDomain=panel==='fx_graph'?'spectrafx':panel==='anim_graph'?'motionweave':null;
  const modelGraph=panel==='modeling';
  const hasGraph=!!graphDomain||modelGraph;
  const [graphMode,setGraphMode]=useState(true);
  const [ops, setOps] = useState<ToolOpInfo[]>([]);
  const [state, setState] = useState<Record<string, unknown>>({});
  const [error, setError] = useState('');
  const [filter, setFilter] = useState('');
  const projectPath = snapshot?.project.path ?? '';
  const readState = useCallback(async (list: ToolOpInfo[]) => {
    const next: Record<string, unknown> = {};
    for (const op of stateOps(list, spec.state)) {
      try { next[op.name] = (await invoke('studio.op', { op: op.name, params: {} })).result; }
      catch (e) { next[op.name] = { error: message(e) }; }
    }
    setState(next);
  }, [spec]);
  useEffect(() => {
    if (!projectPath) { setOps([]); setState({}); return; }
    let live = true;
    void invoke('studio.ops', { panel }).then(r => {
      if (!live) return;
      setOps(r.ops); setError('');
      void readState(r.ops);
    }).catch(e => { if (live) setError(message(e)); });
    return () => { live = false; };
  }, [panel, projectPath, readState]);
  // Undo / redo / edits elsewhere change the state shown here too.
  useEffect(() => { if (ops.length) void readState(ops); }, [snapshot]); // eslint-disable-line react-hooks/exhaustive-deps
  const done = (changed: boolean) => {
    if (!changed) return;
    void invoke('studio.snapshot', {}).then(commitSnapshot).catch(() => {});
  };
  if (!projectPath) return <div className="tool__empty">Open a project to use this panel.</div>;
  const q = filter.trim().toLowerCase();
  const shown = ops.filter(o => !spec.state.includes(o.name) && (!q || o.name.includes(q) || o.description.toLowerCase().includes(q)));
  return <div className="tool">
    {hasGraph&&<div className="au-row"><StudioButton size="sm" disabled={graphMode} onClick={()=>setGraphMode(true)}>Graph</StudioButton><StudioButton size="sm" disabled={!graphMode} onClick={()=>setGraphMode(false)}>Native operations</StudioButton></div>}
    {graphDomain&&<div hidden={!graphMode} style={{flex:1,minHeight:0,overflow:"auto"}}><NativeGraphPanel domain={graphDomain} projectPath={projectPath}/></div>}
    {modelGraph&&<div hidden={!graphMode} style={{flex:1,minHeight:0,overflow:"auto"}}><ModelGraphPanel projectPath={projectPath}/></div>}
    <div hidden={hasGraph&&graphMode} style={{flex:1,minHeight:0,overflow:"auto"}}>
    <div className="tool__head">
      <p>{spec.intro}</p>
      <div className="tool__search"><Search size={12}/><input placeholder="Filter" value={filter} onChange={e => setFilter(e.target.value)}/></div>
      <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Re-read" onClick={() => void readState(ops)}><RefreshCw size={13}/></button>
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    <div className="tool__scroll">
      {BRUSHES[panel] && <SceneBrushBar kinds={BRUSHES[panel]!}/>}
      {Object.entries(state).map(([name, value]) => <div key={name} className="tool__state">
        <JsonTree value={value} name={opLabel(name, spec.prefix)} open/>
      </div>)}
      {shown.map(op => <OpCard key={op.name + projectPath} op={op} prefix={spec.prefix} initiallyOpen={spec.open.includes(op.name)} onDone={done}/>)}
    </div>
    </div>
  </div>;
}
