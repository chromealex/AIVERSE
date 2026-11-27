import {useEffect,useState,useRef} from 'react';
import {StudioAlert,StudioInput,StudioButton,StudioSection,StudioSelect,StudioTextarea} from '../ui';
import {invoke} from '../bridge';
import GraphCanvas from './GraphCanvas';
import type {PortRef} from './graphGeometry';
import {object,type GraphObject} from './nativeGraphAdapters';

const operations=['sphere','box','cylinder','noise','densityTexture','union','subtract','intersect'];
const operands=(op:string)=>['union','subtract','intersect'].includes(op)?['a','b']:op==='noise'?['input']:[];
export const initialVolume=()=>({kind:'volumeCsg',fields:[{id:'sphere',op:'sphere',radius:0.65}],root:'sphere'});

export default function VolumeCsgEditor({geometry,disabled,onEdit}:{geometry:GraphObject;disabled:boolean;onEdit:(g:GraphObject)=>void}) {
 const fields:GraphObject[]=Array.isArray(geometry.fields)?geometry.fields.filter(object):[];
 const [selected,select]=useState(String(geometry.root)),[pending,setPending]=useState<PortRef|null>(null);
 const [source,setSource]=useState(''),[operation,setOperation]=useState('sphere'),[error,setError]=useState('');
 const [texturePath,setTexturePath]=useState(''),[frame,setFrame]=useState(0),[importing,setImporting]=useState(false);
 const request=useRef(0);
 const field=fields.find(n=>n.id===selected);
 useEffect(()=>{setSource(field?JSON.stringify(field,null,2):'');setError('');setImporting(false);return ()=>{request.current++};},[field]);
 async function importFrame() {
  const token=++request.current;setImporting(true);
  try {
   const reply=await invoke('studio.op',{op:'modeling_volume_texture_import',params:{path:texturePath,frame}});
   if(token!==request.current)return;
   const texture=reply.result;
   if(!object(texture)||texture.op!=='densityTexture')throw new Error(object(texture)?String(texture.error??'Invalid texture frame'):'Invalid texture frame');
   setSource(JSON.stringify({...field,...texture},null,2));setError('');
  }catch(e){if(token===request.current)setError(String(e));}
  finally{if(token===request.current)setImporting(false);}
 }
 const nodes=fields.map((n,i)=>({
  id:String(n.id),label:String(n.id)+' · '+String(n.op),
  x:Array.isArray(n.position)?Number(n.position[0]):30+(i%4)*200,
  y:Array.isArray(n.position)?Number(n.position[1]):30+Math.floor(i/4)*105,
  inputs:operands(String(n.op)).map(id=>({id,type:'field'})),outputs:[{id:'field',type:'field'}]
 }));
 const links=fields.flatMap(n=>operands(String(n.op)).filter(key=>typeof n[key]==='string').map(key=>({
  id:String(n.id)+'/'+key,from:String(n[key]),output:'field',to:String(n.id),input:key
 })));
 function replace(id:string,next:GraphObject) {onEdit({...geometry,fields:fields.map(n=>n.id===id?next:n)});}
 function connect(source:PortRef,to:PortRef) {
  if(disabled)return;
  const from=fields.findIndex(n=>n.id===source.node),at=fields.findIndex(n=>n.id===to.node);
  if(from<0||from>=at){setError('Choose an earlier field as the input.');return;}
  replace(to.node,{...fields[at],[to.port]:source.node});setPending(null);setError('');
 }
 function append() {
  let i=1;
  const unique=(prefix:string)=>{while(fields.some(n=>n.id===prefix+i))i++;return prefix+i++;};
  const id=unique(operation);
  const root=String(geometry.root);
  const n:GraphObject={id,op:operation};
  const binary=operands(operation).includes('a');
  if(binary){n.a=root;n.b=selected||root;}
  if(operation==='noise')n.input=root;
  if(operation==='densityTexture'){n.dimension=3;n.texels=Array.from({length:27},(_,j)=>j===13?255:0);}
  const next=[...fields,n];
  if(!binary&&operation!=='noise')next.push({id:unique('union'),op:'union',a:root,b:id});
  onEdit({...geometry,fields:next,root:next[next.length-1].id});select(id);
 }
 return <StudioSection title="Volume CSG">
  <GraphCanvas view={{nodes,links}} label="Volume field tree" selected={selected} disabled={disabled} pendingOutput={pending}
   onOutput={setPending} onCancel={()=>setPending(null)} onSelect={select}
   onInput={to=>{if(pending)connect(pending,to)}} onConnect={connect} enhancedNavigation
   onMove={(id,x,y)=>{const row=fields.find(n=>n.id===id);if(row)replace(id,{...row,position:[x,y]});}}/>
  <div className="au-row"><StudioSelect aria-label="New field operation" value={operation} onChange={e=>setOperation(e.target.value)}>
   {operations.map(o=><option key={o}>{o}</option>)}</StudioSelect>
   <StudioButton disabled={disabled||fields.length+(operands(operation).length?1:2)>48} onClick={append}>Add field</StudioButton></div>
  {field?.op==='densityTexture'&&<div className="au-row">
   <StudioInput aria-label="Project SPVT path" placeholder="textures/density.spvt" value={texturePath} onChange={e=>setTexturePath(e.target.value)}/>
   <StudioInput aria-label="Texture frame" type="number" min={0} step={1} value={frame} onChange={e=>setFrame(Number(e.target.value))}/>
   <StudioButton disabled={disabled||importing||!texturePath} onClick={importFrame}>Load frame into draft</StudioButton></div>}
  {field&&<><StudioTextarea aria-label="Volume field parameters" value={source} rows={6} onChange={e=>setSource(e.target.value)}/>
   <StudioButton disabled={disabled} onClick={()=>{try{const next:unknown=JSON.parse(source);
    if(!object(next)||next.id!==selected)throw new Error('Keep the selected field ID.');
    replace(selected,next);setError('');}catch(e){setError(String(e));}}}>Apply field parameters</StudioButton></>}
  {error&&<StudioAlert tone="danger">{error}</StudioAlert>}
 </StudioSection>;
}
