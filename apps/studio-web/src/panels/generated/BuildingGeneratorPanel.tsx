import { useEffect, useMemo, useState } from 'react';
import { Building2, Dices, RefreshCw, WandSparkles } from 'lucide-react';
import { invoke, nativeConnected } from '../../bridge';
import type {
  StudioSnapshot, StudioBuildingRequest, StudioBuildingStyle,
  StudioBuildingPreview, StudioBuildingGenerated
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

export default function BuildingGeneratorPanel({
  snapshot, showPanel
}: { snapshot:StudioSnapshot|null; showPanel:(p:PanelId)=>void }) {
  const projectLoaded=Boolean(snapshot?.project.path);
  const [styles,setStyles]=useState<StudioBuildingStyle[]>([]);
  const [setting,setSetting]=useState('scifi');
  const [style,setStyle]=useState('scifi');
  const [role,setRole]=useState('factory');
  const [width,setWidth]=useState('24');
  const [height,setHeight]=useState('12');
  const [depth,setDepth]=useState('18');
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
    if(!selectedStyle||!selectedFunction)return 'Choose a style with generated building functions.';
    for(const [name,raw,min,max] of [
      ['Width',width,8,160],['Height',height,5,80],['Depth',depth,8,160]
    ] as const){
      const value=asNumber(raw);
      if(!Number.isFinite(value)||value<min||value>max)
        return name+' must be between '+min+' and '+max+' metres.';
    }
    const n=asNumber(seed);
    if(!Number.isSafeInteger(n)||n<0||n>2147483645)
      return 'Seed must be an integer between 0 and 2147483645.';
    return '';
  },[selectedStyle,selectedFunction,width,height,depth,seed]);

  const request=(useSeed:number):StudioBuildingRequest=>({
    setting,style,function:role,width:asNumber(width),height:asNumber(height),
    depth:asNumber(depth),seed:useSeed
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
      void invoke('studio.windowBroadcast',{event:'ui.assetsChanged',payload:{path:result.path}}).catch(()=>{});
      void invoke('studio.assetThumbnail',{path:result.path}).then(thumb=>{
        if(thumb.url)setThumbnail(thumb.url);
      }).catch(()=>{});
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
          <StudioField label="Building function" htmlFor="abg-role">
            <StudioSelect id="abg-role" value={role} onChange={e=>changeRole(e.target.value)}
              disabled={busy||functions.length===0}>
              {functions.map(item=><option value={item.id} key={item.id}>{item.name||item.id}</option>)}
            </StudioSelect>
          </StudioField>
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
          <small>GUID: {saved.guid}</small>
          <small>{saved.triangles.toLocaleString()} triangles · {(saved.bytes/1024).toFixed(1)} KiB</small>
          <StudioButton onClick={()=>showPanel('assets')}>Show Assets</StudioButton>
        </div>
      </StudioSection>}
    </div>
  </StudioPanel>;
}
