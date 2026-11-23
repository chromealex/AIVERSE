import { useEffect, useMemo, useState } from 'react';
import AssetReferencePicker from '../../components/AssetReferencePicker';
import { invoke, nativeConnected, onNativeEvent } from '../../bridge';
import type { MaterialData, MaterialPatch, MaterialLibrary } from '../materialSchema';
import { materialNames } from '../materialSchema';
import './BuildingMaterialEditor.css';

type NumericKey = 'metallic' | 'roughness' | 'emissiveStrength' | 'normalScale' | 'occlusionStrength';
type UvKey = 'uvScale' | 'uvOffset';
const uvFields: ReadonlyArray<{ key: UvKey; label: string }> = [{key:'uvScale',label:'UV scale (U,V)'},{key:'uvOffset',label:'UV offset (U,V)'}];
type TextureKey = 'texture' | 'normalMap' | 'metallicRoughnessMap' | 'occlusionMap' | 'emissiveMap';
const numeric: ReadonlyArray<{ key: NumericKey; label: string; max: number }> = [
  { key:'metallic', label:'Metallic', max:1 },
  { key:'roughness', label:'Roughness', max:1 },
  { key:'emissiveStrength', label:'Emission strength', max:100 },
  { key:'normalScale', label:'Normal intensity', max:16 },
  { key:'occlusionStrength', label:'Occlusion strength', max:1 },
];
const textures: ReadonlyArray<{ key: TextureKey; label:string }> = [
  {key:'texture', label:'Base Color / Albedo'},
  {key:'normalMap', label:'Normal'},
  {key:'metallicRoughnessMap', label:'Metallic / Roughness'},
  {key:'occlusionMap', label:'Ambient Occlusion'},
  {key:'emissiveMap', label:'Emissive'},
];

const initial = (source: MaterialData): MaterialPatch => {
  const copy: MaterialPatch = {};
  for (const {key} of numeric) {
    const raw = source[key];
    copy[key] = typeof raw === 'number' && Number.isFinite(raw) ? raw : key === 'roughness' ? 1 : 0;
  }
  for (const {key} of uvFields) {
    const raw=source[key];
    copy[key]=Array.isArray(raw)&&raw.length===2
      ? raw.join(',') : typeof raw==='string' ? raw : key==='uvScale'?'1,1':'0,0';
  }
  for (const {key} of textures) copy[key] = typeof source[key] === 'string' ? source[key] : '';
  return copy;
};

const same = (a:MaterialPatch,b:MaterialPatch) =>
  [...numeric.map(f=>f.key),...textures.map(f=>f.key),...uvFields.map(f=>f.key)].every(key => a[key]===b[key]);

/** Edits a single existing Studio materials.json PBR resource.
 *  The same material holds all scalar properties AND texture GUIDs.
 *  Does not silently mutate the generator's bundled architecture style. */
export default function BuildingMaterialEditor({ projectLoaded }:{projectLoaded:boolean}) {
  const [library,setLibrary] = useState<MaterialLibrary>({library:{}});
  const [name,setName] = useState('');
  const [draft,setDraft] = useState<MaterialPatch>({});
  const [baseline,setBaseline] = useState<MaterialPatch>({});
  const [busy,setBusy] = useState(false);
  const [error,setError] = useState('');
  const [message,setMessage] = useState('');
  const dirty = useMemo(()=>!same(draft,baseline),[draft,baseline]);
  const names = useMemo(()=>materialNames(library),[library]);
  const reload = async (requested?:string) => {
    const data = await invoke('studio.materials',{});
    setLibrary(data);
    const next = requested && data.library[requested] ? requested :
      name && data.library[name] ? name : materialNames(data)[0] ?? '';
    setName(next);
    const source = data.library[next] ?? {};
    const values = initial(source);
    setDraft(values);setBaseline(values);
    setMessage('');setError('');
  };
  useEffect(()=>{
    if(!projectLoaded){setLibrary({library:{}});setName('');return;}
    let alive=true;
    void invoke('studio.materials',{}).then(data=>{
      if(!alive)return;
      setLibrary(data);
      const next=materialNames(data)[0]??'';
      setName(next);
      const values=initial(data.library[next]??{});
      setDraft(values);setBaseline(values);
    }).catch(e=>{if(alive)setError(String(e));});
    return ()=>{alive=false;};
  },[projectLoaded]);
  useEffect(()=>onNativeEvent(event=>{
    if(event==='workspace.changed' || event==='document.changed'){
      // Avoid wiping unsaved edits on unrelated document events.
      if(!dirty && projectLoaded)void reload().catch(e=>setError(String(e)));
    }
  }),[dirty,projectLoaded,name]);

  const selectMaterial=(next:string)=>{
    if(dirty && !window.confirm('Discard unsaved material and texture changes?'))return;
    setName(next);
    const values=initial(library.library[next]??{});
    setDraft(values);setBaseline(values);
    setError('');setMessage('');
  };
  const save=async()=>{
    if(!nativeConnected()||!name||!dirty||busy)return;
    for(const {key} of uvFields){
      const source=String(draft[key]??'');
      const parts=source.split(',').map(x=>Number(x.trim()));
      if(parts.length!==2||parts.some(x=>!Number.isFinite(x)||Math.abs(x)>1024)){
        setError('UV scale/offset must contain two finite values between -1024 and 1024.');
        return;
      }
    }
    setBusy(true);setError('');setMessage('');
    try{
      // One native patch persists the scalar material values AND all image
      // GUIDs atomically in the existing materials.json document.
      const saved=await invoke('studio.materialPatch',{name,patch:draft});
      setLibrary(saved);
      const next=initial(saved.library[name]??{});
      setDraft(next);setBaseline(next);
      setMessage('Material and textures saved together in materials.json.');
      window.dispatchEvent(new Event('studio:assets-changed'));
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setBusy(false);}
  };
  return <section className="abg-material" aria-label="PBR material and texture editor">
    <div className="abg-material__header">
      <strong>PBR material + textures</strong>
      <span className="abg-material__muted">Shared project materials.json</span>
    </div>
    <label className="abg-material__field">Material
      <select className="au-input" value={name} disabled={!projectLoaded||busy}
        onChange={e=>selectMaterial(e.target.value)}>
        {names.length===0&&<option value="">No project materials</option>}
        {names.map(item=><option key={item} value={item}>{item}</option>)}
      </select>
    </label>
    {!!name&&<div className="abg-material__grid">
      {numeric.map(({key,label,max})=><label className="abg-material__field" key={key}>{label}
        <input className="au-input" type="number" step="0.01" min="0" max={max}
          disabled={busy} value={String(draft[key]??0)}
          onChange={event=>{
            const number=Number(event.target.value);
            if(Number.isFinite(number))
              setDraft(previous=>({...previous,[key]:Math.max(0,Math.min(max,number))}));
          }}/>
      </label>)}
      {uvFields.map(({key,label})=><label className="abg-material__field" key={key}>{label}
        <input className="au-input" type="text" placeholder="1,1"
          disabled={busy} value={String(draft[key]??'')}
          onChange={event=>setDraft(previous=>({...previous,[key]:event.target.value}))}/>
      </label>)}
      {textures.map(({key,label})=><label className="abg-material__field" key={key}>{label}
        <AssetReferencePicker label={label} kinds={['image']} value={String(draft[key]??'')}
          disabled={busy} onChange={guid=>setDraft(previous=>({...previous,[key]:guid}))}/>
      </label>)}
    </div>}
    <p className="abg-material__note">Choose or drag textures from Assets. References are stored as asset GUIDs alongside the material parameters. Assign this project material to a building style slot above; its factors, UV transform and textures are applied together when generating GLB.</p>
    {error&&<p role="alert" className="abg-material__error">{error}</p>}
    {message&&<p role="status">{message}</p>}
    <div className="abg-material__actions">
      <button className="au-button" type="button" disabled={!dirty||busy}
        onClick={()=>{setDraft({...baseline});setError('');}}>Discard</button>
      <button className="au-button au-button--primary" type="button"
        disabled={!name||!nativeConnected()||!dirty||busy} onClick={()=>void save()}>
        {busy?'Saving…':'Save material + textures'}
      </button>
    </div>
  </section>;
}
