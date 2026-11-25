import type {GraphView,NodeView,Link,Port} from './graphFramework';
import {parseBuildingRoomTopology} from './buildingRoomTopology.ts';
import type {BuildingRoomTopology} from './buildingRoomTopology';

// Only a projection of an owner recipe and its studio.buildingInspect response.
// Optional native room topology is inspected separately; no runtime or file mutation.
type Obj=Record<string,unknown>;
const object=(v:unknown):v is Obj=>!!v&&typeof v==='object'&&!Array.isArray(v);
const fail=(message:string):never=>{throw new Error(message)};
const record=(v:unknown,label:string):Obj=>object(v)?v:fail('Invalid '+label);
const text=(v:unknown,label:string,max=128):string=>typeof v==='string'&&v.length>0&&v.length<=max&&!/[\u0000-\u001f\u007f]/.test(v)?v:fail('Invalid '+label);
const number=(v:unknown,label:string,min=0,max=Number.MAX_SAFE_INTEGER):number=>typeof v==='number'&&Number.isFinite(v)&&v>=min&&v<=max?v:fail('Invalid '+label);
const integer=(v:unknown,label:string,min=0,max=Number.MAX_SAFE_INTEGER):number=>{const n=number(v,label,min,max);return Number.isSafeInteger(n)?n:fail('Invalid '+label)};
const array=(v:unknown,label:string,max:number):unknown[]=>Array.isArray(v)&&v.length<=max?v:fail('Invalid '+label);
const port=(id:string,type:string,multiple=false):Port=>({id,type,multiple});
const node=(id:string,label:string,x:number,y:number,inputs:Port[]=[],outputs:Port[]=[]):NodeView=>({id,label,x,y,inputs,outputs});
export interface BuildingSocketView {id:string;kind:string;side:string;featureIndex:number;width:number;height:number;open:number;outside:readonly [number,number,number]}
export interface BuildingInspectionView {view:GraphView;sockets:readonly BuildingSocketView[];seed:number;triangles:number;vertices:number;solids:number|null;connectionCount:number|null;floors:number|null;roomsX:number|null;roomsZ:number|null;mode:'features'|'preset';topology:BuildingRoomTopology|null}
const kinds=new Set(['large-gate','small-gate','personnel-door','sliding-roof','window','stairs','elevator']);
export function projectBuildingInspection(recipeValue:unknown,inspectionValue:unknown):BuildingInspectionView {
 const recipe=record(recipeValue,'recipe'),inspection=record(inspectionValue,'inspection');
 const mode=recipe.mode==='features'?'features':recipe.mode==='preset'||recipe.mode===undefined?'preset':fail('Unknown building recipe mode');
 const seed=integer(recipe.seed,'recipe seed',0,2147483647);
 if(integer(inspection.seed,'inspection seed',0,2147483647)!==seed)fail('Inspection seed does not match recipe');
 const style=text(recipe.style,'style',48);
 const features=mode==='features'?array(recipe.features??[],'features',24).map((v,i)=>{
  const f=record(v,'feature '+i),kind=text(f.kind,'feature kind');
  if(!kinds.has(kind))fail('Unknown feature kind');
  return {kind};
 }):[];
 const triangles=integer(inspection.triangles,'triangles'),vertices=integer(inspection.vertices,'vertices');
 const nodes:NodeView[]=[],links:Link[]=[];
 const connect=(from:string,output:string,to:string,input:string)=>links.push({id:JSON.stringify([from,output,to,input]),from,output,to,input});
 nodes.push(node('recipe','Recipe · seed '+seed,40,40,[],[port('recipe','recipe',true)]));
 nodes.push(node('style','Style · '+style,40,150,[],[port('style','material',true)]));
 nodes.push(node('structure',mode==='features'?'Native StructureGraph':'Native preset',330,40,[port('recipe','recipe'),port('features','feature',true)],[port('geometry','geometry',true),port('sockets','socket',true)]));
 nodes.push(node('mesh','Native mesh · '+triangles+' triangles',640,40,[port('geometry','geometry'),port('style','material')],[]));
 connect('recipe','recipe','structure','recipe');connect('structure','geometry','mesh','geometry');connect('style','style','mesh','style');
 features.forEach((f,i)=>{const id='feature:'+i;nodes.push(node(id,f.kind+' #'+(i+1),40,270+i*85,[],[port('feature','feature',true)]));connect(id,'feature','structure','features');});
 // These bindings are authored references, not materials resolved by Inspect.
 if(recipe.materialBindings!==undefined){
  const bindings=Object.entries(record(recipe.materialBindings,'material bindings'));
  if(bindings.length>64)fail('Too many material bindings');
  if(bindings.length){nodes.push(node('bindings','Authored bindings (not resolved)',330,150,[port('style','material',true)],[]));connect('style','style','bindings','style');}
  bindings.sort(([a],[b])=>a<b?-1:a>b?1:0).forEach(([slot,name],i)=>{
   text(slot,'material slot',48);const value=text(name,'material name',48);
   const id='binding:'+slot;nodes.push(node(id,slot+' → '+value,330,270+i*85,[],[port('binding','material',true)]));connect(id,'binding','bindings','style');
  });
 }
 let solids:number|null=null,connectionCount:number|null=null,floors:number|null=null,roomsX:number|null=null,roomsZ:number|null=null;
 const sockets:BuildingSocketView[]=[];
 let topology:BuildingRoomTopology|null=null;
 if(mode==='features'){
  solids=integer(inspection.solids,'solids');connectionCount=integer(inspection.connections,'connections');
  floors=integer(inspection.floors,'floors',1,8);roomsX=integer(inspection.roomsX,'roomsX',1,6);roomsZ=integer(inspection.roomsZ,'roomsZ',1,6);
  if(floors*roomsX*roomsZ>128)fail('Invalid native room grid');
  for(const [key,value] of [['floors',floors],['roomsX',roomsX],['roomsZ',roomsZ]] as const){if(recipe[key]!==undefined&&recipe[key]!==value)fail('Inspection room grid does not match recipe');}
  const seen=new Set<string>();
  array(inspection.sockets,'native sockets',256).forEach((v,i)=>{
   const s=record(v,'socket'),id=text(s.socketId,'socket ID'),kind=text(s.kind,'socket kind'),side=text(s.side,'socket side');
   if(seen.has(id))fail('Duplicate native socket ID');seen.add(id);
   const featureIndex=integer(s.featureIndex,'feature index',0,features.length-1);
   if(features[featureIndex].kind!==kind)fail('Socket feature kind does not match recipe');
   const point=array(s.outside,'native anchor',3);if(point.length!==3)fail('Invalid native anchor');
   const outside:[number,number,number]=[number(point[0],'anchor x',-1e6,1e6),number(point[1],'anchor y',-1e6,1e6),number(point[2],'anchor z',-1e6,1e6)];
   sockets.push({id,kind,side,featureIndex,width:number(s.width,'socket width',0,160),height:number(s.height,'socket height',0,160),open:number(s.open,'authored opening',0,1),outside});
   const key='socket:'+id;
   nodes.push(node(key,kind+' · '+id,640,270+i*85,[port('socket','socket'),port('feature','feature')],[]));
   connect('structure','sockets',key,'socket');connect('feature:'+featureIndex,'feature',key,'feature');
  });
  if(inspection.topology!==undefined)topology=parseBuildingRoomTopology(inspection.topology,floors,roomsX,roomsZ,connectionCount,sockets);
 }else if(inspection.sockets!==undefined||inspection.solids!==undefined||inspection.connections!==undefined||inspection.topology!==undefined)fail('Preset inspection must not contain functional structure data');
 return {view:{nodes,links},sockets,seed,triangles,vertices,solids,connectionCount,floors,roomsX,roomsZ,mode,topology};
}
