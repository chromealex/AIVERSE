import {useEffect,useRef,useState} from 'react';
import type {StudioPhysisFrame as Frame} from '../types';
import {invoke,onNativeEvent} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSelect} from '../ui';
import SpectraDockViewport from './SpectraDockViewport';
import PhysisPreviewGrid from './PhysisPreviewGrid';
import PhysisPreviewWind from './PhysisPreviewWind';
import PhysisCellReading from './PhysisCellReading';
import './PhysisDiagnosticsPanel.css';

const layers=['Temperature (°C)','Relative humidity','Pressure (hPa)','Rain','Snow','Surface wetness','Snowpack (m)','Surface ice','Wind speed (m/s)','Vertical wind (m/s)','Gravity (m/s²)','Fire','Quake','Local flood (m)'];
const initial={layer:0,palette:0,size:32,stride:1,centerX:0,centerZ:0};
type View=typeof initial;
const ringColor:Record<string,string>={Wind:'#83efff',Tornado:'#ffcb69',Hurricane:'#ffcb69',Gravity:'#e0aaff'};

function FieldMap({frame,contours,label,cell,pick}:{frame:Frame;contours:boolean;label:string;cell?:number;pick:(index:number)=>void}){
  const canvas=useRef<HTMLCanvasElement>(null);
  useEffect(()=>{
    const context=canvas.current?.getContext('2d');
    if(!context)return;
    const pixels=context.createImageData(frame.width,frame.height);
    frame.colors.forEach((rgb,i)=>pixels.data.set([rgb>>16,(rgb>>8)&255,rgb&255,frame.values[i]===null?0:255],i*4));
    context.putImageData(pixels,0,0);
  },[frame]);
  const valid=frame.values.filter((v):v is number=>v!==null);
  const coord=(i:number)=>[i%frame.width,Math.floor(i/frame.width)];
  const [x,z]=coord(cell??0);
  return <>
    <div className="physis-map" tabIndex={0} role="group" aria-label={label+' map. Use arrow keys to inspect cells.'}
      onClick={e=>{e.currentTarget.focus();const rect=e.currentTarget.getBoundingClientRect();
        const column=Math.min(frame.width-1,Math.floor((e.clientX-rect.left)*frame.width/rect.width));
        const row=Math.min(frame.height-1,Math.floor((e.clientY-rect.top)*frame.height/rect.height));
        pick(row*frame.width+column);}}
      onKeyDown={e=>{
        const delta:Record<string,number[]>={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1]};
        if(!delta[e.key])return;
        e.preventDefault();const [dx,dz]=delta[e.key];
        pick(Math.max(0,Math.min(frame.height-1,z+dz))*frame.width+Math.max(0,Math.min(frame.width-1,x+dx)));
      }}>
      <canvas ref={canvas} width={frame.width} height={frame.height}/>
      <svg viewBox={'0 0 '+frame.width+' '+frame.height} aria-hidden="true">
        {contours&&frame.rings.map((r,i)=><circle key={i} cx={r.x} cy={r.z} r={r.radius} fill="none" stroke={ringColor[r.kind]} strokeWidth="2" vectorEffect="non-scaling-stroke"/>)}
        {cell!==undefined&&<rect x={x} y={z} width="1" height="1" fill="none" stroke="white" strokeWidth="2" vectorEffect="non-scaling-stroke"/>}
      </svg>
    </div>
    {contours&&<div className="physis-legend">{frame.rings.map(r=>r.kind).filter((v,i,a)=>a.indexOf(v)===i).map(k=><span key={k}><i style={{borderColor:ringColor[k]}}/>{k}</span>)}</div>}
    <p>{frame.available?label+' · '+Math.min(...valid).toFixed(3)+' to '+Math.max(...valid).toFixed(3):'No PHYSIS cells in this area.'}</p>
  </>;
}

export default function PhysisDiagnosticsPanel({projectPath,epoch}:{projectPath:string;epoch:string}){
  const [source,setSource]=useState('demo');
  return <section className="physis-diagnostics"><label className="physis-source">Field source<StudioSelect value={source} onChange={e=>setSource(e.target.value)}>
    <option value="demo">Isolated demo</option><option value="map">Current map preview</option><option value="game">Local play-test</option>
  </StudioSelect></label>
  <Diagnostics key={projectPath+source} projectPath={projectPath} source={source} epoch={epoch}/></section>;
}

function Diagnostics({projectPath,source,epoch}:{projectPath:string;source:string;epoch:string}){
  const live=source==='game', map=source==='map';
  const [view,setView]=useState(initial),[frame,setFrame]=useState<Frame>();
  const [busy,setBusy]=useState(true),[error,setError]=useState(''),[contours,showContours]=useState(true);
  const [cell,pick]=useState<number>();
  const [showNature,setShowNature]=useState(false);
  const requestId=useRef(0), pending=useRef(false);
  const [playing,setPlaying]=useState(false),[speed,setSpeed]=useState(1);
  const workspace=useRef<HTMLFieldSetElement>(null);
  async function request(action:'view'|'step'|'step60'|'reset'|'ignite'|'wind'|'playback'|'grid'|'presentation',next:View,selection=cell,options?:{windSpeed:number;windDirection:number}|{speed:number;elapsed:number}|{cellMeters:number;humidity:number}|{igniteOnClick?:boolean;grass?:boolean;flames?:boolean;sound?:boolean}){
    const id=++requestId.current;
    const sameArea=next.size===view.size&&next.stride===view.stride&&next.centerX===view.centerX&&next.centerZ===view.centerZ;
    const rebuild=action==='reset'||action==='grid';
    const selected=sameArea&&!rebuild?selection:undefined;
    const coords=frame&&selected!==undefined?{cellX:frame.originX+(selected%frame.width)*frame.stride,cellZ:frame.originZ+Math.floor(selected/frame.width)*frame.stride}:{};
    pending.current=true;
    if(rebuild)setPlaying(false);
    if(action!=='playback')setBusy(true);
    setError('');
    try{
      const result=map?await invoke('studio.physisNature',{...next,...coords,...options,action,expectedProject:projectPath,expectedDocumentEpoch:epoch}):live?await invoke('studio.physisLive',{...next,...coords,expectedProject:projectPath}):
        (await invoke('studio.op',{op:'physis_demo',params:{action,...next},expectedProject:projectPath})).result as Frame|{active:false};
      if(id!==requestId.current)return;
      pick(result.active?selected:undefined);
      setFrame(result.active?result:undefined);setView(next);
    }catch(e){if(id===requestId.current){setPlaying(false);setError(String(e));}}
    finally{if(id===requestId.current){pending.current=false;setBusy(false);}}
  }
  useEffect(()=>{void request('view',initial);return ()=>{requestId.current++;};},[projectPath]);
  useEffect(()=>onNativeEvent((event,payload)=>{
    if(event==='nature.ignition'&&showNature)setError(String((payload as {message?:string})?.message??''));
    if(event==='nature.previewError'&&showNature){
      setShowNature(false);setError(String((payload as {message?:string})?.message??payload));
    }
  }),[showNature]);
  const advance=useRef<(elapsed:number)=>void>(()=>{});
  useEffect(()=>{
    advance.current=elapsed=>{if(!pending.current)void request('playback',view,cell,{speed,elapsed});};
  });
  useEffect(()=>{
    if(!playing)return;
    let previous=performance.now();
    const timer=window.setInterval(()=>{
      if(document.hidden || !workspace.current?.getClientRects().length){setPlaying(false);return;}
      const now=performance.now();
      advance.current(Math.min(0.25,(now-previous)/1000));
      previous=now;
    },100);
    const visibility=()=>{if(document.hidden)setPlaying(false);};
    document.addEventListener('visibilitychange',visibility);
    return ()=>{clearInterval(timer);document.removeEventListener('visibilitychange',visibility);};
  },[playing]);
  function change(key:keyof View,value:number){void request('view',{...view,[key]:value});}
  return <>
    <header><h3>Field diagnostics</h3>{frame&&<output>Tick {frame.tick}</output>}</header>
    <fieldset ref={workspace} disabled={busy} className={frame?'physis-workspace':undefined}>
      <div className="au-row">{live?<StudioButton onClick={()=>void request('view',view)}>Refresh play-test</StudioButton>:<><StudioButton onClick={()=>void request('reset',{...view,centerX:0,centerZ:0})}>{map?(frame?'Reset map preview':'Create map preview'):(frame?'Reset demo':'Create demo')}</StudioButton>
        {frame&&<>{map&&<StudioButton onClick={()=>setPlaying(!playing)}>{playing?'Pause preview':'Play preview'}</StudioButton>}
          <StudioButton disabled={playing} onClick={()=>void request('step',view)}>Step 1 tick</StudioButton><StudioButton disabled={playing} onClick={()=>void request('step60',view)}>Step 60 ticks</StudioButton>{map&&<StudioButton disabled={!frame.cell?.available} onClick={()=>void request('ignite',view)}>Ignite selected cell</StudioButton>}</>}</>}
      </div>
      {!frame&&<p>{live?'Start a local play-test with PHYSIS enabled, then refresh.':map?'Preview the open map’s weather and fire. Changes stay in this preview.':'Create an isolated environment to inspect its weather and force fields.'}</p>}
      {frame&&<>
        <div className="physis-controls physis-layers"><label>Field<StudioSelect value={view.layer} onChange={e=>change('layer',Number(e.target.value))}>{layers.map((l,i)=><option key={l} value={i}>{l}</option>)}</StudioSelect></label>
          <label>Palette<StudioSelect value={view.palette} onChange={e=>change('palette',Number(e.target.value))}>{['Thermal','High contrast','Ocean'].map((l,i)=><option key={l} value={i}>{l}</option>)}</StudioSelect></label>
        </div>
        <div className="physis-figure">{map&&<div className="physis-view-switch">
          <StudioButton aria-pressed={!showNature} onClick={()=>setShowNature(false)}>Field map</StudioButton>
          <StudioButton aria-pressed={showNature} onClick={()=>setShowNature(true)}>3D nature</StudioButton>
        </div>}{map&&showNature?<><SpectraDockViewport id="nature" label="3D nature preview"/>
          {frame.presentation&&<div className="physis-display-options">
            {([['igniteOnClick','Ignite on click'],['grass','Grass'],['flames','Fire and smoke'],['sound','Sound']] as const).map(([key,label])=>
              <label key={key}><input type="checkbox" checked={frame.presentation![key]}
                onChange={event=>void request('presentation',view,cell,{[key]:event.target.checked})}/>{label}</label>)}
          </div>}<p>Drag to orbit · Scroll to zoom</p></>:<FieldMap key={[view.centerX,view.centerZ,view.size,view.stride].join('/')} frame={frame} cell={cell} pick={live||map?index=>void request('view',view,index):pick} contours={contours} label={layers[view.layer]}/>}</div>
        <div className="physis-options">{map&&<label className="physis-playback-speed">Playback speed · {speed.toFixed(1)}×
          <input type="range" name="playbackSpeed" min="0.1" max="6" step="0.1" value={speed}
            onChange={event=>setSpeed(event.currentTarget.valueAsNumber)}/>
        </label>}<label className="physis-toggle"><input type="checkbox" checked={contours} onChange={e=>showContours(e.target.checked)}/>Volume contours</label>
        <details><summary>Map area</summary><form className="physis-controls" key={[view.size,view.stride,view.centerX,view.centerZ].join('/')} onSubmit={e=>{
          e.preventDefault();const data=new FormData(e.currentTarget);
          void request('view',{...view,size:Number(data.get('size')),stride:Number(data.get('stride')),centerX:Number(data.get('centerX')),centerZ:Number(data.get('centerZ'))});
        }}>
          {([['size','Width (cells)',8,64],['stride','Sample stride',1,8],['centerX','Center X',-1000000,1000000],['centerZ','Center Z',-1000000,1000000]] as const).map(([k,label,min,max])=><label key={k}>{label}
            <StudioInput type="number" name={k} defaultValue={view[k]} min={min} max={max} step={1} required/></label>)}
          <StudioButton type="submit">Apply area</StudioButton>
        </form></details>
        {map&&frame.previewGrid&&<PhysisPreviewGrid key={frame.previewGrid.requestedMeters+'/'+frame.previewGrid.humidity}
          grid={frame.previewGrid} apply={(cellMeters,humidity)=>void request('grid',view,undefined,{cellMeters,humidity})}/>}
        {map&&frame.previewWind&&<PhysisPreviewWind key={frame.previewWind.speed+'/'+frame.previewWind.direction}
          wind={frame.previewWind} apply={(windSpeed,windDirection)=>void request('wind',view,cell,{windSpeed,windDirection})}/>}
        {cell!==undefined&&<PhysisCellReading key={[frame.tick,cell,frame.originX,frame.originZ,frame.stride].join('/')}
          captured={live||map?frame.cell:undefined} projectPath={projectPath} x={frame.originX+(cell%frame.width)*frame.stride} z={frame.originZ+Math.floor(cell/frame.width)*frame.stride}
          tick={frame.tick} label={layers[view.layer]} value={frame.values[cell]} refresh={()=>void request('view',view)}/>}
        </div>
      </>}
    </fieldset>
    {error&&<StudioAlert tone="warning">{error}</StudioAlert>}
  </>;
}
