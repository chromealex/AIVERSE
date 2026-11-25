import { useEffect, useState, type ReactNode } from 'react';
import { Camera, Plus, Save, Trash2 } from 'lucide-react';
import { invoke } from '../bridge';
import './LensCameraPanel.css';

const optics = [
  ['focalLengthMm','Focal length (mm)',50],['sensorHeightMm','Sensor height (mm)',24],
  ['fNumber','Aperture (f-number)',2.8],['focusDistance','Focus distance',10],
  ['shutterSeconds','Shutter (seconds)',1/60],['iso','ISO',100],
  ['exposureCompensation','Exposure compensation (EV)',0],['circleOfConfusionMm','Circle of confusion (mm)',0],
] as const;
const lensSwitches = [['physicalFov','Use physical field of view'],['driveExposure','Drive exposure'],['driveDepthOfField','Drive depth of field']] as const;
type Lens = Partial<Record<typeof optics[number][0],number> & Record<typeof lensSwitches[number][0],boolean>>;

interface CameraRecord {
  id: number;
  name?: string;
  position?: number[];
  lens?: Lens;
  rig?: {mode?:number;enabled?:boolean;trackTarget?:boolean;offset?:number[];distance?:number;yaw?:number;pitch?:number;smoothing?:number};
  update?: {mode?:number;everyNFrames?:number;rateHz?:number};
  stack?: {overlay?:boolean;order?:number;opacity?:number};
  viewport?: {enabled?:boolean;x?:number;y?:number;width?:number;height?:number};
  camera?: { projection?: string; fov?: number; orthographicHalfHeight?: number; near?: number; far?: number; enabled?: boolean };
  output?: { kind?: string; key?: number; width?: number; height?: number; hdr?: boolean; depth?: boolean };
}
interface CameraScene { cameras: CameraRecord[] }
interface Document { scene: CameraScene; revision: string; ok: boolean; error?: string; normalizedScene: CameraScene }

export default function LensCameraPanel({project, operations}:{project:string; operations:ReactNode}) {
  const [tab,setTab]=useState('Cameras');
  const [document,setDocument]=useState<Document>();
  const [scene,setScene]=useState<CameraScene>();
  const [selected,setSelected]=useState<number>();
  const [busy,setBusy]=useState(false);
  const [notice,setNotice]=useState('');
  const [error,setError]=useState('');
  const dirty=scene!==document?.scene;
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
  useEffect(()=>{if(project)void refresh(false);},[project]);
  function update(patch:Partial<CameraRecord>) {
    if(!scene||!camera)return;
    setScene({...scene,cameras:scene.cameras.map(item=>item.id===selected?{...item,...patch}:item)});
    setNotice('');setError('');
  }
  function create() {
    if(!scene)return;
    const ids=new Set([...scene.cameras,...(document?.scene.cameras??[])].map(item=>item.id));
    let id=1;while(ids.has(id))id++;
    const keys=new Set(scene.cameras.map(item=>item.output?.key));
    let key=1;while(keys.has(key))key++;
    const item:CameraRecord={id,name:'Camera '+id,position:[0,12,18],camera:{projection:'perspective',fov:.75},output:{kind:'texture',key,width:512,height:512}};
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
    <nav aria-label="Actors and cameras">{['Cameras','Operations'].map(name=><button key={name} className="au-button au-button--sm" aria-pressed={tab===name} onClick={()=>setTab(name)}>{name}</button>)}</nav>
    {tab==='Operations'?operations:<>
      <div className="lens-actions"><strong>Project cameras</strong>
        <button className="au-button au-button--sm" disabled={busy||!scene} onClick={create}><Plus size={14}/>New camera</button>
        <button className="au-button au-button--sm" disabled={busy||dirty} onClick={()=>void refresh(false)}>Reload</button>
        {dirty&&<button className="au-button au-button--sm" disabled={busy} onClick={()=>{if(document)accept(document);setError('');setNotice('');}}>Discard edits</button>}
        <button className="au-button au-button--sm au-button--primary" type="submit" form="lens-camera-form" disabled={busy||!dirty}><Save size={14}/>Save cameras</button>
      </div>
      {(error||notice)&&<p className={'lens-message '+(error?'is-error':'')} role={error?'alert':'status'}>{error||notice}</p>}
      <div className="lens-layout">
        <aside aria-label="Project cameras">{scene?.cameras.map(item=><button type="button" key={item.id} aria-label={item.name||'Camera '+item.id} aria-pressed={selected===item.id} onClick={()=>setSelected(item.id)}><Camera size={15}/><span>{item.name||'Camera '+item.id}</span><small>#{item.id}</small></button>)}</aside>
        <form id="lens-camera-form" onSubmit={event=>{event.preventDefault();if(dirty&&!busy)void refresh(true);}}>
          {!camera?<p>{busy?'Loading cameras…':scene?'Create a camera to configure its view and output.':'Camera scene unavailable.'}</p>:<fieldset disabled={busy}>
            <div className="lens-title"><label>Camera name<input className="au-input" value={camera.name??''} onChange={e=>update({name:e.target.value})}/></label>
              <button type="button" aria-label="Remove selected camera" title="Remove selected camera" className="au-button au-button--icon" onClick={remove}><Trash2 size={15}/></button></div>
            <h4>View</h4>
            <div className="lens-fields"><label>Projection<select className="au-input" value={view.projection??'perspective'} onChange={e=>viewField({projection:e.target.value})}><option value="perspective">Perspective</option><option value="ortho">Orthographic</option></select></label>
            {view.projection==='ortho'?<NumberField label="Orthographic half-height" value={view.orthographicHalfHeight??10} onValue={v=>viewField({orthographicHalfHeight:v})}/>:<NumberField label="Vertical FOV (degrees)" value={+(180*(view.fov??.75)/Math.PI).toFixed(4)} onValue={v=>viewField({fov:v*Math.PI/180})}/>}
            <NumberField label="Near clip" value={view.near??.1} onValue={v=>viewField({near:v})}/><NumberField label="Far clip" value={view.far??1000} onValue={v=>viewField({far:v})}/></div>
            <label className="lens-toggle"><input type="checkbox" checked={view.enabled??true} onChange={e=>viewField({enabled:e.target.checked})}/>Camera enabled</label>
            <details className="lens-optics"><summary>Physical lens</summary>
              {lensSwitches.map(([key,label])=><label key={key} className="lens-toggle"><input type="checkbox" checked={lens[key]??false} onChange={e=>update({lens:{...camera.lens,[key]:e.target.checked}})}/>{label}</label>)}
              <div className="lens-fields">{optics.map(([key,label,fallback])=><NumberField key={key} label={label} value={lens[key]??fallback} onValue={v=>update({lens:{...camera.lens,[key]:v}})}/>)}</div>
            </details>
            <h4>Position</h4><div className="lens-fields">{['X','Y','Z'].map((axis,i)=><NumberField key={axis} label={'Position '+axis} value={camera.position?.[i]??0} onValue={v=>{const position=[...(camera.position??[0,0,0])];position[i]=v;update({position});}}/>)}</div>
            <h4>Output</h4><div className="lens-fields"><label>Target<select className="au-input" value={output.kind??'screen'} onChange={e=>outputField({kind:e.target.value})}><option value="screen">Screen</option><option value="texture">Render texture</option></select></label>
            {output.kind==='texture'&&<><NumberField label="Texture key" value={output.key??0} onValue={v=>outputField({key:v})}/><NumberField label="Width" value={output.width??0} onValue={v=>outputField({width:v})}/><NumberField label="Height" value={output.height??0} onValue={v=>outputField({height:v})}/></>}</div>
            {output.kind==='texture'&&<div className="lens-flags">{([['hdr','HDR',false],['depth','Depth buffer',true]] as const).map(([key,label,fallback])=><label key={key} className="lens-toggle"><input type="checkbox" checked={output[key]??fallback} onChange={e=>outputField({[key]:e.target.checked})}/>{label}</label>)}</div>}
            <CameraOptions camera={camera} normalized={normalized} onChange={update}/>
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

function CameraOptions({camera,normalized,onChange}:{camera:CameraRecord;normalized?:CameraRecord;onChange:(patch:Partial<CameraRecord>)=>void}) {
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
