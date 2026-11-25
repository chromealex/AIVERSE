import {useState} from 'react';

type Value = string | number | boolean | null | Value[] | {[key:string]:Value};
export type RigUnit = {op:string; [key:string]:Value};
type RigNode = {id:string; units?:RigUnit[]};
type Props = {
 nodes:RigNode[]; selected:string; busy:boolean;
 select:(id:string)=>void; addRig:()=>void;
 nativeEdit:(action:string,params:Record<string,unknown>)=>void;
 update:(id:string,units:RigUnit[])=>void;
};

const kinds=['transform','copy','aim','twoBoneIK','limit','fullBodyIK','surfaceLocomotion'];

function Field({name,value,change}:{name:string;value:Value;change:(v:Value)=>void}){
 if(typeof value==='boolean')return <label><input type="checkbox" checked={value} onChange={e=>change(e.target.checked)}/>{name}</label>;
 if(typeof value==='string')return <label>{name}<input value={value} onChange={e=>change(e.target.value)}/></label>;
 if(typeof value==='number')return <label>{name}<input type="number" step="any" value={value} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber))change(e.target.valueAsNumber);}}/></label>;
 if(Array.isArray(value))return <details><summary>{name} ({value.length})</summary><div className="rig-fields">{value.map((v,i)=><Field key={i} name={value.length===3&&value.every(k=>typeof k==='number')?['X','Y','Z'][i]:String(i+1)} value={v} change={next=>change(value.map((old,j)=>j===i?next:old))}/>)}</div></details>;
 if(value===null)return <span>{name} · empty</span>;
 return <details><summary>{name}</summary><div className="rig-fields">{Object.entries(value).map(([key,v])=><Field key={key} name={key} value={v} change={next=>change({...value,[key]:next})}/>)}</div></details>;
}

export default function RigInspector({nodes,selected,select,busy,addRig,nativeEdit,update}:Props){
 const [kind,setKind]=useState('transform');
 const node=nodes.find(n=>n.id===selected)??nodes[0];
 const units=node?.units??[];
 function move(from:number,to:number){
  const next=[...units];
  next.splice(to,0,...next.splice(from,1));
  update(node.id,next);
 }
 return <fieldset disabled={busy} className="rig-editor">
  <div className="rig-bar">{node&&<label>Rig node<select value={node.id} onChange={e=>select(e.target.value)}>{nodes.map(n=><option key={n.id}>{n.id}</option>)}</select></label>}
  <button onClick={addRig}>Add rig node</button></div>
  {node&&<><div className="rig-bar"><label>Operation<select value={kind} onChange={e=>setKind(e.target.value)}>{kinds.map(k=><option key={k}>{k}</option>)}</select></label>
  <button disabled={units.length>=64} onClick={()=>nativeEdit('addUnit',{kind,id:node.id})}>Add operation</button></div>
  {!units.length&&<p>Add the first operation to this rig.</p>}
  {units.map((unit,i)=><section key={node.id+i+unit.op} className="rig-unit">
   <header><strong>{i+1}. {unit.op}</strong><button disabled={i===0} aria-label={'Move operation '+(i+1)+' up'} onClick={()=>move(i,i-1)}>↑</button>
    <button disabled={i===units.length-1} aria-label={'Move operation '+(i+1)+' down'} onClick={()=>move(i,i+1)}>↓</button>
    <button aria-label={'Remove operation '+(i+1)} onClick={()=>update(node.id,units.filter((_,j)=>j!==i))}>Remove</button></header>
   <div className="rig-fields">{Object.entries(unit).filter(([k])=>k!=='op').map(([key,v])=><Field key={key} name={key} value={v} change={next=>update(node.id,units.map((old,j)=>j===i?{...old,[key]:next}:old))}/>)}</div>
   {unit.op==='fullBodyIK'&&!unit.contacts&&<button onClick={()=>nativeEdit('addContacts',{index:i,id:node.id})}>Add contacts</button>}
  </section>)}</>}
 </fieldset>;
}
