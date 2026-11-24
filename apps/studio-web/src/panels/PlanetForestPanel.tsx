import {useEffect,useRef,useState} from 'react';
import {invoke,onNativeEvent} from '../bridge';
import type {StudioPlanetForestDocument,StudioPlanetForestLive} from '../types';
import AssetReferencePicker from '../components/AssetReferencePicker';
import {StudioInput,StudioSelect} from '../ui';
import {object,rows,patchRow,removeRow,nextId,collisionMode,setPlacement,type ForestRecord} from './planetForestSchema';
import './PlanetForestPanel.css';

type NumSpec=readonly [string,string,number,number,number];
const cadence:NumSpec[]=[['growthInterval','Growth interval (ticks)',32,1,100000],['decayInterval','Decay interval (ticks)',64,1,100000],['nutrientInterval','Nutrient interval (ticks)',256,1,100000],['baselineNutrients','Baseline nutrients (‰)',500,0,1000],['maxPlants','Max plants',4096,1,4096],['naturalFallPermille','Natural fall threshold (‰)',0,0,1000]];
const ecology:NumSpec[]=[['id','Species ID',1,1,1000000],['matureTicks','Maturity (ticks)',120000,1,1000000000],['lifespanTicks','Lifespan (ticks)',2400000,2,1000000000],['seedIntervalTicks','Seed interval (ticks)',24000,1,1000000000],['seedSpreadCm','Seed spread (cm)',700,50,20000],['shadeDeathTicks','Shade death (ticks)',240000,1,1000000000],['floodDeathTicks','Flood death (ticks)',80000,1,1000000000],['droughtDeathTicks','Drought death (ticks)',240000,1,1000000000]];
const profile:NumSpec[]=[['id','Species ID',1,1,1000000],['prototypeSeed','Prototype seed',1,0,2147483647],['minHeightCm','Min height (cm)',800,100,100000],['maxHeightCm','Max height (cm)',1500,100,100000]];
const custom:NumSpec[]=[['trunkRadiusCm','Trunk radius (cm)',24,1,10000],['branches','Branches',22,0,256],['leafRadiusCm','Leaf radius (cm)',95,0,100000],['whorlSize','Whorl size',0,0,8]];
function Numbers({data,specs,change}:{data:ForestRecord;specs:NumSpec[];change:(patch:ForestRecord)=>void}){
  return <>{specs.map(([key,label,def,min,max])=><label className="pf-row" key={key}><span>{label}</span><input className="au-input" type="number" min={min} max={max} step={1} value={data[key]===undefined?'':String(data[key])} placeholder={String(def)} onChange={e=>change({[key]:e.target.value===''?undefined:Number(e.target.value)})}/></label>)}</>;
}
const err=(e:unknown)=>e instanceof Error?e.message:String(e);
export default function PlanetForestPanel({historyVersion,onChanged}:{historyVersion?:string;onChanged:()=>void}){
  const [base,setBase]=useState<StudioPlanetForestDocument|null>(null);
  const [forest,setForest]=useState<ForestRecord|null>(null),[catalog,setCatalog]=useState<ForestRecord|null>(null);
  const [override,setOverride]=useState(false),[busy,setBusy]=useState(false),[error,setError]=useState('');
  const [dirty,setDirty]=useState(false),[stale,setStale]=useState(false),[live,setLive]=useState<StudioPlanetForestLive|null>(null);
  const [body,setBody]=useState(''),[filter,setFilter]=useState('');
  const [sceneBody,setSceneBody]=useState(''),[sceneBodyBusy,setSceneBodyBusy]=useState(false);
  async function changeSceneBody(value:string){
    if(!base)return;setSceneBodyBusy(true);setError('');
    try{const r=await invoke('studio.planetSceneBody',{body:value,mapPath:base.mapPath,expectedRevision:base.revision});setSceneBody(r.override);setSelectedPlant(null);setLive(null);if(value)setBody(value);}
    catch(e){setError(err(e));}finally{setSceneBodyBusy(false);}
  }
  const [armPending,setArmPending]=useState(false);
  const [action,setAction]=useState<'plant'|'pick'|'move'>('plant');
  const [selectedPlant,setSelectedPlant]=useState<{species:number;key:number;direct:boolean;body:string}|null>(null);
  const [armed,setArmed]=useState(false),[plantSpecies,setPlantSpecies]=useState(''),[habit,setHabit]=useState<'tree'|'shrub'>('tree');
  const baseRef=useRef<StudioPlanetForestDocument|null>(null);
  const busyRef=useRef(false);
  function accept(d:StudioPlanetForestDocument){setSelectedPlant(null);baseRef.current=d;setBase(d);setForest(d.forest===null?null:object(d.forest));setCatalog(d.catalog===null?null:object(d.catalog));setOverride(d.override);setDirty(false);setStale(false);setLive(null);}
  async function reload(){busyRef.current=true;setBusy(true);setError('');try{const d=await invoke('studio.planetForest',{});accept(d);if(d.available){try{setSceneBody((await invoke('studio.planetSceneBody',{})).override);}catch{setSceneBody('');}}}catch(e){setError(err(e));}finally{busyRef.current=false;setBusy(false);}}
  useEffect(()=>{void reload();return ()=>{void invoke('studio.planetForestArm',{armed:false}).catch(()=>{});};},[]); // mount per project
  // Recheck the native document identity on history/document notifications.
  // Do not overwrite unsaved draft state, or mark our own successful Apply stale.
  async function checkRevision(){
    if(!baseRef.current||busyRef.current)return;
    try{const d=await invoke('studio.planetForest',{}),current=baseRef.current;
      if(!busyRef.current&&current&&(d.mapPath!==current.mapPath||d.revision!==current.revision||
         JSON.stringify(d.forest)!==JSON.stringify(current.forest)||JSON.stringify(d.catalog)!==JSON.stringify(current.catalog))){
        setStale(true);setLive(null);setSelectedPlant(null);
      }
    }catch{setStale(true);}
  }
  useEffect(()=>{void checkRevision();},[historyVersion]);
  useEffect(()=>onNativeEvent((event,payload)=>{if(event==='document.changed')void checkRevision();
    if(event==='planetForest.planted'){
      setArmed(false);const result=payload as {document?:StudioPlanetForestDocument;error?:string;selected?:{species:number;key:number;direct:boolean;body:string}};
      if(result?.document){accept(result.document);onChanged();}else if(result?.error)setError(result.error);
      if(result?.selected){setSelectedPlant(result.selected);setBody(result.selected.body);setFilter('');}
    }
  }),[]);
  async function arm(next:boolean,mode:'plant'|'pick'|'move'='plant'){
    if(!base||armPending)return;
    setArmPending(true);setError('');setAction(mode);
    if(!next||mode==='pick')setSelectedPlant(null);
    try{const r=await invoke('studio.planetForestArm',{armed:next,mapPath:base.mapPath,expectedRevision:base.revision,
      species:mode==='move'?selectedPlant?.species:Number(plantSpecies||rows(forest?.species)[0]?.id),key:mode==='move'?selectedPlant?.key:undefined,habit,action:mode});setArmed(r.armed);}
    catch(e){setArmed(false);setError(err(e));}finally{setArmPending(false);}
  }
  function editForest(next:ForestRecord|null){setSelectedPlant(null);setForest(next);setOverride(next!==null);setDirty(true);}
  function editCatalog(next:ForestRecord|null){setCatalog(next);setDirty(true);}
  async function apply(){if(!base)return;busyRef.current=true;setBusy(true);setError('');try{
    const d=await invoke('studio.planetForestSet',{mapPath:base.mapPath,expectedRevision:base.revision,forest:override?forest:null,catalog});
    accept(d);onChanged();
  }catch(e){setError(err(e));}finally{busyRef.current=false;setBusy(false);}}
  const [overlayRequest,setOverlayRequest]=useState(false);
  const [overlayNotice,setOverlayNotice]=useState('');
  async function setLiveOverlay(enabled:boolean){setError('');setOverlayRequest(true);try{
    const result=await invoke('studio.evolveLayer',enabled?{live:true,liveForest:true}:{liveForest:false});
    setOverlayNotice(result.liveForest&&result.live?'Live forest requested. Scene status reports actual availability.':'Live forest overlay disabled.');
  }catch(e){setOverlayNotice('');setError(err(e));}finally{setOverlayRequest(false);}}
  async function sample(){setError('');try{setLive(await invoke('studio.planetForestLive',{}));}catch(e){setError(err(e));}}
  if(!base)return <section className="rules__form">{error||'Loading planet forest…'} <button onClick={()=>void reload()}>Reload</button></section>;
  if(!base.available)return <section className="rules__form">Planetary map required. <button onClick={()=>void reload()}>Reload</button></section>;
  const species=rows(forest?.species),plants=rows(forest?.plants),weather=rows(forest?.weather),profiles=rows(catalog?.species);
  const bodies=base.bodies??[...new Set(base.markers.map(m=>m.placement.body))];
  const markers=base.markers.filter(m=>!body||m.placement.body===body);
  const patch=(key:string,i:number,p:ForestRecord)=>forest&&editForest(patchRow(forest,key,i,p));
  const markerPicker=(value:unknown,change:(v:string)=>void)=><select className="au-input" value={String(value??'')} onChange={e=>change(e.target.value)}>
    <option value="">Choose existing surface marker</option>{Boolean(value)&&!base.markers.some(m=>m.id===value)&&<option value={String(value)}>Missing: {String(value)}</option>}
    {base.markers.map(m=><option key={m.id} value={m.id}>{m.id} · {m.placement.body} / {m.placement.face}</option>)}</select>;
  function placementEditor(key:'plants'|'weather',row:ForestRecord,i:number){
    if(!forest||!base)return null;
    const at=object(row.at);
    const direct=Boolean(row.at);
    const change=(placement:{at:ForestRecord}|{entity:string})=>editForest(setPlacement(forest,key,i,placement));
    const update=(p:ForestRecord)=>change({at:{...at,...p}});
    const marker=base.markers.find(m=>m.id===row.entity);
    return <>
      <label className="pf-row"><span>Placement</span><StudioSelect value={direct?'surface':'marker'} onChange={e=>{
        if(e.target.value==='surface'){
          const source=object(marker?.placement);
          change({at:{body:source.body??(body||bodies[0]||''),face:source.face??0,x:source.x??0,y:source.y??0}});
        }else change({entity:markers[0]?.id??base.markers[0]?.id??''});
      }}><option value="marker" disabled={!base.markers.length}>Surface marker</option><option value="surface">Direct surface coordinates</option></StudioSelect></label>
      {direct?<>
        <label className="pf-row"><span>Planet / moon</span><StudioSelect value={String(at.body??'')} onChange={e=>update({body:e.target.value})}>
          <option value="">Choose body</option>{bodies.map(b=><option key={b}>{b}</option>)}</StudioSelect></label>
        <label className="pf-row"><span>Cube face</span><StudioSelect value={String(at.face??'')} onChange={e=>update({face:Number(e.target.value)})}>{[0,1,2,3,4,5].map(n=><option key={n} value={n}>{n}</option>)}</StudioSelect></label>
        {(['x','y'] as const).map(axis=><label className="pf-row" key={axis}><span>Face {axis.toUpperCase()}</span><StudioInput type="number" min={0} step="any" value={typeof at[axis]==='number'?String(at[axis]):''} onChange={e=>update({[axis]:e.target.value===''?null:Number(e.target.value)})}/></label>)}
        <p className="pf-note">Face-local coordinates, not world XYZ. Native validation checks the selected body's face bounds on Apply.</p>
      </>:<label className="pf-row"><span>Surface marker</span>{markerPicker(row.entity,v=>change({entity:v}))}</label>}
    </>;
  }
  return <section className="rules__form pf">
    <div className="pf-toolbar"><h3>Planet forest</h3><button disabled={busy||armed||armPending} onClick={()=>void reload()}>{dirty?'Discard & reload':'Reload'}</button><button disabled={busy||armed||armPending||!dirty||stale} onClick={()=>void apply()}>Apply to map</button></div>
    {error&&<div className="rules__error" role="alert">{error}</div>}
    {stale&&<p className="pf-note">Document changed. Reload before applying; draft has not been overwritten.</p>}
    <p className="pf-note">Map-owned override · Undo/Redo via document history. Save the map and restart local Play. Native physics authority is configured by the host, not this panel.</p>
    {!base.moduleEnabled&&<p className="pf-note">world_processes native module is not installed in this project. Only the visual ARBOR catalogue can be edited.</p>}
      <label className="pf-row"><span>Scene body</span><StudioSelect disabled={busy||armed||armPending||sceneBodyBusy||stale} value={sceneBody} onChange={e=>void changeSceneBody(e.target.value)}><option value="">Auto (entity selection)</option>{bodies.map(b=><option key={b} value={b}>{b}</option>)}</StudioSelect></label>
    <fieldset disabled={busy||armed||armPending} className="rules__group"><legend>Simulation</legend>
      <label className="pf-row"><span>Map override</span><input type="checkbox" checked={override} disabled={!base.moduleEnabled} onChange={e=>{setOverride(e.target.checked);if(e.target.checked&&!forest)setForest({species:[{id:1}],plants:[]});setDirty(true);}}/></label>
      <p className="pf-note">{override?'Use this map’s simulation settings':'Inherit native module rules'} · physics requested: {base.physicsRequested?'yes':'no'}</p>
      {override&&forest&&<Numbers data={forest} specs={cadence} change={p=>editForest({...forest,...p})}/>}
    </fieldset>
    {override&&forest&&<>
    <fieldset disabled={busy||armed||armPending} className="rules__group"><legend>Ecology species</legend>
      {species.map((row,i)=><details key={i} open={i===0}><summary>Species {String(row.id??i+1)}</summary>
        <Numbers data={row} specs={ecology} change={p=>patch('species',i,p)}/>
        <label className="pf-row"><span>Collision</span><select className="au-input" value={!row.naturalFallBody?'none':object(row.naturalFallBody).fromArbor?'arbor':'capsule'} onChange={e=>editForest({...forest,species:species.map((r,j)=>j===i?collisionMode(r,e.target.value):r)})}><option value="none">None / external owner</option><option value="arbor">ARBOR trunk</option><option value="capsule">Manual capsule</option></select></label>
        {!!row.naturalFallBody&&<Numbers data={object(row.naturalFallBody)} specs={[...(object(row.naturalFallBody).fromArbor?[]:[['radiusCm','Radius (cm)',15,2,100000],['halfHeightCm','Half height (cm)',200,2,100000]] as NumSpec[]),['density','Density',600,1,1000000],['frictionPermille','Friction (‰)',500,0,10000]]} change={p=>patch('species',i,{naturalFallBody:{...object(row.naturalFallBody),...p}})}/>}
        <button onClick={()=>editForest(removeRow(forest,'species',i))}>Remove species</button>
      </details>)}
      <button disabled={species.length>=64} onClick={()=>editForest({...forest,species:[...species,{id:nextId(species,'id')}]})}>Add species</button>
    </fieldset>
    <fieldset disabled={busy} className="rules__group"><legend>Rooted plants</legend>
      <label className="pf-row"><span>Plant species</span><select disabled={armed||armPending} className="au-input" value={plantSpecies||String(species[0]?.id??'')} onChange={e=>setPlantSpecies(e.target.value)}>{species.map((s,i)=><option key={i} value={String(s.id)}>{String(s.id)}</option>)}</select></label>
      <label className="pf-row"><span>Plant habit</span><select disabled={armed||armPending} className="au-input" value={habit} onChange={e=>setHabit(e.target.value as 'tree'|'shrub')}><option>tree</option><option>shrub</option></select></label>
      <button disabled={armPending||(!armed&&(dirty||stale||!species.length))} onClick={()=>void arm(!armed)}>{armed?'Cancel Scene action':'Plant in Scene'}</button>
      <button disabled={armed||armPending||dirty||stale||!plants.length} onClick={()=>void arm(true,'pick')}>Pick plant in Scene</button>
      <button disabled={armed||armPending||dirty||stale||!selectedPlant?.direct} onClick={()=>void arm(true,'move')}>Move selected plant in Scene</button>
      {selectedPlant&&<p className="pf-note">Selected: {selectedPlant.species}:{selectedPlant.key}{!selectedPlant.direct?' · convert marker binding to direct coordinates and Apply before moving':''}</p>}
      {armed&&<p className="pf-note">{action==='pick'?'Click a visible authored plant trunk.':action==='move'?'Click the destination or hold and drag across terrain, then release to relocate the selected plant.':'Click planetary terrain once.'} Esc cancels.</p>}
      <fieldset disabled={armed||armPending} className="rules__group"><legend>Plant placements</legend>
      <label className="pf-row"><span>Planet / moon</span><select className="au-input" value={body} onChange={e=>setBody(e.target.value)}><option value="">All bodies</option>{bodies.map(b=><option key={b}>{b}</option>)}</select></label>
      <label className="pf-row"><span>Search markers</span><input className="au-input" value={filter} onChange={e=>setFilter(e.target.value)}/></label>
      {plants.map((row,i)=>({row,i})).filter(({row})=>(!body||(object(row.at).body??base.markers.find(m=>m.id===row.entity)?.placement.body)===body)&&String(row.entity??object(row.at).body??'').toLowerCase().includes(filter.toLowerCase())).map(({row,i})=><details key={i} open={selectedPlant?.species===Number(row.species)&&selectedPlant?.key===Number(row.key)?true:undefined}><summary>{selectedPlant?.species===Number(row.species)&&selectedPlant?.key===Number(row.key)?'◆ ':''}{String(row.entity??(row.at?String(object(row.at).body)+' / '+String(object(row.at).face):'Unbound'))} · {String(row.species)}:{String(row.key)}</summary>
        {placementEditor('plants',row,i)}
        <label className="pf-row"><span>Species</span><select className="au-input" value={String(row.species??'')} onChange={e=>patch('plants',i,{species:Number(e.target.value)})}><option value="">Choose species</option>{species.map((r,j)=><option key={j} value={String(r.id)}>{String(r.id)}</option>)}</select></label>
        <Numbers data={row} specs={[['key','Stable plant key',1,1,1000000000],['ageTicks','Initial age (ticks; blank = mature)',0,0,1000000000]]} change={p=>patch('plants',i,p)}/>
        <label className="pf-row"><span>Habit</span><select className="au-input" value={String(row.habit??'tree')} onChange={e=>patch('plants',i,{habit:e.target.value})}><option>tree</option><option>shrub</option></select></label>
        <button onClick={()=>editForest(removeRow(forest,'plants',i))}>Remove plant</button>
      </details>)}
      <button disabled={!bodies.length||!species.length||plants.length>=4096} onClick={()=>editForest({...forest,plants:[...plants,{at:{body:body||bodies[0],face:0,x:0,y:0},species:species[0].id,key:nextId(plants,'key'),habit:'tree'}]})}>Add plant by coordinates</button>
      <button disabled={!markers.length||!species.length||plants.length>=4096} onClick={()=>editForest({...forest,plants:[...plants,{entity:markers[0].id,species:species[0].id,key:nextId(plants,'key'),habit:'tree'}]})}>Add plant at marker</button>
      </fieldset>
    </fieldset>
    <fieldset disabled={busy||armed||armPending} className="rules__group"><legend>PHYSIS weather mappings</legend>
      {weather.map((row,i)=><details key={i}><summary>{String(row.entity??object(row.at).body??'Unbound')}</summary>{placementEditor('weather',row,i)}<Numbers data={row} specs={[['x','Climate cell X',0,-1000000,1000000],['z','Climate cell Z',0,-1000000,1000000],['coverageMetres','Coverage (m)',32,1,4096]]} change={p=>patch('weather',i,p)}/><button onClick={()=>editForest(removeRow(forest,'weather',i))}>Remove mapping</button></details>)}
      <button disabled={!bodies.length} onClick={()=>editForest({...forest,weather:[...weather,{at:{body:body||bodies[0],face:0,x:0,y:0},x:0,z:0,coverageMetres:32}]})}>Add mapping by coordinates</button>
      <button disabled={!markers.length} onClick={()=>editForest({...forest,weather:[...weather,{entity:markers[0].id,x:0,z:0,coverageMetres:32}]})}>Add mapping</button>
    </fieldset></>}
    <fieldset disabled={busy||armed||armPending} className="rules__group"><legend>ARBOR visual catalogue</legend>
      {!catalog?<button onClick={()=>editCatalog({species:[{id:1,preset:'oak'}]})}>Create catalogue</button>:<>
      <div className="pf-row"><span>Botanical textures (content folder)</span><AssetReferencePicker folderMode label="botanical texture folder" disabled={busy||armed||armPending} value={String(catalog.botanicalTextures??'')} onChange={value=>editCatalog({...catalog,botanicalTextures:value})}/></div>
      {profiles.map((row,i)=><details key={i}><summary>ARBOR {String(row.id??i+1)} · {String(row.preset??row.shape??'custom')}</summary>
        <label className="pf-row"><span>Preset</span><select className="au-input" value={String(row.preset??'')} onChange={e=>editCatalog(patchRow(catalog,'species',i,{preset:e.target.value||undefined}))}><option value="">Custom</option>{(base.presets??[]).map(p=><option key={p}>{p}</option>)}</select></label>
        <Numbers data={row} specs={profile} change={p=>editCatalog(patchRow(catalog,'species',i,p))}/>
        {!row.preset&&<><label className="pf-row"><span>Crown shape</span><select className="au-input" value={String(row.shape??'broadleaf')} onChange={e=>editCatalog(patchRow(catalog,'species',i,{shape:e.target.value}))}><option>broadleaf</option><option>conifer</option><option>columnar</option></select></label><Numbers data={row} specs={custom} change={p=>editCatalog(patchRow(catalog,'species',i,p))}/></>}
        <button onClick={()=>editCatalog(removeRow(catalog,'species',i))}>Remove profile</button>
      </details>)}
      <button disabled={profiles.length>=64} onClick={()=>editCatalog({...catalog,species:[...profiles,{id:nextId(profiles,'id'),preset:'oak'}]})}>Add ARBOR profile</button>
      <button onClick={()=>editCatalog(null)}>Remove catalogue</button></>}
    </fieldset>
    <fieldset className="rules__group"><legend>Live forest inspector</legend><button disabled={overlayRequest} onClick={()=>void setLiveOverlay(true)}>Show live forest in Scene</button><button disabled={overlayRequest} onClick={()=>void setLiveOverlay(false)}>Hide live forest overlay</button>{overlayNotice&&<p role="status" className="pf-note">{overlayNotice}</p>}<p className="pf-note">Live meshes and sampled terrain require the current saved local Play map. Choose a Scene body above (or Auto to follow entity selection). Disable Live before authoring forest placement; enable Scene gizmos for diagnostics. Green/red: root support; yellow: pending; orange/blue: awake/sleeping capsule; grey: spent or harvested. Shared sample budget may omit plants.</p><button onClick={()=>void sample()}>Sample completed tick</button>
      {live&&!live.active&&<p className="pf-note">Start local Play with the current saved planetary map.</p>}
      {live?.active&&<><p>Tick {live.tick} · plants {live.plantCount} · dead {live.deadCount} · fallen {live.fallenCount} · spent {live.spentCount} · physical {live.physicalCount} · pending {live.pendingCount}</p>
      <p>{live.focusBody||'All bodies'} · sampled {(live.plants??[]).length} / {live.focusPlantCount??live.plantCount??0} plants; summary counts are global.</p>{live.truncated&&<p className="pf-note">Sample limited by the shared diagnostic budget.</p>}{(live.windZonesCapped||live.stormsCapped)&&<p className="pf-note">Wind field sample capped; local zones or storms omitted.</p>}
      {(live.plants??[]).map(p=><details key={p.species+':'+p.key}><summary>{p.body} · {p.species}:{p.key} · {p.spent?'spent':p.harvested?'harvested':p.physical?'physical':p.pending?'pending':p.fallen?'fallen':p.dead?'dead':'standing'}</summary><p>Cell {p.cell} · age {p.ageTicks} · root support {p.supportPermille}‰</p><p>Size {p.sizePermille??'—'}‰ · foliage {p.foliagePermille??'—'}‰ · exposed roots {p.exposedCm??'—'} cm</p><p>PHYSIS {p.climateTick??'—'} · tangent wind {p.windLocal?.map(v=>v.toFixed(2)).join(' / ')??'—'} m/s · season {p.seasonPermille??'—'}‰</p><p>Storm contribution {p.stormWindLocal?.map(v=>v.toFixed(2)).join(' / ')??'—'} m/s</p><p>Wood {p.remainingUnits??'—'} / {p.initialUnits??'—'} · motion tick {p.motionTick??'—'} {p.awake?'· awake':''}</p></details>)}</>}
    </fieldset>
  </section>;
}
