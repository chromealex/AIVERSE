import { useCallback, useEffect, useMemo, useState, useRef } from 'react';
import { invoke } from '../bridge';
import GraphCanvas from '../graphs/GraphCanvas';
import type { GraphView } from '../graphs/graphFramework';
import { StudioPanel, StudioSection, StudioButton, StudioBadge, StudioAlert,
  StudioEmptyState, StudioInput, StudioSelect, StudioField } from '../ui';

interface Task {
  id: string; kind: string; title?: string; note?: string; acceptance?: string[];
}
interface Owner {
  owner: string;
  counts: Record<'manifests' | 'capabilities' | 'graphs' | 'tasks', number>;
  manifests: {id: string; state: string}[];
  tasks: Task[];
}
interface ReadinessReport {
  schema: number; sha: string; snapshotOnly: boolean; freshness: string;
  readiness: {scope: string; owners: Owner[]; counts: Owner['counts']; errors: string[]};
  release: {state: string; scope: string; blockers: {feature: string | null; reason: string}[]};
  policy?: {dependencies?: {order: string[]; features: Record<string, {state: string; requires: string[]; blocked_by: string[]}>}};
  automation?: {schedule?: {task: string; owner: string; agent: string | null; state: string; reason: string | null}[]};
}

export default function FeatureReadinessPanel({connected}: {connected: boolean}) {
  const [report, setReport] = useState<ReadinessReport | null>(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [owner, setOwner] = useState('');
  const [query, setQuery] = useState('');
  const [kind, setKind] = useState('');
  const [auto, setAuto] = useState(false);
  const [refreshing, setRefreshing] = useState(false);
  const [jobNote, setJobNote] = useState('');
  const [selected, setSelected] = useState('');
  const inFlight = useRef(false);
  const live = useRef(false);
  const load = useCallback(async () => {
    if (!connected) return;
    setBusy(true); setError('');
    try {
      const result = (await invoke('studio.op', {op: 'feature_readiness_get', params: {}})).result as ReadinessReport;
      if (!result || result.schema !== 1 || !Array.isArray(result.readiness?.owners) ||
          !Array.isArray(result.release?.blockers)) throw new Error('Invalid readiness report');
      setReport(result);
    } catch (e) {
      setReport(null);
      setError(e instanceof Error ? e.message : String(e));
    } finally { setBusy(false); }
  }, [connected]);
  const regenerate = useCallback(async () => {
    if (!connected || inFlight.current) return;
    inFlight.current = true;
    setRefreshing(true); setJobNote('');
    try {
      const start = (await invoke('studio.op', {op:'feature_readiness_refresh', params:{}})).result as {runCount:number};
      for (let i=0;i<120&&live.current;i++) {
        await new Promise<void>(resolve=>window.setTimeout(resolve,2000));
        if (!live.current) return;
        const status = (await invoke('studio.op',{op:'feature_readiness_status_get',params:{}})).result as {busy:boolean;label:string;runCount:number;exitCode:number};
        if (!status.busy && status.runCount>start.runCount) {
          if (status.label!=='feature-readiness') throw new Error('Report job was superseded');
          if (status.exitCode!==0) setJobNote('Report generator returned diagnostics; check the native job log.');
          await load();
          return;
        }
      }
      if (live.current) setJobNote('Report still running; refresh the snapshot later.');
    } catch(e) { if (live.current) setJobNote(e instanceof Error?e.message:String(e)); }
    finally { inFlight.current=false; if (live.current) setRefreshing(false); }
  },[connected,load]);
  useEffect(() => {
    live.current=connected;
    if (connected) void load(); else {setReport(null);setRefreshing(false);}
    return ()=>{live.current=false;};
  }, [connected, load]);
  useEffect(() => {
    if (!connected || !auto) return;
    void regenerate();
    const timer=window.setInterval(()=>{void regenerate();},60000);
    return ()=>window.clearInterval(timer);
  },[connected,auto,regenerate]);
  const dep = report?.policy?.dependencies;
  const graph = useMemo<GraphView>(() => {
    const ids=(dep?.order??[]).filter(id=>dep?.features[id]).slice(0,200);
    const visible=new Set(ids);
    const depth=new Map<string,number>(), counts=new Map<number,number>();
    const nodes=ids.map(id=>{
      const row=dep!.features[id];
      const d=Math.min(20,Math.max(0,...row.requires.map(p=>(depth.get(p)??-1)+1)));
      depth.set(id,d);const n=counts.get(d)??0;counts.set(d,n+1);
      return {id,label:row.state,x:24+d*190,y:24+n*90,inputs:[{id:'requires',type:'feature',multiple:true}],outputs:[{id:'provided',type:'feature'}]};
    });
    const links=ids.flatMap(id=>dep!.features[id].requires.filter(p=>visible.has(p)).map(p=>({id:p+'->'+id,from:p,output:'provided',to:id,input:'requires'})));
    return {nodes,links};
  },[dep]);
  const owners = useMemo(() => (report?.readiness.owners ?? []).filter(o => !owner || o.owner === owner), [report, owner]);
  const tasks = useMemo(() => owners.flatMap(o => o.tasks.map(t => ({...t, owner: o.owner})))
    .filter(t => (!kind || t.kind === kind) && [t.id, t.title, t.note, t.owner].join(' ').toLowerCase().includes(query.toLowerCase())),
    [owners, kind, query]);
  return <StudioPanel title="Feature Readiness" eyebrow="ENGINE / QUALITY"
    subtitle={report ? 'Report commit ' + report.sha.slice(0,12) : 'Engine audit snapshot'}
    actions={<div className="au-row"><StudioButton loading={busy} disabled={!connected||refreshing} onClick={() => void load()}>Read</StudioButton><StudioButton loading={refreshing} disabled={!connected||busy} onClick={()=>void regenerate()}>Regenerate</StudioButton></div>}>
    <div className="au-stack">
      <label className="au-row"><input type="checkbox" checked={auto} disabled={!connected} onChange={e=>setAuto(e.target.checked)}/> Auto regenerate every minute</label>
      {jobNote&&<StudioAlert tone="warning">{jobNote}</StudioAlert>}
      {!connected && <StudioEmptyState title="Native host unavailable" description="Connect Studio to the engine."/>}
      {error && <StudioAlert tone="danger" title="Report unavailable">{error}</StudioAlert>}
      {connected && !report && !error && !busy && <StudioEmptyState title="No report" description="Select Regenerate or enable automatic regeneration."/>}
      {report && <>
        <StudioAlert tone="warning" title="Snapshot only">Freshness and independent runtime verification are not established by Studio. Regenerate the report after changes.</StudioAlert>
        <StudioSection title="Release">
          <StudioBadge tone={report.release.state === 'blocked' ? 'danger' : 'warning'}>{report.release.state}</StudioBadge>
          <p>{report.release.scope}</p>
          <p>{report.release.blockers.length} recorded blockers</p>
          {report.release.blockers.slice(0, 40).map((b,i) => <div className="au-card" key={i}>{b.feature ? b.feature + ': ' : ''}{b.reason}</div>)}
          {report.release.blockers.length > 40 && <p>Showing 40 blockers; see the full JSON report.</p>}
        </StudioSection>
        {dep&&<StudioSection title="Feature dependencies">
          <small>Declared prerequisites · {graph.nodes.length} visible features · read-only</small>
          {graph.nodes.length>0?<div style={{height:320,display:'flex',flexDirection:'column',minWidth:0}}><GraphCanvas view={graph} label="Feature dependency graph" readOnly selected={selected} onSelect={setSelected} onMove={()=>{}}/></div>:<StudioEmptyState title="No feature nodes"/>}
          {selected&&dep.features[selected]&&<div className="au-card"><strong>{selected}</strong><p>{dep.features[selected].state}</p><p>Requires: {dep.features[selected].requires.join(', ')||'None declared'}</p><p>Blocked by: {dep.features[selected].blocked_by.join(', ')||'None declared'}</p></div>}
          {dep.order.length>200&&<small>First 200 nodes shown; full data is available through the MCP query.</small>}
        </StudioSection>}
        {report.automation?.schedule&&<StudioSection title="Agent proposals">
          {report.automation.schedule.slice(0,30).map(row=><div className="au-card" key={row.task}><strong>{row.task}</strong><p>{row.agent??'Unassigned'} · {row.state}</p>{row.reason&&<small>{row.reason}</small>}</div>)}
          {report.automation.schedule.length>30&&<small>First 30 proposals; query the full schedule via MCP.</small>}
        </StudioSection>}
        <StudioSection title="Subsystems">
          <StudioField label="Owner" htmlFor="feature-owner">
            <StudioSelect id="feature-owner" value={owner} onChange={e=>setOwner(e.target.value)}>
              <option value="">All owners</option>
              {report.readiness.owners.map(o=><option key={o.owner} value={o.owner}>{o.owner}</option>)}
            </StudioSelect>
          </StudioField>
          {owners.map(o=><div className="au-card" key={o.owner}>
            <strong>{o.owner}</strong>
            <p>{o.counts.manifests} manifests · {o.counts.capabilities} catalog capabilities · {o.counts.graphs} graphs · {o.counts.tasks} tasks</p>
            {o.manifests.map(m=><div className="au-row" key={m.id}><span>{m.id}</span><StudioBadge>{m.state}</StudioBadge></div>)}
          </div>)}
          <p>{report.readiness.scope}</p>
        </StudioSection>
        <StudioSection title={'Audit tasks (' + tasks.length + ')'}>
          <StudioField label="Search" htmlFor="feature-task-search"><StudioInput id="feature-task-search" value={query} onChange={e=>setQuery(e.target.value)}/></StudioField>
          <StudioField label="Task type" htmlFor="feature-task-kind"><StudioSelect id="feature-task-kind" value={kind} onChange={e=>setKind(e.target.value)}>
            <option value="">All tasks</option><option value="fix_manifest">Fix manifest</option>
            <option value="implement_gap">Implement gap</option><option value="investigate">Investigate</option>
          </StudioSelect></StudioField>
          {!tasks.length && <StudioEmptyState title="No matching tasks"/>}
          {tasks.slice(0,100).map(t=><div className="au-card" key={t.id}>
            <strong>{t.title ?? t.id}</strong><div className="au-row"><StudioBadge>{t.kind}</StudioBadge><span>{t.owner}</span></div>
            {t.note && <p>{t.note}</p>}
            {t.acceptance?.map((a,i)=><div key={i}>{a}</div>)}
          </div>)}
          {tasks.length > 100 && <p>Showing 100 tasks; refine the search or read the JSON report.</p>}
        </StudioSection>
      </>}
    </div>
  </StudioPanel>;
}
