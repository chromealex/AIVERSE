import { useState } from 'react';
import { invoke } from '../bridge';
import type { StudioBuildInfo, StudioSnapshot } from '../types';
import { StudioButton, StudioCheckbox, StudioField, StudioInput, StudioSelect } from '../ui';

const presets=['1280x720','1600x900','1920x1080','1024x768','960x540'];

type Value = string | boolean | number;
type Props = {
  info: StudioBuildInfo; project: string; target: string; disabled: boolean;
  saved: (snapshot:StudioSnapshot)=>Promise<void>; failed: (message:string)=>void;
};

export default function BuildGameWindow(props: Props) {
  const [perPlatform,setPerPlatform]=useState(false);
  const {target,info}=props;
  const desktop=['windows','linux','macos'].includes(target);
  return <details className="build__card build__game-window"><summary>Game window</summary>
    {desktop ? <>
      <StudioField label="Apply to"><StudioSelect aria-label="Window settings scope" value={perPlatform?'target':'desktop'}
        onChange={e=>setPerPlatform(e.target.value==='target')}>
        <option value="desktop">All desktops</option><option value="target">This platform ({target})</option>
      </StudioSelect></StudioField>
      <WindowForm key={JSON.stringify([props.project,target,perPlatform,info.windowRevision])}
        {...props} scope={perPlatform?target:'desktop'}/>
    </> : <p>{target==='web'?'The game fills the browser page.':'The game runs fullscreen on mobile devices.'}</p>}
  </details>;
}

function WindowForm({info,project,scope,disabled,saved,failed}: Props & {scope:string}) {
  const base=info.gameWindow??{};
  const override=scope==='desktop'?null:base[scope];
  const [changes,setChanges]=useState<Record<string,Value>>({});
  const [working,setWorking]=useState(false);
  const values={...base,...(typeof override==='object'?override:{}),...changes};
  const set=(key:string,value:Value)=>setChanges(previous=>({...previous,[key]:value}));
  const mode=String(values.mode??'windowed');
  const width=Number(values.width??1280), height=Number(values.height??720);
  const fillsScreen=mode==='fullscreen'||mode==='maximized';
  const previewWidth=fillsScreen?192:Math.max(0,Math.min(width/10||0,192));
  const previewHeight=fillsScreen?(mode==='fullscreen'?108:103):Math.max(0,Math.min(height/10||0,108));
  const locked=disabled||working||info.windowRevision===undefined;
  const submit=async(reset:boolean)=>{
    if(locked)return;
    setWorking(true);
    try {
      const next=await invoke('studio.buildGameWindow',{project,scope,revision:info.windowRevision!,changes,reset});
      await saved(next);setChanges({});
    } catch(e){failed(e instanceof Error?e.message:String(e));}
    finally{setWorking(false);}
  };
  const dimension=(key:string,label:string,value:number,min:number)=><StudioField label={label}>
    <StudioInput type="number" aria-label={label} min={min} max={16384} step={1}
      value={Number.isFinite(value)?value:''} onChange={e=>set(key,e.currentTarget.valueAsNumber)}/>
  </StudioField>;
  return <fieldset className="build__window-form" disabled={locked}>
    <StudioField label="Window mode"><StudioSelect aria-label="Window mode" value={mode} onChange={e=>set('mode',e.target.value)}>
      {['windowed','fixed','maximized','fullscreen'].map(id=><option key={id} value={id}>{id}</option>)}
    </StudioSelect></StudioField>
    <StudioField label="Size preset"><StudioSelect aria-label="Size preset" value={width+'x'+height} onChange={e=>{
      const [w,h]=e.target.value.split('x').map(Number);setChanges(old=>({...old,width:w,height:h}));
    }}>
      {!presets.includes(width+'x'+height)&&<option value={width+'x'+height}>Custom</option>}
      {presets.map(size=><option key={size} value={size}>{size.replace('x',' × ')}</option>)}
    </StudioSelect></StudioField>
    {dimension('width','Width',width,320)}{dimension('height','Height',height,240)}
    {mode!=='fixed'&&<>
      <StudioCheckbox label="Resizable" checked={values.resizable!==false} onChange={e=>set('resizable',e.currentTarget.checked)}/>
      <StudioCheckbox label="Keep aspect ratio" checked={values.keepAspect===true} onChange={e=>set('keepAspect',e.currentTarget.checked)}/>
      {dimension('minWidth','Minimum width',Number(values.minWidth??0),0)}
      {dimension('minHeight','Minimum height',Number(values.minHeight??0),0)}
    </>}
    <StudioCheckbox label="Allow fullscreen" checked={values.allowFullscreen!==false} onChange={e=>set('allowFullscreen',e.currentTarget.checked)}/>
    <figure className="build__window-preview">
      <svg viewBox="0 0 192 108" role="img" aria-label="Game window on a 1920 by 1080 display">
        <rect width="192" height="108" fill="var(--au-canvas)"/>
        <rect x={(192-previewWidth)/2} y={fillsScreen?0:(108-previewHeight)/2} width={previewWidth} height={previewHeight}
          fill="var(--au-selection,var(--au-panel-raised))" stroke="var(--au-accent)"/>
      </svg>
      <figcaption>1920 × 1080 display preview</figcaption>
    </figure>
    <div className="au-row"><StudioButton size="sm" disabled={!Object.keys(changes).length} onClick={()=>void submit(false)}>Apply window settings</StudioButton>
      {override!=null&&<StudioButton size="sm" onClick={()=>void submit(true)}>Use desktop defaults</StudioButton>}</div>
  </fieldset>;
}
