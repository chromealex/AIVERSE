import {GraphRegistry} from './graphFramework';
import type {GraphAdapter,GraphView} from './graphFramework';
import {layoutShaderGraph,moveShaderGraphNode} from '../panels/shaderGraphLayout';
import type {GraphSource,ShaderNodeOperation} from '../panels/shaderGraphLayout';

export function shaderProjection(source:GraphSource,operations:readonly ShaderNodeOperation[]=[]):GraphView {
 const diagram=layoutShaderGraph(source);
 const channelsByNode=new Map<string,string[]>();
 for(const [channel,id] of Object.entries(source.outputs??{})){
  const channels=channelsByNode.get(id)??[];
  if(channel==='color')channels.unshift(channel);else channels.push(channel);
  channelsByNode.set(id,channels);
 }
 const nodes=diagram.nodes.map(n=>({id:n.id,label:n.op,values:n.values,outputChannels:channelsByNode.get(n.id),x:n.x,y:n.y,
  inputs:Array.from({length:Math.max(n.inputs.length,Math.min(16,operations.find(o=>o.id===n.op&&o.domains.includes(source.domain??'material'))?.inputs??0))},(_,index)=>({id:String(index),type:'float4'})),
  outputs:[{id:'value',type:'float4'}]}));
 const links=diagram.nodes.flatMap(n=>n.inputs.flatMap((from,index)=>from?[{
  id:n.id+':'+index,from,output:'value',to:n.id,input:String(index)
 }]:[]));
 return {nodes,links};
}
export const shaderAdapters=new GraphRegistry<GraphSource>();
for(const domain of ['material','postfx','particle']){
 const adapter:GraphAdapter<GraphSource>={domain,policy:{cycles:'reject'},
  project:source=>shaderProjection(source),move:moveShaderGraphNode};
 shaderAdapters.register(adapter);
}
