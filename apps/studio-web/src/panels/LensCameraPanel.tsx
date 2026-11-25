import { useEffect, useLayoutEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { Camera, Plus, Save, Trash2 } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import SpectraDockViewport from './SpectraDockViewport';
import { ActorSimulation } from './ActorSimulation';
import './LensCameraPanel.css';

const optics = [
  ['focalLengthMm','Focal length (mm)',50],['sensorHeightMm','Sensor height (mm)',24],
  ['fNumber','Aperture (f-number)',2.8],['focusDistance','Focus distance',10],
  ['shutterSeconds','Shutter (seconds)',1/60],['iso','ISO',100],
  ['exposureCompensation','Exposure compensation (EV)',0],['circleOfConfusionMm','Circle of confusion (mm)',0],
] as const;
const lensSwitches = [['physicalFov','Use physical field of view'],['driveExposure','Drive exposure'],['driveDepthOfField','Drive depth of field']] as const;
type Lens = Partial<Record<typeof optics[number][0],number> & Record<typeof lensSwitches[number][0],boolean>>;

const followFields = [
  ['pitch','Follow pitch (degrees)',58],['yaw','Follow yaw (degrees)',0],['distance','Follow distance',20],
  ['smoothing','Follow smoothing',6],['velocityLookAhead','Velocity lead (seconds)',0],
  ['maxLookAhead','Maximum lead',0],['fov','Follow FOV (degrees)',0],
] as const;
type FollowBody = {preset?:string} & Partial<Record<typeof followFields[number][0],number>>;
interface FollowPreset {name:string;body:string;keys:FollowBody}

interface CameraRecord {
  id: number;
  name?: string;
  position?: number[];
  rotation?: number[];
  lens?: Lens;
  follow?: FollowBody;
  rig?: {mode?:number;enabled?:boolean;trackTarget?:boolean;offset?:number[];distance?:number;yaw?:number;pitch?:number;smoothing?:number};
  update?: {mode?:number;everyNFrames?:number;rateHz?:number};
  stack?: {overlay?:boolean;order?:number;opacity?:number};
  viewport?: {enabled?:boolean;x?:number;y?:number;width?:number;height?:number};
  camera?: { projection?: string; fov?: number; orthographicHalfHeight?: number; near?: number; far?: number; enabled?: boolean };
  output?: { kind?: string; key?: number; width?: number; height?: number; hdr?: boolean; depth?: boolean };
}
interface CameraScene { cameras: CameraRecord[] }
interface Document { scene: CameraScene; revision: string; ok: boolean; error?: string; normalizedScene: CameraScene }

// A dock tab unmounts when Scene is focused. Keep only unsaved camera documents
// and their original revisions so that returning to the tab preserves edits.
const drafts=new Map<string,{document:Document;scene:CameraScene;selected?:number}>();

export default function LensCameraPanel({project, operations,openScene,worldSpace}:{worldSpace:boolean;project:string; operations:ReactNode;openScene:()=>void}) {
  const [presets,setPresets]=useState<FollowPreset[]>([]);
  const [presetError,setPresetError]=useState('');
  useEffect(()=>{
    if(!project)return;
    let active=true;
    void invoke('studio.op',{op:'lens_camera_presets',expectedProject:project,params:{}}).then(({result})=>{
      if(active)setPresets((result as {presets:FollowPreset[]}).presets.filter(p=>p.body==='follow'));
    }).catch(e=>{if(active)setPresetError(e instanceof Error?e.message:String(e));});
    return ()=>{active=false;};
  },[project]);
  const [showOptics,setShowOptics]=useState(false);
  const [tab,setTab]=useState('Cameras');
  const [document,setDocument]=useState<Document|undefined>(()=>drafts.get(project)?.document);
  const [scene,setScene]=useState<CameraScene|undefined>(()=>drafts.get(project)?.scene);
  const [selected,setSelected]=useState<number|undefined>(()=>drafts.get(project)?.selected);
  const [busy,setBusy]=useState(false);
  const [notice,setNotice]=useState('');
  const [error,setError]=useState('');
  const dirty=scene!==document?.scene;
  useLayoutEffect(()=>{
    if(document&&scene&&dirty)drafts.set(project,{document,scene,selected});
  },[project,document,scene,selected,dirty]);
  const camera=scene?.cameras.find(item=>item.id===selected);
  const normalized=document?.normalizedScene.cameras.find(item=>item.id===selected);
  const view={...normalized?.camera,...camera?.camera};
  const lens={...normalized?.lens,...camera?.lens};
  const output={...normalized?.output,...camera?.output};
  async function request(op:string,params:Record<string,unknown>) {
    const {result}=await invoke('studio.op',{op,params,expectedProject:project});
    const response=result as Document;
    if(!response?.ok||!response.scene||!Array.isArray(response.scene.cameras))throw new Error(response?.error??'Could not read the camera scene');
    return response;
  }
  function accept(response:Document) {
    if(drafts.get(project)?.scene===scene)drafts.delete(project);
    setDocument(response);setScene(response.scene);
    setSelected(id=>response.scene.cameras.some(item=>item.id===id)?id:response.scene.cameras[0]?.id);
  }
  async function refresh(save:boolean) {
    setError('');setNotice('');setBusy(true);
    try {
      const response=save?await request('lens_scene_set',{scene,expectedRevision:document?.revision}):await request('lens_scene_get',{});
      accept(response);setNotice(save?'Saved cameras.json':'');
    } catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  }
  useEffect(()=>{if(project&&!drafts.has(project))void refresh(false);},[project]);
  function update(patch:Partial<CameraRecord>) {
    if(!scene||!camera)return;
    setScene({...scene,cameras:scene.cameras.map(item=>item.id===selected?{...item,...patch}:item)});
    setNotice('');setError('');
  }
  async function create(fromScene:boolean) {
    if(!scene)return;
    const ids=new Set([...scene.cameras,...(document?.scene.cameras??[])].map(item=>item.id));
    let id=1;while(ids.has(id))id++;
    const keys=new Set(scene.cameras.map(item=>item.output?.key));
    let key=1;while(keys.has(key))key++;
    const item:CameraRecord={id,name:'Camera '+id,position:[0,12,18],camera:{projection:'perspective',fov:.75},output:{kind:'texture',key,width:512,height:512}};
    if(fromScene){
      setBusy(true);
      try{
        const pose=await invoke('studio.lensPreview',{project,copyScene:true});
        if(!pose.camera||!pose.position||!pose.rotation)throw new Error('Open Scene to create a camera from its view');
        Object.assign(item,{position:pose.position,rotation:pose.rotation,camera:pose.camera});
      }catch(e){setError(e instanceof Error?e.message:String(e));return;}finally{setBusy(false);}
    }
    setScene({...scene,cameras:[...scene.cameras,item]});setSelected(id);setNotice('');setError('');
  }
  function remove() {
    if(!scene||!camera)return;
    const cameras=scene.cameras.filter(item=>item.id!==selected);
    setScene({...scene,cameras});setSelected(cameras[0]?.id);setNotice('');setError('');
  }
  const viewField=(patch:CameraRecord['camera'])=>update({camera:{...camera?.camera,...patch}});
  const outputField=(patch:CameraRecord['output'])=>update({output:{...camera?.output,...patch}});
  if(!project)return <div className="tool__empty">Open a project to edit cameras.</div>;
  return <div className="lens-page">
    <nav aria-label="Actors and cameras">{(worldSpace?['Cameras','Simulation','Operations']:['Cameras','Operations']).map(name=><button key={name} className="au-button au-button--sm" aria-pressed={tab===name} onClick={()=>setTab(name)}>{name}</button>)}</nav>
    {tab==='Simulation' && worldSpace?<ActorSimulation key={project} project={project}/>:tab==='Operations'?operations:<>
      <div className="lens-actions"><strong>Project cameras</strong>
        <button className="au-button au-button--sm" disabled={busy||!scene} onClick={()=>void create(false)}><Plus size={14}/>New camera</button>
        <button className="au-button au-button--sm" disabled={busy||!scene} onClick={()=>void create(true)}>New from Scene</button>
        <button className="au-button au-button--sm" disabled={busy||dirty} onClick={()=>void refresh(false)}>Reload</button>
        {dirty&&<button className="au-button au-button--sm" disabled={busy} onClick={()=>void refresh(false)}>Discard edits</button>}
        <button className="au-button au-button--sm au-button--primary" type="submit" form="lens-camera-form" disabled={busy||!dirty}><Save size={14}/>Save cameras</button>
      </div>
      {(error||notice)&&<p className={'lens-message '+(error?'is-error':'')} role={error?'alert':'status'}>{error||notice}</p>}
      <div className="lens-layout">
        {scene&&scene.cameras.length>0&&<label className="lens-camera-picker">Selected camera<select className="au-input" value={selected??''} onChange={e=>setSelected(Number(e.target.value))}>{scene.cameras.map(item=><option key={item.id} value={item.id}>{item.name||'Camera '+item.id}</option>)}</select></label>}
        <aside aria-label="Project cameras">{scene?.cameras.map(item=><button type="button" key={item.id} aria-label={item.name||'Camera '+item.id} aria-pressed={selected===item.id} onClick={()=>setSelected(item.id)}><Camera size={15}/><span>{item.name||'Camera '+item.id}</span><small>#{item.id}</small></button>)}</aside>
        <form id="lens-camera-form" onSubmit={event=>{event.preventDefault();if(dirty&&!busy)void refresh(true);}}>
          {!camera?<p>{busy?'Loading cameras…':scene?'Create a camera to configure its view and output.':'Camera scene unavailable.'}</p>:<fieldset disabled={busy}>
            <div className="lens-title"><label>Camera name<input className="au-input" value={camera.name??''} onChange={e=>update({name:e.target.value})}/></label>
              <button type="button" aria-label="Remove selected camera" title="Remove selected camera" className="au-button au-button--icon" onClick={remove}><Trash2 size={15}/></button></div>
            <CameraPreview openScene={openScene} key={camera.id} project={project} scene={scene!} camera={camera} onChange={update}/>
            <h4>View</h4>
            <div className="lens-fields"><label>Projection<select className="au-input" value={view.projection??'perspective'} onChange={e=>viewField({projection:e.target.value})}><option value="perspective">Perspective</option><option value="ortho">Orthographic</option></select></label>
            {view.projection==='ortho'?<NumberField label="Orthographic half-height" value={view.orthographicHalfHeight??10} onValue={v=>viewField({orthographicHalfHeight:v})}/>:<NumberField label="Vertical FOV (degrees)" value={+(180*(view.fov??.75)/Math.PI).toFixed(4)} onValue={v=>viewField({fov:v*Math.PI/180})}/>}
            <NumberField label="Near clip" value={view.near??.1} onValue={v=>viewField({near:v})}/><NumberField label="Far clip" value={view.far??1000} onValue={v=>viewField({far:v})}/></div>
            <label className="lens-toggle"><input type="checkbox" checked={view.enabled??true} onChange={e=>viewField({enabled:e.target.checked})}/>Camera enabled</label>
            <details className="lens-optics" onToggle={e=>setShowOptics(e.currentTarget.open)}><summary>Physical lens</summary>
              {lensSwitches.map(([key,label])=><label key={key} className="lens-toggle"><input type="checkbox" checked={lens[key]??false} onChange={e=>update({lens:{...camera.lens,[key]:e.target.checked}})}/>{label}</label>)}
              <div className="lens-fields">{optics.map(([key,label,fallback])=><NumberField key={key} label={label} value={lens[key]??fallback} onValue={v=>update({lens:{...camera.lens,[key]:v}})}/>)}</div>
              {showOptics&&<LensReadings project={project} lens={camera.lens}/>}
            </details>
            <h4>Position</h4><div className="lens-fields">{['X','Y','Z'].map((axis,i)=><NumberField key={axis} label={'Position '+axis} value={camera.position?.[i]??0} onValue={v=>{const position=[...(camera.position??[0,0,0])];position[i]=v;update({position});}}/>)}</div>
            <h4>Output</h4><div className="lens-fields"><label>Target<select className="au-input" value={output.kind??'screen'} onChange={e=>outputField({kind:e.target.value})}><option value="screen">Screen</option><option value="texture">Render texture</option></select></label>
            {output.kind==='texture'&&<><NumberField label="Texture key" value={output.key??0} onValue={v=>outputField({key:v})}/><NumberField label="Width" value={output.width??0} onValue={v=>outputField({width:v})}/><NumberField label="Height" value={output.height??0} onValue={v=>outputField({height:v})}/></>}</div>
            {output.kind==='texture'&&<div className="lens-flags">{([['hdr','HDR',false],['depth','Depth buffer',true]] as const).map(([key,label,fallback])=><label key={key} className="lens-toggle"><input type="checkbox" checked={output[key]??fallback} onChange={e=>outputField({[key]:e.target.checked})}/>{label}</label>)}</div>}
            <CameraOptions presets={presets} presetError={presetError} camera={camera} normalized={normalized} onChange={update}/>
          </fieldset>}
        </form>
      </div>
    </>}
  </div>;
}

function NumberField({label,value,onValue}:{label:string;value:number;onValue:(value:number)=>void}) {
  const [text,setText]=useState(String(value));
  const [focused,setFocused]=useState(false);
  useEffect(()=>{if(!focused)setText(String(value));},[value,focused]);
  return <label>{label}<input className="au-input" type="number" step="any" required value={text} onFocus={()=>setFocused(true)} onBlur={()=>setFocused(false)} onChange={e=>{setText(e.target.value);if(Number.isFinite(e.target.valueAsNumber))onValue(e.target.valueAsNumber);}}/></label>;
}

function CameraOptions({camera,normalized,onChange,presets,presetError}:{presets:FollowPreset[];presetError:string;camera:CameraRecord;normalized?:CameraRecord;onChange:(patch:Partial<CameraRecord>)=>void}) {
  const rig={...normalized?.rig,...camera.rig};
  const refresh={...normalized?.update,...camera.update};
  const viewport={...normalized?.viewport,...camera.viewport};
  const stack={...normalized?.stack,...camera.stack};
  const editRig=(patch:CameraRecord['rig'])=>onChange({rig:{...camera.rig,...patch}});
  return <>
    <details className="lens-optics"><summary>Camera rig</summary>
      <label className="lens-toggle"><input type="checkbox" checked={rig.enabled??true} onChange={e=>editRig({enabled:e.target.checked})}/>Rig enabled</label>
      <label>Rig mode<select className="au-input" value={rig.mode??0} onChange={e=>editRig({mode:Number(e.target.value)})}>
        {['Static','Look at','Follow','Orbit','Rail','Chase','First person'].map((label,index)=><option value={index} key={label}>{label}</option>)}</select></label>
      <div className="lens-fields">{['X','Y','Z'].map((axis,i)=><NumberField key={axis} label={'Rig offset '+axis} value={(rig.offset??[0,2,-5])[i]} onValue={v=>{const offset=[...(rig.offset??[0,2,-5])];offset[i]=v;editRig({offset});}}/>)}</div>
      <div className="lens-fields">{([['distance','Distance',5],['yaw','Yaw (rad)',0],['pitch','Pitch (rad)',0],['smoothing','Smoothing',8]] as const).map(([key,label,base])=><NumberField key={key} label={label} value={rig[key]??base} onValue={v=>editRig({[key]:v})}/>)}</div>
      <label className="lens-toggle"><input type="checkbox" checked={rig.trackTarget??true} onChange={e=>editRig({trackTarget:e.target.checked})}/>Track target</label>
    </details>
    <details className="lens-optics"><summary>Follow body</summary>
      <label className="lens-toggle"><input type="checkbox" checked={!!camera.follow} onChange={e=>onChange(e.target.checked?{rig:{...camera.rig,mode:2},follow:{}}:{follow:undefined})}/>Use Follow body</label>
      {camera.follow&&<>
        <label>Follow preset<select className="au-input" value={camera.follow.preset??''} onChange={e=>{
          const preset=presets.find(item=>item.name===e.target.value);
          if(!preset)return;
          const previous=presets.find(item=>item.name===camera.follow?.preset);
          const overrides={...previous?.keys,...camera.follow};delete overrides.preset;
          for(const key of Object.keys(preset.keys))delete overrides[key as keyof FollowBody];
          onChange({follow:{preset:preset.name,...overrides}});
        }}><option disabled value="">Choose a preset</option>{presets.map(p=><option key={p.name}>{p.name}</option>)}</select></label>
        {presetError&&<p role="alert">{presetError}</p>}
        <div className="lens-fields">{followFields.map(([key,label,base])=><NumberField key={key} label={label} value={camera.follow?.[key]??presets.find(p=>p.name===camera.follow?.preset)?.keys[key]??base} onValue={v=>onChange({follow:{...camera.follow,[key]:v}})}/>)}</div>
      </>}
    </details>
    <details className="lens-optics"><summary>Refresh and composition</summary>
      <div className="lens-fields"><label>Refresh policy<select className="au-input" value={refresh.mode??0} onChange={e=>onChange({update:{...camera.update,mode:Number(e.target.value)}})}>
        {['Every frame','Every N frames','Fixed rate','On change','Manual'].map((label,index)=><option value={index} key={label}>{label}</option>)}</select></label>
        {refresh.mode===1&&<NumberField label="Frame interval" value={refresh.everyNFrames??1} onValue={v=>onChange({update:{...camera.update,everyNFrames:v}})}/>}
        {refresh.mode===2&&<NumberField label="Rate (Hz)" value={refresh.rateHz??30} onValue={v=>onChange({update:{...camera.update,rateHz:v}})}/>}</div>
      <label className="lens-toggle"><input type="checkbox" checked={!!stack.overlay} onChange={e=>onChange({stack:{...camera.stack,overlay:e.target.checked}})}/>Overlay camera</label>
      <div className="lens-fields"><NumberField label="Stack order" value={stack.order??0} onValue={v=>onChange({stack:{...camera.stack,order:v}})}/>
        <NumberField label="Opacity" value={stack.opacity??1} onValue={v=>onChange({stack:{...camera.stack,opacity:v}})}/></div>
      <label className="lens-toggle"><input type="checkbox" checked={!!viewport.enabled} onChange={e=>onChange({viewport:{...camera.viewport,enabled:e.target.checked}})}/>Custom viewport</label>
      {viewport.enabled&&<div className="lens-fields">{([['x','Viewport X',0],['y','Viewport Y',0],['width','Viewport width',1],['height','Viewport height',1]] as const).map(([key,label,base])=><NumberField key={key} label={label} value={viewport[key]??base} onValue={v=>onChange({viewport:{...camera.viewport,[key]:v}})}/>)}</div>}
    </details>
  </>;
}

function CameraPreview({project,scene,camera,onChange,openScene}:{openScene:()=>void;project:string;scene:CameraScene;camera:CameraRecord;onChange:(patch:Partial<CameraRecord>)=>void}) {
  const [visible,setVisible]=useState(false);
  const [publishedCamera,setPublishedCamera]=useState('');
  const draftSignature=useMemo(()=>JSON.stringify(camera),[camera]);
  const published=!!publishedCamera;
  const needsUpdate=published&&publishedCamera!==draftSignature;
  const [capturing,setCapturing]=useState(false);
  const [saved,setSaved]=useState('');
  const mounted=useRef(true);
  const preview=useRef<HTMLElement>(null);
  useLayoutEffect(()=>{if(published)preview.current?.scrollIntoView({block:'nearest'});},[published]);
  const [message,setMessage]=useState('');
  const [busy,setBusy]=useState(false);
  useEffect(()=>{
    mounted.current=true;
    const stop=onNativeEvent((event,payload)=>{
      if(event==='lens.previewError'){setVisible(false);setPublishedCamera('');setCapturing(false);setMessage(typeof payload==='string'?payload:(payload as {message?:string})?.message??'Camera preview failed');}
      if(event==='lens.capture'){
        const result=payload as {id:number;project:string;path?:string;error?:string};
        if(result?.id===camera.id&&result.project===project){setCapturing(false);setSaved(result.path??'');setMessage(result.error??'');}
      }
    });
    return ()=>{mounted.current=false;stop();void invoke('studio.lensPreview',{project,close:true}).catch(()=>{});};
  },[project]);
  async function publish() {
    setVisible(true);setBusy(true);setMessage('');
    try {await invoke('studio.lensPreview',{project,document:scene,id:camera.id});setPublishedCamera(draftSignature);}
    catch(e){if(!published)setVisible(false);setMessage(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  }
  async function capture(){
    setCapturing(true);setSaved('');setMessage('');
    try{await invoke('studio.lensPreview',{project,capture:true,id:camera.id});}
    catch(e){setCapturing(false);setMessage(e instanceof Error?e.message:String(e));}
  }
  async function useInScene(){
    setBusy(true);setMessage('');
    try{await invoke('studio.lensPreview',{project,useInScene:true,document:scene,id:camera.id});if(mounted.current)openScene();}
    catch(e){setMessage(e instanceof Error?e.message:String(e));}finally{setBusy(false);}
  }
  async function copyView(){
    setBusy(true);setMessage('');
    try{
      const pose=await invoke('studio.lensPreview',{project,copyScene:true});
      if(mounted.current&&pose.position&&pose.rotation)onChange({position:pose.position,rotation:pose.rotation});
    }catch(e){setMessage(e instanceof Error?e.message:String(e));}finally{setBusy(false);}
  }
  return <section ref={preview} className="lens-model-preview"><div className="lens-flags">
    <button className="au-button au-button--sm" type="button" disabled={busy||capturing} onClick={()=>void publish()}>{published?'Update preview':'Preview camera'}</button>
    <button className="au-button au-button--sm" type="button" disabled={busy} onClick={()=>void copyView()}>Copy Scene pose</button>
    <button className="au-button au-button--sm" type="button" disabled={busy||capturing} onClick={()=>void useInScene()}>Use in Scene</button>
    {published&&<button className="au-button au-button--sm" type="button" disabled={busy||capturing||needsUpdate} title={needsUpdate?'Update the preview to capture your changes':'Save the displayed camera frame to captures/lens'} onClick={()=>void capture()}>{capturing?'Capturing…':'Capture PNG'}</button>}</div>
    {needsUpdate&&<p className="lens-message" role="status">Camera settings changed. Update the preview to capture this view.</p>}
    {saved&&!needsUpdate&&<p className="lens-message" role="status">{saved}</p>}
    {message&&<p className="lens-message is-error" role="alert">{message}</p>}
    {visible&&<SpectraDockViewport id="lensCamera" label="Selected camera preview"/>}
  </section>;
}

interface OpticalReadings {
  ev100:number;
  exposureStops:number;
  nearMetres?:number;
  farMetres?:number;
  farInfinite?:boolean;
  hyperfocalMetres?:number;
}

function LensReadings({project,lens}:{project:string;lens?:Lens}) {
  const [values,setValues]=useState<OpticalReadings>();
  const [error,setError]=useState('');
  useEffect(()=>{
    let active=true;
    setValues(undefined);setError('');
    const timer=window.setTimeout(async()=>{
      try{
        const {result}=await invoke('studio.op',{op:'lens_scene_validate',expectedProject:project,params:{
          opticsId:1,scene:{version:1,cameras:[{id:1,camera:{},lens:lens??{}}]},
        }});
        const response=result as {optics?:OpticalReadings;error?:string};
        if(!response.optics)throw new Error(response.error??'Lens readings unavailable');
        if(active)setValues(response.optics);
      }catch(e){if(active)setError(e instanceof Error?e.message:String(e));}
    },120);
    return ()=>{active=false;window.clearTimeout(timer);};
  },[project,lens]);
  if(error)return <p className="lens-message is-error" role="status">{error}</p>;
  if(!values)return null;
  return <dl className="lens-readings" aria-label="Physical lens readings">
    <div><dt>EV100</dt><dd>{values.ev100.toFixed(2)}</dd></div>
    <div><dt>Exposure</dt><dd>{values.exposureStops>0?'+':''}{values.exposureStops.toFixed(2)} stops</dd></div>
    <div><dt>Sharp range</dt><dd>{values.nearMetres===undefined?'Unavailable':values.nearMetres.toFixed(2)+' m – '+(values.farInfinite?'∞':values.farMetres?.toFixed(2)+' m')}</dd></div>
    {values.hyperfocalMetres!==undefined&&<div><dt>Hyperfocal distance</dt><dd>{values.hyperfocalMetres.toFixed(2)} m</dd></div>}
  </dl>;
}
