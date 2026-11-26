import {useEffect,useRef,useState} from 'react';
import {invoke} from '../bridge';
import {StudioAlert,StudioButton,StudioInput,StudioSelect} from '../ui';
import './PhysisDiagnosticsPanel.css';

const layers=['Temperature (°C)','Relative humidity','Pressure (hPa)','Rain','Snow','Surface wetness','Snowpack (m)','Surface ice','Wind speed (m/s)','Vertical wind (m/s)','Gravity (m/s²)','Fire','Quake','Local flood (m)'];
const initial={layer:0,palette:0,size:32,stride:1,centerX:0,centerZ:0};
type View=typeof initial;
type Ring={kind:string;x:number;z:number;radius:number};
type Frame={active:true;tick:number;width:number;height:number;stride:number;originX:number;originZ:number;available:number;values:(number|null)[];colors:number[];rings:Ring[]};
const ringColor:Record<string,string>={Wind:'#83efff',Tornado:'#ffcb69',Hurricane:'#ffcb69',Gravity:'#e0aaff'};

function FieldMap({frame,contours,label}:{frame:Frame;contours:boolean;label:string}){
  const canvas=useRef<HTMLCanvasElement>(null);
  const [cell,pick]=useState<number>();
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
      onClick={e=>{const rect=e.currentTarget.getBoundingClientRect();
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
    {cell!==undefined&&<output>Cell ({frame.originX+x*frame.stride}, {frame.originZ+z*frame.stride}) · {frame.values[cell]?.toFixed(4)??'Outside grid'}</output>}
  </>;
}

export default function PhysisDiagnosticsPanel({projectPath}:{projectPath:string}){
  const [view,setView]=useState(initial),[frame,setFrame]=useState<Frame>();
  const [busy,setBusy]=useState(true),[error,setError]=useState(''),[contours,showContours]=useState(true);
  const requestId=useRef(0);
  async function request(action:string,next:View){
    const id=++requestId.current;
    setBusy(true);setError('');
    try{
      const reply=await invoke('studio.op',{op:'physis_demo',params:{action,...next},expectedProject:projectPath});
      if(id!==requestId.current)return;
      const result=reply.result as Frame|{active:false};
      setFrame(result.active?result:undefined);setView(next);
    }catch(e){if(id===requestId.current)setError(String(e));}
    finally{if(id===requestId.current)setBusy(false);}
  }
  useEffect(()=>{void request('view',initial);return ()=>{requestId.current++;};},[projectPath]);
  function change(key:keyof View,value:number){void request('view',{...view,[key]:value});}
  return <section className="physis-diagnostics">
    <header><h3>Field diagnostics</h3>{frame&&<output>Tick {frame.tick}</output>}</header>
    <fieldset disabled={busy} className={frame?'physis-workspace':undefined}>
      <div className="au-row"><StudioButton onClick={()=>void request('reset',{...view,centerX:0,centerZ:0})}>{frame?'Reset demo':'Create demo'}</StudioButton>
        {frame&&<><StudioButton onClick={()=>void request('step',view)}>Step 1 tick</StudioButton><StudioButton onClick={()=>void request('step60',view)}>Step 60 ticks</StudioButton></>}
      </div>
      {!frame&&<p>Create an isolated environment to inspect its weather and force fields.</p>}
      {frame&&<>
        <div className="physis-controls physis-layers"><label>Field<StudioSelect value={view.layer} onChange={e=>change('layer',Number(e.target.value))}>{layers.map((l,i)=><option key={l} value={i}>{l}</option>)}</StudioSelect></label>
          <label>Palette<StudioSelect value={view.palette} onChange={e=>change('palette',Number(e.target.value))}>{['Thermal','High contrast','Ocean'].map((l,i)=><option key={l} value={i}>{l}</option>)}</StudioSelect></label>
        </div>
        <div className="physis-figure"><FieldMap key={[view.centerX,view.centerZ,view.size,view.stride].join('/')} frame={frame} contours={contours} label={layers[view.layer]}/></div>
        <div className="physis-options"><label className="physis-toggle"><input type="checkbox" checked={contours} onChange={e=>showContours(e.target.checked)}/>Volume contours</label>
        <details><summary>Map area</summary><form className="physis-controls" key={[view.size,view.stride,view.centerX,view.centerZ].join('/')} onSubmit={e=>{
          e.preventDefault();const data=new FormData(e.currentTarget);
          void request('view',{...view,size:Number(data.get('size')),stride:Number(data.get('stride')),centerX:Number(data.get('centerX')),centerZ:Number(data.get('centerZ'))});
        }}>
          {([['size','Width (cells)',8,64],['stride','Sample stride',1,8],['centerX','Center X',-1000000,1000000],['centerZ','Center Z',-1000000,1000000]] as const).map(([k,label,min,max])=><label key={k}>{label}
            <StudioInput type="number" name={k} defaultValue={view[k]} min={min} max={max} step={1} required/></label>)}
          <StudioButton type="submit">Apply area</StudioButton>
        </form></details></div>
      </>}
    </fieldset>
    {error&&<StudioAlert tone="warning">{error}</StudioAlert>}
  </section>;
}
