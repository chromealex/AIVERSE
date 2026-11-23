import type {GraphView,NodeView,Link} from './graphFramework';
import type {MaterialData,MaterialPatch,MaterialKey} from '../panels/materialSchema';

export const textureChannels=[
 ['texture','Albedo'],['normalMap','Normal'],['metallicRoughnessMap','Metallic / roughness'],['occlusionMap','Occlusion'],['emissiveMap','Emission'],['teamMask','Team mask']
] as const;
export type TextureChannel=typeof textureChannels[number][0];
export type TextureOutput='color'|'normal'|'mask';
export const generatedChannel:Record<TextureOutput,TextureChannel>={color:'texture',normal:'normalMap',mask:'teamMask'};
export function materialGraph(name:string,material:MaterialData,patch:MaterialPatch={}):GraphView {
 const effective={...material,...patch},nodes:NodeView[]=[],links:Link[]=[];
 nodes.push({id:'material',label:name,x:560,y:150,inputs:[
  ...textureChannels.map(([id])=>({id,type:'texture'})),{id:'factors',type:'pbr'},{id:'shader',type:'shader'},{id:'base',type:'material'}
 ],outputs:[{id:'surface',type:'material'}]});
 textureChannels.forEach(([id,label],i)=>{
  const value=effective[id];if(typeof value!=='string'||!value)return;
  const nodeId='texture:'+id;
  nodes.push({id:nodeId,label,x:30,y:30+i*90,inputs:[],outputs:[{id:'image',type:'texture'}]});
  links.push({id:nodeId,from:nodeId,output:'image',to:'material',input:id});
 });
 nodes.push({id:'factors',label:'PBR factors',x:300,y:30,inputs:[],outputs:[{id:'value',type:'pbr'}]});
 links.push({id:'factors',from:'factors',output:'value',to:'material',input:'factors'});
 if(effective.shaderGraph&&typeof effective.shaderGraph==='object'){
  nodes.push({id:'shader',label:'Embedded shader snapshot',x:300,y:130,inputs:[],outputs:[{id:'value',type:'shader'}]});
  links.push({id:'shader',from:'shader',output:'value',to:'material',input:'shader'});
 }
 if(typeof effective.base==='string'&&effective.base){
  nodes.push({id:'base',label:effective.base,x:300,y:230,inputs:[],outputs:[{id:'value',type:'material'}]});
  links.push({id:'base',from:'base',output:'value',to:'material',input:'base'});
 }
 return {nodes,links};
}
export interface TextureRecipe {pattern:string;output:TextureOutput;seed:number;size:number;scale:number;name:string}
export function textureRecipeParams(recipe:TextureRecipe,patterns:readonly string[]):Record<string,unknown>{
 if(!patterns.includes(recipe.pattern)||!['color','normal','mask'].includes(recipe.output)||
 !Number.isSafeInteger(recipe.seed)||recipe.seed<0||!Number.isSafeInteger(recipe.size)||recipe.size<8||recipe.size>2048||
 !Number.isFinite(recipe.scale)||recipe.scale<=0||recipe.scale>256||!/^[A-Za-z0-9_-]{1,64}$/.test(recipe.name))throw new Error('Invalid texture recipe');
 return {pattern:recipe.pattern,output:recipe.output,seed:recipe.seed,size:recipe.size,scale:recipe.scale,path:'content/textures/'+recipe.name+'.png',overwrite:false};
}
export function textureChannelForOutput(output:TextureOutput):MaterialKey{return generatedChannel[output];}
