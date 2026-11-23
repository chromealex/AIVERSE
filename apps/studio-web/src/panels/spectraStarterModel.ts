/** First-open SpectraStudio graph creation. Pure authoring helpers only.
 * The native fx_graph_validate compiler remains the source of truth.
 * Never overwrite published files, never create filesystem paths in React.
 */
import { safeGraphName, type SpectraGraphV2 } from './spectraDataInterfaceModel';

export function nextUnusedGraphName(
  existing: readonly {name:string}[], base='preview_fx'
): string {
  const taken = new Set(existing.map(entry=>entry.name));
  const safe = safeGraphName(base) ? base : 'preview_fx';
  if(!taken.has(safe))return safe;
  for(let i=2;i<100000;i++){
    const suffix='_'+i;
    const name=safe.slice(0,48-suffix.length)+suffix;
    if(!taken.has(name))return name;
  }
  throw new Error('No free VFX graph name.');
}

/** Valid V2 starter from the same execution path as the actual native
 * StudioSpectraPreview Metal test (Spawn -> Initialize -> SpriteOutput).
 * Does not touch original graph or alter instance parameter definitions.
 */
export function createBurstStarter(base:SpectraGraphV2):SpectraGraphV2|null {
  if(base.version!==2 || base.nodes.length || base.links.length || base.outputs.length)
    return null;
  return {
    ...base,
    description:'SpectraFX starter · real Scene preview · Spawn → Initialize → SpriteOutput',
    nodes:[
      {id:'spawn',type:'Spawn',properties:{burst:32},position:[45,135]},
      {id:'initialize',type:'Initialize',
       properties:{life:3,speed:0.8,size:0.45,spread:0.4,gravity:[0,0,0]},
       position:[295,135]},
      {id:'output',type:'SpriteOutput',
       properties:{texture:'builtin:soft',blend:'additive'},
       position:[555,135]}
    ],
    links:[
      {from:'spawn',to:'initialize'},
      {from:'initialize',to:'output'}
    ],
    outputs:['output']
  };
}
