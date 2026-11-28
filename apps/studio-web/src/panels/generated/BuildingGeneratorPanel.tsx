import { revealProjectAsset } from '../../assetNavigation';
import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { Building2, Dices, RefreshCw, WandSparkles } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../../bridge';
import type {
  StudioSnapshot, StudioBuildingRequest, StudioBuildingStyle,
  StudioBuildingPreview, StudioBuildingGenerated,
  StudioBuildingFeature, StudioBuildingFeatureKind
} from '../../types';
import type { StudioPanel as PanelId } from '../../nestedDock';
import {
  StudioPanel, StudioSection, StudioBadge, StudioAlert, StudioEmptyState,
  StudioButton, StudioIconButton, StudioField, StudioInput, StudioSelect, StudioTabs
} from '../../ui';
import BuildingMaterialEditor from './BuildingMaterialEditor';
import BuildingPlanPreview from './BuildingPlanPreview';
import {BuildingLiveGraphMonitor} from '../../graphs/BuildingLiveGraphMonitor';
import {BuildingMaterialDependenciesView} from '../../graphs/BuildingMaterialDependenciesView';
import {BuildingStructureGraphView} from '../../graphs/BuildingStructureGraphView';
import {BuildingCandidateGate,buildingDraftKey} from '../../graphs/buildingCandidateCapture';
import type {BuildingCandidateTicket} from '../../graphs/buildingCandidateCapture';
import type { MaterialLibrary } from '../materialSchema';
import './BuildingGeneratorPanel.css';

function BuildingSurfacePreview({style,visible,resolution,projectLoaded,onPublished}:{style:StudioBuildingStyle|undefined;visible:boolean;resolution:128|256|512;projectLoaded:boolean;onPublished:()=>void}){
  const slots=Object.keys(style?.surfaces??{}).filter(slot=>!!style?.surfaces?.[slot]?.pattern);
  const [selected,setSelected]=useState('');
  const slot=slots.includes(selected)?selected:slots[0]??'';
  const [result,setResult]=useState<import('../../types').StudioBuildingSurfacePreview|null>(null);
  const [error,setError]=useState('');
  const [busy,setBusy]=useState(false);
  const styleId=style?.id??'';
  const [name,setName]=useState('');
  const [publishing,setPublishing]=useState(false);
  const [published,setPublished]=useState('');
  const defaultName=('building-'+styleId+'-'+slot+'-'+resolution).slice(0,64);
  const publishName=name||defaultName;
  const publish=async()=>{
    if(publishing||!projectLoaded||!slot||!nativeConnected())return;
    setPublishing(true);setError('');setPublished('');
    try{
      const result=await invoke('studio.buildingPublishMaterial',{style:styleId,slot,name:publishName,resolution});
      setPublished(result.name+' · '+result.assets.length+' new textures');onPublished();
      window.dispatchEvent(new Event('studio:assets-changed'));
      void invoke('studio.windowBroadcast',{event:'ui.assetsChanged',payload:{path:result.path}}).catch(()=>{});
    }catch(e){setError(explain(e));}finally{setPublishing(false);}
  };
  useEffect(()=>{
    setResult(null);setError('');setBusy(false);
    if(!visible||!styleId||!slot||!nativeConnected())return;
    let alive=true;setBusy(true);
    void invoke('studio.buildingSurfacePreview',{style:styleId,slot,resolution}).then(value=>{
      if(!alive)return;
      if(value.style!==styleId||value.slot!==slot||value.resolution!==resolution)throw Error('Texture preview identity mismatch');
      setResult(value);
    }).catch(e=>{if(alive)setError(explain(e));}).finally(()=>{if(alive)setBusy(false);});
    return()=>{alive=false};
  },[styleId,slot,visible,resolution]);
  const current=result?.style===styleId&&result.slot===slot&&result.resolution===resolution?result:null;
  return <StudioSection title="Generated PBR textures">
    {!slot?<StudioEmptyState title="No procedural surface" description="This style has no procedural surface patterns."/>:<>
      <StudioField label="Material slot" htmlFor="abg-surface-slot">
        <StudioSelect id="abg-surface-slot" value={slot} onChange={e=>setSelected(e.target.value)}>
          {slots.map(s=><option value={s} key={s}>{s} · {style?.surfaces?.[s]?.pattern}</option>)}
        </StudioSelect>
      </StudioField>
      {busy&&<small>Generating native PBR maps…</small>}
      {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
      {current&&<div className="abg__surface-grid">
        {current.maps.map(map=><figure key={map.channel}>
          <img src={map.url} alt={map.label+' generated texture'} width={128} height={128}/>
          <figcaption>{map.label}</figcaption>
        </figure>)}
      </div>}
      <StudioField label="Reusable material name" htmlFor="abg-publish-material">
        <StudioInput id="abg-publish-material" value={publishName} disabled={publishing} onChange={e=>setName(e.target.value)}/>
      </StudioField>
      <StudioButton disabled={!projectLoaded||publishing||!nativeConnected()||!/^[a-zA-Z0-9_-]{1,64}$/.test(publishName)} onClick={()=>void publish()}>
        {publishing?'Publishing…':'Publish textures + material'}
      </StudioButton>
      {published&&<small>{published}</small>}
      <small className="abg__note">Publishes the authored style slot, not a seed palette or project override. Existing names are never replaced.</small>
      <small className="abg__note">Actual native style maps (including material cavity AO) · {resolution} × {resolution} · deterministic material seed. Base Color is neutral modulation of the authored tint. Imported project textures override corresponding channels; GLB embeds the generated PNGs for remaining channels.</small>
    </>}
  </StudioSection>;
}

type Candidate = { seed:number; batch:number; scope:string; recipe:StudioBuildingRequest; data?:StudioBuildingPreview; error?:string };
const explain = (error:unknown) => error instanceof Error ? error.message : String(error);
const settingLabel = (id:string) => ({
  'medieval-fantasy':'Medieval Fantasy',
  'dark-fantasy':'Dark Fantasy',
  steampunk:'Steampunk',
  cyberpunk:'Cyberpunk',
  'scifi-space':'Sci-Fi / Space',
  scifi:'Sci-Fi (original)',
  'alien-civilization':'Alien Civilization',
  'post-apocalypse':'Post-Apocalypse',
  modern:'Modern / Contemporary',
  'ancient-civilizations':'Ancient Civilizations',
  'asian-historical':'Asian Historical',
  solarpunk:'Solarpunk',
  underwater:'Underwater',
} as Record<string,string>)[id] ?? id;
const asNumber = (raw:string) => raw.trim()==='' ? NaN : Number(raw);
const makeFeature = (kind:StudioBuildingFeatureKind):StudioBuildingFeature => ({
  kind, side:'front', count:1, width:kind==='large-gate'?5:kind==='small-gate'?2.7:kind==='sliding-roof'?5:kind==='window'?1.6:kind==='stairs'?2.6:kind==='elevator'?2:1.2,
  height:kind==='large-gate'?4.5:kind==='small-gate'?2.9:kind==='sliding-roof'?0:kind==='window'?1.1:2.35,
  depth:kind==='sliding-roof'?5:kind==='stairs'?6.6:kind==='elevator'?2.2:0, open:0,
  position:-1, positionZ:-1, spacing:0.35, floor:0, sill:0.9
});
const propertyLabels:Record<StudioBuildingFeatureKind,string> = {
  'large-gate':'Large vehicle gates', 'small-gate':'Small gates',
  'personnel-door':'Personnel entrances', 'sliding-roof':'Retractable roof',
  'window':'Windows / glazing',
  'stairs':'Switchback stairs', 'elevator':'Lift shaft / platform'
};

type GraphNumericField='width'|'height'|'depth'|'open'|'position'|'positionZ';
function GraphFeatureEditor({feature,disabled,onApply}:{feature:StudioBuildingFeature;disabled:boolean;onApply:(patch:Partial<StudioBuildingFeature>)=>void}){
 const planar=['sliding-roof','stairs','elevator'].includes(feature.kind);
 const fields:GraphNumericField[]=['width',planar?'depth':'height','position',...(planar?['positionZ' as const]:[]),...(!['window','stairs','elevator'].includes(feature.kind)?['open' as const]:[])];
 const [draft,setDraft]=useState<Partial<Record<GraphNumericField,string>>>({});
 const valid=(key:GraphNumericField,value:number)=>Number.isFinite(value)&&(key==='open'?value>=0&&value<=1:key==='position'||key==='positionZ'?value===-1||value>=0:value>0)&&value<=160;
 const changes=Object.entries(draft) as [GraphNumericField,string][];
 const canApply=changes.length>0&&changes.every(([key,raw])=>valid(key,asNumber(raw)));
 return <div className="au-stack">
  <strong>Edit source feature · {propertyLabels[feature.kind]}</strong>
  <div className="abg__fields">{fields.map(key=><StudioField key={key} label={key} htmlFor={'graph-feature-'+key}>
   <StudioInput id={'graph-feature-'+key} type="number" step={0.1} value={draft[key]??String(feature[key])} disabled={disabled}
     onChange={e=>setDraft(old=>({...old,[key]:e.target.value}))}/>
  </StudioField>)}</div>
  <small>Apply changes to the owner recipe; regenerate the candidate to inspect the new geometry. Native validation remains authoritative.</small>
  <StudioButton size="sm" disabled={disabled||!canApply} onClick={()=>onApply(Object.fromEntries(changes.map(([key,raw])=>[key,asNumber(raw)])))}>Apply feature draft</StudioButton>
 </div>;
}

export default function BuildingGeneratorPanel({
  snapshot, showPanel, commitSnapshot
}: { snapshot:StudioSnapshot|null; showPanel:(p:PanelId)=>void; commitSnapshot:(s:StudioSnapshot)=>void }) {
  const projectLoaded=Boolean(snapshot?.project.path);
  const [styles,setStyles]=useState<StudioBuildingStyle[]>([]);
  const [materialLibrary,setMaterialLibrary]=useState<MaterialLibrary>({library:{}});
  const [materialBindings,setMaterialBindings]=useState<Record<string,string>>({});
  const [setting,setSetting]=useState('scifi');
  const [style,setStyle]=useState('scifi');
  const [role,setRole]=useState('factory');
  const [mode,setMode]=useState<'features'|'preset'>('features');
  const [textureResolution,setTextureResolution]=useState<128|256|512>(256);
  const [uniqueAtlas,setUniqueAtlas]=useState(false);
  const [generateVisualLods,setGenerateVisualLods]=useState(false);
  const [roofProfile,setRoofProfile]=useState<import('../../types').StudioBuildingRoofProfile>('flat');
  const [roofRise,setRoofRise]=useState('0');
  const [features,setFeatures]=useState<StudioBuildingFeature[]>([
    makeFeature('large-gate'),
    {...makeFeature('small-gate'), side:'back',count:2},
    {...makeFeature('personnel-door'), side:'front'},
    makeFeature('sliding-roof')
  ]);
  const [width,setWidth]=useState('24');
  const [height,setHeight]=useState('12');
  const [depth,setDepth]=useState('18');
  const [floors,setFloors]=useState('1');
  const [roomsX,setRoomsX]=useState('1');
  const [roomsZ,setRoomsZ]=useState('1');
  const [footprint,setFootprint]=useState<string[]>([]);
  const [floorFootprints,setFloorFootprints]=useState<string[][]>([]);
  const [walkableSetbacks,setWalkableSetbacks]=useState(false);
  const [cornerChamfers,setCornerChamfers]=useState<[number,number,number,number]>([0,0,0,0]);
  const [damageZones,setDamageZones]=useState<NonNullable<StudioBuildingRequest['damageZones']>>([]);
  const [interiorProfile,setInteriorProfile]=useState<NonNullable<StudioBuildingRequest['interiorProfile']>>('none');
  const [furnitureDensity,setFurnitureDensity]=useState(3);
  const [interiorLighting,setInteriorLighting]=useState(false);
  const [interiorDecor,setInteriorDecor]=useState(false);
  const [roomUses,setRoomUses]=useState<NonNullable<StudioBuildingRequest['roomUses']>>([]);
  const [interiorCell,setInteriorCell]=useState('0:0');
  const [editFloor,setEditFloor]=useState(0);
  const shaped=footprint.some(row=>row.includes('0'))||floorFootprints.some(mask=>mask.some(row=>row.includes('0')));
  const editingMask=floorFootprints.length?floorFootprints[editFloor]??[]:footprint;
  const [seed,setSeed]=useState('482733');
  const [selectedSeed,setSelectedSeed]=useState<number|null>(null);
  const [variants,setVariants]=useState<Candidate[]>([]);
  const [saved,setSaved]=useState<StudioBuildingGenerated|null>(null);
  const [thumbnail,setThumbnail]=useState('');
  const [busy,setBusy]=useState(false);
  const [loading,setLoading]=useState(false);
  const [error,setError]=useState('');
  const [status,setStatus]=useState('');
  const [staleMaterials,setStaleMaterials]=useState<Array<{recipe:string;changes:Array<{guid:string;reason:string}>}>>([]);
  const [checkingMaterials,setCheckingMaterials]=useState(false);
  const [regeneratingRecipe,setRegeneratingRecipe]=useState('');
  const [autoRegenerate,setAutoRegenerate]=useState(false);
  const [savedRecipes,setSavedRecipes]=useState<string[]>([]);
  const [sourceRecipe,setSourceRecipe]=useState('');
  const [readingRecipe,setReadingRecipe]=useState(false);
  const [livePreview,setLivePreview]=useState<{token:string;url:string;key:string}|null>(null);
  const previewToken=useRef('');
  const previewScope=useRef(0);
  const [viewCamera,setViewCamera]=useState({yaw:.65,pitch:.42,zoom:1,panX:0,panY:0});
  const desiredCamera=useRef(viewCamera);
  const queuedCamera=useRef<typeof viewCamera|null>(null);
  const cameraInFlight=useRef(false);
  const [cameraBusy,setCameraBusy]=useState(false);
  const dragView=useRef<{x:number;y:number;pan:boolean}|null>(null);
  const [previewBusy,setPreviewBusy]=useState(false);
  const candidateGate=useRef(new BuildingCandidateGate<StudioBuildingRequest>());
  const [page,setPage]=useState<'recipe'|'features'|'materials'|'graphs'|'runtime'>('recipe');
  const focusId=useRef('');
  const workspaceRoot=useRef<HTMLDivElement>(null);
  const findControl=(id:string)=>workspaceRoot.current?.querySelector<HTMLElement>('#'+CSS.escape(id));
  useLayoutEffect(()=>{
    if(!focusId.current){
      const body=workspaceRoot.current?.closest('.au-panel__body');
      if(body)body.scrollTop=0;
      return;
    }
    const target=findControl(focusId.current);
    focusId.current='';
    target?.scrollIntoView({block:'center'});
    target?.focus();
  },[page]);
  const focusOn=(next:typeof page,id:string)=>{
    if(next===page){const target=findControl(id);target?.scrollIntoView({block:'center'});target?.focus();return;}
    focusId.current=id;setPage(next);
  };
  const focusMaterial=(slot:string)=>focusOn('materials','abg-material-binding-'+slot);

  const regeneratedThisSession=useRef(new Set<string>());
  useEffect(()=>{
    regeneratedThisSession.current.clear();
    setStaleMaterials([]);
  },[snapshot?.project.path]);


  useEffect(() => {
    if(!projectLoaded){setMaterialLibrary({library:{}});return;}
    let active=true;
    void invoke('studio.materials',{}).then(lib=>{
      if(active)setMaterialLibrary(lib);
    }).catch(e=>{if(active)setError(explain(e));});
    return ()=>{active=false;};
  },[projectLoaded,snapshot?.project.path]);

  useEffect(() => {
    let active=true;
    setSaved(null);setVariants([]);setThumbnail('');setError('');
    if(!projectLoaded)return () => {active=false;};
    setLoading(true);
    void invoke('studio.buildingCatalog',{}).then(value=>{
      if(!active)return;
      setStyles(value.styles);
      const first=value.styles.find(s=>s.id==='scifi' && s.functions.length>0)
        ?? value.styles.find(s=>s.functions.length>0) ?? value.styles[0];
      if(first){
        setSetting(first.setting);setStyle(first.id);setRole(first.functions[0]?.id ?? '');
        setMaterialBindings({});
      }
    }).catch(e=>{if(active)setError(explain(e));})
      .finally(()=>{if(active)setLoading(false);});
    return ()=>{active=false;};
  },[projectLoaded,snapshot?.project.path]);

  const reloadRecipes=async()=>{
    if(!projectLoaded||!nativeConnected())return;
    const r=await invoke('studio.buildingRecipes',{});
    setSavedRecipes(r.recipes);
  };
  useEffect(()=>{
    setSourceRecipe('');setSavedRecipes([]);
    if(!projectLoaded)return;
    void invoke('studio.buildingRecipes',{}).then(r=>setSavedRecipes(r.recipes))
      .catch(e=>setError(explain(e)));
  },[projectLoaded,snapshot?.project.path]);

  const loadRecipe=async (relative:string)=>{
    if(!relative||busy||readingRecipe)return;
    setReadingRecipe(true);setError('');
    try{
      const result=await invoke('studio.buildingReadRecipe',{recipe:relative});
      const item=result.request;
      const selected=styles.find(s=>s.id===item.style);
      if(!selected)throw Error('The authored architectural style is missing: '+item.style);
      setSourceRecipe(relative);
      setSetting(selected.setting);setStyle(selected.id);
      setRole(item.function??'');
      setMode(item.mode??'features');
      setTextureResolution(item.textureResolution??128);setUniqueAtlas(item.uniqueAtlas??false);setGenerateVisualLods(item.generateVisualLods??false);
      setRoofProfile(item.roofProfile??'flat');setRoofRise(String(item.roofRise??0));
      setWidth(String(item.width));setHeight(String(item.height));
      setDepth(String(item.depth));setSeed(String(item.seed));
      setFloors(String(item.floors??1));
      setRoomsX(String(item.roomsX??1));setRoomsZ(String(item.roomsZ??1));setFootprint(item.footprint??[]);setFloorFootprints(item.floorFootprints??[]);setWalkableSetbacks(item.walkableSetbacks??false);setCornerChamfers(item.cornerChamfers??[0,0,0,0]);setDamageZones(item.damageZones??[]);setEditFloor(0);
      setInteriorProfile(item.interiorProfile??'none');setFurnitureDensity(item.furnitureDensity??3);setInteriorLighting(item.interiorLighting??false);setInteriorDecor(item.interiorDecor??false);setRoomUses(item.roomUses??[]);
      setFeatures((item.features??[]).map(f=>({...makeFeature(f.kind),...f})));
      setMaterialBindings(item.materialBindings??{});
      clearGenerated();
      setStatus('Recipe loaded: '+relative+'. Generate creates a new GLB; original asset is preserved.');
    }catch(e){setError(explain(e));}
    finally{setReadingRecipe(false);}
  };

  const settings=useMemo(()=>[...new Set(styles.map(item=>item.setting))], [styles]);
  const choices=useMemo(()=>styles.filter(item=>item.setting===setting),[styles,setting]);
  const selectedStyle=choices.find(item=>item.id===style);
  const functions=selectedStyle?.functions ?? [];
  const selectedFunction=functions.find(item=>item.id===role);
  const parameterError=useMemo(()=>{
    if(!selectedStyle||(mode==='preset'&&!selectedFunction))
      return 'Choose an architectural style and a valid legacy silhouette.';
    for(const [name,raw,min,max] of [
      ['Width',width,8,160],['Height',height,mode==='features'?4:5,80],['Depth',depth,8,160]
    ] as const){
      const value=asNumber(raw);
      if(!Number.isFinite(value)||value<min||value>max)
        return name+' must be between '+min+' and '+max+' metres.';
    }
    if(mode==='features'){
      const rise=asNumber(roofRise);
      if(!Number.isFinite(rise)||(roofProfile==='flat'?rise!==0:rise<.5||rise>Math.min(12,asNumber(height)*.45)))
        return 'Invalid architectural roof rise (0 for flat; 0.5–12 m, at most 45% of height).';
      if(roofProfile!=='flat'&&features.some(f=>f.kind==='sliding-roof'))
        return 'Retractable roof requires the flat roof profile.';
      const f=asNumber(floors),rx=asNumber(roomsX),rz=asNumber(roomsZ);
      if(!Number.isInteger(f)||f<1||f>8||!Number.isInteger(rx)||rx<1||rx>6||
         !Number.isInteger(rz)||rz<1||rz>6||f*rx*rz>128)
        return 'Use 1–8 floors and 1–6 rooms per axis (maximum 128 rooms).';
      if(floorFootprints.length&&(floorFootprints.length!==f||floorFootprints.some(mask=>mask.length!==rz||mask.some(row=>row.length!==rx||!/^[01]+$/.test(row)))))return 'Floor footprint grid does not match dimensions.';
      if(interiorProfile!=='none'&&cornerChamfers.some(c=>c>0))return 'Furnishing currently requires orthogonal rooms.';
      if(interiorProfile!=='none'&&roomUses.some(r=>r.floor>=f||r.x>=rx||r.z>=rz||(floorFootprints[r.floor]??footprint)[r.z]?.[r.x]==='0'))return 'A room use override references an absent room; remove it first.';
      if(damageZones.length&&cornerChamfers.some(c=>c>0))return 'Damage zones require orthogonal walls.';
      for(const zone of damageZones){
        if(zone.kind==='roof'&&roofProfile!=='flat')return 'Roof damage requires a flat roof.';
        const limits=[asNumber(width),asNumber(height),asNumber(depth)];
        if(zone.minimum.some((v,i)=>!Number.isFinite(v)||!Number.isFinite(zone.maximum[i])||v<0||zone.maximum[i]>limits[i]||zone.maximum[i]-v<.01))
          return 'Damage zone bounds must fit the building envelope.';
      }
      const maxCut=Math.min(4,.4*Math.min(asNumber(width)/rx,asNumber(depth)/rz));
      if(cornerChamfers.some(c=>!Number.isFinite(c)||c<0||c>maxCut||(c>0&&c<.5)))return 'Corner cuts must be zero or 0.5–'+maxCut.toFixed(2)+'m.';
      if(cornerChamfers.some(c=>c>0)&&(shaped||roofProfile!=='flat'||walkableSetbacks||features.some(f=>f.kind==='sliding-roof')))
        return 'Chamfered corners require a rectangular footprint and flat non-retractable roof.';
      if(walkableSetbacks&&(!floorFootprints.length||f<2||
          !floorFootprints.some((mask,i)=>i>0&&mask.some((row,z)=>[...row].some((v,x)=>v==='0'&&floorFootprints[i-1][z]?.[x]==='1')))))
        return 'Walkable terraces require at least one supported upper-floor setback.';
      if(shaped){
        if(features.some(f=>f.kind==='sliding-roof'))return 'Custom footprints do not support a retractable panel.';
        const masks=floorFootprints.length?floorFootprints:[footprint];
        for(let level=0;level<masks.length;level++){
        const mask=masks[level];
        if(mask.length!==rz||mask.some(row=>row.length!==rx||!/^[01]+$/.test(row)))return 'Footprint grid does not match room dimensions.';
        const cells=mask.flatMap((row,z)=>[...row].flatMap((v,x)=>v==='1'?[[x,z]]:[]));
        if(!cells.length)return 'Every floor must have occupied cells.';
        if(level>0&&cells.some(([x,z])=>masks[level-1][z][x]!=='1'))return 'Upper floor cells must have occupied support below.';
        if(level===0&&(!cells.some(([x])=>x===0)||!cells.some(([x])=>x===rx-1)||!cells.some(([,z])=>z===0)||!cells.some(([,z])=>z===rz-1)))
          return 'Occupied cells must touch all four bounding sides.';
        const seen=new Set<string>(),queue=[cells[0]];
        seen.add(cells[0].join(','));
        for(let i=0;i<queue.length;i++){
          const [x,z]=queue[i];
          for(const [dx,dz] of [[1,0],[-1,0],[0,1],[0,-1]]){
            const a=x+dx,b=z+dz,key=a+','+b;
            if(mask[b]?.[a]==='1'&&!seen.has(key)){seen.add(key);queue.push([a,b]);}
          }
        }
        if(seen.size!==cells.length)return 'All footprint cells must be connected along an edge.';
        }
      }
      if((asNumber(height)-asNumber(roofRise)-0.52)/f<2.8||asNumber(width)/rx<2.5||asNumber(depth)/rz<2.5)
        return 'Every storey requires 2.8m height; each room at least 2.5m on each side.';
    }
    const n=asNumber(seed);
    if(!Number.isSafeInteger(n)||n<0||n>2147483645)
      return 'Seed must be an integer between 0 and 2147483645.';
    if(mode==='features'){
      if(features.length>24)return 'Maximum 24 functional properties.';
      const roofs=features.filter(f=>f.kind==='sliding-roof');
      if(roofs.length>1)return 'Only one retractable roof is supported.';
      let entrances=0;
      const used:{[side:string]:{width:number;count:number}}={};
      for(const feature of features){
        if(!Number.isFinite(feature.open)||feature.open<0||feature.open>1)
          return 'Open fraction must be between 0 and 1.';
        if(!Number.isFinite(feature.position)||!Number.isFinite(feature.positionZ)||
           !Number.isFinite(feature.spacing)||feature.position < -1||
           feature.positionZ < -1||feature.spacing<0.35||feature.spacing>8)
          return 'Placement must be automatic (-1) or a positive metre offset; spacing 0.35–8m.';
        const vertical=feature.kind==='stairs'||feature.kind==='elevator';
        if(vertical){
          if(asNumber(floors)<2||feature.count!==1||feature.floor!==0)
            return 'Stairs/lifts need at least two floors and one vertical shaft.';
          const roomW=asNumber(width)/asNumber(roomsX),roomD=asNumber(depth)/asNumber(roomsZ);
          if(feature.width>roomW-0.7||feature.depth>roomD-0.7)
            return 'Vertical core must fit inside one room cell.';
          const storey=(asNumber(height)-asNumber(roofRise)-0.52)/asNumber(floors);
          if(feature.kind==='stairs'){
            const minRun=0.9+Math.ceil((storey+0.24)/0.18/2)*0.24;
            if(feature.width<2.4||feature.depth<Math.max(5.6,minRun))
              return 'Stair well must support <=18cm risers and >=24cm treads.';
          }else if(feature.width<1.4||feature.depth<1.4)
            return 'Lift needs at least a 1.4x1.4m shaft.';
          continue;
        }
        if(feature.kind==='sliding-roof'){
          if(feature.width<1||feature.depth<1||feature.width>asNumber(width)*0.48||feature.depth>asNumber(depth)*0.75)
            return 'Roof opening must fit within 48% of building width and 75% of depth.';
          const cx=feature.position>=0?feature.position:asNumber(width)*0.5;
          const cz=feature.positionZ>=0?feature.positionZ:asNumber(depth)*0.5;
          const freeX=Math.min(cx-feature.width*0.5,asNumber(width)-(cx+feature.width*0.5));
          const freeZ=Math.min(cz-feature.depth*0.5,asNumber(depth)-(cz+feature.depth*0.5));
          if(freeX<Math.max(0.6,feature.width*feature.open*0.5)||freeZ<0.6)
            return 'Roof aperture or sliding panels would leave the building envelope.';
        }else{
          if(!Number.isInteger(feature.count)||feature.count<1||feature.count>8)
            return 'Each entrance property requires 1 to 8 openings.';
          entrances+=feature.count;
          if(entrances>32)return 'Maximum 32 total wall apertures.';
          const maxWidth=feature.kind==='large-gate'?20:feature.kind==='small-gate'?8:3;
          const isWindow=feature.kind==='window';
          const balconyDepth=feature.balconyDepth??0;
          const balcony=balconyDepth>0;
          if(!Number.isFinite(balconyDepth)||balconyDepth<0||(balcony&&(feature.kind!=='personnel-door'||feature.floor===0||feature.count!==1||feature.position<0||!floorFootprints.length||(feature.wallLine??-1)<=0||balconyDepth<1.4||balconyDepth>(feature.balconySupport==='columns'?3:1.8))))return 'Projecting balcony requires one upper personnel door, an explicit recessed facade line and depth 1.4–1.8m (columns up to 3m).';
          const maxWidthForKind=isWindow?6:maxWidth;
          const storey=(asNumber(height)-asNumber(roofRise)-0.52)/asNumber(floors);
          if(!Number.isInteger(feature.floor)||feature.floor<0||feature.floor>=asNumber(floors)||
             (!isWindow&&feature.floor!==0&&((!walkableSetbacks&&!balcony)||feature.kind!=='personnel-door'||feature.position<0)))
            return 'Selected floor is invalid for the opening.';
          if(!Number.isFinite(feature.sill)||feature.sill<0||feature.sill>4)
            return 'Window sill must be within 0–4m.';
          if(feature.width<0.85||feature.width>maxWidthForKind||
             feature.height<(isWindow?0.4:2)||
             feature.height>(isWindow?3.5:storey-0.6)||
             (isWindow&&(feature.sill<0.65||feature.sill+feature.height>storey-0.4)))
            return 'Opening dimensions or window sill exceed the selected storey.';
          if(feature.position>=0){
            const span=feature.width*feature.count+(feature.count-1)*feature.spacing;
            const sideLength=(feature.side==='front'||feature.side==='back')?asNumber(width):asNumber(depth);
            if(feature.position-span*0.5<0.72||feature.position+span*0.5>sideLength-0.72)
              return 'Exact opening position is outside its usable wall.';
          }
          const line=feature.wallLine??-1;
          if(!Number.isInteger(line)||line<-1||line>((feature.side==='front'||feature.side==='back')?asNumber(roomsZ):asNumber(roomsX)))
            return 'Invalid facade grid line.';
          // Native graph checks individual exposed spans and exact placement.
          if(line<0&&!shaped){
            const info=used[feature.side]??{width:0,count:0};
            info.width+=feature.width*feature.count;info.count+=feature.count;
            used[feature.side]=info;
          }
        }
      }
      for(const [side,info] of Object.entries(used)){
        const length=(side==='front'||side==='back')?asNumber(width):asNumber(depth);
        if((length-1.44-info.width)/(info.count+1)<0.35)
          return 'Too many or overly wide openings on the '+side+' wall.';
      }
    }
    return '';
  },[selectedStyle,selectedFunction,mode,features,width,height,depth,seed,floors,roomsX,roomsZ,roofProfile,roofRise,footprint,shaped,floorFootprints,walkableSetbacks,cornerChamfers,interiorProfile,furnitureDensity,roomUses,damageZones]);

  const request=(useSeed:number):StudioBuildingRequest=>({
    setting,style,function:mode==='preset'?role:'',mode,textureResolution,uniqueAtlas,
    generateVisualLods:mode==='features'&&!uniqueAtlas&&generateVisualLods?true:undefined,
    roofProfile:mode==='features'?roofProfile:undefined,
    roofRise:mode==='features'?asNumber(roofRise):undefined,
    features:mode==='features'?features:undefined,
    materialBindings:Object.keys(materialBindings).length?materialBindings:undefined,
    width:asNumber(width),height:asNumber(height),depth:asNumber(depth),seed:useSeed,
    floors:asNumber(floors),roomsX:asNumber(roomsX),roomsZ:asNumber(roomsZ),
    footprint:mode==='features'&&!floorFootprints.length&&footprint.length?footprint:undefined,
    floorFootprints:mode==='features'&&floorFootprints.length?floorFootprints:undefined,
    walkableSetbacks:mode==='features'&&walkableSetbacks?true:undefined,
    cornerChamfers:mode==='features'&&cornerChamfers.some(c=>c!==0)?cornerChamfers:undefined,
    damageZones:mode==='features'&&damageZones.length?damageZones:undefined,
    interiorProfile:mode==='features'?interiorProfile:undefined,
    furnitureDensity:mode==='features'&&interiorProfile!=='none'?furnitureDensity:undefined,
    interiorLighting:mode==='features'&&interiorProfile!=='none'?interiorLighting:undefined,
    interiorDecor:mode==='features'&&interiorProfile!=='none'?interiorDecor:undefined,
    roomUses:mode==='features'&&interiorProfile!=='none'&&roomUses.length?roomUses:undefined
  });
  // A seed is not a request identity. Include all authoring inputs and the project.
  const draftKey=buildingDraftKey(snapshot?.project.path??'',request(asNumber(seed)));
  const invalidateCandidates=useCallback(()=>{
    if(candidateGate.current.invalidate())setBusy(false);
    setVariants([]);setSelectedSeed(null);
  },[]);
  useLayoutEffect(()=>{invalidateCandidates();},[draftKey,invalidateCandidates]);
  useEffect(()=>{
    const off=onNativeEvent(event=>{
      if(event==='workspace.changed'||event==='project.splash'||event==='project.loading')
        invalidateCandidates();
    });
    return ()=>{off();candidateGate.current.invalidate();};
  },[invalidateCandidates]);
  const visibleVariants=variants.filter(item=>item.scope===draftKey);
  const activeCandidate=visibleVariants.find(item=>item.seed===selectedSeed&&item.data);
  const focusFeature=(index:number)=>{
    if(!activeCandidate||mode!=='features'||!Number.isInteger(index)||index<0||index>=features.length)return;
    focusOn('features','abg-feature-'+index+'-kind');
  };
  const clearGenerated=()=>{invalidateCandidates();setSaved(null);setThumbnail('');setStatus('');};
  const setBinding=(slot:string,value:string)=>{
    if(busy||!projectLoaded||!selectedStyle?.materials?.includes(slot))return;
    if(value&&!Object.prototype.hasOwnProperty.call(materialLibrary.library,value))return;
    setMaterialBindings(old=>{const next={...old};if(value)next[slot]=value;else delete next[slot];return next;});
    clearGenerated();
  };
  const changeSetting=(value:string)=>{
    const first=styles.find(item=>item.setting===value);
    setSetting(value);setStyle(first?.id??'');setRole(first?.functions[0]?.id??'');
    setMaterialBindings({});
    clearGenerated();
  };
  const changeStyle=(value:string)=>{
    const item=styles.find(x=>x.id===value);
    setStyle(value);setRole(item?.functions[0]?.id??'');setMaterialBindings({});clearGenerated();
  };
  const changeRole=(value:string)=>{setRole(value);clearGenerated();};
  const changeMode=(value:'features'|'preset')=>{setMode(value);clearGenerated();};
  // Starting layouts are explicit recipe edits, not hidden gameplay roles.
  const applyFunctionalPreset=(id:string)=>{
    if(busy||!id)return;
    const presets:Record<string,{size:[number,number,number];rooms:[number,number,number];features:StudioBuildingFeature[]}>={
      residence:{size:[18,7,16],rooms:[2,2,2],features:[makeFeature('personnel-door'),{...makeFeature('window'),side:'back',count:2},{...makeFeature('window'),side:'left',floor:1,count:2},{...makeFeature('stairs'),position:4.5,positionZ:4,depth:7.2}]},
      market:{size:[24,8,18],rooms:[1,3,2],features:[{...makeFeature('small-gate'),count:2},{...makeFeature('personnel-door'),side:'back'},{...makeFeature('window'),side:'left',count:3}]},
      administration:{size:[24,12,20],rooms:[3,2,2],features:[makeFeature('personnel-door'),{...makeFeature('window'),side:'back',count:3},{...makeFeature('window'),side:'right',floor:1,count:3},{...makeFeature('elevator'),position:6,positionZ:5},{...makeFeature('stairs'),position:18,positionZ:5,depth:8.5}]},
      warehouse:{size:[32,10,24],rooms:[1,2,1],features:[{...makeFeature('large-gate'),count:2},{...makeFeature('personnel-door'),side:'back'},{...makeFeature('window'),side:'right',count:3}]},
      workshop:{size:[24,10,18],rooms:[1,2,1],features:[makeFeature('large-gate'),{...makeFeature('personnel-door'),side:'back'},{...makeFeature('window'),side:'left',count:2},makeFeature('sliding-roof')]},
      hall:{size:[28,8,24],rooms:[1,1,1],features:[{...makeFeature('small-gate'),count:2},{...makeFeature('window'),side:'left',count:4},{...makeFeature('window'),side:'right',count:4}]}
    };
    const p=presets[id];if(!p)return;
    setMode('features');setWidth(String(p.size[0]));setHeight(String(p.size[1]));setDepth(String(p.size[2]));
    setFloors(String(p.rooms[0]));setRoomsX(String(p.rooms[1]));setRoomsZ(String(p.rooms[2]));
    setFeatures(p.features);setFootprint([]);setFloorFootprints([]);setWalkableSetbacks(false);setCornerChamfers([0,0,0,0]);setEditFloor(0);setRoomUses([]);setDamageZones([]);
    const profile=p.features.some(f=>f.kind==='sliding-roof')?'flat':selectedStyle?.recommendedRoof??'flat';
    const rise=profile==='flat'?0:Math.min(3,Math.max(1.5,p.size[0]*.12));
    setRoofProfile(profile);setRoofRise(String(rise));setHeight(String(p.size[1]+rise));
    clearGenerated();setPage('recipe');
  };
  const setFeature=(index:number,key:keyof StudioBuildingFeature,value:string|number)=>{
    setFeatures(old=>old.map((f,i)=>i===index?({...f,[key]:value} as StudioBuildingFeature):f));
    clearGenerated();
  };
  const changeFeatureKind=(index:number,kind:StudioBuildingFeatureKind)=>{
    setFeatures(old=>old.map((f,i)=>i===index?makeFeature(kind):f));
    clearGenerated();
  };
  const addFeature=(kind:StudioBuildingFeatureKind)=>{
    if(features.length>=24||(kind==='sliding-roof'&&features.some(f=>f.kind==='sliding-roof'))||
       (['stairs','elevator'].includes(kind)&&asNumber(floors)<2))return;
    setFeatures(old=>[...old,makeFeature(kind)]);clearGenerated();
  };
  const removeFeature=(index:number)=>{
    setFeatures(old=>old.filter((_,i)=>i!==index));clearGenerated();
  };
  const setDimension=(update:(v:string)=>void)=>(e:{target:{value:string}})=>{
    update(e.target.value);clearGenerated();
  };
  const randomize=()=>{
    const n=Math.floor(Math.random()*2147483645);
    setSeed(String(n));clearGenerated();
  };

  const checkMaterialDependencies=async()=>{
    if(!nativeConnected()||!projectLoaded||checkingMaterials)return;
    setCheckingMaterials(true);setError('');
    try{
      const result=await invoke('studio.buildingStaleMaterials',{});
      setStaleMaterials(result.stale);
      setStatus(result.stale.length
        ? result.stale.length+' generated building recipes contain outdated texture sources.'
        : 'No stale tracked texture dependencies found.');
    }catch(e){setError(explain(e));}
    finally{setCheckingMaterials(false);}
  };

  const refreshGeneratedThumbnail=(path:string)=>{
    setThumbnail('');
    // StudioHost.assetThumbnail expects a project content/ path, while
    // studio.buildingGenerate returns a path relative to content/.
    const assetPath=path.startsWith('content/')?path:'content/'+path;
    void invoke('studio.assetThumbnail',{path:assetPath}).then(thumb=>{
      if(thumb.url)setThumbnail(thumb.url);
    }).catch(()=>{});
  };

  const regenerateRecipe=async (recipe:string)=>{
    if(!projectLoaded||!nativeConnected()||busy||regeneratingRecipe)return;
    setRegeneratingRecipe(recipe);setError('');setStatus('');
    try{
      const generated=await invoke('studio.buildingRegenerate',{recipe});
      regeneratedThisSession.current.add(recipe);
      setSaved(generated);
      refreshGeneratedThumbnail(generated.path);
      setStatus('Updated copy created: '+generated.path+'. Original building and GUID were preserved.');
      window.dispatchEvent(new Event('studio:assets-changed'));
      void invoke('studio.windowBroadcast',{
        event:'ui.assetsChanged',payload:{path:generated.path}
      }).catch(()=>{});
      const report=await invoke('studio.buildingStaleMaterials',{});
      setStaleMaterials(report.stale);
    }catch(e){setError(explain(e));}
    finally{setRegeneratingRecipe('');}
  };

  // Automatic *detection* responds to existing Assets updates. Automatic
  // *generation* is explicitly opt-in and creates new assets only.
  useEffect(()=>{
    if(!projectLoaded||!nativeConnected())return;
    let active=true;
    let inflight=false;
    const check=async()=>{
      if(inflight)return;
      inflight=true;
      try{
        const report=await invoke('studio.buildingStaleMaterials',{});
        if(!active)return;
        setStaleMaterials(report.stale);
        if(autoRegenerate && report.stale.length){
          // One bounded create-only update per source recipe this session;
          // the old recipe remains stale because its GLB is immutable.
          const candidate=report.stale.find(item=>
            !regeneratedThisSession.current.has(item.recipe));
          if(candidate){
            regeneratedThisSession.current.add(candidate.recipe);
            try{
              const generated=await invoke('studio.buildingRegenerate',{
                recipe:candidate.recipe
              });
              if(!active)return;
              setSaved(generated);
              refreshGeneratedThumbnail(generated.path);
              setStatus('Auto-generated new copy: '+generated.path+'. Existing GLB unchanged.');
              window.dispatchEvent(new Event('studio:assets-changed'));
              void invoke('studio.windowBroadcast',{
                event:'ui.assetsChanged',payload:{path:generated.path}
              }).catch(()=>{});
            }catch(e){
              if(active)setError(explain(e));
            }
          }
        }
      }catch(e){if(active)setError(explain(e));}
      finally{inflight=false;}
    };
    const onAssets=()=>void check();
    window.addEventListener('studio:assets-changed',onAssets);
    const off=onNativeEvent(event=>{
      if(event==='ui.assetsChanged'||event==='document.changed')void check();
    });
    void check();
    return ()=>{
      active=false;
      off();
      window.removeEventListener('studio:assets-changed',onAssets);
    };
  },[projectLoaded,snapshot?.project.path,autoRegenerate]);

  const previewKey=buildingDraftKey(snapshot?.project.path??'',activeCandidate?.recipe??request(asNumber(seed)));
  const previewNative3D=async()=>{
    if(parameterError||busy||previewBusy||cameraInFlight.current||!nativeConnected())return;
    const scope=previewScope.current;
    setPreviewBusy(true);setError('');
    try{
      const reply=await invoke('studio.buildingPreview',activeCandidate?.recipe??request(asNumber(seed)));
      if(!reply.active||!reply.url||!reply.token)throw Error('Native Building panel preview unavailable');
      if(scope!==previewScope.current){void invoke('studio.buildingPreviewStop',{token:reply.token}).catch(()=>{});return;}
      previewToken.current=reply.token;
      setLivePreview({token:reply.token,url:reply.url,key:previewKey});
      const camera={yaw:.65,pitch:.42,zoom:1,panX:0,panY:0};desiredCamera.current=camera;setViewCamera(camera);setPage('recipe');
      setStatus('Native 3D preview in Building Generator. Scene and project assets are unchanged.');
    }catch(e){if(scope===previewScope.current)setError(explain(e));}
    finally{if(scope===previewScope.current)setPreviewBusy(false);}
  };
  // Coalesce input to the newest desired camera while the independent
  // GPU worker renders. At most one bridge request is outstanding.
  const updatePreviewView=(next:typeof viewCamera)=>{
    if(!previewToken.current||previewBusy)return;
    desiredCamera.current=next;setViewCamera(next);queuedCamera.current=next;
    if(cameraInFlight.current)return;
    const scope=previewScope.current,token=previewToken.current;
    cameraInFlight.current=true;setCameraBusy(true);
    const pump=async()=>{
      try{
        while(scope===previewScope.current&&previewToken.current===token&&queuedCamera.current){
          const camera=queuedCamera.current;queuedCamera.current=null;
          const reply=await invoke('studio.buildingPreviewView',{token,...camera});
          if(scope!==previewScope.current||previewToken.current!==token)return;
          if(reply.token!==token||!reply.url)throw Error('Invalid camera preview reply');
          setLivePreview(old=>old?.token===token?{...old,url:reply.url}:old);
        }
      }catch(e){if(scope===previewScope.current){queuedCamera.current=null;setError(explain(e));}}
      finally{if(scope===previewScope.current){cameraInFlight.current=false;setCameraBusy(false);}}
    };
    void pump();
  };
  const closeNative3D=async()=>{
    if(previewBusy||cameraInFlight.current||!livePreview)return;
    setPreviewBusy(true);setError('');
    try{
      await invoke('studio.buildingPreviewStop',{token:livePreview.token});
      previewToken.current='';setLivePreview(null);
      setStatus('Temporary 3D preview released.');
    }catch(e){setError(explain(e));}
    finally{setPreviewBusy(false);}
  };
  useEffect(()=>{
    setLivePreview(null);setPreviewBusy(false);setCameraBusy(false);cameraInFlight.current=false;queuedCamera.current=null;
    return ()=>{
      ++previewScope.current;queuedCamera.current=null;cameraInFlight.current=false;
      const token=previewToken.current;previewToken.current='';
      if(token&&nativeConnected())void invoke('studio.buildingPreviewStop',{token}).catch(()=>{});
    };
  },[snapshot?.project.path]);

  const preview=async()=>{
    if(parameterError||busy||!nativeConnected())return;
    setBusy(true);setError('');setStatus('');setSaved(null);setThumbnail('');
    setVariants([]);setSelectedSeed(null);
    const first=asNumber(seed);
    let ticket:BuildingCandidateTicket<StudioBuildingRequest>|undefined;
    try{
      const captured=candidateGate.current.begin(draftKey,[first,first+1,first+2].map(request));
      ticket=captured;
      const choices=await Promise.all(captured.requests.map(async (recipe):Promise<Candidate>=>{
        const base={seed:recipe.seed,batch:captured.id,scope:captured.scope,recipe};
        try{
          const data=await invoke('studio.buildingInspect',recipe);
          if(data.seed!==recipe.seed)throw Error('Native inspection seed does not match captured request');
          return {...base,data};
        }catch(e){return {...base,error:explain(e)};}
      }));
      if(!candidateGate.current.isCurrent(captured))return;
      setVariants(choices);
      const firstGood=choices.find(x=>x.data);
      setSelectedSeed(firstGood?.seed??null);
      if(!firstGood)setError('No feasible building variant for these dimensions. Change the size or style.');
      else {setPage('graphs');setStatus('Three seed candidates evaluated from real procedural mesh geometry.');}
    }catch(e){if(!ticket||candidateGate.current.isCurrent(ticket))setError(explain(e));}
    finally{if(!ticket||candidateGate.current.finish(ticket))setBusy(false);}
  };

  const generate=async()=>{
    if(parameterError||busy)return;
    setBusy(true);setError('');setStatus('');setSaved(null);setThumbnail('');
    try{
      const result=await invoke('studio.buildingGenerate',activeCandidate?.recipe??request(asNumber(seed)));
      setSaved(result);setPage('recipe');setStatus('New GLB saved to Assets with persistent GUID.');
      void reloadRecipes().catch(()=>{});
      // Existing browser windows can refresh their Assets lists on demand.
      window.dispatchEvent(new Event('studio:assets-changed'));
      void invoke('studio.windowBroadcast',{event:'ui.assetsChanged',payload:{path:result.path}}).catch(()=>{});
      refreshGeneratedThumbnail(result.path);
    }catch(e){setError(explain(e));}
    finally{setBusy(false);}
  };

  const reveal=()=>{
    if(!saved)return;
    revealProjectAsset(snapshot?.project.path ?? '', saved.path);
    showPanel('assets');
  };
  const applyToSelected=async()=>{
    if(!saved||busy||!snapshot?.selectedId?.startsWith('entity:'))return;
    if(!window.confirm('Replace the selected object’s mesh with the updated generated GLB? This is an undoable scene edit.'))return;
    setBusy(true);setError('');
    try{
      // StudioSession::setMeshRenderer validates the existing authored
      // entity, asset GUID and file; StudioScene owns GPU reload/retire.
      const updated=await invoke('studio.setProperty',{
        entityId:snapshot.selectedId,componentId:'MeshRenderer',
        key:'asset',value:saved.guid
      });
      commitSnapshot(updated);
      setStatus('Selected scene object now references the updated asset. Original GLB remains in Assets.');
      showPanel('scene');
    }catch(e){setError(explain(e));}
    finally{setBusy(false);}
  };

  const place=async()=>{
    if(!saved||busy)return;
    setBusy(true);setError('');
    try{
      const updated=await invoke('studio.assetPlace',{asset:'content/'+saved.path,x:0,z:0});
      commitSnapshot(updated);
      setStatus('Generated model placed as a scene entity at the map origin. Use Transform to move it.');
      showPanel('scene');
    }catch(e){setError(explain(e));}
    finally{setBusy(false);}
  };

  if(!projectLoaded)return <StudioPanel title="Building Generator" eyebrow="WORLD / PROCEDURAL">
    <StudioEmptyState title="Open a project" icon={<Building2 size={24}/>}
      description="Generated models are saved as real .glb files in your project's content/ folder."/>
  </StudioPanel>;

  return <StudioPanel className="abg" title="Building Generator" eyebrow="WORLD / PROCEDURAL"
    subtitle={snapshot?.project.name??''}
    actions={<StudioIconButton icon={<RefreshCw size={15}/>} aria-label="Reload building styles"
      disabled={busy||loading} onClick={()=>void invoke('studio.buildingCatalog',{}).then(r=>setStyles(r.styles)).catch(e=>setError(explain(e)))}/>}
    footer={<div className="abg__footer"><StudioBadge tone={error?'danger':saved?'success':'neutral'}>
      {busy?'Generating…':error?'Check parameters':saved?'Saved in Assets':'Ready'}
    </StudioBadge>
        <div className="abg__actions abg__primary-actions">
          <StudioButton variant="secondary" loading={busy} disabled={Boolean(parameterError)||busy||!nativeConnected()}
            onClick={()=>void preview()}>Evaluate 3 seeds</StudioButton>
          <StudioButton variant="secondary" loading={previewBusy}
            disabled={Boolean(parameterError)||busy||previewBusy||cameraBusy||!nativeConnected()}
            onClick={()=>void previewNative3D()}>
            Preview 3D
          </StudioButton>
          {livePreview&&<StudioButton variant="secondary" disabled={previewBusy||cameraBusy}
            onClick={()=>void closeNative3D()}>Close 3D preview</StudioButton>}
          <StudioButton variant="primary" loading={busy}
            disabled={Boolean(parameterError)||busy||!nativeConnected()}
            onClick={()=>void generate()}>
            <WandSparkles size={15}/> Generate GLB
          </StudioButton>
        </div>
    </div>}>
    <div className="au-stack abg__root" ref={workspaceRoot}>
      {!nativeConnected()&&<StudioAlert tone="warning">Open native AIVERSE Studio to generate and save 3D models. Browser demo cannot write project assets.</StudioAlert>}
      {error&&<StudioAlert tone="danger" title="Generator error">{error}</StudioAlert>}
      {status&&!error&&<StudioAlert tone="success">{status}</StudioAlert>}
      {loading&&<StudioAlert>Loading available building styles…</StudioAlert>}
      {!loading&&styles.length===0&&<StudioEmptyState title="No building styles"
        icon={<Building2 size={22}/>} description="Install engine/assets/building-styles or open native AIVERSE Studio."/>}
      {parameterError&&<StudioAlert tone="warning" title="Parameters">{parameterError}</StudioAlert>}
      <StudioTabs<typeof page> label="Building workspace" selected={page} onChange={setPage} items={[
        {id:'recipe',label:'Recipe'},{id:'features',label:'Features'},{id:'materials',label:'Materials'},
        {id:'graphs',label:'Graphs'},{id:'runtime',label:'Runtime'}
      ]}/>
      <div className="abg__page au-stack" hidden={page!=='recipe'}>
        <div className="abg__recipe-layout">
          <div className="abg__recipe-controls au-stack">
        <div className="abg__recipe-grid">
      <StudioSection title="Building recipe">
        <div className="abg__fields">
          <StudioField label="Setting" htmlFor="abg-setting">
            <StudioSelect id="abg-setting" value={setting} onChange={e=>changeSetting(e.target.value)}
              disabled={busy||settings.length===0}>
              {settings.map(id=><option value={id} key={id}>{settingLabel(id)}</option>)}
            </StudioSelect>
          </StudioField>
          <StudioField label="Architectural style" htmlFor="abg-style">
            <StudioSelect id="abg-style" value={style} onChange={e=>changeStyle(e.target.value)}
              disabled={busy||choices.length===0}>
              {choices.map(item=><option value={item.id} key={item.id}>{item.name||item.id}</option>)}
            </StudioSelect>
          </StudioField>
          <StudioField label="Construction mode" htmlFor="abg-mode">
            <StudioSelect id="abg-mode" value={mode} onChange={e=>changeMode(e.target.value as 'features'|'preset')} disabled={busy}>
              <option value="features">Functional properties</option>
              <option value="preset">Legacy structure silhouette</option>
            </StudioSelect>
          </StudioField>
          {mode==='features'&&<StudioField label="Starting layout" htmlFor="abg-layout-preset">
            <StudioSelect id="abg-layout-preset" value="" disabled={busy} onChange={e=>applyFunctionalPreset(e.target.value)}>
              <option value="">Custom · apply a starting layout…</option>
              {['residence','market','administration','warehouse','workshop','hall'].map(id=><option key={id} value={id}>{id}</option>)}
            </StudioSelect>
          </StudioField>}
          {mode==='preset'&&<StudioField label="Architecture type" htmlFor="abg-role">
            <StudioSelect id="abg-role" value={role} onChange={e=>changeRole(e.target.value)}
              disabled={busy||functions.length===0}>
              {functions.map(item=><option value={item.id} key={item.id}>{item.name||item.id}</option>)}
            </StudioSelect>
          </StudioField>}
        </div>
      </StudioSection>
      <StudioSection title="Dimensions">
        <div className="abg__dimensions">
          <StudioField label="Width, m" htmlFor="abg-width">
            <StudioInput id="abg-width" type="number" min={8} max={160} step={0.1} value={width}
              onChange={setDimension(setWidth)} disabled={busy}/>
          </StudioField>
          <StudioField label="Height, m" htmlFor="abg-height">
            <StudioInput id="abg-height" type="number" min={5} max={80} step={0.1} value={height}
              onChange={setDimension(setHeight)} disabled={busy}/>
          </StudioField>
          <StudioField label="Depth, m" htmlFor="abg-depth">
            <StudioInput id="abg-depth" type="number" min={8} max={160} step={0.1} value={depth}
              onChange={setDimension(setDepth)} disabled={busy}/>
          </StudioField>
        </div>
        <div className="abg__seed">
          <StudioField label="Seed" htmlFor="abg-seed">
            <StudioInput id="abg-seed" type="number" min={0} max={2147483645} step={1}
              value={seed} onChange={setDimension(setSeed)} disabled={busy}/>
          </StudioField>
          <StudioIconButton icon={<Dices size={16}/>} aria-label="Generate random seed"
            disabled={busy} onClick={randomize}/>
        </div>
      </StudioSection>
      {mode==='features'&&<StudioSection title="Architectural roof">
        <div className="abg__fields">
          <StudioField label="Profile" htmlFor="abg-roof-profile">
            <StudioSelect id="abg-roof-profile" value={roofProfile} disabled={busy}
              onChange={e=>{
                const value=e.target.value as typeof roofProfile;setRoofProfile(value);
                if(value==='flat')setRoofRise('0');else if(roofProfile==='flat')setRoofRise('2');
                clearGenerated();
              }}>
              {['flat','gable','shed','barrel','hip','dome','terrace','pagoda'].map(value=><option key={value} value={value}>{value}</option>)}
            </StudioSelect>
          </StudioField>
          <StudioField label="Rise, m" htmlFor="abg-roof-rise">
            <StudioInput id="abg-roof-rise" type="number" min={roofProfile==='flat'?0:.5} max={12} step={.1} value={roofRise}
              disabled={busy||roofProfile==='flat'} onChange={setDimension(setRoofRise)}/>
          </StudioField>
        </div>
        <StudioButton size="sm" disabled={busy||!selectedStyle?.recommendedRoof||(selectedStyle.recommendedRoof!=='flat'&&features.some(f=>f.kind==='sliding-roof'))}
          onClick={()=>{const profile=selectedStyle?.recommendedRoof??'flat';const old=asNumber(roofRise);
            const rise=profile==='flat'?0:Math.min(3,Math.max(1.5,asNumber(width)*.12));
            setRoofProfile(profile);setRoofRise(String(rise));if(Number.isFinite(old))setHeight(String(asNumber(height)-old+rise));clearGenerated();}}>Apply setting roof · {selectedStyle?.recommendedRoof??'flat'}</StudioButton>
        <small className="abg__note">Height includes the cap. Complex footprints automatically split the upper roof into supported sections. Lower setbacks remain flat. Retractable roofs require a rectangular flat profile.</small>
      </StudioSection>}
      {mode==='features'&&<StudioSection title="Floors and rooms">
        <div className="abg__dimensions">
          <StudioField label="Floors" htmlFor="abg-floors">
            <StudioInput id="abg-floors" type="number" min={1} max={8} step={1} value={floors}
              onChange={e=>{setFloors(e.target.value);setFloorFootprints([]);setWalkableSetbacks(false);setEditFloor(0);clearGenerated();}} disabled={busy}/>
          </StudioField>
          <StudioField label="Rooms X" htmlFor="abg-rooms-x">
            <StudioInput id="abg-rooms-x" type="number" min={1} max={6} step={1} value={roomsX}
              onChange={e=>{setRoomsX(e.target.value);setFootprint([]);setFloorFootprints([]);setWalkableSetbacks(false);setEditFloor(0);clearGenerated();}} disabled={busy}/>
          </StudioField>
          <StudioField label="Rooms Z" htmlFor="abg-rooms-z">
            <StudioInput id="abg-rooms-z" type="number" min={1} max={6} step={1} value={roomsZ}
              onChange={e=>{setRoomsZ(e.target.value);setFootprint([]);setFloorFootprints([]);setWalkableSetbacks(false);setEditFloor(0);clearGenerated();}} disabled={busy}/>
          </StudioField>
        </div>
        <StudioField label="Footprint preset" htmlFor="abg-footprint-preset">
          <StudioSelect id="abg-footprint-preset" value="" disabled={busy} onChange={e=>{
            const patterns:Record<string,string[]>={L:['100','100','111'],T:['111','010','010'],U:['101','101','111'],H:['101','111','101'],courtyard:['111','101','111']};
            setFloorFootprints([]);setWalkableSetbacks(false);setCornerChamfers([0,0,0,0]);setEditFloor(0);
            if(e.target.value==='rect')setFootprint([]);
            else if(patterns[e.target.value]){
              const rows=patterns[e.target.value];setRoomsX('3');setRoomsZ('3');setFootprint(rows);
            }
            clearGenerated();
          }}>
            <option value="">Choose a starting contour…</option><option value="rect">Rectangle</option>
            <option value="L">L shape</option><option value="T">T shape</option><option value="U">U shape</option>
            <option value="H">H shape</option><option value="courtyard">Courtyard</option>
          </StudioSelect>
        </StudioField>
        <div className="abg__dimensions">
          {['Front left','Front right','Back right','Back left'].map((label,i)=>
            <StudioField key={i} label={label+' chamfer, m'} htmlFor={'abg-corner-cut-'+i}>
              <StudioInput id={'abg-corner-cut-'+i} type="number" min={0} max={4} step={.1}
                value={String(cornerChamfers[i])} disabled={busy} onChange={e=>{
                  setCornerChamfers(old=>{const next:[number,number,number,number]=[...old];next[i]=Number(e.target.value);return next;});
                  clearGenerated();
                }}/>
            </StudioField>)}
        </div>
        <StudioButton variant="secondary" disabled={busy} onClick={()=>{setCornerChamfers([0,0,0,0]);clearGenerated();}}>Reset corner cuts</StudioButton>
        <small className="abg__note">Chamfer distances cut the actual shell, slabs and roof. Supported on rectangular plans with flat roofs; place openings away from cut corners. Physical strips approximate the exact diagonal.</small>
        <StudioField label="Floor plan mode" htmlFor="abg-floor-plan-mode">
          <StudioSelect id="abg-floor-plan-mode" value={floorFootprints.length?'per-floor':'shared'} disabled={busy} onChange={e=>{
            if(e.target.value==='per-floor'){
              const nx=asNumber(roomsX),nz=asNumber(roomsZ),nf=asNumber(floors);
              if(!Number.isInteger(nx)||nx<1||nx>6||!Number.isInteger(nz)||nz<1||nz>6||!Number.isInteger(nf)||nf<1||nf>8)return;
              const base=footprint.length?footprint:Array.from({length:nz},()=> '1'.repeat(nx));
              setFloorFootprints(Array.from({length:nf},()=>base.slice()));
            }else{
              setFootprint(floorFootprints[0]??footprint);setFloorFootprints([]);setWalkableSetbacks(false);
            }
            setEditFloor(0);clearGenerated();
          }}>
            <option value="shared">Same plan on all floors</option><option value="per-floor">Independent floor setbacks</option>
          </StudioSelect>
        </StudioField>
        {floorFootprints.length>1&&<StudioField label="Setback surfaces" htmlFor="abg-walkable-setbacks">
          <StudioSelect id="abg-walkable-setbacks" value={walkableSetbacks?'terrace':'roof'} disabled={busy} onChange={e=>{setWalkableSetbacks(e.target.value==='terrace');clearGenerated();}}>
            <option value="roof">Closed roof setbacks</option><option value="terrace">Walkable terraces + parapets</option>
          </StudioSelect>
        </StudioField>}
        {floorFootprints.length>0&&<StudioField label="Editing floor" htmlFor="abg-edit-floor">
          <StudioSelect id="abg-edit-floor" value={String(editFloor)} disabled={busy} onChange={e=>setEditFloor(Number(e.target.value))}>
            {floorFootprints.map((_,i)=><option key={i} value={i}>Floor {i}</option>)}
          </StudioSelect>
        </StudioField>}
        {Number.isInteger(asNumber(roomsX))&&asNumber(roomsX)>=1&&asNumber(roomsX)<=6&&Number.isInteger(asNumber(roomsZ))&&asNumber(roomsZ)>=1&&asNumber(roomsZ)<=6&&
          <div className="abg__footprint-grid" style={{gridTemplateColumns:'repeat('+roomsX+', minmax(32px, 1fr))'}}>
            {Array.from({length:asNumber(roomsZ)*asNumber(roomsX)},(_,i)=>{
              const x=i%asNumber(roomsX),z=Math.floor(i/asNumber(roomsX));
              const active=editingMask[z]?.[x]!=='0';
              return <button type="button" key={i} className={'abg__footprint-cell'+(active?' is-active':'')} aria-pressed={active}
                aria-label={'Room cell '+(x+1)+', '+(z+1)} disabled={busy} onClick={()=>{
                  const rows=editingMask.length?editingMask.slice():Array.from({length:asNumber(roomsZ)},()=> '1'.repeat(asNumber(roomsX)));
                  rows[z]=rows[z].slice(0,x)+(active?'0':'1')+rows[z].slice(x+1);
                  if(floorFootprints.length)setFloorFootprints(old=>old.map((mask,i)=>i===editFloor?rows:mask));
                  else setFootprint(rows);
                  clearGenerated();
                }}>{active?'■':'·'}</button>;
            })}
          </div>}
        <small className="abg__note">Click cells to add or remove rooms in the selected plan. Independent upper floors must be supported by the floor below; removed cells can form walkable terraces with parapets. To add an upper personnel door, select its floor, facade line and explicit wall position; native validation requires a deck immediately outside, or a projecting balcony into a clear recessed cell. The top row is Front. Custom shapes support composed upper roofs; select a facade grid line for openings on recessed or courtyard walls. Existing incompatible features must be edited explicitly.</small>
        <small className="abg__note">Internal room partitions, stair and lift geometry follow the selected functional recipe. Native ECS/PHYSIS and graph owners handle dynamic traversal separately; changing setting never changes gameplay permissions.</small>
      </StudioSection>}
      {mode==='features'&&<StudioSection title="Architectural damage">
        <small className="abg__note">Remove real solid geometry using local XYZ bounds in metres. Roof cuts require a flat roof; panes are removed entirely. Functional moving panels and cores are protected.</small>
        <div className="abg__actions">
          <StudioButton variant="secondary" disabled={busy||damageZones.length>=16} onClick={()=>{
            const w=asNumber(width),h=asNumber(height);
            if(!Number.isFinite(w)||!Number.isFinite(h)||w<8||h<4)return;
            setDamageZones(old=>[...old,{kind:'wall',minimum:[w*.22,.24,0],maximum:[w*.22+1.5,Math.min(2.8,h),.36]}]);clearGenerated();
          }}>+ Wall breach</StudioButton>
          <StudioButton variant="secondary" disabled={busy||damageZones.length>=16||roofProfile!=='flat'} onClick={()=>{
            const w=asNumber(width),d=asNumber(depth),h=asNumber(height);
            if(![w,d,h].every(Number.isFinite)||w<8||d<8||h<4)return;
            setDamageZones(old=>[...old,{kind:'roof',minimum:[w*.4,h-.28,d*.4],maximum:[w*.6,h,d*.6]}]);clearGenerated();
          }}>+ Roof hole</StudioButton>
          <StudioButton variant="secondary" disabled={busy||!damageZones.length} onClick={()=>{setDamageZones([]);clearGenerated();}}>Clear damage</StudioButton>
        </div>
        {damageZones.map((zone,i)=><div className="abg__fields" key={i}>
          <StudioField label={'Damage '+(i+1)} htmlFor={'abg-damage-kind-'+i}>
            <StudioSelect id={'abg-damage-kind-'+i} disabled={busy} value={zone.kind} onChange={e=>{
              const kind=e.target.value as typeof zone.kind;setDamageZones(old=>old.map((v,j)=>j===i?{...v,kind}:v));clearGenerated();
            }}>{['wall','window','roof'].map(k=><option key={k} value={k}>{k}</option>)}</StudioSelect>
          </StudioField>
          <StudioButton variant="secondary" disabled={busy} onClick={()=>{setDamageZones(old=>old.filter((_,j)=>j!==i));clearGenerated();}}>Remove</StudioButton>
          {(['minimum','maximum'] as const).flatMap(key=>['X','Y','Z'].map((axis,a)=>
            <StudioField key={key+axis} label={key+' '+axis+', m'} htmlFor={'abg-damage-'+i+'-'+key+axis}>
              <StudioInput id={'abg-damage-'+i+'-'+key+axis} type="number" step={.01} value={String(zone[key][a])} disabled={busy} onChange={e=>{
                const value=Number(e.target.value);setDamageZones(old=>old.map((v,j)=>{
                  if(j!==i)return v;const next:[number,number,number]=[...v[key]];next[a]=value;return {...v,[key]:next};
                }));clearGenerated();
              }}/>
            </StudioField>))}
        </div>)}
      </StudioSection>}
      {mode==='features'&&<StudioSection title="Procedural interior">
        <StudioField label="Interior profile" htmlFor="abg-interior-profile">
          <StudioSelect id="abg-interior-profile" value={interiorProfile} disabled={busy}
            onChange={e=>{setInteriorProfile(e.target.value as typeof interiorProfile);clearGenerated();}}>
            {['none','residential','office','industrial','commercial','mixed'].map(p=><option key={p} value={p}>{p}</option>)}
          </StudioSelect>
        </StudioField>
        {interiorProfile!=='none'&&<>
          <StudioField label="Furniture items per room (maximum)" htmlFor="abg-furniture-density">
            <StudioSelect id="abg-furniture-density" value={String(furnitureDensity)} disabled={busy}
              onChange={e=>{setFurnitureDensity(Number(e.target.value));clearGenerated();}}>
              {[0,1,2,3,4].map(n=><option key={n} value={n}>{n===0?'Room uses only':n}</option>)}
            </StudioSelect>
          </StudioField>
          <StudioField label="Ceiling fixtures and lights" htmlFor="abg-interior-light">
            <StudioSelect id="abg-interior-light" value={interiorLighting?'on':'off'} disabled={busy} onChange={e=>{setInteriorLighting(e.target.value==='on');clearGenerated();}}><option value="off">Off</option><option value="on">Automatic</option></StudioSelect>
          </StudioField>
          <StudioField label="Furniture-top decorations" htmlFor="abg-interior-decor">
            <StudioSelect id="abg-interior-decor" value={interiorDecor?'on':'off'} disabled={busy} onChange={e=>{setInteriorDecor(e.target.value==='on');clearGenerated();}}><option value="off">Off</option><option value="on">Automatic</option></StudioSelect>
          </StudioField>
          <StudioButton variant="secondary" disabled={busy||!roomUses.length} onClick={()=>{setRoomUses([]);clearGenerated();}}>Clear room overrides</StudioButton>
          <small className="abg__note">Override room use on the current editing floor. Entrance, shaft and terrace-access rooms remain empty circulation spaces.</small>
          {Number.isInteger(asNumber(roomsX))&&asNumber(roomsX)>=1&&asNumber(roomsX)<=6&&Number.isInteger(asNumber(roomsZ))&&asNumber(roomsZ)>=1&&asNumber(roomsZ)<=6&&
            Number.isInteger(asNumber(floors))&&asNumber(floors)>=1&&asNumber(floors)<=8&&<>
            <StudioField label="Interior floor" htmlFor="abg-interior-floor">
              <StudioSelect id="abg-interior-floor" value={String(editFloor)} disabled={busy} onChange={e=>setEditFloor(Number(e.target.value))}>
                {Array.from({length:asNumber(floors)},(_,i)=><option key={i} value={i}>Floor {i}</option>)}
              </StudioSelect>
            </StudioField>
            {(()=>{
              const cells=Array.from({length:asNumber(roomsX)*asNumber(roomsZ)},(_,i)=>({x:i%asNumber(roomsX),z:Math.floor(i/asNumber(roomsX))})).filter(({x,z})=>(floorFootprints[editFloor]??footprint)[z]?.[x]!=='0');
              const cell=cells.find(c=>c.x+':'+c.z===interiorCell)??cells[0];
              if(!cell)return <small>No occupied rooms on this floor.</small>;
              const {x,z}=cell;
              const chosen=roomUses.find(r=>r.floor===editFloor&&r.x===x&&r.z===z);
              type RoomEdit=NonNullable<StudioBuildingRequest['roomUses']>[number];
              const patch=(change:Partial<RoomEdit>)=>{
                setRoomUses(old=>{
                  const prior=old.find(r=>r.floor===editFloor&&r.x===x&&r.z===z);
                  const next=old.filter(r=>r.floor!==editFloor||r.x!==x||r.z!==z);
                  const value={floor:editFloor,x,z,...prior,...change};
                  if(value.use!==undefined||value.furnitureDensity!==undefined||value.lighting!==undefined||value.decor!==undefined)next.push(value);
                  return next;
                });clearGenerated();
              };
              const tri=(value:boolean|undefined)=>value===undefined?'inherit':value?'on':'off';
              const fromTri=(value:string)=>value==='inherit'?undefined:value==='on';
              return <div className="au-stack">
                <StudioField label="Select room" htmlFor="abg-interior-cell">
                  <StudioSelect id="abg-interior-cell" value={x+':'+z} disabled={busy} onChange={e=>setInteriorCell(e.target.value)}>
                    {cells.map(c=><option key={c.x+':'+c.z} value={c.x+':'+c.z}>Room {c.x+1}, {c.z+1}{roomUses.some(r=>r.floor===editFloor&&r.x===c.x&&r.z===c.z)?' · edited':''}</option>)}
                  </StudioSelect>
                </StudioField>
                <div className="abg__fields">
                  <StudioField label="Room use" htmlFor="abg-room-use">
                    <StudioSelect id="abg-room-use" value={chosen?.use??'auto'} disabled={busy} onChange={e=>patch({use:e.target.value==='auto'?undefined:e.target.value as RoomEdit['use']})}>
                      {['auto','lounge','bedroom','kitchen','office','meeting','workshop','storage','retail','circulation','empty'].map(use=><option key={use} value={use}>{use}</option>)}
                    </StudioSelect>
                  </StudioField>
                  <StudioField label="Room furniture limit" htmlFor="abg-room-density">
                    <StudioSelect id="abg-room-density" value={chosen?.furnitureDensity===undefined?'inherit':String(chosen.furnitureDensity)} disabled={busy} onChange={e=>patch({furnitureDensity:e.target.value==='inherit'?undefined:Number(e.target.value)})}>
                      <option value="inherit">Inherit ({furnitureDensity})</option>{[0,1,2,3,4].map(n=><option key={n} value={n}>{n}</option>)}
                    </StudioSelect>
                  </StudioField>
                  <StudioField label="Room lighting" htmlFor="abg-room-light">
                    <StudioSelect id="abg-room-light" value={tri(chosen?.lighting)} disabled={busy} onChange={e=>patch({lighting:fromTri(e.target.value)})}><option value="inherit">Inherit</option><option value="on">On</option><option value="off">Off</option></StudioSelect>
                  </StudioField>
                  <StudioField label="Room decor" htmlFor="abg-room-decor">
                    <StudioSelect id="abg-room-decor" value={tri(chosen?.decor)} disabled={busy} onChange={e=>patch({decor:fromTri(e.target.value)})}><option value="inherit">Inherit</option><option value="on">On</option><option value="off">Off</option></StudioSelect>
                  </StudioField>
                </div>
                <StudioButton variant="secondary" disabled={busy||!chosen} onClick={()=>patch({use:undefined,furnitureDensity:undefined,lighting:undefined,decor:undefined})}>Reset selected room</StudioButton>
              </div>;
            })()}
          </>}
        </>}
        <small className="abg__note">Static procedural furniture uses style materials and physical bounds. Small rooms may remain empty to preserve circulation.</small>
      </StudioSection>}
        </div>
          </div>
          <StudioSection title="Building preview" className="abg__preview">
            {livePreview?<><div className="abg__preview-frame"
              onPointerDown={e=>{if(!previewBusy&&(e.button===0||e.button===1)){e.preventDefault();dragView.current={x:e.clientX,y:e.clientY,pan:e.shiftKey||e.button===1};e.currentTarget.setPointerCapture(e.pointerId);}}}
              onPointerMove={e=>{
                const start=dragView.current;if(!start||previewBusy)return;
                const dx=e.clientX-start.x,dy=e.clientY-start.y;
                dragView.current={x:e.clientX,y:e.clientY,pan:start.pan};
                const v=desiredCamera.current;
                if(start.pan)updatePreviewView({...v,panX:Math.max(-2,Math.min(2,v.panX-dx*.006*v.zoom)),panY:Math.max(-2,Math.min(2,v.panY+dy*.006*v.zoom))});
                else updatePreviewView({...v,yaw:((v.yaw+dx*.012)%6.2831853),pitch:Math.max(-1.35,Math.min(1.35,v.pitch+dy*.012))});
              }}
              onWheel={e=>{const v=desiredCamera.current;updatePreviewView({...v,zoom:Math.max(.55,Math.min(2.5,v.zoom*Math.exp(e.deltaY*.0015)))});}}
              onPointerCancel={()=>{dragView.current=null;}}
              onLostPointerCapture={()=>{dragView.current=null;}}
              onPointerUp={()=>{dragView.current=null;}}>
              <img src={livePreview.url} alt="Native generated building preview" draggable={false}/>
              {(previewBusy||cameraBusy)&&<span className="abg__preview-busy">Rendering…</span>}
            </div>
            <div className="abg__actions">
              <StudioButton size="sm" disabled={previewBusy} onClick={()=>void updatePreviewView({...viewCamera,yaw:viewCamera.yaw-.4})}>Rotate left</StudioButton>
              <StudioButton size="sm" disabled={previewBusy} onClick={()=>void updatePreviewView({...viewCamera,yaw:viewCamera.yaw+.4})}>Rotate right</StudioButton>
              <StudioButton size="sm" disabled={previewBusy||viewCamera.zoom<=.55} onClick={()=>void updatePreviewView({...viewCamera,zoom:Math.max(.55,viewCamera.zoom*.8)})}>Zoom in</StudioButton>
              <StudioButton size="sm" disabled={previewBusy||viewCamera.zoom>=2.5} onClick={()=>void updatePreviewView({...viewCamera,zoom:Math.min(2.5,viewCamera.zoom*1.25)})}>Zoom out</StudioButton>
              <StudioButton size="sm" disabled={previewBusy} onClick={()=>void updatePreviewView({yaw:.65,pitch:.42,zoom:1,panX:0,panY:0})}>Reset view</StudioButton>
            </div>
            {livePreview.key!==previewKey&&<StudioAlert tone="warning">Recipe changed. Preview 3D updates the model.</StudioAlert>}
            <small className="abg__note">Drag to orbit · Shift-drag or middle button to pan · wheel to zoom. The newest camera is rendered while dragging, independently of Scene.</small></>:<StudioEmptyState title="Preview a building" description="Render the current recipe here without replacing the Scene." action={<StudioButton disabled={Boolean(parameterError)||busy||previewBusy||!nativeConnected()} onClick={()=>void previewNative3D()}>Preview 3D</StudioButton>}/>}
          </StudioSection>
        </div>
      {saved&&<StudioSection title="Generated asset">
        <div className="abg__saved">
          {thumbnail&&<img src={thumbnail} alt="Native model asset thumbnail" className="abg__thumbnail"/>}
          <strong>{saved.path}</strong>
          {saved.recipePath&&<small>Editable recipe: {saved.recipePath}</small>}
          <small>GUID: {saved.guid}</small>
          <small>{saved.triangles.toLocaleString()} triangles · {(saved.bytes/1024).toFixed(1)} KiB</small>
          <div className="abg__actions">
            <StudioButton onClick={reveal}>Reveal in Assets</StudioButton>
            <StudioButton variant="primary" disabled={busy} loading={busy} onClick={()=>void place()}>Place in Scene</StudioButton>
            <StudioButton variant="secondary" disabled={busy||!snapshot?.selectedId?.startsWith('entity:')}
              onClick={()=>void applyToSelected()}>Use on Selected Object</StudioButton>
          </div>
        </div>
      </StudioSection>}
      <StudioSection title="Saved generated buildings">
        <div className="abg__actions">
          <StudioSelect aria-label="Saved building recipe" value={sourceRecipe}
            disabled={busy||readingRecipe||savedRecipes.length===0}
            onChange={e=>void loadRecipe(e.target.value)}>
            <option value="">Choose a saved building…</option>
            {savedRecipes.map(path=><option value={path} key={path}>{path}</option>)}
          </StudioSelect>
          <StudioButton variant="secondary" disabled={busy||readingRecipe||!nativeConnected()}
            onClick={()=>void reloadRecipes().catch(e=>setError(explain(e)))}>
            <RefreshCw size={15}/> Refresh
          </StudioButton>
        </div>
      </StudioSection>
      </div>
      <div className="abg__page au-stack" hidden={page!=='features'}>
        {mode!=='features'&&<StudioAlert>Switch to functional properties in Recipe to edit openings and vertical circulation.</StudioAlert>}
      {mode==='features'&&<StudioSection title="Functional properties">
        <div className="abg__property-list">
          {features.map((feature,i)=><div className="abg__property" key={i}>
            <div className="abg__property-header">
              <strong>{propertyLabels[feature.kind]}</strong>
              <StudioButton variant="secondary" disabled={busy} onClick={()=>removeFeature(i)}>Remove</StudioButton>
            </div>
            <div className="abg__property-controls">
              <StudioField label="Property type" htmlFor={"abg-feature-"+i+"-kind"}>
                <StudioSelect id={"abg-feature-"+i+"-kind"} value={feature.kind} disabled={busy}
                  onChange={e=>changeFeatureKind(i,e.target.value as StudioBuildingFeatureKind)}>
                  {Object.entries(propertyLabels).map(([kind,label])=><option key={kind} value={kind}>{label}</option>)}
                </StudioSelect>
              </StudioField>
              {!['sliding-roof','stairs','elevator'].includes(feature.kind)&&<StudioField label="Wall" htmlFor={"abg-feature-"+i+"-side"}>
                <StudioSelect id={"abg-feature-"+i+"-side"} value={feature.side} disabled={busy}
                  onChange={e=>{setFeatures(old=>old.map((f,j)=>j===i?{...f,side:e.target.value as StudioBuildingFeature['side'],wallLine:-1}:f));clearGenerated();}}>
                  {['front','back','left','right'].map(side=><option key={side} value={side}>{side}</option>)}
                </StudioSelect>
              </StudioField>}
              {!['sliding-roof','stairs','elevator'].includes(feature.kind)&&<StudioField label="Facade grid line" htmlFor={'abg-feature-'+i+'-wall-line'}>
                <StudioSelect id={'abg-feature-'+i+'-wall-line'} value={String(feature.wallLine??-1)} disabled={busy} onChange={e=>setFeature(i,'wallLine',Number(e.target.value))}>
                  <option value={-1}>Outer envelope (default)</option>
                  {Array.from({length:Math.max(0,Math.min(5,((feature.side==='front'||feature.side==='back')?asNumber(roomsZ):asNumber(roomsX))-1))},(_,k)=><option key={k+1} value={k+1}>Interior grid line {k+1}</option>)}
                </StudioSelect>
              </StudioField>}
              {!['sliding-roof','stairs','elevator'].includes(feature.kind)&&<StudioField label="Count" htmlFor={"abg-feature-"+i+"-count"}>
                <StudioInput id={"abg-feature-"+i+"-count"} type="number" min={1} max={8} step={1} value={String(feature.count)} disabled={busy}
                  onChange={e=>setFeature(i,'count',Number(e.target.value))}/>
              </StudioField>}
              <StudioField label={['stairs','elevator'].includes(feature.kind)?'Shaft width, m':feature.kind==='sliding-roof'?'Aperture width, m':'Clear width, m'} htmlFor={"abg-feature-"+i+"-width"}>
                <StudioInput id={"abg-feature-"+i+"-width"} type="number" min={0.85} step={0.1} value={String(feature.width)} disabled={busy}
                  onChange={e=>setFeature(i,'width',Number(e.target.value))}/>
              </StudioField>
              {['sliding-roof','stairs','elevator'].includes(feature.kind)?<StudioField label={feature.kind==='sliding-roof'?'Aperture depth, m':'Shaft depth, m'} htmlFor={"abg-feature-"+i+"-depth"}>
                <StudioInput id={"abg-feature-"+i+"-depth"} type="number" min={1} step={0.1} value={String(feature.depth)} disabled={busy}
                  onChange={e=>setFeature(i,'depth',Number(e.target.value))}/>
              </StudioField>:<StudioField label="Clear height, m" htmlFor={"abg-feature-"+i+"-height"}>
                <StudioInput id={"abg-feature-"+i+"-height"} type="number" min={feature.kind==='window'?0.4:2} step={0.1} value={String(feature.height)} disabled={busy}
                  onChange={e=>setFeature(i,'height',Number(e.target.value))}/>
              </StudioField>}
              <StudioField label={['sliding-roof','stairs','elevator'].includes(feature.kind)?'Plan centre X, m (-1 auto)':'Wall centre, m (-1 auto)'} htmlFor={"abg-feature-"+i+"-position"}>
                <StudioInput id={"abg-feature-"+i+"-position"} type="number" min={-1} step={0.1} value={String(feature.position)} disabled={busy}
                  onChange={e=>setFeature(i,'position',Number(e.target.value))}/>
              </StudioField>
              {['sliding-roof','stairs','elevator'].includes(feature.kind)?<StudioField label="Plan centre Z, m (-1 auto)" htmlFor={"abg-feature-"+i+"-position-z"}>
                <StudioInput id={"abg-feature-"+i+"-position-z"} type="number" min={-1} step={0.1} value={String(feature.positionZ)} disabled={busy}
                  onChange={e=>setFeature(i,'positionZ',Number(e.target.value))}/>
              </StudioField>:feature.count>1?<StudioField label="Gap between openings, m" htmlFor={"abg-feature-"+i+"-spacing"}>
                <StudioInput id={"abg-feature-"+i+"-spacing"} type="number" min={0.35} max={8} step={0.05}
                  value={String(feature.spacing)} disabled={busy}
                  onChange={e=>setFeature(i,'spacing',Number(e.target.value))}/>
              </StudioField>:null}
              {(feature.kind==='window'||((walkableSetbacks||floorFootprints.length>1)&&feature.kind==='personnel-door'))&&<StudioField label="Floor (starting at 0)" htmlFor={"abg-feature-"+i+"-floor"}>
                <StudioInput id={"abg-feature-"+i+"-floor"} type="number" min={0} max={7} step={1}
                  value={String(feature.floor)} disabled={busy}
                  onChange={e=>setFeature(i,'floor',Number(e.target.value))}/>
              </StudioField>}
              {feature.kind==='personnel-door'&&feature.floor>0&&floorFootprints.length>1&&<>
                <StudioField label="Projecting balcony depth, m (0 = terrace only)" htmlFor={'abg-feature-'+i+'-balcony-depth'}>
                  <StudioInput id={'abg-feature-'+i+'-balcony-depth'} type="number" min={0} max={3} step={.1} value={String(feature.balconyDepth??0)} disabled={busy}
                    onChange={e=>setFeature(i,'balconyDepth',Number(e.target.value))}/>
                </StudioField>
                {(feature.balconyDepth??0)>0&&<StudioField label="Balcony support" htmlFor={'abg-feature-'+i+'-balcony-support'}>
                  <StudioSelect id={'abg-feature-'+i+'-balcony-support'} value={feature.balconySupport??'cantilever'} disabled={busy}
                    onChange={e=>setFeature(i,'balconySupport',e.target.value)}>
                    <option value="cantilever">Cantilever beams (up to 1.8m)</option><option value="columns">Ground columns (up to 3m)</option>
                  </StudioSelect>
                </StudioField>}
              </>}
              {feature.kind==='window'&&<StudioField label="Sill height, m" htmlFor={"abg-feature-"+i+"-sill"}>
                <StudioInput id={"abg-feature-"+i+"-sill"} type="number" min={0.65} max={4} step={0.05}
                  value={String(feature.sill)} disabled={busy}
                  onChange={e=>setFeature(i,'sill',Number(e.target.value))}/>
              </StudioField>}
              {!['window','stairs','elevator'].includes(feature.kind)&&<StudioField label="Open (0–1)" htmlFor={"abg-feature-"+i+"-open"}>
                <StudioInput id={"abg-feature-"+i+"-open"} type="number" min={0} max={1} step={0.1} value={String(feature.open)} disabled={busy}
                  onChange={e=>setFeature(i,'open',Number(e.target.value))}/>
              </StudioField>}
            </div>
          </div>)}
        </div>
        <div className="abg__property-add">
          {Object.entries(propertyLabels).map(([kind,label])=><StudioButton key={kind}
            variant="secondary" disabled={busy||features.length>=24||
              (kind==='sliding-roof'&&features.some(f=>f.kind==='sliding-roof'))||
              (['stairs','elevator'].includes(kind)&&asNumber(floors)<2)}
            onClick={()=>addFeature(kind as StudioBuildingFeatureKind)}>+ {label}</StudioButton>)}
        </div>
        <small className="abg__note">Functionality is independent of setting. Entrances cut real wall openings;
          roof panels retract from a real roof aperture. Use Open to test different runtime states.</small>
      </StudioSection>}
      </div>
      <div className="abg__page au-stack" hidden={page!=='materials'}>
      <StudioSection title="Geometry detail">
        <StudioField label="Runtime representation" htmlFor="abg-visual-lods">
          <StudioSelect id="abg-visual-lods" value={mode==='features'&&!uniqueAtlas&&generateVisualLods?'lod':'rooms'} disabled={busy||mode!=='features'||uniqueAtlas} onChange={e=>{setGenerateVisualLods(e.target.value==='lod');clearGenerated();}}>
            <option value="rooms">Room groups / full detail</option><option value="lod">Semantic distance LOD</option>
          </StudioSelect>
        </StudioField>
        <small className="abg__note">Semantic LOD simplifies decorative geometry without changing collision or layout. It uses whole-building meshes and tiled textures; room partition and unique atlas are separate modes.</small>
      </StudioSection>
      <StudioSection title="Texture quality">
        <StudioField label="Generated map resolution" htmlFor="abg-texture-resolution">
          <StudioSelect id="abg-texture-resolution" value={textureResolution} disabled={busy} onChange={e=>{setTextureResolution(Number(e.target.value) as 128|256|512);clearGenerated();}}>
            <option value={128}>128 × 128 · Compact</option><option value={256}>256 × 256 · Standard</option><option value={512}>512 × 512 · Detailed</option>
          </StudioSelect>
        </StudioField>
        <StudioField label="Texture layout" htmlFor="abg-texture-layout">
          <StudioSelect id="abg-texture-layout" value={uniqueAtlas?'unique':'tiled'} disabled={busy} onChange={e=>{setUniqueAtlas(e.target.value==='unique');if(e.target.value==='unique')setGenerateVisualLods(false);clearGenerated();}}>
            <option value="tiled">Tiled material maps</option><option value="unique">Unique UV atlas (up to 2048)</option>
          </StudioSelect>
        </StudioField>
        <small className="abg__note">Unique atlas bakes procedural materials into four shared maps with separate triangle charts. Imported textures and UV rotation are not supported in this mode. Tiled mode preserves original texture detail. Applies to preview and exported GLB.</small>
      </StudioSection>
      <BuildingSurfacePreview key={(snapshot?.project.path??'')+':'+(selectedStyle?.id??'')} style={selectedStyle} visible={page==='materials'} resolution={textureResolution} projectLoaded={projectLoaded} onPublished={()=>{void invoke('studio.materials',{}).then(setMaterialLibrary).catch(e=>setError(explain(e)));}}/>
      <StudioSection title="Architectural material bindings">
        <p className="abg__note">Each patterned style slot automatically generates Base Color, Metallic/Roughness, Normal and AO maps in native preview and GLB. Project Assets override individual channels; no imported texture is required.</p>
        <div className="abg__fields">
          {(selectedStyle?.materials??[]).map(slot=><StudioField key={slot}
            label={slot} hint={selectedStyle?.surfaces?.[slot]?.pattern?'Generated · '+selectedStyle.surfaces[slot].pattern+' · '+textureResolution+' × '+textureResolution:'Authored style factors / texture references'} htmlFor={'abg-material-binding-'+slot}>
            <StudioSelect id={'abg-material-binding-'+slot}
              disabled={busy||!projectLoaded}
              value={materialBindings[slot]??''}
              onChange={e=>setBinding(slot,e.target.value)}>
              <option value="">Use style material</option>
              {Object.keys(materialLibrary.library).sort().map(material=>
                <option value={material} key={material}>{material}</option>)}
            </StudioSelect>
          </StudioField>)}
        </div>
      </StudioSection>
      <BuildingMaterialDependenciesView key={snapshot?.project.path??''} bindings={materialBindings} library={materialLibrary}
        onFocusSlot={focusMaterial}
        onRevealAsset={path=>{revealProjectAsset(snapshot?.project.path ?? '', path);showPanel('assets');}}/>
      <StudioSection title="Texture reimport dependencies">
        <div className="abg__actions">
          <StudioButton variant="secondary" disabled={busy||checkingMaterials||!nativeConnected()}
            onClick={()=>void checkMaterialDependencies()}>
            <RefreshCw size={15}/> {checkingMaterials?'Checking…':'Check generated material dependencies'}
          </StudioButton>
        </div>
        <label className="abg__note">
          <input type="checkbox" checked={autoRegenerate}
            onChange={e=>setAutoRegenerate(e.target.checked)}
            disabled={busy||Boolean(regeneratingRecipe)}/>
          Auto-create updated copies when Assets change (existing GLBs are never overwritten)
        </label>
        {staleMaterials.map(item=><div className="abg__property" key={item.recipe}>
          <strong>{item.recipe}</strong>
          {item.changes.map((change,index)=><small key={index} className="abg__note">
            {change.guid||'Asset'}: {change.reason}
          </small>)}
          <div className="abg__actions">
            <StudioButton variant="secondary" disabled={busy||Boolean(regeneratingRecipe)}
              onClick={()=>void regenerateRecipe(item.recipe)}>
              {regeneratingRecipe===item.recipe?'Regenerating…':'Create updated copy'}
            </StudioButton>
          </div>
        </div>)}
        <small className="abg__note">Automatically detects changes notified by Studio Assets. Optional auto-update creates at most one new copy per stale source recipe per Studio session; the original GLB and GUID are retained. Replacing a scene instance requires its existing renderer owner.</small>
      </StudioSection>
      <StudioSection title="PBR materials & textures">
        <BuildingMaterialEditor projectLoaded={projectLoaded} onSaved={()=>{
          void invoke('studio.materials',{}).then(setMaterialLibrary).catch(e=>setError(explain(e)));
          setStaleMaterials([]);
          clearGenerated();
        }}/>
      </StudioSection>
      </div>
      <div className="abg__page au-stack" hidden={page!=='graphs'}>
      {visibleVariants.length>0&&<StudioSection title="Variants">
        {visibleVariants.length>0&&<div className="abg__candidates" role="group" aria-label="Generated building candidates">
          {visibleVariants.map(candidate=><button type="button" key={candidate.batch+':'+candidate.seed}
            className={'abg__candidate'+(candidate.seed===selectedSeed?' abg__candidate--active':'')}
            disabled={busy||!candidate.data}
            aria-pressed={candidate.seed===selectedSeed}
            onClick={()=>setSelectedSeed(candidate.seed)}>
              <strong>Seed {candidate.seed}</strong>
              {candidate.data
                ?<small>{candidate.data.triangles.toLocaleString()} triangles · {candidate.data.materials} materials</small>
                :<small>{candidate.error??'Unavailable'}</small>}
            </button>)}
        </div>}
        <small className="abg__note">Variant evaluation measures native CPU geometry and reports real sockets. Use Preview 3D for the native Renderer3D model without writing an Asset. Generate GLB only when ready to persist the model.</small>
      </StudioSection>}

        {!activeCandidate?.data&&<StudioEmptyState title="Evaluate a building first" description="Evaluate 3 seeds to inspect the actual native geometry, rooms and routes. No graph is fabricated before evaluation."/>}
      {activeCandidate?.data?.visualLods&&<StudioSection title="Generated semantic LODs">
        <small>LOD 0 · {activeCandidate.data.triangles.toLocaleString()} triangles</small>
        {activeCandidate.data.visualLods.map(lod=><small key={lod.detailLevel}>LOD {lod.detailLevel} · {lod.triangles.toLocaleString()} triangles · ≤ {lod.maxPixels} px</small>)}
        {!activeCandidate.data.visualLods.length&&<small>No cheaper semantic alternatives for this recipe.</small>}
      </StudioSection>}
      {activeCandidate?.data&&<StudioSection title="Generated layout and structure">
        {activeCandidate.data.sockets&&<BuildingPlanPreview
          key={'plan:'+activeCandidate.batch+':'+activeCandidate.seed}
          preview={activeCandidate.data} onSelectFeature={focusFeature}/>}
        <BuildingStructureGraphView key={'graph:'+activeCandidate.batch+':'+activeCandidate.seed}
          recipe={activeCandidate.recipe} inspection={activeCandidate.data} onSelectFeature={focusFeature}
          onRoute={async routeQuery=>(await invoke('studio.buildingInspect',{...activeCandidate.recipe,routeQuery})).route}
          renderFeatureEditor={index=>features[index]?<GraphFeatureEditor key={index} feature={features[index]} disabled={busy}
            onApply={patch=>{if(busy||!activeCandidate||mode!=='features')return;
              setFeatures(old=>old.map((feature,i)=>i===index?{...feature,...patch}:feature));clearGenerated();}}/>:null}
          renderMaterialInspector={slot=>{
            const name=materialBindings[slot]??'',material=materialLibrary.library[name];
            return <div className="au-stack">
              <strong>Material slot · {slot}</strong>
              <StudioSelect aria-label={'Graph material '+slot} value={name} disabled={busy||!projectLoaded} onChange={e=>setBinding(slot,e.target.value)}>
                <option value="">Use style material</option>
                {Object.keys(materialLibrary.library).sort().map(item=><option key={item} value={item}>{item}</option>)}
              </StudioSelect>
              {name&&!material&&<StudioAlert tone="warning">Project material not found in current library.</StudioAlert>}
              {material&&['texture','normalMap','metallicRoughnessMap','occlusionMap','emissiveMap'].map(channel=><small key={channel}>{channel}: {typeof material[channel]==='string'&&material[channel]?String(material[channel]):'Not explicitly assigned'}</small>)}
              <small>Current project material references only, not resolved exported GLB dependencies.</small>
              <StudioButton size="sm" onClick={()=>focusMaterial(slot)}>Focus source material binding</StudioButton>
            </div>;
          }}/>
      </StudioSection>}
      </div>
      <div className="abg__page au-stack" hidden={page!=='runtime'}>
      <BuildingLiveGraphMonitor key={snapshot?.project.path??''}/>
      </div>
    </div>
  </StudioPanel>;
}
