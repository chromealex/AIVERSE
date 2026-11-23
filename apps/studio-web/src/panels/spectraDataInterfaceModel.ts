/** SpectraFX Studio: pure, transactional graph-v2 Data Interface authoring.
 * Browser never writes files. fx_graph_validate / fx_graph_set are native
 * Workspace operations and remain the final schema/compile authority.
 */
export interface FxInstanceScalar {
  slot: number;
  default: number;
  min: number;
  max: number;
}
export interface FxDataInterface {
  provider: string;
  channel: string;
  scale?: number;
  offset?: number;
  fallback?: number;
  required?: boolean;
}
export interface FxNode {
  id: string;
  type: string;
  properties?: Record<string, unknown>;
  position?: number[];
}
export interface SpectraGraphV2 {
  version: 2;
  instanceParameters?: Record<string, FxInstanceScalar>;
  dataInterfaces?: Record<string, FxDataInterface>;
  nodes: FxNode[];
  links: unknown[];
  outputs: string[];
  [field: string]: unknown;
}
export type GraphEdit = { graph: SpectraGraphV2; error: string };
const validName = (name: string): boolean => /^[a-zA-Z0-9_]{1,64}$/.test(name);
export const safeGraphName = (name: string): boolean => /^[a-z0-9_]{1,48}$/.test(name) && name !== 'fx';
const finite = (n: unknown, bound = 1e7): n is number =>
  typeof n === 'number' && Number.isFinite(n) && Math.abs(n) <= bound;
const record = (v: unknown): v is Record<string, unknown> =>
  !!v && typeof v === 'object' && !Array.isArray(v);
const failure = (graph: SpectraGraphV2, error: string): GraphEdit => ({ graph, error });

/** Never mutate the author's source graph, including unknown metadata. */
export function graphV2(value: unknown): SpectraGraphV2 | null {
  if (!record(value) || value.version !== 2 || !Array.isArray(value.nodes) ||
      !Array.isArray(value.links) || !Array.isArray(value.outputs) ||
      (value.instanceParameters !== undefined && !record(value.instanceParameters)) ||
      (value.dataInterfaces !== undefined && !record(value.dataInterfaces))) return null;
  return value as unknown as SpectraGraphV2;
}
export function scalarParameters(graph: SpectraGraphV2): Array<[string,FxInstanceScalar]> {
  const map = graph.instanceParameters ?? {};
  return Object.entries(map).filter(([name,p]) =>
    validName(name) && !!p && Number.isInteger(p.slot) && p.slot >= 0 &&
    p.slot < 8 && finite(p.default) && finite(p.min) && finite(p.max) &&
    p.min <= p.default && p.default <= p.max);
}
export function validateInterfaces(graph: SpectraGraphV2): string | null {
  const params = scalarParameters(graph);
  if (params.length !== Object.keys(graph.instanceParameters ?? {}).length ||
      params.length > 8 || new Set(params.map(([,p])=>p.slot)).size !== params.length)
    return 'Invalid or duplicate instance parameter slots (0–7).';
  const bindings=Object.entries(graph.dataInterfaces ?? {});
  if (bindings.length>8) return 'At most eight Data Interface bindings are supported.';
  for (const [name,b] of bindings) {
    const p=graph.instanceParameters?.[name];
    if (!p || !record(b)) return 'Binding references an unknown instance parameter: '+name;
    const fields=Object.keys(b);
    if (fields.some(key=>!['provider','channel','scale','offset','fallback','required'].includes(key)))
      return 'Unknown Data Interface setting on '+name;
    if (typeof b.provider!=='string' || !validName(b.provider) ||
        typeof b.channel!=='string' || !validName(b.channel))
      return 'Provider and channel must be 1–64 letters, digits or underscores.';
    if (b.scale!==undefined && !finite(b.scale,1000))return 'Scale must be finite and within ±1000.';
    if (b.offset!==undefined && !finite(b.offset))return 'Offset must be finite and within ±10000000.';
    if (b.fallback!==undefined && (!finite(b.fallback) || b.fallback<p.min || b.fallback>p.max))
      return 'Fallback must be within '+name+' parameter bounds.';
    if (b.required!==undefined && typeof b.required!=='boolean')
      return 'Required must be a boolean.';
  }
  for (const node of graph.nodes) {
    if (node.type==='DataInterface' &&
        (typeof node.properties?.name!=='string' || !bindings.some(([name])=>name===node.properties?.name)))
      return 'DataInterface node '+node.id+' needs a matching declared binding.';
  }
  return null;
}
export function putInterface(graph: SpectraGraphV2, name: string, value: FxDataInterface): GraphEdit {
  if (!validName(name) || !graph.instanceParameters?.[name])
    return failure(graph,'Choose an existing instance parameter.');
  const next={...graph,dataInterfaces:{...(graph.dataInterfaces??{}),[name]:{...value}}};
  return {graph: validateInterfaces(next) ? graph : next,
          error: validateInterfaces(next) ?? ''};
}
export function dropInterface(graph: SpectraGraphV2,name:string): GraphEdit {
  if (!graph.dataInterfaces?.[name]) return failure(graph,'Binding does not exist.');
  if (graph.nodes.some(n=>n.type==='DataInterface'&&n.properties?.name===name))
    return failure(graph,'Delete or reconnect DataInterface nodes before removing this binding.');
  const values={...graph.dataInterfaces};
  delete values[name];
  return {graph:{...graph,dataInterfaces:values},error:''};
}
export function putParameter(graph:SpectraGraphV2,name:string,def:FxInstanceScalar):GraphEdit {
  if (!validName(name))return failure(graph,'Parameter name must be 1–64 letters, digits or underscores.');
  if (!Number.isInteger(def.slot) || def.slot<0 || def.slot>7 ||
      !finite(def.default)||!finite(def.min)||!finite(def.max) ||
      def.min>def.default||def.default>def.max)
    return failure(graph,'Invalid slot or scalar range.');
  const next={...graph,instanceParameters:{...(graph.instanceParameters??{}),[name]:{...def}}};
  const error=validateInterfaces(next);
  return error?failure(graph,error):{graph:next,error:''};
}
export function dropParameter(graph:SpectraGraphV2,name:string):GraphEdit {
  if (!graph.instanceParameters?.[name])return failure(graph,'Parameter does not exist.');
  if (graph.dataInterfaces?.[name] || graph.nodes.some(n=>
      ['DataInterface','InstanceParameter'].includes(n.type) && n.properties?.name===name))
    return failure(graph,'Remove bindings and parameter nodes before deleting this parameter.');
  const values={...graph.instanceParameters};
  delete values[name];
  return {graph:{...graph,instanceParameters:values},error:''};
}
export function putNode(graph:SpectraGraphV2,name:string):GraphEdit {
  if (!graph.dataInterfaces?.[name])return failure(graph,'Create the Data Interface binding first.');
  if (graph.nodes.length>=256)return failure(graph,'Graph node budget exceeded.');
  if (graph.nodes.some(node=>node.type==='DataInterface'&&node.properties?.name===name))
    return failure(graph,'This binding already has a Data Interface node.');
  const base='data_'+name.slice(0,48);
  let id=base,index=1;
  const ids=new Set(graph.nodes.map(n=>n.id));
  while(ids.has(id) && index<1000)id=base+'_'+index++;
  if(ids.has(id))return failure(graph,'No free graph node ID.');
  return {graph:{...graph,nodes:[...graph.nodes,{
    id,type:'DataInterface',properties:{name},position:[80,360+graph.nodes.length*26]
  }]},error:''};
}
export function dropNode(graph:SpectraGraphV2,id:string):GraphEdit {
  if (!graph.nodes.some(n=>n.id===id && n.type==='DataInterface'))
    return failure(graph,'Choose a Data Interface node to remove.');
  return {graph:{...graph,nodes:graph.nodes.filter(n=>n.id!==id),
    links:graph.links.filter(e=>!record(e)||(e.from!==id&&e.to!==id))},
    error:''};
}
export function providerNames(graph:SpectraGraphV2, query=''):string[] {
  const q=query.trim().toLowerCase();
  return [...new Set(Object.values(graph.dataInterfaces??{}).map(b=>b.provider))]
    .filter(name=>validName(name)&&name.toLowerCase().includes(q)).sort();
}

/** Only select an actual image listed by the native Studio Assets catalog.
 * Native ContentDirectory reports content-relative paths; SpectraFX texture
 * loader resolves project-relative references ("content/" + path).
 */
export function putSpriteTexture(graph:SpectraGraphV2,nodeId:string,
                                 assetPath:string,knownAssets:readonly string[]):GraphEdit {
  const node=graph.nodes.find(n=>n.id===nodeId && n.type==='SpriteOutput');
  if(!node)return failure(graph,'Select an existing SpriteOutput node.');
  if(!knownAssets.includes(assetPath) ||
     !assetPath || assetPath.startsWith('/') || assetPath.includes('\\') ||
     assetPath.split('/').some(p=>p===''||p==='.'||p==='..') ||
     !/\.(png|jpe?g|tga)$/i.test(assetPath))
    return failure(graph,'Choose a supported image from the project Assets catalog.');
  const path='content/'+assetPath;
  return {graph:{...graph,nodes:graph.nodes.map(n=>n.id===nodeId?
    {...n,properties:{...(n.properties??{}),texture:path}}:n)},error:''};
}
