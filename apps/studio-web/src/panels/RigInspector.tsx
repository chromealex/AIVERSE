import {useState} from 'react';

type Value = string | number | boolean | null | Value[] | {[key:string]:Value};
export type RigUnit = {op:string; [key:string]:Value};
export type ItemPresets = Record<string,Value>;
type RigNode = {id:string; units?:RigUnit[]};
type Props = {
 presets:ItemPresets; nodes:RigNode[]; selected:string; busy:boolean;
 select:(id:string)=>void; addRig:()=>void;
 nativeEdit:(action:string,params:Record<string,unknown>)=>void;
 update:(id:string,units:RigUnit[])=>void;
};

const kinds=['transform','copy','aim','twoBoneIK','limit','fullBodyIK','surfaceLocomotion'];

const itemLabels:Record<string,string>={effectors:'effector',joints:'joint',feet:'foot',capsules:'capsule',masses:'mass',legs:'leg',obstacles:'obstacle'};

function displayName(key:string){
 const spaced=key.replace(/([a-z])([A-Z])/g,'$1 $2').toLowerCase();
 return spaced.charAt(0).toUpperCase()+spaced.slice(1);
}
function orderedFields(fields:{[key:string]:Value}){
 const priority=(key:string)=>key==='bone'?-2:key==='weight'?-1:key==='contacts'||key==='balance'?3:itemLabels[key]?2:0;
 return Object.entries(fields).sort(([left],[right])=>priority(left)-priority(right)||left.localeCompare(right));
}

function fieldsWithLists(fields:{[key:string]:Value},names:string[]){
 const result={...fields};
 for(const name of names)if(!Object.hasOwn(result,name))result[name]=[];
 return result;
}

function Field({name,value,change,presets}:{name:string;value:Value;change:(v:Value)=>void;presets:ItemPresets}){
 const label=itemLabels[name];
 if(label&&(Array.isArray(value)||value===null||(typeof value==='object'&&!Object.keys(value).length))){
  const items=Array.isArray(value)?value:[];
  return <div className="rig-collection"><header><strong>{displayName(name)} ({items.length})</strong>
   <button disabled={!Object.hasOwn(presets,name)} onClick={()=>change([...items,presets[name]])}>Add {label}</button></header>
   {items.map((item,i)=><div className="rig-item" key={i}>
    <Field name={label+' '+(i+1)} value={item} presets={presets} change={next=>change(items.map((old,j)=>i===j?next:old))}/>
    <button aria-label={'Remove '+label+' '+(i+1)} onClick={()=>change(items.filter((_,j)=>i!==j))}>Remove</button>
   </div>)}</div>;
 }
 if(typeof value==='boolean')return <label><input type="checkbox" checked={value} onChange={e=>change(e.target.checked)}/>{displayName(name)}</label>;
 if(typeof value==='string')return <label>{displayName(name)}<input value={value} onChange={e=>change(e.target.value)}/></label>;
 if(typeof value==='number')return <label>{displayName(name)}<input type="number" step="any" value={value} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber))change(e.target.valueAsNumber);}}/></label>;
 if(Array.isArray(value))return <details><summary>{displayName(name)} ({value.length})</summary><div className="rig-fields">{value.map((v,i)=><Field presets={presets} key={i} name={value.length===3&&value.every(k=>typeof k==='number')?['X','Y','Z'][i]:String(i+1)} value={v} change={next=>change(value.map((old,j)=>j===i?next:old))}/>)}</div></details>;
 if(value===null)return <span>{name} · empty</span>;
 return <details className={name==='contacts'||name==='balance'?'rig-subsection':undefined}><summary>{displayName(name)}</summary><div className="rig-fields">{orderedFields(fieldsWithLists(value,name==='contacts'?['feet','capsules','obstacles']:name==='balance'?['masses']:[])).map(([key,v])=><Field presets={presets} key={key} name={key} value={v} change={next=>change({...value,[key]:next})}/>)}</div></details>;
}

export default function RigInspector({presets,nodes,selected,select,busy,addRig,nativeEdit,update}:Props){
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
   <div className="rig-fields">{orderedFields(fieldsWithLists(unit,unit.op==='fullBodyIK'?['effectors','joints']:unit.op==='surfaceLocomotion'?['legs','capsules','obstacles']:[])).filter(([k])=>k!=='op').map(([key,v])=><Field presets={presets} key={key} name={key} value={v} change={next=>update(node.id,units.map((old,j)=>j===i?{...old,[key]:next}:old))}/>)}</div>
   {unit.op==='fullBodyIK'&&!unit.contacts&&<button onClick={()=>nativeEdit('addContacts',{index:i,id:node.id})}>Add contacts</button>}
  </section>)}</>}
 </fieldset>;
}
