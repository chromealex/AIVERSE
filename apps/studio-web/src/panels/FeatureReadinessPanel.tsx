import { useCallback, useEffect, useMemo, useState } from 'react';
import { invoke } from '../bridge';
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
}

export default function FeatureReadinessPanel({connected}: {connected: boolean}) {
  const [report, setReport] = useState<ReadinessReport | null>(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [owner, setOwner] = useState('');
  const [query, setQuery] = useState('');
  const [kind, setKind] = useState('');
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
  useEffect(() => { if (connected) void load(); else setReport(null); }, [connected, load]);
  const owners = useMemo(() => (report?.readiness.owners ?? []).filter(o => !owner || o.owner === owner), [report, owner]);
  const tasks = useMemo(() => owners.flatMap(o => o.tasks.map(t => ({...t, owner: o.owner})))
    .filter(t => (!kind || t.kind === kind) && [t.id, t.title, t.note, t.owner].join(' ').toLowerCase().includes(query.toLowerCase())),
    [owners, kind, query]);
  return <StudioPanel title="Feature Readiness" eyebrow="ENGINE / QUALITY"
    subtitle={report ? 'Report commit ' + report.sha.slice(0,12) : 'Engine audit snapshot'}
    actions={<StudioButton loading={busy} disabled={!connected} onClick={() => void load()}>Refresh</StudioButton>}>
    <div className="au-stack">
      {!connected && <StudioEmptyState title="Native host unavailable" description="Connect Studio to the engine."/>}
      {error && <StudioAlert tone="danger" title="Report unavailable">{error}</StudioAlert>}
      {connected && !report && !error && !busy && <StudioEmptyState title="No report" description="Generate the engine readiness report from the repository."/>}
      {report && <>
        <StudioAlert tone="warning" title="Snapshot only">Freshness and independent runtime verification are not established by Studio. Regenerate the report after changes.</StudioAlert>
        <StudioSection title="Release">
          <StudioBadge tone={report.release.state === 'blocked' ? 'danger' : 'warning'}>{report.release.state}</StudioBadge>
          <p>{report.release.scope}</p>
          <p>{report.release.blockers.length} recorded blockers</p>
          {report.release.blockers.slice(0, 40).map((b,i) => <div className="au-card" key={i}>{b.feature ? b.feature + ': ' : ''}{b.reason}</div>)}
          {report.release.blockers.length > 40 && <p>Showing 40 blockers; see the full JSON report.</p>}
        </StudioSection>
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
