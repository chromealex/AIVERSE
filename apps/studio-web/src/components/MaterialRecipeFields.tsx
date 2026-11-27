import { useState } from 'react';
import AssetReferencePicker from './AssetReferencePicker';
import { StudioSelect } from '../ui';
import { MATERIAL_PRESETS, type MaterialPreset } from '../panels/materialSchema';

export function useMaterialRecipe() {
 const [newPreset,setNewPreset]=useState<MaterialPreset>('matte');
 const [colorMap,setColorMap]=useState('');
 const [normalMap,setNormalMap]=useState('');
 const [repeatMetres,setRepeatMetres]=useState('1');
 const recipeValid=newPreset!=='triplanar' || (colorMap.trim() && Number(repeatMetres)>=0.001 && Number(repeatMetres)<=100000);
 return {newPreset,setNewPreset,colorMap,setColorMap,normalMap,setNormalMap,repeatMetres,setRepeatMetres,valid:recipeValid,params:{preset:newPreset,...(newPreset==='triplanar'?{colorGuid:colorMap,normalGuid:normalMap,repeatMetres:Number(repeatMetres)}:{}) }};
}

export function MaterialRecipeFields({recipe,busy}:{recipe:ReturnType<typeof useMaterialRecipe>;busy:boolean}) {
 const {newPreset,setNewPreset,colorMap,setColorMap,normalMap,setNormalMap,repeatMetres,setRepeatMetres}=recipe;
 return <>
      <label>Preset<StudioSelect aria-label="Material preset" value={newPreset}
        onChange={e=>setNewPreset(e.target.value as MaterialPreset)}>
        {MATERIAL_PRESETS.map(p=><option key={p} value={p}>{p}</option>)}
      </StudioSelect></label>
      {newPreset==='triplanar'&&<>
        <div className="property-row"><span>Colour texture</span><AssetReferencePicker label="Colour texture" value={colorMap} kinds={['image']} disabled={busy} onChange={setColorMap}/></div>
        <div className="property-row"><span>Normal texture (optional)</span><AssetReferencePicker label="Normal texture (optional)" value={normalMap} kinds={['image']} disabled={busy} onChange={setNormalMap}/></div>
        <label>Repeat length (metres)<input type="number" min="0.001" max="100000" step="any" value={repeatMetres} onChange={e=>setRepeatMetres(e.target.value)}/></label>
      </>}
 </>;
}
