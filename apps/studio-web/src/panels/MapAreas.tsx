import { useRef, useState } from 'react';
import { Scan } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioMapProperties, StudioSnapshot } from '../types';
import { StudioButton, StudioInput } from '../ui';
import type { CameraBookmark } from '../components/sceneBookmarks';

type Bounds=[number,number,number,number];
type Fields={name:string; bounds:string[]};
type Draft={base:StudioMapProperties;original:string|null;fields:Fields};
const labels=['Corner X','Corner Z','Width','Height'];
function boundsOf(raw:unknown):Bounds{
  const values=Array.isArray(raw)?raw:[];
  return [0,1,2,3].map(i=>typeof values[i]==='number' && Number.isFinite(values[i])?values[i]:i<2?0:1) as Bounds;
}
function readFields(data:StudioMapProperties,name:string):Fields{
  return {name,bounds:boundsOf(data.areas[name]).map(String)};
}
function valid(fields:Fields){
  return fields.name.trim() && fields.bounds.every(x=>x.trim() && Number.isFinite(Number(x))) &&
    Number(fields.bounds[2])>0 && Number(fields.bounds[3])>0;
}

export function MapAreas({data,revision,busy,onApply}:{
  data:StudioMapProperties;revision:string;busy:boolean;onApply:(snapshot:StudioSnapshot)=>void;
}){
  const [selection,setSelection]=useState('');
  const [draft,setDraft]=useState<Draft>();
  const [deleting,setDeleting]=useState(false);
  const [pending,setPending]=useState(false);
  const [error,setError]=useState('');
  const [drawing,setDrawing]=useState(false);
  const [preview,setPreview]=useState<Bounds>();
  const origin=useRef<[number,number]|undefined>(undefined);
  const base=draft?.base??data;
  const names=Object.keys(base.areas);
  const current=draft?draft.original:names.includes(selection)?selection:names[0];
  const fields=draft?.fields??readFields(base,current??'');
  const stale=base.scope.revision!==revision;
  const locked=pending||busy||stale;
  const dirty=!!draft && (current===null || JSON.stringify(fields)!==JSON.stringify(readFields(base,current??'')));
  const duplicate=fields.name!==current && Object.hasOwn(base.areas,fields.name);
  const discard=()=>{setDraft(undefined);setError('');setDeleting(false);setDrawing(false);setPreview(undefined);origin.current=undefined;};
  const edit=(values:Partial<Fields>)=>{
    setError('');
    setDraft(prev=>({base:prev?.base??base,original:prev?prev.original:current??null,
      fields:{...(prev?.fields??fields),...values}}));
  };
  function create(){
    let i=1;while(Object.hasOwn(data.areas,'Area '+i))i++;
    discard();setDraft({base:data,original:null,fields:{name:'Area '+i,bounds:['0','0','1','1']}});
  }
  async function save(remove=false){
    setPending(true);setError('');
    try{
      const action=remove?'remove':current===null?'create':'edit';
      const next=await invoke('studio.mapArea',{scope:base.scope,action,name:current??fields.name,
        ...(remove?{}:{newName:fields.name,rect:fields.bounds.map(Number)})});
      onApply(next);setSelection(remove?'':fields.name);discard();
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setPending(false);}
  }
  async function focus(){
    setError('');
    try{
      const [x,z,w,h]=fields.bounds.map(Number);
      const camera=await invoke('studio.viewportInput',{kind:'camera'}) as unknown as CameraBookmark;
      await invoke('studio.viewportInput',{kind:'camera',set:{...camera,x:x+w/2,y:0,z:z+h/2,distance:Math.max(6,Math.max(w,h)*2)}});
    }catch(e){setError(e instanceof Error?e.message:String(e));}
  }
  function cell(event:React.PointerEvent<SVGSVGElement>):[number,number]{
    const svg=event.currentTarget;
    const matrix=svg.getScreenCTM()!;
    const p=new DOMPoint(event.clientX,event.clientY).matrixTransform(matrix.inverse());
    return [Math.max(0,Math.min(base.width-1,Math.floor(p.x))),Math.max(0,Math.min(base.height-1,Math.floor(p.y)))];
  }
  function draggedBounds(event:React.PointerEvent<SVGSVGElement>):Bounds|undefined{
    const start=origin.current;if(!start)return;
    const [x,z]=cell(event);
    return [Math.min(start[0],x),Math.min(start[1],z),Math.abs(x-start[0])+1,Math.abs(z-start[1])+1];
  }
  const shown=current!=null||!!draft;
  const bounds=preview??fields.bounds.map(Number);
  return <section className="insp-map insp-areas" aria-label="Map areas">
    <header><Scan size={19}/><div><h2>Map areas</h2><small>Rectangles used by triggers and wave routes</small></div></header>
    <div className="insp-areas__picker">
      <label>Area<select value={current??''} disabled={locked||!!draft||!names.length} onChange={e=>{setSelection(e.target.value);discard();}}>
        {current==null && <option value="">{draft?'New area':'No areas'}</option>}
        {names.map(n=><option key={n}>{n}</option>)}</select></label>
      <StudioButton disabled={locked||!!draft} onClick={create}>New area</StudioButton>
    </div>
    {base.grid && <div className="insp-areas__map">
      <svg viewBox={`0 0 ${base.width} ${base.height}`} role="img" aria-label="Area bounds on the map"
        className={drawing?'is-drawing':''} onPointerDown={e=>{
          if(!drawing||locked||e.button!==0)return;
          origin.current=cell(e);setPreview(draggedBounds(e));e.currentTarget.setPointerCapture(e.pointerId);
        }} onPointerMove={e=>{if(origin.current)setPreview(draggedBounds(e));}} onPointerUp={e=>{
          const rect=draggedBounds(e);
          if(!rect)return;
          if(!locked)edit({bounds:rect.map(String)});
          origin.current=undefined;setPreview(undefined);setDrawing(false);
        }} onPointerCancel={()=>{origin.current=undefined;setPreview(undefined);}}>
        <rect width={base.width} height={base.height} className="map-board"/>
        {names.filter(n=>n!==current).map(name=>{const [x,y,w,h]=boundsOf(base.areas[name]);return <rect key={name} x={x} y={y}
          width={Math.max(0,w)} height={Math.max(0,h)}><title>{name}</title></rect>;})}
        {shown && (preview || valid(fields)) && <rect className="is-selected" x={bounds[0]} y={bounds[1]} width={bounds[2]} height={bounds[3]}/>}
      </svg>
      <small>{drawing?'Drag from one corner of the area to the other.':`${base.width} × ${base.height} cells`}</small>
    </div>}
    {shown && <form onSubmit={e=>{e.preventDefault();void save();}}><fieldset disabled={locked||deleting}>
      <label>Area name<StudioInput required value={fields.name} onChange={e=>edit({name:e.target.value})}/></label>
      {duplicate && <small role="alert">An area already has this name.</small>}
      <div className="insp-map__size">{labels.map((label,i)=><label key={label}>{label}<StudioInput required type="number" step="any"
        value={fields.bounds[i]} onChange={e=>edit({bounds:fields.bounds.map((v,k)=>k===i?e.target.value:v)})}/></label>)}</div>
      {base.grid && <div className="insp-map__actions"><StudioButton aria-pressed={drawing} onClick={()=>setDrawing(!drawing)}>{drawing?'Cancel drawing':'Draw bounds'}</StudioButton>
        <StudioButton disabled={!valid(fields)} onClick={()=>void focus()}>Show in Scene</StudioButton></div>}
      {current && fields.name!==current && <small>Map triggers and wave routes will use the new name. Check script references separately.</small>}
      <div className="insp-map__actions"><StudioButton type="submit" variant="primary" disabled={!dirty||duplicate||!valid(fields)}>{current===null?'Create area':'Apply'}</StudioButton>
        {!dirty && current && <StudioButton onClick={()=>{setDraft({base,original:current,fields});setDeleting(true);}}>Remove area</StudioButton>}</div>
    </fieldset></form>}
    {deleting && <div className="insp-map__actions"><span>Remove {current}?</span><StudioButton disabled={locked} onClick={()=>void save(true)}>Confirm removal</StudioButton></div>}
    {stale && draft && <p role="alert">The map changed. Reload its areas before applying.</p>}
    {error && <p role="alert">{error}</p>}
    {draft && <StudioButton disabled={pending} onClick={discard}>{stale?'Reload':deleting?'Cancel':'Discard'}</StudioButton>}
  </section>;
}
