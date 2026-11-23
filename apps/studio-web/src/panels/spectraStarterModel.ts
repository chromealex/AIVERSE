/** First-open SpectraStudio graph creation. Pure authoring helpers only.
 * Trust the native fx_graph_edit(new) factory for default nodes and links:
 * it owns the canonical initial v2 graph. React never duplicates that schema.
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

/** Native newGraphV2() already contains the canonical playable
 * Spawn -> Initialize -> SpriteOutput chain. Pass through unchanged;
 * native fx_graph_validate checks compilation before preview.
 */
export function createBurstStarter(nativeNew:SpectraGraphV2):SpectraGraphV2|null {
  if(nativeNew.version!==2 || nativeNew.nodes.length===0 ||
     nativeNew.outputs.length===0) return null;
  return nativeNew;
}

/** Explicit blank variant for advanced authors; incomplete until nodes
 * are added. Keep native schema metadata, never modify the source graph.
 */
export function createBlankGraph(nativeNew:SpectraGraphV2):SpectraGraphV2 {
  return {...nativeNew,nodes:[],links:[],outputs:[]};
}
