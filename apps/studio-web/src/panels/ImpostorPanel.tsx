import {useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert, StudioButton, StudioInput, StudioSection} from '../ui';

type Chart = {direction:string; x:number; y:number; bounds:[number,number,number,number]};
type Preview = {
  charts:Chart[]; png_base64:string; width:number; height:number; resolution:number; digest:string;
  originalArea:number; chosenArea:number; areaSaved:number; proxyTriangles:number;
};

export default function ImpostorPanel({projectPath}:{projectPath:string}) {
  const [model,setModel] = useState('');
  const [target,setTarget] = useState('');
  const [mode,setMode] = useState('box');
  const [fit,setFit] = useState('auto');
  const [view,setView] = useState(0);
  const [report,setReport] = useState<Preview|null>(null);
  const [message,setMessage] = useState('');
  const [error,setError] = useState('');
  const [busy,setBusy] = useState(false);

  function invalidate() {
    setReport(null); setMessage(''); setError(''); setView(0);
  }
  async function run(save:boolean) {
    setBusy(true); setError(''); setMessage('');
    try {
      const params = {model:model.trim(), mode, fit,
        ...(save ? {target:target.trim(), expectedDigest:report?.digest} : {})};
      const response = await invoke('studio.op', {expectedProject:projectPath,
        op:save ? 'geometry_proxy_bake' : 'geometry_proxy_analyze', params});
      const result = response.result as Preview & {error?:string; model?:string};
      if (result?.error) throw new Error(result.error);
      if (save) {
        if (!result?.model) throw new Error('The proxy was not saved');
        setMessage('Saved '+result.model);
      } else {
        if (!result?.charts?.length || !result.png_base64) throw new Error('The atlas preview is unavailable');
        setReport(result); setView(0);
      }
    } catch (e) {
      if (!save) setReport(null);
      setError(e instanceof Error ? e.message : String(e));
    } finally { setBusy(false); }
  }
  const chart = report?.charts[view];
  return <StudioSection title="Impostor geometry">
    <fieldset className="au-stack" disabled={busy}>
      <label>Source model<StudioInput value={model} placeholder="content/models/tree.glb"
        onChange={e=>{invalidate(); setModel(e.target.value); setTarget(e.target.value.replace(/\.[^./]+$/,'')+'.proxy.glb');}}/></label>
      <label>Projection <select value={mode} onChange={e=>{invalidate();setMode(e.target.value);}}>
        <option value="box">Box (six directions)</option><option value="plane">Plane (two directions)</option>
      </select></label>
      <label>Card geometry <select value={fit} onChange={e=>{invalidate();setFit(e.target.value);}}>
        <option value="auto">Auto (trim at 20% area saved)</option><option value="full">Full cards</option>
        <option value="trim">Fit to alpha mask</option>
      </select></label>
      <StudioButton disabled={!model.trim()} onClick={()=>void run(false)}>Analyze model</StudioButton>
      {report && chart && <>
        <label>View <select value={view} onChange={e=>setView(Number(e.target.value))}>
          {report.charts.map((c,i)=><option key={c.direction} value={i}>{c.direction}</option>)}
        </select></label>
        <svg role="img" aria-label={'Baked atlas and selected rectangle, '+chart.direction}
          viewBox={`${chart.x} ${chart.y} ${report.resolution} ${report.resolution}`}
          style={{width:'100%',maxHeight:220,background:'#242931'}} overflow="hidden">
          <svg x={chart.x} y={chart.y} width={report.resolution} height={report.resolution}
            viewBox={`${chart.x} ${chart.y} ${report.resolution} ${report.resolution}`} overflow="hidden">
            <image href={'data:image/png;base64,'+report.png_base64} width={report.width} height={report.height}/>
            <rect x={chart.x+chart.bounds[0]+0.5} y={chart.y+chart.bounds[1]+0.5}
              width={chart.bounds[2]-chart.bounds[0]-1} height={chart.bounds[3]-chart.bounds[1]-1}
              fill="none" stroke="#55ecc0" strokeWidth="1"/>
          </svg>
        </svg>
        <span>{chart.bounds[2]-chart.bounds[0]} × {chart.bounds[3]-chart.bounds[1]} of {report.resolution} × {report.resolution} texels in this view</span>
        <span>Total card area {report.originalArea.toLocaleString()} → {report.chosenArea.toLocaleString()} units²
          {' ('+(100*report.areaSaved).toFixed(1)+'% saved)'}</span>
        <span>{report.proxyTriangles} triangles · 1 material primitive</span>
        <label>New GLB path<StudioInput value={target} onChange={e=>{setTarget(e.target.value);setMessage('');}}/></label>
        <StudioButton disabled={!target.trim() || !!message} onClick={()=>void run(true)}>Save baked GLB</StudioButton>
      </>}
    </fieldset>
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
    {message && <span role="status">{message}</span>}
  </StudioSection>;
}
