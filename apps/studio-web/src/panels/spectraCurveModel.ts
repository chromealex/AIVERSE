/** SpectraFX Graph v2 Curve/Gradient key editing.
 * Authoring-only, immutable. Renderer and graph compiler are authoritative.
 * Curves use [[normalizedAge,value],...], gradients [[age,"#rrggbbaa"],...].
 */
import type { SpectraGraphV2, FxNode, GraphEdit } from './spectraDataInterfaceModel';

export interface CurveKey { t:number; value:number }
export interface GradientKey { t:number; color:string }
const epsilon=0.0001;
const bounded=(v:unknown,min:number,max:number):v is number=>
  typeof v==='number' && Number.isFinite(v) && v>=min && v<=max;
const colorRE=/^#[a-fA-F0-9]{6}([a-fA-F0-9]{2})?$/;
export const validRgbaHex=(value:unknown):value is string=>
  typeof value==='string' && colorRE.test(value);
export const rgba8=(color:string):string=>validRgbaHex(color)
  ?color.length===7?color.toLowerCase()+'ff':color.toLowerCase():'#ffffffff';
export const colorAndAlpha=(rgba:string):{hex:string;alpha:number}=>{
  const safe=rgba8(rgba);
  return {hex:safe.slice(0,7),alpha:parseInt(safe.slice(7,9),16)/255};
};
export function rgbaWithAlpha(color:string,alpha:number):string|null {
  if(!validRgbaHex(color)||!bounded(alpha,0,1))return null;
  const component=Math.round(alpha*255).toString(16).padStart(2,'0');
  return rgba8(color).slice(0,7)+component;
}
const parsed=(n:FxNode):unknown=>n.properties?.value;
export function readCurve(n:FxNode):CurveKey[]|null {
  if(n.type!=='Curve' || !Array.isArray(parsed(n)))return null;
  const rows=parsed(n) as unknown[];
  if(rows.length<1 || rows.length>32)return null;
  const result:CurveKey[]=[];
  for(const row of rows) {
    if(!Array.isArray(row)||row.length!==2||
       !bounded(row[0],0,1)||!bounded(row[1],-1e7,1e7))return null;
    result.push({t:row[0],value:row[1]});
  }
  return result.sort((a,b)=>a.t-b.t);
}
export function readGradient(n:FxNode):GradientKey[]|null {
  if(n.type!=='Gradient' || !Array.isArray(parsed(n)))return null;
  const rows=parsed(n) as unknown[];
  if(rows.length<1 || rows.length>32)return null;
  const result:GradientKey[]=[];
  for(const row of rows) {
    if(!Array.isArray(row)||row.length!==2||
       !bounded(row[0],0,1)||!validRgbaHex(row[1]))return null;
    result.push({t:row[0],color:rgba8(row[1])});
  }
  return result.sort((a,b)=>a.t-b.t);
}
export function gradientCss(keys:readonly GradientKey[]):string {
  const safe=keys.filter(k=>bounded(k.t,0,1)&&validRgbaHex(k.color));
  return 'linear-gradient(90deg,'+safe.map(k=>
    `${rgba8(k.color)} ${(k.t*100).toFixed(2)}%`).join(',')+')';
}
const fail=(graph:SpectraGraphV2,error:string):GraphEdit=>({graph,error});
function writeKeys(graph:SpectraGraphV2,id:string,kind:'Curve'|'Gradient',
                   values:Array<[number,number|string]>):GraphEdit {
  const index=graph.nodes.findIndex(n=>n.id===id&&n.type===kind);
  if(index<0)return fail(graph,'Selected node is not a '+kind+'.');
  if(!values.length||values.length>32)return fail(graph,'Need 1–32 keys.');
  const ordered=[...values].sort((a,b)=>a[0]-b[0]);
  for(let i=0;i<ordered.length;i++){
    const [time,value]=ordered[i];
    if(!bounded(time,0,1) ||
       (kind==='Curve' ? !bounded(value,-1e7,1e7) : !validRgbaHex(value)) ||
       (i>0 && time-ordered[i-1][0]<epsilon))
      return fail(graph,'Keys must have distinct positions in [0,1] and valid values.');
  }
  return {graph:{...graph,nodes:graph.nodes.map((node,i)=>i!==index?node:{
    ...node,properties:{...(node.properties??{}),value:ordered.map(([time,value])=>
      [time,typeof value==='string'?rgba8(value):value])}
  })},error:''};
}
export function editCurveKey(graph:SpectraGraphV2,id:string,index:number,
                             key:CurveKey):GraphEdit {
  const node=graph.nodes.find(n=>n.id===id);
  if(!node)return fail(graph,'Missing curve node.');
  const keys=readCurve(node);
  if(!keys||index<0||index>=keys.length)return fail(graph,'Curve key does not exist.');
  const result=keys.map((k,i)=>i===index?key:k);
  return writeKeys(graph,id,'Curve',result.map(k=>[k.t,k.value]));
}
export function editGradientKey(graph:SpectraGraphV2,id:string,index:number,
                                key:GradientKey):GraphEdit {
  const node=graph.nodes.find(n=>n.id===id);
  if(!node)return fail(graph,'Missing gradient node.');
  const keys=readGradient(node);
  if(!keys||index<0||index>=keys.length)return fail(graph,'Gradient key does not exist.');
  const result=keys.map((k,i)=>i===index?key:k);
  return writeKeys(graph,id,'Gradient',result.map(k=>[k.t,k.color]));
}
export function addCurveKey(graph:SpectraGraphV2,id:string):GraphEdit {
  const node=graph.nodes.find(n=>n.id===id);
  if(!node)return fail(graph,'Missing curve node.');
  const keys=readCurve(node);
  if(!keys||keys.length>=32)return fail(graph,'Curve has too many keys or invalid data.');
  let gap=0,center=0.5;
  const points=[0,...keys.map(k=>k.t),1].sort((a,b)=>a-b);
  for(let i=1;i<points.length;i++) {
    const g=points[i]-points[i-1];
    if(g>gap || (i===1 && g===gap)) {gap=g;center=(points[i]+points[i-1])*0.5;}
  }
  if(gap<2*epsilon)return fail(graph,'No space for another key.');
  const prev=[...keys].sort((a,b)=>Math.abs(a.t-center)-Math.abs(b.t-center))[0];
  return writeKeys(graph,id,'Curve',[...keys.map(k=>[k.t,k.value] as [number,number]),[center,prev?.value??0]]);
}
export function addGradientKey(graph:SpectraGraphV2,id:string):GraphEdit {
  const node=graph.nodes.find(n=>n.id===id);
  if(!node)return fail(graph,'Missing gradient node.');
  const keys=readGradient(node);
  if(!keys||keys.length>=32)return fail(graph,'Gradient has too many keys or invalid data.');
  let gap=0,center=0.5;
  const points=[0,...keys.map(k=>k.t),1].sort((a,b)=>a-b);
  for(let i=1;i<points.length;i++){
    const g=points[i]-points[i-1];
    if(g>gap){gap=g;center=(points[i]+points[i-1])/2;}
  }
  if(gap<2*epsilon)return fail(graph,'No room for another gradient key.');
  const prev=[...keys].sort((a,b)=>Math.abs(a.t-center)-Math.abs(b.t-center))[0];
  return writeKeys(graph,id,'Gradient',[...keys.map(k=>[k.t,k.color] as [number,string]),[center,prev?.color??'#ffffffff']]);
}
export function removeCurveKey(graph:SpectraGraphV2,id:string,index:number):GraphEdit {
  const node=graph.nodes.find(n=>n.id===id);
  const keys=node&&readCurve(node);
  if(!keys||keys.length<=1||index<0||index>=keys.length)return fail(graph,'Cannot remove the last curve key.');
  return writeKeys(graph,id,'Curve',keys.filter((_,i)=>i!==index).map(k=>[k.t,k.value]));
}
export function removeGradientKey(graph:SpectraGraphV2,id:string,index:number):GraphEdit {
  const node=graph.nodes.find(n=>n.id===id);
  const keys=node&&readGradient(node);
  if(!keys||keys.length<=1||index<0||index>=keys.length)return fail(graph,'Cannot remove the last gradient key.');
  return writeKeys(graph,id,'Gradient',keys.filter((_,i)=>i!==index).map(k=>[k.t,k.color]));
}
