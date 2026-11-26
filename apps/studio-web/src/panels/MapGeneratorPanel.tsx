import {useEffect,useState} from 'react';
import {invoke} from '../bridge';
import type {StudioGraphDocumentScope} from '../types';
import {StudioAlert,StudioButton,StudioCheckbox,StudioInput,StudioSection,StudioSelect,StudioTextarea} from '../ui';

interface Preset {id:string; title:string; params:Record<string,unknown>}
interface Catalog {presets:Preset[]; matchStart?:{seed:string; preset:string}}
const maxSeed=9223372036854775807n;

export default function MapGeneratorPanel({scope,onChanged}:{scope:StudioGraphDocumentScope;onChanged:()=>void}) {
  const [catalog,setCatalog]=useState<Catalog>();
  const [loadedRevision,setLoadedRevision]=useState('');
  const [id,setId]=useState('');
  const [seed,setSeed]=useState('1');
  const [players,setPlayers]=useState(2);
  const [symmetry,setSymmetry]=useState('auto');
  const [useTemplate,setTemplate]=useState(true),[keepSize,setKeepSize]=useState(false);
  const [layout,setLayout]=useState('shared');
  const [overrides,setOverrides]=useState('');
  const [busy,setBusy]=useState(false),[error,setError]=useState(''),[result,setResult]=useState('');
  const {projectPath,epoch,revision}=scope;
  useEffect(()=>{
    let active=true;
    void invoke('studio.op',{op:'gen_presets',params:{},expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:revision})
      .then(r=>{if(active){setCatalog(r.result as Catalog);setLoadedRevision(revision);}})
      .catch(e=>{if(active)setError(String(e));});
    return ()=>{active=false;};
  },[projectPath,epoch,revision]);
  const preset=catalog?.presets.find(p=>p.id===id) ?? catalog?.presets[0];
  const disabled=busy || loadedRevision!==revision || !preset;
  async function run(mode:'bake'|'next'|'match'|'fixed'|'off') {
    if(disabled||!preset)return;
    setBusy(true);setError('');setResult('');
    try {
      const guard={expectedProject:projectPath,expectedDocumentEpoch:epoch,expectedDocumentRevision:revision};
      if(mode==='off') {
        await invoke('studio.op',{op:'map_generation_disable',params:{},...guard});
        setResult('Match-start generation disabled.');
      } else {
        if(!/^\d{1,19}$/.test(seed)||BigInt(seed)>maxSeed)throw Error('Enter a seed between 0 and '+maxSeed+'.');
        const next=mode==='next'?(BigInt(seed)+1n)&maxSeed:BigInt(seed);
        const params=overrides.trim()?JSON.parse(overrides):{};
        if(!params||typeof params!=='object'||Array.isArray(params))throw Error('Preset overrides must be a JSON object.');
        if(preset.params.shape==='planet')params.layout=layout;
        const reply=await invoke('studio.op',{op:'map_generate',params:{mode:mode==='next'?'bake':mode,preset:preset.id,seedText:String(next),players,symmetry,useTemplate,keepSize,params},...guard});
        setSeed(String(next));setResult((reply.result as {summary:string}).summary);
      }
      onChanged();
    }catch(e){setError(e instanceof Error?e.message:String(e));}finally{setBusy(false);}
  }
  return <div className="map-generator">
    <StudioSection title="Generate map">
      <label>Preset<StudioSelect value={preset?.id??''} onChange={e=>{setId(e.target.value);setOverrides('');}}>
        {catalog?.presets.map(p=><option key={p.id} value={p.id}>{p.title}</option>)}</StudioSelect></label>
      <label>Seed<span className="au-row"><StudioInput inputMode="numeric" value={seed} onChange={e=>setSeed(e.target.value)}/>
        <StudioButton onClick={()=>setSeed(String(crypto.getRandomValues(new BigUint64Array(1))[0]&maxSeed))}>Random</StudioButton></span></label>
      <label>Players<span className="au-row"><input type="range" min={1} max={8} value={players} onChange={e=>setPlayers(Number(e.target.value))}/><output>{players}</output></span></label>
      <label>Symmetry<StudioSelect value={symmetry} onChange={e=>setSymmetry(e.target.value)}>{['auto','none','point','mirror','mirror-y','mirror4','rot4','rotational'].map(s=><option key={s}>{s}</option>)}</StudioSelect></label>
      {preset?.params.shape==='planet' && <label>Planet layout<StudioSelect value={layout} onChange={e=>setLayout(e.target.value)}><option value="shared">Shared planet</option><option value="separate">Planet per player</option></StudioSelect></label>}
      <StudioCheckbox label="Use map objects as a template" checked={useTemplate} onChange={e=>setTemplate(e.target.checked)}/>
      <StudioCheckbox label="Keep current map size" checked={keepSize} onChange={e=>setKeepSize(e.target.checked)}/>
      <details><summary>Preset parameters</summary><label>JSON overrides<StudioTextarea rows={5} value={overrides} placeholder={JSON.stringify(preset?.params??{},null,2)} onChange={e=>setOverrides(e.target.value)}/></label></details>
      <div className="au-row"><StudioButton variant="primary" disabled={disabled} onClick={()=>void run('bake')}>Generate map</StudioButton>
        <StudioButton disabled={disabled} onClick={()=>void run('next')}>Next seed</StudioButton></div>
    </StudioSection>
    <StudioSection title="At match start">
      <p>{catalog?.matchStart ? catalog.matchStart.preset+' · '+(catalog.matchStart.seed==='match'?'Match seed':'Seed '+catalog.matchStart.seed) : 'Use the saved map.'}</p>
      <div className="au-row"><StudioButton disabled={disabled} onClick={()=>void run('match')}>Use match seed</StudioButton>
        <StudioButton disabled={disabled} onClick={()=>void run('fixed')}>Use fixed seed</StudioButton>
        {catalog?.matchStart&&<StudioButton disabled={disabled} onClick={()=>void run('off')}>Disable generation</StudioButton>}</div>
    </StudioSection>
    {error&&<StudioAlert tone="danger">{error}</StudioAlert>}{result&&<p role="status">{result}</p>}
  </div>;
}
