import { useEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { shortcutKey } from '../keyboard';
import { Box, ChevronDown, ChevronRight, Copy, Eye, EyeOff, Focus, Lightbulb, Lock, LockOpen, Plus, RefreshCw, Search, Shield, Trash2, User, X } from 'lucide-react';
import { nestedDocumentRows, groupedHierarchy, matchesHierarchy, parseHierarchyFilter, rangeIds, type HierarchyGrouping } from '../hierarchyFilter';
import { HierarchyPreviewRow } from './HierarchyPreviewRow';
import { invoke } from '../bridge';
import type { StudioMapProperties, StudioEntity, StudioEntityCatalog, StudioSnapshot, StudioHierarchyPaths } from '../types';
import { StudioButton, StudioContextMenu, StudioIconButton, StudioSelect, type StudioMenuItem } from '../ui';
import type { StudioPanelContext } from './StudioPanelRenderer';
import './HierarchyPanel.css';

const DRAG_MIME = 'application/x-aiverse-entity';
const kindIcon = (node: StudioEntity) => node.kind === 'Light' ? Lightbulb
  : node.kind === 'Collider' ? Shield : node.kind === 'Spawn' ? User : Box;

/** Compact, content-first Hierarchy: search on top, dense rows, drag reorder, context menu. */
export default function HierarchyPanel({ snapshot, busy, connected, search, setSearch,
  rows, select, selectIds, hoverId, updateSnapshot, setBusy }: StudioPanelContext) {
  const [grouping, setGrouping] = useState<HierarchyGrouping>('scene');
  const [preview,setPreview]=useState<StudioEntity[]>([]);
  const [paths,setPaths]=useState<StudioHierarchyPaths>();
  const [parents,setParents]=useState<Record<string,string>>();
  const [refresh,refreshGroups]=useState(0);
  const refreshSeen=useRef(0);
  const [collapsed, setCollapsed] = useState<Set<string>>(new Set());
  const anchor = useRef<string | null>(null);
  const [catalog, setCatalog] = useState<StudioEntityCatalog | null>(null);
  const [error, setError] = useState('');
  const [creating, setCreating] = useState(false);
  const [type, setType] = useState('');
  const [menu, setMenu] = useState<{ x: number; y: number; id: string } | null>(null);
  const [drop, setDrop] = useState<{ id: string; placement: 'before' | 'after' } | null>(null);
  const [rename, setRename] = useState<{id: string; epoch: unknown; value: string} | null>(null);
  const renameTarget = snapshot?.inspector;
  const startRename = (id: string) => {
    if (renameTarget?.id === id && renameTarget.nameEditable)
      setRename({ id, epoch: snapshot?.graphDocument?.epoch, value: renameTarget.name });
  };
  const searchRef = useRef<HTMLInputElement>(null);
  const listRef = useRef<HTMLDivElement>(null);
  const project = snapshot?.project.path;
  useEffect(() => {
    let active = true;
    setGrouping('scene'); setCollapsed(new Set()); anchor.current = null;
    setCatalog(null); setCreating(false); setError('');
    if (connected && project) void invoke('studio.entityCatalog', {}).then(result => {
      if (active) { setCatalog(result); setType(result.types[0]?.id ?? ''); }
    }).catch(e => { if (active) setError(String(e)); });
    return () => { active = false; };
  }, [connected, project]);
  const [playersOpen, setPlayersOpen] = useState(false);
  const [areasOpen, setAreasOpen] = useState(false);
  const [scenarioOpen,setScenarioOpen]=useState(false);
  const [surfacesOpen,setSurfacesOpen] = useState(false);
  const planetary = snapshot?.scene.mapProperties && snapshot.scene.mapRegions === false && !snapshot.scene.worldSpace;
  const [records, setRecords] = useState<{revision: string; epoch: string; names: string[]; scenario:NonNullable<StudioMapProperties['scenarioIndex']>; areas: string[]; bodies: {id:string;faceSize:number;metadata:Record<string,unknown>}[]}>();
  const searching = search.trim().length > 0;
  const needRecords = playersOpen || areasOpen || scenarioOpen || (planetary && surfacesOpen) || searching;
  const scope = snapshot?.graphDocument;
  useEffect(() => {
    if (!needRecords || !scope || snapshot?.scene.worldSpace) return;
    let current = true;
    void invoke('studio.mapProperties', {scope,includeBodies:!!planetary,scenarioIndex:searching || scenarioOpen}).then(data => {
      if (current) setRecords({ revision: data.scope.revision, epoch: data.scope.epoch, names: data.roster.map(p => p.name), areas: Object.keys(data.areas).sort(), scenario:data.scenarioIndex ?? [], bodies:data.bodies ?? [] });
    }).catch(e => { if (current) setError(String(e)); });
    return () => { current = false; };
  }, [needRecords, searching, scenarioOpen, planetary, scope?.projectPath, scope?.mapPath, scope?.epoch, scope?.revision, snapshot?.scene.worldSpace]);
  const currentRecords = scope && records?.revision === scope.revision && records.epoch === scope.epoch ? records : undefined;
  const [regionsOpen,setRegionsOpen] = useState(false);
  const [regionList,setRegionList] = useState<{epoch:string;revision:string;items:{letter:string;name:string;cells:number}[]}>();
  const needRegions = (searching || regionsOpen) && snapshot?.scene.mapRegions;
  useEffect(() => {
    if (!needRegions || !scope) return;
    let current = true;
    void invoke('studio.mapRegions',{scope,letter:'A'}).then(data=>{
      if (current) setRegionList({epoch:scope.epoch,revision:scope.revision,items:data.regions.filter(r=>r.cells || r.name)});
    }).catch(e=>{if(current)setError(String(e));});
    return ()=>{current=false;};
  },[needRegions,scope?.projectPath,scope?.mapPath,scope?.epoch,scope?.revision]);
  const currentRegions = scope && regionList?.epoch===scope.epoch && regionList.revision===scope.revision ? regionList.items : undefined;
  const selectRecord = (params: {kind:'player'|'wave'|'trigger';index:number} | {kind:'area'|'region'|'body';key:string}) => {
    if (scope && (params.kind==='region' ? currentRegions : currentRecords)) void invoke('studio.op', { op:'editor_select', expectedProject:scope.projectPath,
      expectedDocumentEpoch:scope.epoch, expectedDocumentRevision:scope.revision, params }).catch(e => setError(String(e)));
  };
  const mapEntities = !!catalog?.mapEntities;
  const canCreate = mapEntities && (catalog?.types.length ?? 0) > 0;
  const selectedId = snapshot?.selectedId ?? null;
  const selectedSet = useMemo(() => new Set(snapshot?.selectedIds ?? (selectedId ? [selectedId] : [])), [snapshot?.selectedIds, selectedId]);
  const editable = (id: string | null | undefined) => mapEntities && !!id?.startsWith('entity:');
  const allRows=useMemo(()=>[...rows,...preview.map(node=>({node,depth:0}))],[rows,preview]);
  const filter = useMemo(() => parseHierarchyFilter(search), [search]);
  const matches = useMemo(() => allRows.filter(({node}) => matchesHierarchy(node, filter, selectedSet, currentRecords?.names)), [allRows, filter, selectedSet, currentRecords]);
  const players = (currentRecords?.names ?? []).map((name, i) => ({name, index:i+1}))
    .filter(({name, index}) => matchesHierarchy({id:`player:${index}`, name, kind:'player', owner:index}, filter, selectedSet, currentRecords?.names));
  const areas = (currentRecords?.areas ?? []).filter(name =>
    matchesHierarchy({id:`area:${name}`, name, kind:'area'}, filter, selectedSet));

  const regions = (currentRegions ?? []).filter(({letter,name}) =>
    matchesHierarchy({id:`region:${letter}`,name:letter+' '+name,kind:'region'},filter,selectedSet));

  const scenario=(currentRecords?.scenario ?? []).filter(({kind,name,index})=>
    matchesHierarchy({id:kind+':'+index,kind,name},filter,selectedSet));
  const bodies = (currentRecords?.bodies ?? []).filter(({id,faceSize,metadata}) =>
    matchesHierarchy({id,name:id,kind:[metadata.kind,'body',faceSize ? 'planet surface' : ''].join(' ')},filter,selectedSet));
  const showSurface = (body:string) => {
    if(scope && currentRecords) void invoke('studio.planetSceneBody',{body,mapPath:scope.mapPath,expectedRevision:scope.revision})
      .catch(e=>setError(String(e)));
  };

  const selectPreview=async(id:string)=>{
    if(!scope || !snapshot || busy)return;
    try {
      updateSnapshot(await invoke('studio.select',{previewRef:id.slice('preview:'.length),scope,historyVersion:snapshot.project.historyVersion ?? ''}));
      setError('');
    }catch(e){setError(String(e));}
  };
  const group=grouping.startsWith('project:')?Number(grouping.slice(8)):-1;
  useEffect(()=>{
    setPaths(undefined);setParents(undefined);setPreview([]);
    if(!mapEntities || !project || !scope)return;
    let active=true,timer:number|undefined;
    const load=async(force:boolean)=>{
      try{
        const result=await invoke('studio.hierarchyGroups',{expectedProject:project,scope,...(group>=0?{group}:{}),refresh:force});
        if(!active)return;
        if(result.pending)timer=window.setTimeout(()=>void load(false),120);
        else {setPreview(result.preview??[]);setParents(result.parents);setPaths(result.paths??{});setError('');}
      }catch(e){if(active)setError(String(e));}
    };
    const force=refreshSeen.current!==refresh;
    refreshSeen.current=refresh;
    void load(force);
    return ()=>{active=false;window.clearTimeout(timer);};
  },[mapEntities,project,scope?.mapPath,scope?.epoch,scope?.revision,snapshot?.project.historyVersion,group,refresh]);
  const display = useMemo(() => group>=0 && !paths ? [] : group>=0 && parents ? nestedDocumentRows(matches,parents,collapsed) : groupedHierarchy(matches, grouping, collapsed,paths), [matches, grouping, collapsed,paths,group,parents]);
  const visible = useMemo(() => display.flatMap(row => 'node' in row && !row.node.preview ? [row] : []), [display]);
  const occurrenceKeys = useMemo(() => visible.map(r => r.occurrence ?? r.node.id), [visible]);
  const idsByOccurrence = useMemo(() => new Map(visible.map(r => [r.occurrence ?? r.node.id, r.node.id])), [visible]);
  const canReorder = grouping === 'scene' && !search && !busy;
  // Selection targets for row actions: the whole selection when the row is part of it.
  const toggleCollapsed=(key:string)=>setCollapsed(current=>{
    const next=new Set(current);if(next.has(key))next.delete(key);else next.add(key);return next;
  });
  const targets = (id: string) => selectedSet.has(id) ? [...selectedSet] : [id];
  const click = (event: React.MouseEvent, id: string, occurrence:string) => {
    if (event.shiftKey && anchor.current) {
      const range=rangeIds(occurrenceKeys, anchor.current, occurrence);
      const ids=new Set(range.map(key=>idsByOccurrence.get(key)!));
      ids.delete(id);
      void selectIds([...ids,id], event.metaKey || event.ctrlKey ? 'add' : 'replace');
      return;
    }
    anchor.current = occurrence;
    void select(id, event.metaKey || event.ctrlKey ? 'toggle' : 'replace');
  };
  const setFlags = (ids: string[], flags: { hidden?: boolean; locked?: boolean }) =>
    void mutate(() => invoke('studio.entityFlags', { entityIds: ids, ...flags }));
  const hover = (id: string) => void invoke('studio.viewportInput', { kind: 'hover', id }).catch(() => {});

  const mutate = async (operation: () => Promise<StudioSnapshot>) => {
    if (busy) return;
    setBusy(true); setError('');
    try { updateSnapshot(await operation()); setCreating(false); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  // Native delete/duplicate act on the current selection: select the target first.
  const onSelected = (id: string, op: (id: string) => Promise<StudioSnapshot>) => mutate(async () => {
    if (selectedId !== id) await invoke('studio.select', { id });
    return op(id);
  });
  const remove = (id: string) => {
    const ids = targets(id).filter(editable);
    if (ids.length > 1) void mutate(() => invoke('studio.entitiesDelete', { entityIds: ids }));
    else if (editable(id)) void onSelected(id, entityId => invoke('studio.entityDelete', { entityId }));
  };
  const duplicate = (id: string) => {
    const ids = targets(id).filter(editable);
    if (ids.length > 1) void mutate(() => invoke('studio.entitiesDuplicate', { entityIds: ids }));
    else if (editable(id)) void onSelected(id, entityId => invoke('studio.entityDuplicate', { entityId }));
  };
  const focus = async (id: string) => {
    if (selectedId !== id) await select(id);
    void invoke('studio.viewportInput', { kind: 'focus' }).catch(console.warn);
  };
  const create = () => { if (canCreate && type) void mutate(() => invoke('studio.entityCreate', { type, x: 0, z: 0, owner: 0 })); };

  const onKeyDown = (event: React.KeyboardEvent) => {
    if (event.target instanceof HTMLElement && event.target.closest('input,select,textarea')) {
      if (event.key === 'ArrowDown' && event.target === searchRef.current) {
        event.preventDefault(); listRef.current?.querySelector<HTMLElement>('[role=option]')?.focus();
      }
      return;
    }
    const key = shortcutKey(event);
    if ((event.metaKey || event.ctrlKey) && key === 'f') { event.preventDefault(); searchRef.current?.focus(); return; }
    if (busy) return;
    const focused = event.target instanceof HTMLElement ? event.target.closest<HTMLElement>('[data-occurrence]')?.dataset.occurrence : undefined;
    if (event.key === 'F2' && event.target instanceof HTMLElement) {
      const id = event.target.closest<HTMLElement>('[data-id]')?.dataset.id ?? selectedId;
      if (id && id === selectedId) { event.preventDefault(); startRename(id); }
      return;
    }
    const index = focused ? occurrenceKeys.indexOf(focused) : visible.findIndex(({node}) => node.id === selectedId);
    if ((event.metaKey || event.ctrlKey) && key === 'a') { event.preventDefault(); void selectIds([...new Set(visible.map(r => r.node.id))]); return; }
    if ((event.key === 'Enter' || event.key === ' ') && event.target instanceof HTMLElement && event.target.matches('[role=option]')) {
      const id = event.target.dataset.id;
      if (id) { event.preventDefault(); anchor.current = focused ?? id; void select(id, event.metaKey || event.ctrlKey ? 'toggle' : 'replace'); }
      return;
    }
    if (!visible.length) return;
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault();
      const next = visible[Math.max(0, Math.min(visible.length - 1, index + (event.key === 'ArrowDown' ? 1 : -1)))];
      if (next) { anchor.current = next.occurrence ?? next.node.id; void select(next.node.id, event.shiftKey ? 'add' : 'replace');
        listRef.current?.querySelector<HTMLElement>(`[data-occurrence="${CSS.escape(next.occurrence ?? next.node.id)}"]`)?.focus(); }
    } else if (!selectedId) return;
    else if (event.key === 'Delete' || event.key === 'Backspace') { event.preventDefault(); remove(selectedId); }
    else if ((event.metaKey || event.ctrlKey) && key === 'd') { event.preventDefault(); duplicate(selectedId); }
    else if (key === 'f' && !event.metaKey && !event.ctrlKey) { event.preventDefault(); void focus(selectedId); }
  };

  const dragOver = (event: DragEvent<HTMLElement>, id: string) => {
    if (!canReorder || !event.dataTransfer.types.includes(DRAG_MIME) || !editable(id)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'move';
    const r = event.currentTarget.getBoundingClientRect();
    const placement = event.clientY < r.top + r.height / 2 ? 'before' : 'after';
    if (drop?.id !== id || drop.placement !== placement) setDrop({ id, placement });
  };
  const onDrop = (event: DragEvent<HTMLElement>, targetId: string) => {
    const entityId = event.dataTransfer.getData(DRAG_MIME);
    const placement = drop?.placement ?? 'before';
    setDrop(null);
    if (!canReorder || !entityId || entityId === targetId || !editable(entityId) || !editable(targetId)) return;
    event.preventDefault();
    void mutate(() => invoke('studio.entityMove', { entityId, targetId, placement }));
  };

  const menuItems = (id: string): StudioMenuItem[] => [
    { id: 'focus', label: 'Focus in Scene', shortcut: 'F', icon: <Focus size={13}/>, onSelect: () => void focus(id) },
    { id: 'rename', label: 'Rename', shortcut: 'F2', disabled: busy || renameTarget?.id !== id || !renameTarget.nameEditable, onSelect: () => startRename(id) },
    { id: 'duplicate', label: 'Duplicate', shortcut: '⌘D', icon: <Copy size={13}/>, disabled: !editable(id) || busy, onSelect: () => duplicate(id) },
    { id: 'delete', label: 'Delete', shortcut: '⌫', icon: <Trash2 size={13}/>, danger: true, disabled: !editable(id) || busy, onSelect: () => remove(id) },
    { id: 'hide', label: rows.find(r => r.node.id === id)?.node.visible === false ? 'Show in Scene' : 'Hide in Scene', icon: <EyeOff size={13}/>, separator: true,
      disabled: busy, onSelect: () => setFlags(targets(id), { hidden: rows.find(r => r.node.id === id)?.node.visible !== false }) },
    { id: 'lock', label: rows.find(r => r.node.id === id)?.node.locked ? 'Unlock Picking' : 'Lock Picking', icon: <Lock size={13}/>,
      disabled: busy, onSelect: () => setFlags(targets(id), { locked: !rows.find(r => r.node.id === id)?.node.locked }) },
    { id: 'create', label: 'Create Entity…', icon: <Plus size={13}/>, separator: true, disabled: !canCreate || busy, onSelect: () => setCreating(true) },
  ];

  return <div className="hier" onKeyDown={onKeyDown}>
    <div className="hier__toolbar">
      <label className="hier__search">
        <Search size={12} aria-hidden="true"/>
        <input ref={searchRef} type="search" placeholder="Search hierarchy…" title="Search by name, #map-id, t:type, p:player, owner:1 or is:hidden" aria-label="Search scene objects"
          value={search} onChange={event => setSearch(event.target.value)}
          onKeyDown={event => { if (event.key === 'Escape' && search) { event.stopPropagation(); setSearch(''); } }}/>
        {search && <button type="button" aria-label="Clear search" onClick={() => setSearch('')}><X size={11}/></button>}
      </label>
      <StudioIconButton className="hier__tool" aria-label="Create entity" title="Create entity"
        icon={<Plus size={14}/>} disabled={busy || !canCreate} aria-expanded={creating}
        onClick={() => setCreating(value => !value)}/>
    </div>
    {project && <div className="hier__grouping"><span>Group by</span>
      <StudioSelect value={grouping} aria-label="Group scene objects by" onChange={event => {
        setGrouping(event.target.value as HierarchyGrouping); setCollapsed(new Set()); anchor.current = null; setDrop(null);
      }}>
        <option value="scene">Scene order</option><option value="type">Type</option><option value="owner-type">Owner → type</option>
        {snapshot?.scene.groupings?.map((title,i)=><option key={i} value={'project:'+i}>{title}</option>)}
      </StudioSelect>
      {mapEntities && <StudioIconButton className="hier__tool" aria-label="Refresh preview" title="Refresh preview from project files"
        icon={<RefreshCw size={13}/>} onClick={()=>refreshGroups(v=>v+1)}/>}
    </div>}
    {group>=0&&!paths&&!error&&<div className="hier__create" role="status">Reading preview…</div>}
    {creating && <form className="hier__create" onSubmit={event => { event.preventDefault(); create(); }}>
      <StudioSelect aria-label="Entity type" value={type} disabled={busy} onChange={event => setType(event.target.value)}>
        {catalog?.types.map(item => <option key={item.id} value={item.id}>{item.name || item.id}</option>)}
      </StudioSelect>
      <StudioButton type="submit" variant="primary" disabled={busy || !type}>Create</StudioButton>
      <StudioIconButton aria-label="Cancel" icon={<X size={13}/>} onClick={() => setCreating(false)}/>
    </form>}
    {error && <div className="hier__error" role="alert">{error}
      <button type="button" aria-label="Dismiss" onClick={() => setError('')}><X size={11}/></button></div>}
    <div ref={listRef} className="hier__list" role="listbox" aria-label="Scene objects" aria-multiselectable="true"
      onMouseLeave={() => hover('')}
      onDragLeave={event => { if (!event.currentTarget.contains(event.relatedTarget as Node)) setDrop(null); }}>
      {!project && <p className="hier__empty">Open a project to see its scene objects.</p>}
      {project && matches.length === 0 && (!searching || (!players.length && !areas.length && !regions.length && !bodies.length && !scenario.length)) && <p className="hier__empty">{search ? 'No matching objects.' : 'The scene is empty.'}</p>}
      {display.map(row => {
        if ('members' in row) {
          const documentMembers=row.members.filter(id=>!id.startsWith('preview:'));
          const Chevron = row.collapsed ? ChevronRight : ChevronDown;
          return <div key={'group:'+row.key} className="hier__group" style={{paddingInlineStart:4+row.depth*12}}>
            <button type="button" aria-label={(row.collapsed?'Expand ':'Collapse ')+row.label} aria-expanded={!row.collapsed}
              onClick={() => toggleCollapsed(row.key)}><Chevron size={13}/></button>
            <button type="button" title={documentMembers.length?'Select '+documentMembers.length+' map objects':'Preview objects'} disabled={busy || !documentMembers.length}
              onClick={event => {anchor.current = visible.find(r=>r.node.id===documentMembers.at(-1) && r.occurrence?.startsWith(row.key.slice(0,-1)+','))?.occurrence ?? null; void selectIds(documentMembers, event.metaKey || event.ctrlKey ? 'add' : 'replace');}}>
              <span>{row.label}</span><small>{row.members.length}</small>
            </button>
          </div>;
        }
        const {node, depth} = row;
        const Icon = kindIcon(node);
        const selected = selectedSet.has(node.id);
        const hidden = node.visible === false;
        const dragging = editable(node.id) && canReorder;
        const occurrence=row.occurrence ?? node.id;
        if(node.preview)return <HierarchyPreviewRow key={occurrence} onSelect={()=>void selectPreview(node.id)} selected={snapshot?.selectedPreviewIds?.includes(node.id)} node={node} depth={depth}
          childCount={row.children} collapsed={collapsed.has(node.id)} toggle={()=>toggleCollapsed(node.id)}/>;
        return <div key={occurrence} data-occurrence={occurrence} data-id={node.id} data-nested-leaf={row.children===0 || undefined} role="option" aria-selected={selected} tabIndex={selected ? 0 : -1}
          className={'hier__row' + (selected ? ' is-selected' : '') + (node.id === selectedId ? ' is-primary' : '') +
            (node.id === hoverId ? ' is-hover' : '') + (hidden ? ' is-hidden' : '') +
            (drop?.id === node.id ? ' is-drop-' + drop.placement : '')}
          style={{ paddingInlineStart: 6 + depth * 12 }}
          draggable={dragging}
          onDragStart={event => { event.dataTransfer.setData(DRAG_MIME, node.id); event.dataTransfer.effectAllowed = 'move'; }}
          onDragEnd={() => setDrop(null)}
          onDragOver={event => dragOver(event, node.id)}
          onDrop={event => onDrop(event, node.id)}
          onClick={event => click(event, node.id, occurrence)}
          onMouseEnter={() => hover(node.id)}
          onDoubleClick={() => void focus(node.id)}
          onContextMenu={event => { event.preventDefault(); if (!selectedSet.has(node.id)) void select(node.id); setMenu({ x: event.clientX, y: event.clientY, id: node.id }); }}>
          {!!row.children && <button type="button" className="hier__disclosure" aria-expanded={!collapsed.has(node.id)}
            aria-label={(collapsed.has(node.id)?'Expand ':'Collapse ')+node.name}
            onClick={event=>{event.stopPropagation();toggleCollapsed(node.id);}}>{collapsed.has(node.id)?<ChevronRight size={13}/>:<ChevronDown size={13}/>}</button>}
          <Icon size={12} className="hier__icon"/>
          {rename?.id === node.id && renameTarget?.id === node.id && rename.epoch === snapshot?.graphDocument?.epoch ?
            <input className="au-input hier__name" aria-label="Rename object" autoFocus value={rename.value}
              onFocus={event => event.currentTarget.select()} onClick={event => event.stopPropagation()}
              onChange={event => setRename({ ...rename, value: event.target.value })}
              onBlur={() => setRename(null)} onKeyDown={event => {
                event.stopPropagation();
                if (event.key === 'Escape') setRename(null);
                if (event.key === 'Enter') {
                  event.preventDefault();
                  const value = rename.value.trim();
                  setRename(null);
                  if (value && value !== renameTarget.name) void mutate(() => invoke('studio.setProperty',
                    { entityId: node.id, componentId: 'Entity', key: 'name', value }));
                }
              }}/> : <span className="hier__name" title={node.name}>{node.name}</span>}
          {node.kind && node.kind !== node.name && <span className="hier__kind">{node.kind}</span>}
          <button type="button" className={'hier__flag' + (hidden ? ' is-on' : '')} aria-label={hidden ? 'Show in Scene' : 'Hide in Scene'}
            title={hidden ? 'Hidden in Scene' : 'Hide in Scene'} disabled={busy}
            onClick={event => { event.stopPropagation(); setFlags(targets(node.id), { hidden: !hidden }); }}>
            {hidden ? <EyeOff size={11}/> : <Eye size={11}/>}</button>
          <button type="button" className={'hier__flag' + (node.locked ? ' is-on' : '')} aria-label={node.locked ? 'Unlock picking' : 'Lock picking'}
            title={node.locked ? 'Locked: not pickable in Scene' : 'Lock picking'} disabled={busy}
            onClick={event => { event.stopPropagation(); setFlags(targets(node.id), { locked: !node.locked }); }}>
            {node.locked ? <Lock size={11}/> : <LockOpen size={11}/>}</button>
        </div>;
      })}
    </div>
    {scope && !snapshot?.scene.worldSpace && (!searching || players.length > 0) && <details className="hier__players" open={searching || playersOpen}
      onToggle={event => { if (!searching) setPlayersOpen(event.currentTarget.open); }}>
      <summary>Map players</summary>
      {(playersOpen || searching) && players.map(({name,index}) => <StudioButton key={index} size="sm" disabled={busy}
          onClick={() => selectRecord({kind:'player',index})}>
          {index} · {name}
        </StudioButton>)}
    </details>}
    {scope && !snapshot?.scene.worldSpace && (!searching || areas.length > 0) && <details className="hier__players" open={searching || areasOpen}
      onToggle={event => { if (!searching) setAreasOpen(event.currentTarget.open); }}>
      <summary>Map areas</summary>
      {(areasOpen || searching) && currentRecords && (areas.length ? areas.map(key =>
        <StudioButton key={key} size="sm" disabled={busy} onClick={() => selectRecord({kind:'area',key})}>{key}</StudioButton>)
        : <span>No named areas</span>)}
    </details>}
    {scope && snapshot?.scene.mapRegions && (!searching || regions.length > 0) && <details className="hier__players" open={searching || regionsOpen}
      onToggle={event => { if (!searching) setRegionsOpen(event.currentTarget.open); }}>
      <summary>Map regions</summary>
      {(regionsOpen || searching) && currentRegions && (regions.length ? regions.map(({letter,name,cells}) =>
        <StudioButton key={letter} size="sm" disabled={busy} onClick={()=>selectRecord({kind:'region',key:letter})}>
          {letter}{name && ' · '+name} ({cells})
        </StudioButton>) : <span>No named or painted regions</span>)}
    </details>}
    {scope && !snapshot?.scene.worldSpace && (!searching || scenario.length>0) && <details className="hier__players" open={searching || scenarioOpen}
      onToggle={event=>{if(!searching)setScenarioOpen(event.currentTarget.open);}}>
      <summary>Waves and triggers</summary>
      {(searching || scenarioOpen) && scenario.map(({kind,index,name})=><StudioButton key={kind+index} size="sm" disabled={busy}
        onClick={()=>selectRecord({kind,index})}>{name}</StudioButton>)}
      {scenarioOpen && !searching && currentRecords && !scenario.length && <span>No waves or triggers</span>}
    </details>}
    {planetary && (!searching || bodies.length > 0) && <details className="hier__players" open={searching || surfacesOpen}
      onToggle={event=>{if(!searching)setSurfacesOpen(event.currentTarget.open);}}>
      <summary>Celestial bodies</summary>
      {(surfacesOpen || searching) && currentRecords && <>
        {!searching && <StudioButton size="sm" disabled={busy} onClick={()=>showSurface('')}>Follow entity selection</StudioButton>}
        {bodies.map(({id,faceSize})=><div key={id}>
          <StudioButton size="sm" disabled={busy} onClick={()=>selectRecord({kind:'body',key:id})}>
            {id} · {faceSize ? faceSize+' × '+faceSize+' per face' : 'No surface'}
          </StudioButton>
          {faceSize>0 && <StudioIconButton aria-label={'Show '+id+' in Scene'} title={'Show '+id+' in Scene'} disabled={busy}
            icon={<Focus size={13}/>} onClick={()=>showSurface(id)}/>}
        </div>)}
      </>}
    </details>}
    <div className="hier__footer">{search ? `${matches.length} / ${allRows.length}` : allRows.length} objects{preview.length>0 && ` · ${preview.length} preview`}{selectedSet.size > 1 ? ` · ${selectedSet.size} selected` : ''}</div>
    {menu && <StudioContextMenu x={menu.x} y={menu.y} label="Entity actions"
      items={menuItems(menu.id)} onClose={() => setMenu(null)}/>}
  </div>;
}
