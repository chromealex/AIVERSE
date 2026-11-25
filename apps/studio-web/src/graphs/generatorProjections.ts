import type {GraphView,NodeView,Link} from './graphFramework';
import {object} from './nativeGraphAdapters.ts';
import type {GraphObject} from './nativeGraphAdapters';
import {parseCaveSnapshot,projectCavePage} from './caveGraphInspection.ts';

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
 const links=[link('recipe','structure'),link('structure','mesh'),link('style','bindings')];
 if(Array.isArray(recipe.features))recipe.features.slice(0,48).forEach((feature,i)=>{
  if(!object(feature))return;
  const id='feature:'+i;
  nodes.push(node(id,String(feature.kind??'Feature')+' · '+String(feature.side??''),40,260+i*70,null,'recipe'));
  links.push(link(id,'structure'));
 });
 if(object(recipe.materialBindings))Object.entries(recipe.materialBindings).slice(0,32).forEach(([slot,material],i)=>{
  const id='material:'+i;
  nodes.push(node(id,slot+' → '+String(material),800,260+i*70,null,'material'));
  links.push(link(id,'bindings'));
 });
 return {nodes,links};
}
/** Compatibility projection of the first bounded page, using the canonical parser. */
export function caveSnapshotProjection(snapshot:GraphObject):GraphView {
 return projectCavePage(parseCaveSnapshot(snapshot)).view;
}
