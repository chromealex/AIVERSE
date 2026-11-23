import type {GraphView,NodeView,Link} from './graphFramework';
import {object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

/** Read-only dependency projections of existing owner recipes. Never an evaluator. */
const node=(id:string,label:string,x:number,y:number,input:string|null,output:string|null):NodeView=>({
 id,label,x,y,inputs:input?[{id:'in',type:input,multiple:true}]:[],outputs:output?[{id:'out',type:output,multiple:true}]:[]
});
const link=(from:string,to:string):Link=>({id:JSON.stringify([from,to]),from,to,output:'out',input:'in'});
export function terrainRecipeProjection(terrain:GraphObject,forest:GraphObject):GraphView {
 const nodes=[node('terrain','Seed '+String(terrain.seed)+' · '+String(terrain.octaves)+' octaves',40,40,null,'heightfield'),
  node('heightfield','Native heightfield',300,40,'heightfield','heightfield'),
  node('forest','Arbor · '+(forest.enabled===true?'enabled':'disabled'),560,40,'heightfield',null)];
 return {nodes,links:[link('terrain','heightfield'),link('heightfield','forest')]};
}
export function buildingRecipeProjection(recipe:GraphObject):GraphView {
 const nodes=[node('recipe','Building recipe',40,40,null,'recipe'),node('style','Style · '+String(recipe.style??''),40,150,null,'material'),
 node('structure','StructureGraph',320,40,'recipe','structure'),node('mesh','Native mesh / collision',580,40,'structure',null)];
 // Material and structure are independent owners. A separate visual binding node
 // represents authored material slots, not an alternate building solver.
 nodes.push(node('bindings','Material bindings',320,150,'material',null));
 return {nodes,links:[link('recipe','structure'),link('structure','mesh'),link('style','bindings')]};
}
export function caveSnapshotProjection(snapshot:GraphObject):GraphView {
 if(snapshot.version!==1||!object(snapshot.settings)||!Array.isArray(snapshot.stamps)||!Array.isArray(snapshot.entrances))throw new Error('Unsupported Caverns snapshot');
 const nodes=[node('settings','Caverns generator settings',40,40,null,'volume'),node('volume','Authored SDF volume',600,40,'volume','volume')];
 const links=[link('settings','volume')];
 // Bounded presentation; never truncate or rewrite the persisted snapshot.
 const rows=[...snapshot.stamps.slice(0,64).map(s=>({s,kind:'stamp'})),...snapshot.entrances.slice(0,32).map(s=>({s,kind:'entrance'}))];
 const seen=new Set<string>();
 rows.forEach(({s,kind},i)=>{
  if(!object(s)||typeof s.id!=='number'||!Number.isSafeInteger(s.id)||s.id<=0)return;
  const id=kind+':'+s.id;if(seen.has(id))return;seen.add(id);
  nodes.push(node(id,kind==='stamp'?String(s.mode)+' · '+String(s.radiusCm)+' cm':'Entrance · '+String(s.radiusCm)+' cm',300,40+i*70,null,'volume'));
  links.push(link(id,'volume'));
 });
 return {nodes,links};
}
