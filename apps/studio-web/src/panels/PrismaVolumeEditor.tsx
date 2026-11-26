import {useState, type FormEvent} from 'react';
import {StudioButton, StudioInput, StudioSelect} from '../ui';

type Shape = 'global' | 'sphere' | 'box';
interface Volume {
  id?: string; shape?: Shape; enabled?: boolean; center?: number[]; halfExtents?: number[]; radius?: number;
  weight?: number; blendDistance?: number; priority?: number; layerMask?: number; profile?: Record<string,unknown>;
}
interface Block { volumes: Volume[]; replace?: boolean }

function VolumeForm({volume, apply, remove}: {volume: Volume; apply:(v:Volume)=>void; remove:()=>void}) {
  const [shape,setShape] = useState<Shape>(volume.shape ?? 'global');
  const [error,setError] = useState('');
  const scalar = (name:string,label:string,value:number,min?:number,max?:number,step='any') =>
    <label key={name}>{label}<StudioInput name={name} type="number" required defaultValue={value} min={min} max={max} step={step}/></label>;
  function submit(e:FormEvent<HTMLFormElement>) {
    e.preventDefault();
    const data = new FormData(e.currentTarget);
    const n = (name:string) => Number(data.get(name));
    const xyz = (name:string) => ['X','Y','Z'].map(axis=>n(name+axis));
    try {
      const profile = JSON.parse(String(data.get('profile')));
      if (!profile || typeof profile !== 'object' || Array.isArray(profile)) throw Error('Enter an effect profile object.');
      const next:Volume = {...volume, shape, enabled:data.has('enabled'), weight:n('weight'), priority:n('priority'),
        blendDistance:n('blendDistance'), layerMask:n('layerMask'), profile};
      const id = String(data.get('id')).trim();
      if (id) next.id=id; else delete next.id;
      if (shape!=='global') next.center=xyz('center');
      if (shape==='sphere') next.radius=n('radius');
      if (shape==='box') next.halfExtents=xyz('halfExtents');
      setError(''); apply(next);
    } catch (error) { setError(String(error)); }
  }
  return <form onSubmit={submit}>
    <div className="pfx-volume-grid">
      <label>Volume ID<StudioInput name="id" defaultValue={volume.id} maxLength={96}/></label>
      <label>Shape<StudioSelect name="shape" value={shape} onChange={e=>setShape(e.target.value as Shape)}>
        <option value="global">Global</option><option value="sphere">Sphere</option><option value="box">Box</option>
      </StudioSelect></label>
      {shape!=='global' && ['X','Y','Z'].map((a,i)=>scalar('center'+a,'Center '+a,volume.center?.[i]??0))}
      {shape==='sphere' && scalar('radius','Radius',volume.radius??1,0.00001,100000)}
      {shape==='box' && ['X','Y','Z'].map((a,i)=>scalar('halfExtents'+a,'Half extent '+a,volume.halfExtents?.[i]??1,0.00001))}
      {scalar('weight','Weight',volume.weight??1,0,1)}
      {scalar('blendDistance','Blend distance',volume.blendDistance??0,0,100000)}
      {scalar('priority','Priority',volume.priority??0,-100000,100000,'1')}
      {scalar('layerMask','Layer mask',volume.layerMask??4294967295,0,4294967295,'1')}
    </div>
    <label className="ptp__check"><input name="enabled" type="checkbox" defaultChecked={volume.enabled!==false}/>Enabled</label>
    <details><summary>Effect profile</summary><textarea className="au-input" name="profile" aria-label="Effect profile JSON" rows={6}
      defaultValue={JSON.stringify(volume.profile??{effects:[]},null,2)}/></details>
    {error && <p role="alert">{error}</p>}
    <StudioButton type="submit" variant="primary">Apply volume</StudioButton> <StudioButton onClick={remove}>Remove volume</StudioButton>
  </form>;
}

export default function PrismaVolumeEditor({scope,stored,save}: {
  scope:'map'|'game'; stored:string; save:(block:Block,expected:string)=>Promise<void>;
}) {
  const [selected,setSelected]=useState(0);
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  const block:Block=JSON.parse(stored??'null')??{volumes:[]};
  const volumes = block.volumes??[];
  const index=Math.min(selected,volumes.length-1);
  async function commit(next:Block,selection=index) {
    setBusy(true);setError('');
    try { await save(next,stored);setSelected(Math.max(0,selection)); }
    catch(e) {setError(e instanceof Error?e.message:String(e));}
    finally {setBusy(false);}
  }
  if (!Array.isArray(volumes) || volumes.some(v=>!v || typeof v!=='object'))
    return <p role="alert">Invalid camera volume list in the {scope} document.</p>;
  function add() {
    let n=volumes.length+1;
    while(volumes.some(v=>v.id===scope+'_volume_'+n)) ++n;
    const volume:Volume={id:scope+'_volume_'+n,shape:'sphere',center:[0,0,0],radius:5,profile:{effects:[]}};
    void commit({...block,volumes:[...volumes,volume]},volumes.length);
  }
  return <section className="pfx-volumes"><fieldset disabled={busy}><legend>Camera volumes</legend>
    {scope==='map' && <label className="ptp__check"><input type="checkbox" checked={!!block.replace}
      onChange={e=>void commit({...block,volumes,replace:e.target.checked})}/>Replace game volumes on this map</label>}
    <div className="pfx-volume-nav">
      {volumes.length>0 && <StudioSelect aria-label="Camera volume" value={index} onChange={e=>setSelected(Number(e.target.value))}>
        {volumes.map((v,i)=><option key={i} value={i}>{v.id??scope+':'+i}</option>)}</StudioSelect>}
      <StudioButton onClick={add}>Add volume</StudioButton>
    </div>
    {index>=0 && <VolumeForm key={JSON.stringify([stored,index])} volume={volumes[index]}
      apply={v=>void commit({...block,volumes:volumes.map((old,i)=>i===index?v:old)})}
      remove={()=>void commit({...block,volumes:volumes.filter((_,i)=>i!==index)},index-1)}/>}
    {error && <p role="alert">{error}</p>}
  </fieldset></section>;
}
