import { useEffect, useMemo, useState } from 'react';
import { Building2, Dices, RefreshCw, WandSparkles } from 'lucide-react';
import { invoke, nativeConnected } from '../../bridge';
import type {
  StudioSnapshot, StudioBuildingRequest, StudioBuildingStyle,
  StudioBuildingPreview, StudioBuildingGenerated,
  StudioBuildingFeature, StudioBuildingFeatureKind
} from '../../types';
import type { StudioPanel as PanelId } from '../../nestedDock';
import {
  StudioPanel, StudioSection, StudioBadge, StudioAlert, StudioEmptyState,
  StudioButton, StudioIconButton, StudioField, StudioInput, StudioSelect
} from '../../ui';
import './BuildingGeneratorPanel.css';

type Candidate = { seed:number; data?:StudioBuildingPreview; error?:string };
const explain = (error:unknown) => error instanceof Error ? error.message : String(error);
const settingLabel = (id:string) => ({scifi:'Sci-fi',modern:'Modern'} as Record<string,string>)[id] ?? id;
const asNumber = (raw:string) => raw.trim()==='' ? NaN : Number(raw);
const makeFeature = (kind:StudioBuildingFeatureKind):StudioBuildingFeature => ({
  kind, side:'front', count:1, width:kind==='large-gate'?5:kind==='small-gate'?2.7:kind==='sliding-roof'?5:kind==='window'?1.6:1.2,
  height:kind==='large-gate'?4.5:kind==='small-gate'?2.9:kind==='sliding-roof'?0:kind==='window'?1.1:2.35,
  depth:kind==='sliding-roof'?5:0, open:0,
  position:-1, positionZ:-1, spacing:0.35, floor:0, sill:0.9
});
const propertyLabels:Record<StudioBuildingFeatureKind,string> = {
  'large-gate':'Large vehicle gates', 'small-gate':'Small gates',
  'personnel-door':'Personnel entrances', 'sliding-roof':'Retractable roof',
  'window':'Windows / glazing'
};

export default function BuildingGeneratorPanel({
  snapshot, showPanel, commitSnapshot
}: { snapshot:StudioSnapshot|null; showPanel:(p:PanelId)=>void; commitSnapshot:(s:StudioSnapshot)=>void }) {
  const projectLoaded=Boolean(snapshot?.project.path);
  const [styles,setStyles]=useState<StudioBuildingStyle[]>([]);
  const [setting,setSetting]=useState('scifi');
  const [style,setStyle]=useState('scifi');
  const [role,setRole]=useState('factory');
  const [mode,setMode]=useState<'features'|'preset'>('features');
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
  const [seed,setSeed]=useState('482733');
  const [selectedSeed,setSelectedSeed]=useState<number|null>(null);
  const [variants,setVariants]=useState<Candidate[]>([]);
  const [saved,setSaved]=useState<StudioBuildingGenerated|null>(null);
  const [thumbnail,setThumbnail]=useState('');
  const [busy,setBusy]=useState(false);
  const [loading,setLoading]=useState(false);
  const [error,setError]=useState('');
  const [status,setStatus]=useState('');

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
      }
    }).catch(e=>{if(active)setError(explain(e));})
      .finally(()=>{if(active)setLoading(false);});
    return ()=>{active=false;};
  },[projectLoaded,snapshot?.project.path]);

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
      const f=asNumber(floors),rx=asNumber(roomsX),rz=asNumber(roomsZ);
      if(!Number.isInteger(f)||f<1||f>8||!Number.isInteger(rx)||rx<1||rx>6||
         !Number.isInteger(rz)||rz<1||rz>6||f*rx*rz>128)
        return 'Use 1–8 floors and 1–6 rooms per axis (maximum 128 rooms).';
      if((asNumber(height)-0.52)/f<2.8||asNumber(width)/rx<2.5||asNumber(depth)/rz<2.5)
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
          const maxWidthForKind=isWindow?6:maxWidth;
          const storey=(asNumber(height)-0.52)/asNumber(floors);
          if(!Number.isInteger(feature.floor)||feature.floor<0||feature.floor>=asNumber(floors)||
             (!isWindow&&feature.floor!==0))
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
          const info=used[feature.side]??{width:0,count:0};
          info.width+=feature.width*feature.count;info.count+=feature.count;
          used[feature.side]=info;
        }
      }
      for(const [side,info] of Object.entries(used)){
        const length=(side==='front'||side==='back')?asNumber(width):asNumber(depth);
        if((length-1.44-info.width)/(info.count+1)<0.35)
          return 'Too many or overly wide openings on the '+side+' wall.';
      }
    }
    return '';
  },[selectedStyle,selectedFunction,mode,features,width,height,depth,seed,floors,roomsX,roomsZ]);

  const request=(useSeed:number):StudioBuildingRequest=>({
    setting,style,function:mode==='preset'?role:'',mode,
    features:mode==='features'?features:undefined,
    width:asNumber(width),height:asNumber(height),depth:asNumber(depth),seed:useSeed,
    floors:asNumber(floors),roomsX:asNumber(roomsX),roomsZ:asNumber(roomsZ)
  });
  const clearGenerated=()=>{setVariants([]);setSelectedSeed(null);setSaved(null);setThumbnail('');setStatus('');};
  const changeSetting=(value:string)=>{
    const first=styles.find(item=>item.setting===value);
    setSetting(value);setStyle(first?.id??'');setRole(first?.functions[0]?.id??'');
    clearGenerated();
  };
  const changeStyle=(value:string)=>{
    const item=styles.find(x=>x.id===value);
    setStyle(value);setRole(item?.functions[0]?.id??'');clearGenerated();
  };
  const changeRole=(value:string)=>{setRole(value);clearGenerated();};
  const changeMode=(value:'features'|'preset')=>{setMode(value);clearGenerated();};
  const setFeature=(index:number,key:keyof StudioBuildingFeature,value:string|number)=>{
    setFeatures(old=>old.map((f,i)=>i===index?({...f,[key]:value} as StudioBuildingFeature):f));
    clearGenerated();
  };
  const changeFeatureKind=(index:number,kind:StudioBuildingFeatureKind)=>{
    setFeatures(old=>old.map((f,i)=>i===index?makeFeature(kind):f));
    clearGenerated();
  };
  const addFeature=(kind:StudioBuildingFeatureKind)=>{
    if(features.length>=24||(kind==='sliding-roof'&&features.some(f=>f.kind==='sliding-roof')))return;
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

  const preview=async()=>{
    if(parameterError||busy)return;
    setBusy(true);setError('');setStatus('');setSaved(null);setThumbnail('');
    const first=asNumber(seed);
    try{
      const choices=await Promise.all([first,first+1,first+2].map(async current=>{
        try{
          return {seed:current,data:await invoke('studio.buildingInspect',request(current))} as Candidate;
        }catch(e){return {seed:current,error:explain(e)} as Candidate;}
      }));
      setVariants(choices);
      const firstGood=choices.find(x=>x.data);
      setSelectedSeed(firstGood?.seed??null);
      if(!firstGood)setError('No feasible building variant for these dimensions. Change the size or style.');
      else setStatus('Three seed candidates evaluated from real procedural mesh geometry.');
    }catch(e){setError(explain(e));}
    finally{setBusy(false);}
  };

  const generate=async()=>{
    if(parameterError||busy)return;
    setBusy(true);setError('');setStatus('');setSaved(null);setThumbnail('');
    try{
      const chosen=selectedSeed??asNumber(seed);
      const result=await invoke('studio.buildingGenerate',request(chosen));
      setSaved(result);setStatus('New GLB saved to Assets with persistent GUID.');
      // Existing browser windows can refresh their Assets lists on demand.
      window.dispatchEvent(new Event('studio:assets-changed'));
      void invoke('studio.windowBroadcast',{event:'ui.assetsChanged',payload:{path:result.path}}).catch(()=>{});
      void invoke('studio.assetThumbnail',{path:result.path}).then(thumb=>{
        if(thumb.url)setThumbnail(thumb.url);
      }).catch(()=>{});
    }catch(e){setError(explain(e));}
    finally{setBusy(false);}
  };

  const reveal=()=>{
    if(!saved)return;
    showPanel('assets');
    // Reopening a dock may mount Assets only on the next React commit.
    requestAnimationFrame(()=>window.dispatchEvent(new CustomEvent('studio:reveal-asset',{detail:{path:saved.path}})));
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

  return <StudioPanel title="Building Generator" eyebrow="WORLD / PROCEDURAL"
    subtitle={snapshot?.project.name??''}
    actions={<StudioIconButton icon={<RefreshCw size={15}/>} aria-label="Reload building styles"
      disabled={busy||loading} onClick={()=>void invoke('studio.buildingCatalog',{}).then(r=>setStyles(r.styles)).catch(e=>setError(explain(e)))}/>}
    footer={<StudioBadge tone={error?'danger':saved?'success':'neutral'}>
      {busy?'Generating…':error?'Check parameters':saved?'Saved in Assets':'Ready'}
    </StudioBadge>}>
    <div className="au-stack">
      {!nativeConnected()&&<StudioAlert tone="warning">Open native AIVERSE Studio to generate and save 3D models. Browser demo cannot write project assets.</StudioAlert>}
      {error&&<StudioAlert tone="danger" title="Generator error">{error}</StudioAlert>}
      {status&&!error&&<StudioAlert tone="success">{status}</StudioAlert>}
      {loading&&<StudioAlert>Loading available building styles…</StudioAlert>}
      {!loading&&styles.length===0&&<StudioEmptyState title="No building styles"
        icon={<Building2 size={22}/>} description="Install engine/assets/building-styles or open native AIVERSE Studio."/>}
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
          {mode==='preset'&&<StudioField label="Legacy silhouette preset" htmlFor="abg-role">
            <StudioSelect id="abg-role" value={role} onChange={e=>changeRole(e.target.value)}
              disabled={busy||functions.length===0}>
              {functions.map(item=><option value={item.id} key={item.id}>{item.name||item.id}</option>)}
            </StudioSelect>
          </StudioField>}
        </div>
      </StudioSection>
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
              {feature.kind!=='sliding-roof'&&<StudioField label="Wall" htmlFor={"abg-feature-"+i+"-side"}>
                <StudioSelect id={"abg-feature-"+i+"-side"} value={feature.side} disabled={busy}
                  onChange={e=>setFeature(i,'side',e.target.value)}>
                  {['front','back','left','right'].map(side=><option key={side} value={side}>{side}</option>)}
                </StudioSelect>
              </StudioField>}
              {feature.kind!=='sliding-roof'&&<StudioField label="Count" htmlFor={"abg-feature-"+i+"-count"}>
                <StudioInput id={"abg-feature-"+i+"-count"} type="number" min={1} max={8} step={1} value={String(feature.count)} disabled={busy}
                  onChange={e=>setFeature(i,'count',Number(e.target.value))}/>
              </StudioField>}
              <StudioField label={feature.kind==='sliding-roof'?'Aperture width, m':'Clear width, m'} htmlFor={"abg-feature-"+i+"-width"}>
                <StudioInput id={"abg-feature-"+i+"-width"} type="number" min={0.85} step={0.1} value={String(feature.width)} disabled={busy}
                  onChange={e=>setFeature(i,'width',Number(e.target.value))}/>
              </StudioField>
              {feature.kind==='sliding-roof'?<StudioField label="Aperture depth, m" htmlFor={"abg-feature-"+i+"-depth"}>
                <StudioInput id={"abg-feature-"+i+"-depth"} type="number" min={1} step={0.1} value={String(feature.depth)} disabled={busy}
                  onChange={e=>setFeature(i,'depth',Number(e.target.value))}/>
              </StudioField>:<StudioField label="Clear height, m" htmlFor={"abg-feature-"+i+"-height"}>
                <StudioInput id={"abg-feature-"+i+"-height"} type="number" min={2} step={0.1} value={String(feature.height)} disabled={busy}
                  onChange={e=>setFeature(i,'height',Number(e.target.value))}/>
              </StudioField>}
              <StudioField label={feature.kind==='sliding-roof'?'Roof centre X, m (-1 auto)':'Wall centre, m (-1 auto)'} htmlFor={"abg-feature-"+i+"-position"}>
                <StudioInput id={"abg-feature-"+i+"-position"} type="number" min={-1} step={0.1} value={String(feature.position)} disabled={busy}
                  onChange={e=>setFeature(i,'position',Number(e.target.value))}/>
              </StudioField>
              {feature.kind==='sliding-roof'?<StudioField label="Roof centre Z, m (-1 auto)" htmlFor={"abg-feature-"+i+"-position-z"}>
                <StudioInput id={"abg-feature-"+i+"-position-z"} type="number" min={-1} step={0.1} value={String(feature.positionZ)} disabled={busy}
                  onChange={e=>setFeature(i,'positionZ',Number(e.target.value))}/>
              </StudioField>:feature.count>1?<StudioField label="Gap between openings, m" htmlFor={"abg-feature-"+i+"-spacing"}>
                <StudioInput id={"abg-feature-"+i+"-spacing"} type="number" min={0.35} max={8} step={0.05}
                  value={String(feature.spacing)} disabled={busy}
                  onChange={e=>setFeature(i,'spacing',Number(e.target.value))}/>
              </StudioField>:null}
              {feature.kind==='window'&&<StudioField label="Floor (starting at 0)" htmlFor={"abg-feature-"+i+"-floor"}>
                <StudioInput id={"abg-feature-"+i+"-floor"} type="number" min={0} max={7} step={1}
                  value={String(feature.floor)} disabled={busy}
                  onChange={e=>setFeature(i,'floor',Number(e.target.value))}/>
              </StudioField>}
              {feature.kind==='window'&&<StudioField label="Sill height, m" htmlFor={"abg-feature-"+i+"-sill"}>
                <StudioInput id={"abg-feature-"+i+"-sill"} type="number" min={0.65} max={4} step={0.05}
                  value={String(feature.sill)} disabled={busy}
                  onChange={e=>setFeature(i,'sill',Number(e.target.value))}/>
              </StudioField>}
              {feature.kind!=='window'&&<StudioField label="Open (0–1)" htmlFor={"abg-feature-"+i+"-open"}>
                <StudioInput id={"abg-feature-"+i+"-open"} type="number" min={0} max={1} step={0.1} value={String(feature.open)} disabled={busy}
                  onChange={e=>setFeature(i,'open',Number(e.target.value))}/>
              </StudioField>}
            </div>
          </div>)}
        </div>
        <div className="abg__property-add">
          {Object.entries(propertyLabels).map(([kind,label])=><StudioButton key={kind}
            variant="secondary" disabled={busy||features.length>=24||
              (kind==='sliding-roof'&&features.some(f=>f.kind==='sliding-roof'))}
            onClick={()=>addFeature(kind as StudioBuildingFeatureKind)}>+ {label}</StudioButton>)}
        </div>
        <small className="abg__note">Functionality is independent of setting. Entrances cut real wall openings;
          roof panels retract from a real roof aperture. Use Open to test different runtime states.</small>
      </StudioSection>}
      {mode==='features'&&<StudioSection title="Floors and rooms">
        <div className="abg__dimensions">
          <StudioField label="Floors" htmlFor="abg-floors">
            <StudioInput id="abg-floors" type="number" min={1} max={8} step={1} value={floors}
              onChange={setDimension(setFloors)} disabled={busy}/>
          </StudioField>
          <StudioField label="Rooms X" htmlFor="abg-rooms-x">
            <StudioInput id="abg-rooms-x" type="number" min={1} max={6} step={1} value={roomsX}
              onChange={setDimension(setRoomsX)} disabled={busy}/>
          </StudioField>
          <StudioField label="Rooms Z" htmlFor="abg-rooms-z">
            <StudioInput id="abg-rooms-z" type="number" min={1} max={6} step={1} value={roomsZ}
              onChange={setDimension(setRoomsZ)} disabled={busy}/>
          </StudioField>
        </div>
        <small className="abg__note">Internal partition passages are generated on every floor. Stairs, lifts and inter-floor navmesh are not yet generated.</small>
      </StudioSection>}
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
        {parameterError&&<StudioAlert tone="warning" title="Parameters">{parameterError}</StudioAlert>}
      </StudioSection>
      <StudioSection title="Variants">
        <div className="abg__actions">
          <StudioButton variant="secondary" loading={busy} disabled={Boolean(parameterError)||busy}
            onClick={()=>void preview()}>Evaluate 3 seeds</StudioButton>
          <StudioButton variant="primary" loading={busy}
            disabled={Boolean(parameterError)||busy||!nativeConnected()}
            onClick={()=>void generate()}>
            <WandSparkles size={15}/> Generate GLB
          </StudioButton>
        </div>
        {variants.length>0&&<div className="abg__candidates" role="group" aria-label="Generated building candidates">
          {variants.map(candidate=><button type="button" key={candidate.seed}
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
        <small className="abg__note">Variant evaluation uses the real CPU geometry generator. Interactive 3D preview is a separate native viewport feature; these cards do not represent GPU screenshots.</small>
      </StudioSection>
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
          </div>
        </div>
      </StudioSection>}
    </div>
  </StudioPanel>;
}
