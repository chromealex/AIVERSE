import HumanPreviewAnimationControls from '../HumanPreviewAnimationControls';
import HumanStudioLightingControls from '../HumanStudioLightingControls';
import HumanFaceControls from '../HumanFaceControls';
import HumanWardrobeControls from '../HumanWardrobeControls';
import { useState } from 'react';
import type { HumanLook, StudioSnapshot } from '../../types';
import { invoke, nativeConnected } from '../../bridge';
import { emptyLook, useHumanLook } from '../useHumanLook';
import AssetReferencePicker from '../../components/AssetReferencePicker';
import { StudioPanel, StudioSection, StudioBadge, StudioAlert, StudioEmptyState,
  StudioButton, StudioField, StudioInput, StudioSelect } from '../../ui';
import './HumanAppearancePanel.css';
import { validateHumanLook } from '../humanLookSchema';
const groups:Record<string,string>={skin:'Skin · tone & texture',hair:'Hair · colour & finish',
  lashes:'Eyelashes',eyes:'Eyes · iris & sclera',cloth:'Clothing · fabric',body:'Body proportions'};
export default function HumanAppearancePanel({snapshot}:{snapshot:StudioSnapshot|null}) {
 const project=snapshot?.project.path ?? '';
 const h=useHumanLook(project);
 const {info,face,setFace,draft,setDraft,edit,guid,setGuid,saved,setSaved,busy,error,status,setStatus,undo,redo,run,loadAsset}=h;
 const [name,setName]=useState('character');const [selected,setSelected]=useState('');
 const apply=()=>h.apply();
 const validName=/^[A-Za-z0-9_-]{1,64}$/.test(name);
 if(!project)return <StudioPanel title="VIVID Appearance"><StudioEmptyState title="Open a project" description="Choose a character model from project content to edit its appearance."/></StudioPanel>;
 if(!nativeConnected())return <StudioPanel title="VIVID Appearance"><StudioAlert>Open native Studio to edit and render a character. Browser demo does not simulate material changes.</StudioAlert></StudioPanel>;
 return <StudioPanel className="studio-panel-human_appearance" title="VIVID Appearance" eyebrow="CHARACTER / LOOK DEVELOPMENT"
  subtitle="Skin, hair, eyes, fabric and body"
  footer={<StudioBadge tone={info?.preview.runtimeReady?'success':'neutral'}>{info?.preview.runtimeReady?'Native preview ready':'Preview pending'}</StudioBadge>}>
  <div className="au-stack">
   {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
   {status&&<StudioAlert>{status}</StudioAlert>}
   {info?.preview.bound&&!info.preview.runtimeReady&&<StudioAlert tone="warning">{info.preview.reason}</StudioAlert>}
   <StudioSection title="Character">
    <AssetReferencePicker value={guid} onChange={v=>{setGuid(v);setFace(null);setStatus('Load the selected model profile before applying');}} kinds={['model']} label="Character model" disabled={busy}/>
    <div className="au-row"><StudioButton disabled={busy||!guid} onClick={()=>void loadAsset()}>Load model profile</StudioButton>
    <StudioButton variant="primary" disabled={busy||!guid||!info} onClick={()=>void apply()}>Apply to preview</StudioButton></div>
    <StudioField label="Import appearance JSON" htmlFor="human-look-import" hint="Read a profile; native validation runs on Apply or Save.">
     <StudioInput id="human-look-import" type="file" accept=".json,application/json" disabled={busy} onChange={e=>{
      const file=e.target.files?.[0];if(!file)return;
      void run(async()=>{if(file.size>128*1024)throw Error('Profile exceeds 128 KiB');
       const value=JSON.parse(await file.text()) as HumanLook;
       if(!info||!validateHumanLook(value,info.schema))throw Error('Invalid profile: check fields, roles and numeric ranges');
       edit(value);});
     }}/>
    </StudioField>
   </StudioSection>
   <div className="au-row">
    <StudioButton disabled={busy||!undo.length} onClick={h.undoLook}>Undo</StudioButton>
    <StudioButton disabled={busy||!redo.length} onClick={h.redoLook}>Redo</StudioButton>
    <StudioButton disabled={busy||!info} onClick={()=>edit({...draft,values:{}})}>Reset values</StudioButton>
   </div>
   {Object.entries(groups).map(([group,title])=><details key={group} open={group==='skin'}>
    <summary>{title}</summary><StudioSection title={title}>
     {info?.schema.filter(f=>f.key.startsWith(group+'.')).map(f=>{
      const id='human-'+f.key.replace('.','-');const value=draft.values[f.key]??f.initial;
      return <StudioField key={f.key} label={f.label} htmlFor={id}>
       <div className="human-look-value">
        <StudioInput id={id} type="range" min={f.min} max={f.max} step={(f.max-f.min)>10?1:.001} value={value} disabled={busy}
          onChange={e=>edit({...draft,values:{...draft.values,[f.key]:Number(e.target.value)}})}/>
        <StudioInput aria-label={f.label+' value'} type="number" min={f.min} max={f.max} step={.01} value={Number(value.toFixed(3))} disabled={busy}
          onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n)&&n>=f.min&&n<=f.max)edit({...draft,values:{...draft.values,[f.key]:n}});}}/>
       </div>
      </StudioField>;
     })}
     {group==='body'&&<StudioAlert>Upright skinned characters only. Clothing follows skeleton deformation; check intersections at extreme proportions.</StudioAlert>}
     {group==='hair'&&<StudioAlert>Colour effects use prepared root/strand attributes. These controls do not regenerate the hairstyle geometry.</StudioAlert>}
    </StudioSection></details>)}
   <details><summary>Material assignments ({Object.keys(draft.materials).length})</summary><StudioSection title="Source material roles">
    {Object.entries(draft.materials).map(([material,role])=><StudioField key={material} label={material} htmlFor={'role-'+material}>
     <StudioSelect id={'role-'+material} value={role} disabled={busy} onChange={e=>edit({...draft,materials:{...draft.materials,[material]:e.target.value}})}>
      {['skin','hair','lashes','eyes','cloth'].map(r=><option key={r}>{r}</option>)}
     </StudioSelect></StudioField>)}
    {!Object.keys(draft.materials).length&&<StudioAlert>Load the model profile or import appearance.json to assign its materials.</StudioAlert>}
   </StudioSection></details>
   <HumanPreviewAnimationControls key={"animation"+project}/>
   <HumanStudioLightingControls key={"lighting"+project}/>
   <HumanFaceControls key={"face"+project+guid} guid={guid} face={face} schema={info?.faceSchema??[]} onChange={setFace} onImported={v=>{setGuid(v);setFace(null);setDraft(emptyLook());}}/>
   <HumanWardrobeControls key={project+guid} guid={guid} look={draft} face={face}/>
   <StudioSection title="Saved looks">
    <StudioField label="Profile name" htmlFor="human-look-name"><StudioInput id="human-look-name" value={name} disabled={busy} onChange={e=>setName(e.target.value)}/></StudioField>
    <StudioButton disabled={busy||!validName} onClick={()=>void run(async()=>{
     await invoke('studio.humanAppearanceSave',{name,look:draft});setSaved((await invoke('studio.humanAppearanceList',{})).looks);setStatus('Saved in project/human-looks/'+name+'.json');
    })}>Save profile</StudioButton>
    <StudioField label="Saved profile" htmlFor="human-look-saved"><StudioSelect id="human-look-saved" value={selected} disabled={busy} onChange={e=>setSelected(e.target.value)}>
     <option value="">Select a profile</option>{saved.map(n=><option key={n}>{n}</option>)}
    </StudioSelect></StudioField>
    <StudioButton disabled={busy||!selected} onClick={()=>void run(async()=>{const result=await invoke('studio.humanAppearanceLoad',{name:selected});edit(result.look);setName(result.name);})}>Load profile</StudioButton>
   </StudioSection>
  </div>
 </StudioPanel>;
}
