import { useEffect, useRef, useState, type ReactNode } from 'react';
import { ChevronLeft, ChevronRight, Radio, Search } from 'lucide-react';
import { StudioIconButton } from '../ui';
import { invoke } from '../bridge';
import type { StudioRuntimeEntity, StudioRuntimePage, StudioGameState } from '../types';
import { type LiveRef, setShowLive, useLivePlay } from './livePlay';
import './LivePanels.css';

/** Scene / Live switch shown above Hierarchy and Inspector while the game runs (Unity play mode). */
export function LiveSwitch({ children }: { children: ReactNode }) {
  const { showLive } = useLivePlay();
  return <div className="live">
    <div className="live__switch" role="tablist" aria-label="Play mode view">
      <button type="button" role="tab" aria-selected={!showLive} className={!showLive ? 'is-on' : ''} onClick={() => setShowLive(false)}>Scene</button>
      <button type="button" role="tab" aria-selected={showLive} className={showLive ? 'is-on' : ''} onClick={() => setShowLive(true)}>
        <Radio size={11}/>Live</button>
      <span className="live__hint">{showLive ? 'Running game · read only' : 'Map edits apply after Stop'}</span>
    </div>
    <div className="live__body">{children}</div>
  </div>;
}

const pageSize = 100;

export function LiveHierarchy({project,selection,onSelected}:{project?:string;selection?:LiveRef|null;onSelected?:(state:StudioGameState)=>void}) {
  const [page, setPage] = useState<StudioRuntimePage | null>(null);
  const [query, setQuery] = useState('');
  const [offset, setOffset] = useState(0);
  const epoch = useRef<string | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    setPage(null); setError('');
    let live = true, busy = false;
    const poll = async () => {
      if (busy) return; busy = true;
      try {
        const next = await invoke('studio.gameEntities', { offset, limit: pageSize, query, expectedProject:project });
        if (!live) return;
        const changedEpoch = epoch.current !== null && epoch.current !== next.epoch;
        epoch.current = next.epoch;
        if (changedEpoch && offset !== 0) { setOffset(0); return; }
        const lastOffset = Math.floor(Math.max(0, next.total - 1) / pageSize) * pageSize;
        if (offset > lastOffset) { setOffset(lastOffset); return; }
        setPage(next); setError('');
      }
      catch (e) { if (live) setError(e instanceof Error ? e.message : String(e)); }
      finally { busy = false; }
    };
    void poll(); const t = window.setInterval(() => void poll(), 600);
    return () => { live = false; window.clearInterval(t); };
  }, [query, offset, project]);
  return <div className="live-hier">
    <label className="live-hier__search"><Search size={12} aria-hidden="true"/>
      <input value={query} onChange={e => {setQuery(e.target.value); setOffset(0);}} placeholder="Search live · t:type p:owner c:component #id" aria-label="Search live entities"/></label>
    {error && <div className="live__error" role="alert">{error}</div>}
    {!page ? (!error && <div className="live__empty">Reading live entities…</div>) : !page.active ? <div className="live__empty">The game is loading or in its menus.</div> :
      <><div role="listbox" aria-label="Live entities" className="live-hier__rows">
        {page.rows.map(row => <button key={row.ref} type="button" role="option" title={row.name + ' · ref:' + row.ref}
          aria-selected={selection?.ref === row.ref && selection.epoch === page.epoch}
          className={'live-hier__row' + (selection?.ref === row.ref && selection.epoch === page.epoch ? ' is-selected' : '')}
          onClick={() => {
            if(project) void invoke('studio.gameSelectEntity',{expectedProject:project,epoch:page.epoch,ref:row.ref})
              .then(onSelected).catch(e=>setError(String(e)));
          }}>
          <span className={'live-hier__owner live-hier__owner--' + Math.min(row.owner, 4)}/>
          <b>{row.name}</b><small>{row.kind}{row.health > 0 ? ' · ' + Math.round(row.health) + ' hp' : ''}</small>
        </button>)}
        {!page.rows.length && <p className="live__empty">{query ? 'No matching live entities.' : 'No live entities.'}</p>}
      </div><footer className="live-hier__pager">
        <span className="live__count">{page.total > pageSize ? `${offset + 1}–${offset + page.rows.length} of ${page.total}` : `${page.total} ${page.total === 1 ? 'entity' : 'entities'}`} · tick {page.tick}</span>
        {page.total > pageSize && <nav aria-label="Live entity pages">
          <StudioIconButton aria-label="Previous page" title="Previous page" icon={<ChevronLeft size={14}/>} disabled={offset === 0} onClick={() => setOffset(offset - pageSize)}/>
          <StudioIconButton aria-label="Next page" title="Next page" icon={<ChevronRight size={14}/>} disabled={offset + pageSize >= page.total} onClick={() => setOffset(offset + pageSize)}/>
        </nav>}
      </footer></>}
  </div>;
}

function Value({ value, depth = 0 }: { value: unknown; depth?: number }) {
  if (value === null || typeof value !== 'object') return <span>{typeof value === 'number' ? String(Math.round(value * 1000) / 1000) : String(value ?? '—')}</span>;
  if (depth > 8) return <span>…</span>;
  const record = value as Record<string, unknown>;
  if (typeof record.$entity === 'number' && record.$entity > 0) return <span className="live__ref">{String(record.label || `Entity ${record.$entity}`)}</span>;
  if (Array.isArray(value) && value.length <= 4 && value.every(v => typeof v === 'number'))
    return <span>{(value as number[]).map(v => Math.round(v * 1000) / 1000).join(', ')}</span>;
  const entries = Object.entries(value).slice(0, 120);
  if (!entries.length) return <span>{Array.isArray(value) ? '[]' : '{}'}</span>;
  return <dl className="live__fields">{entries.map(([k, v]) => <div key={k}><dt>{k}</dt><dd><Value value={v} depth={depth + 1}/></dd></div>)}</dl>;
}

export function LiveInspector({project,selection}:{project?:string;selection?:LiveRef|null}) {
  const [detail, setDetail] = useState<StudioRuntimeEntity | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    setDetail(null); setError('');
    if (!selection) return;
    let live = true, busy = false;
    const poll = async () => {
      if(busy)return; busy=true;
      try { const next = await invoke('studio.gameInspect', {...selection,expectedProject:project}); if (live) {setDetail(next);setError('');} }
      catch (e) { if (live) { setError(e instanceof Error ? e.message : String(e)); setDetail(null); } }
      finally {busy=false;}
    };
    void poll(); const t = window.setInterval(() => void poll(), 500);
    return () => { live = false; window.clearInterval(t); };
  }, [selection?.epoch, selection?.ref, project]);
  if (!selection) return <div className="live__empty">{error || 'Select a live entity in the Hierarchy.'}</div>;
  if (error) return <p className="live__error" role="alert">{error}</p>;
  if (!detail || detail.ref!==selection.ref || detail.epoch!==selection.epoch) return <div className="live__empty">Reading entity…</div>;
  return <div className="live-insp">
    <header><b>Entity {detail.ref}</b><small>tick {detail.tick}</small></header>
    {Object.entries(detail.components).map(([name, value]) => <details key={name} open className="live-insp__comp">
      <summary>{name}</summary><Value value={value}/></details>)}
    {detail.stats && <details open className="live-insp__comp"><summary>Effective stats</summary><Value value={detail.stats}/></details>}
  </div>;
}
