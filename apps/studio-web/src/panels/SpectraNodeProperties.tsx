import {useState} from 'react';
import SpectraLiteralEditor from './SpectraLiteralEditor';
import {Minus} from 'lucide-react';
import {StudioAlert,StudioButton,StudioInput,StudioSelect,StudioTextarea} from '../ui';
import type {FxNode} from './spectraDataInterfaceModel';

export interface ModuleField {
  key:string;label:string;kind:'number'|'integer'|'boolean'|'string'|'choice'|'vector3'|'color';
  min:number;max:number;initial:unknown;choices:string;
}
type ObjectValue=Record<string,unknown>;
const object=(value:unknown):value is ObjectValue=>value!==null&&typeof value==='object'&&!Array.isArray(value);

export function GraphObjectEditor({label,value,disabled,apply}:{
  label:string;value:unknown;disabled:boolean;apply:(value:ObjectValue)=>void|Promise<void>;
}){
  const original=JSON.stringify(value??{},null,2);
  const [draft,setDraft]=useState(original);
  const [error,setError]=useState('');
  async function submit(){
    try{
      const parsed:unknown=JSON.parse(draft);
      if(!object(parsed))throw Error('Enter a JSON object.');
      await apply(parsed);setError('');
    }catch(e){setError(e instanceof Error?e.message:String(e));}
  }
  return <details className="spectra-property-source"><summary>{label}</summary>
    <StudioTextarea aria-label={label} rows={6} value={draft} spellCheck={false} disabled={disabled}
      onChange={e=>{setDraft(e.target.value);setError('');}}/>
    <div className="au-row">
      <StudioButton disabled={disabled||draft===original} onClick={submit}>Apply {label.toLowerCase()}</StudioButton>
      {draft!==original&&<StudioButton disabled={disabled} onClick={()=>{setDraft(original);setError('');}}>Revert text</StudioButton>}
    </div>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  </details>;
}

function FieldRow({field,value,disabled,set,remove}:{field:ModuleField;value:unknown;disabled:boolean;
  set:(value:unknown)=>void;remove:()=>void}){
  const [text,edit]=useState(()=>typeof value==='string'?value:JSON.stringify(value)??'');
  const [vector,setVector]=useState(()=>Array.isArray(value)?value.map(String):[]);
  const [error,setError]=useState('');
  const label=field.label;
  const number=field.kind==='integer'||field.kind==='number';
  const isVector=field.kind==='vector3'&&Array.isArray(value)&&value.length===3&&value.every(v=>typeof v==='number');
  const complex=number?typeof value!=='number':field.kind==='vector3'?!isVector:
    field.kind==='color'?typeof value!=='string'&&!(Array.isArray(value)&&value.length===4&&value.every(v=>typeof v==='number')):
    field.kind==='boolean'?typeof value!=='boolean':typeof value!=='string';
  function apply(){
    try{
      if(isVector){
        if(vector.some(v=>!v.trim()||!Number.isFinite(Number(v))))throw Error('Enter three finite numbers.');
        set(vector.map(Number));
      }else if(number){
        if(!text.trim()||!Number.isFinite(Number(text)))throw Error('Enter a finite number.');
        set(Number(text));
      }else set(field.kind==='color'&&text.trimStart().startsWith('[')?JSON.parse(text):text);
      setError('');
    }catch(e){setError(e instanceof Error?e.message:String(e));}
  }
  const changed=isVector?Array.isArray(value)&&vector.some((v,i)=>v!==String(value[i])):text!==(typeof value==='string'?value:JSON.stringify(value));
  return <div className="spectra-property-row"><span>{label}</span>
    <div className="spectra-property-control">
      {complex?<small>Editable in Node properties JSON</small>:field.kind==='boolean'?
        <input type="checkbox" aria-label={label} checked={value===true} disabled={disabled} onChange={e=>set(e.target.checked)}/>:
      field.kind==='choice'?<StudioSelect aria-label={label} disabled={disabled} value={String(value)} onChange={e=>set(e.target.value)}>
        {[...new Set([String(value),...field.choices.split('|')])].map(v=><option key={v}>{v}</option>)}</StudioSelect>:
      <form onSubmit={e=>{e.preventDefault();apply();}}>
        {isVector?vector.map((v,i)=><StudioInput key={i} type="number" step="any" aria-label={label+' '+'XYZ'[i]} disabled={disabled} value={v}
          onChange={e=>setVector(vector.map((old,j)=>j===i?e.target.value:old))}/>):
          <StudioInput type={number?'number':'text'} min={number?field.min:undefined} max={number?field.max:undefined}
            step={number?(field.kind==='integer'?1:'any'):undefined} aria-label={label} disabled={disabled} value={text} onChange={e=>edit(e.target.value)}/>}
        {changed&&<StudioButton type="submit" size="sm" disabled={disabled}>Apply</StudioButton>}
      </form>}
      <StudioButton variant="ghost" size="sm" aria-label={'Remove '+label} title={'Remove '+label} disabled={disabled} onClick={remove}><Minus size={14}/></StudioButton>
    </div>{error&&<StudioAlert tone="danger">{error}</StudioAlert>}
  </div>;
}

export default function SpectraNodeProperties({node,fields,functions,disabled,edit,replace}:{
  node:FxNode;fields:ModuleField[];functions:unknown;disabled:boolean;
  edit:(params:Record<string,unknown>)=>void;replace:(properties:ObjectValue)=>void;
}){
  const props=node.properties??{};
  const absent=fields.filter(field=>!Object.hasOwn(props,field.key));
  const fn=typeof props.function==='string'?props.function:'';
  return <div className="spectra-property-inspector">
    <SpectraLiteralEditor key={JSON.stringify([node.type,props.value,props.name])} node={node} disabled={disabled}
      apply={(property,value)=>replace({...props,[property]:value})}/>
    {node.type==='FunctionCall'&&object(functions)&&<label>Function
      <StudioSelect aria-label="Function" disabled={disabled} value={fn} onChange={e=>replace({...props,function:e.target.value})}>
        {[...new Set([fn,...Object.keys(functions)])].map(id=><option key={id} value={id}>{id||'Choose function…'}</option>)}
      </StudioSelect></label>}
    {fields.filter(field=>Object.hasOwn(props,field.key)).map(field=><FieldRow key={field.key+JSON.stringify(props[field.key])}
      field={field} value={props[field.key]} disabled={disabled}
      set={value=>edit({action:'setTypedField',node:node.id,property:field.key,value})}
      remove={()=>edit({action:'removeTypedField',node:node.id,property:field.key})}/>)}
    {absent.length>0&&<StudioSelect aria-label="Add module property" disabled={disabled} value="" onChange={e=>{
      const field=absent.find(row=>row.key===e.target.value);
      if(field)edit({action:'setTypedField',node:node.id,property:field.key,value:field.initial});
    }}><option value="">Add property…</option>{absent.map(field=><option key={field.key} value={field.key}>{field.label}</option>)}</StudioSelect>}
    <GraphObjectEditor key={JSON.stringify(props)} label="Node properties JSON" value={props} disabled={disabled} apply={replace}/>
  </div>;
}
