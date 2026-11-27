import { useEffect, useRef, useState, type ReactNode } from 'react';
import { ChevronLeft, ChevronRight, Radio, Search } from 'lucide-react';
import { StudioIconButton } from '../ui';
import { invoke } from '../bridge';
import type { StudioRuntimeEntity, StudioRuntimePage, StudioGameState, StudioSnapshot } from '../types';
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

export function LiveHierarchy({project,groupings,selection,onSelected}:{project?:string;groupings?:string[];selection?:LiveRef|null;onSelected?:(state:StudioGameState)=>void}) {
  const [page, setPage] = useState<StudioRuntimePage | null>(null);
  const [query, setQuery] = useState('');
  const [offset, setOffset] = useState(0);
  const [group, setGroup] = useState(-1);
  const [path, setPath] = useState<string[]>([]);
  const [collapsed,setCollapsed]=useState<string[]>([]);
  const epoch = useRef<string | null>(null);
  const [error, setError] = useState('');
  useEffect(() => {
    setPage(null); setError('');
    let live = true, busy = false;
    const poll = async () => {
      if (busy) return; busy = true;
      try {
        const next = await invoke('studio.gameEntities', { offset, limit: pageSize, query, expectedProject:project, ...(group >= 0 ? {group,path,collapsed} : {}) });
        if (!live) return;
        const changedEpoch = epoch.current !== null && epoch.current !== next.epoch;
        epoch.current = next.epoch;
        if (changedEpoch && (offset || path.length || collapsed.length)) { setCollapsed([]);setOffset(0); setPath([]); return; }
        const lastOffset = Math.floor(Math.max(0, (next.groupCount ?? next.total) - 1) / pageSize) * pageSize;
        if (offset > lastOffset) { setOffset(lastOffset); return; }
        setPage(next); setError('');
      }
      catch (e) { if (live) setError(e instanceof Error ? e.message : String(e)); }
      finally { busy = false; }
    };
    void poll(); const t = window.setInterval(() => void poll(), 600);
    return () => { live = false; window.clearInterval(t); };
  }, [query, offset, project, group, path, collapsed]);
  const count = page?.groupCount ?? page?.total ?? 0;
  const shown = page?.groups?.length ?? page?.rows.length ?? 0;
  const noun = page?.groups ? 'group' : 'entity';
  const plural = page?.groups ? 'groups' : 'entities';
  const navigate = (next:string[]) => {setPage(null);setOffset(0);setPath(next);};
  return <div className="live-hier">
    <label className="live-hier__search"><Search size={12} aria-hidden="true"/>
      <input value={query} onChange={e => {setQuery(e.target.value); setOffset(0);}} placeholder="Search live · t:type p:owner c:component #id" aria-label="Search live entities"/></label>
    {!!groupings?.length && <label className="live-hier__grouping">Group by
      <select value={group} onChange={e=>{setCollapsed([]);setGroup(Number(e.target.value));navigate([]);}}>
        <option value={-1}>All entities</option>
        {groupings.map((name,index)=><option key={index} value={index}>{name}</option>)}
      </select>
    </label>}
    {path.length>0 && <nav className="live-hier__path" aria-label="Live group path">
      <button type="button" onClick={()=>navigate([])}>All groups</button>
      {path.map((name,i)=><span key={i}> / {i===path.length-1 ? name :
        <button type="button" onClick={()=>navigate(path.slice(0,i+1))}>{name}</button>}</span>)}
    </nav>}
    {error && <div className="live__error" role="alert">{error}</div>}
    {!page ? (!error && <div className="live__empty">Reading live entities…</div>) : !page.active ? <div className="live__empty">The game is loading or in its menus.</div> :
      <>{page.groups ? <div className="live-hier__rows" aria-label="Live entity groups">
        {page.groups.map(g=><button key={g.name} type="button" className="live-hier__row live-hier__group"
          onClick={()=>navigate([...path,g.name])}><ChevronRight size={13}/><b>{g.name}</b><small>{g.count}</small></button>)}
        {!page.groups.length && <p className="live__empty">No matching groups.</p>}
      </div> : <div role="listbox" aria-multiselectable="true" aria-label="Live entities" className="live-hier__rows">
        {page.rows.map(row => <div key={row.ref} role="presentation" className="live-hier__entry" data-depth={row.depth}
          style={row.depth===undefined?undefined:{paddingInlineStart:Math.min(row.depth,16)*12}}>
          {!!row.children && <button type="button" className="hier__disclosure" aria-expanded={!collapsed.includes(row.ref)}
            aria-label={(collapsed.includes(row.ref)?'Expand ':'Collapse ')+row.name}
            onClick={()=>setCollapsed(current=>current.includes(row.ref)?current.filter(ref=>ref!==row.ref):[...current,row.ref])}>
            <ChevronRight size={13}/></button>}
          <button type="button" role="option" title={row.name + ' · ref:' + row.ref}
          aria-selected={selection?.epoch === page.epoch && !!selection && (selection.refs?.includes(row.ref) ?? selection.ref===row.ref)}
          className={'live-hier__row' + (selection?.epoch === page.epoch && !!selection && (selection.refs?.includes(row.ref) ?? selection.ref===row.ref) ? ' is-selected' : '')}
          onClick={event => {
            if(project) void invoke('studio.gameSelectEntity',{expectedProject:project,epoch:page.epoch,ref:row.ref,focus:event.detail===2})
              .then(onSelected).catch(e=>setError(String(e)));
          }}>
          <span className={'live-hier__owner live-hier__owner--' + Math.min(row.owner, 4)}/>
          <b>{row.name}</b><small>{row.kind}{row.health > 0 ? ' · ' + Math.round(row.health) + ' hp' : ''}</small>
        </button></div>)}
        {!page.rows.length && <p className="live__empty">{query ? 'No matching live entities.' : 'No live entities.'}</p>}
      </div>}<footer className="live-hier__pager">
        <span className="live__count">{count > pageSize ? `${offset + 1}–${offset + shown} of ${count} ${plural}` : `${count} ${count === 1 ? noun : plural}`} · tick {page.tick}</span>
        {count > pageSize && <nav aria-label="Live entity pages">
          <StudioIconButton aria-label="Previous page" title="Previous page" icon={<ChevronLeft size={14}/>} disabled={offset === 0} onClick={() => setOffset(offset - pageSize)}/>
          <StudioIconButton aria-label="Next page" title="Next page" icon={<ChevronRight size={14}/>} disabled={offset + pageSize >= count} onClick={() => setOffset(offset + pageSize)}/>
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
  return <EntityDetails detail={detail} title={'Entity '+detail.ref} subtitle={'tick '+detail.tick}/>;
}

export function PreviewInspector({detail}:{detail:NonNullable<StudioSnapshot['previewInspector']>}) {
  return <EntityDetails detail={detail} title={detail.row.name} subtitle={'Preview · '+detail.row.ref}/>;
}

function EntityDetails({detail,title,subtitle}:{detail:Pick<StudioRuntimeEntity,'components'|'stats'> & {rules?:unknown;data?:unknown};title:string;subtitle:string}){
  return <div className="live-insp">
    <header><b>{title}</b><small>{subtitle}</small></header>
    {Object.entries(detail.components).map(([name, value]) => <details key={name} open className="live-insp__comp">
      <summary>{name}</summary><Value value={value}/></details>)}
    {detail.rules!=null && <details className="live-insp__comp"><summary>Type rules</summary><Value value={detail.rules}/></details>}
    {detail.data!=null && <details className="live-insp__comp"><summary>Script data</summary><Value value={detail.data}/></details>}
    {detail.stats && <details open className="live-insp__comp"><summary>Effective stats</summary><Value value={detail.stats}/></details>}
  </div>;
}
