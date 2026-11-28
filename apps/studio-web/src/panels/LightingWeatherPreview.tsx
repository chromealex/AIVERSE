import {useEffect,useState} from 'react';
import {invoke,onNativeEvent} from '../bridge';
import type {StudioPreviewWeather} from '../types';
import type {LightingState} from './lightingSchema';
import SpectraDockViewport from './SpectraDockViewport';

export default function LightingWeatherPreview({epoch,lighting,busy,apply}:{
  epoch:string;lighting:LightingState['lighting'];busy:boolean;apply:(weather:StudioPreviewWeather)=>Promise<void>
}) {
  const [open,setOpen]=useState(false);
  const [weather,setWeather]=useState<StudioPreviewWeather>({time:.5,clouds:0,storm:0});
  const [error,setError]=useState('');
  useEffect(()=>{
    if(!open)return;
    let active=true;
    const timer=window.setTimeout(()=>{
      void invoke('studio.lightingPreview',{key:'weather',value:weather,expectedDocumentEpoch:epoch})
        .then(()=>{if(active)setError('');}).catch(e=>{if(active)setError(String(e));});
    },40);
    return ()=>{active=false;window.clearTimeout(timer);};
  },[epoch,open,weather,lighting?.preset,lighting?.nightLevel]);
  useEffect(()=>onNativeEvent((event,payload)=>{
    if(event==='lighting.previewError')setError(String((payload as {message?:string})?.message??'Lighting preview unavailable'));
  }),[]);
  return <details className="lighting-weather" onToggle={event=>{
    const visible=event.currentTarget.open;
    if(visible)setWeather({time:lighting?.time??.5,clouds:lighting?.clouds??0,storm:lighting?.storm??0});
    setOpen(visible);
  }}>
    <summary>Weather preview</summary>
    {open&&<>
      <SpectraDockViewport id="lighting" label="Map lighting preview"/>
      {error&&<p role="alert">{error}</p>}
      {(['time','clouds','storm'] as const).map(key=><label key={key}>
        <span>{{time:'Time of day',clouds:'Clouds',storm:'Storm'}[key]}</span>
        <input type="range" min="0" max="1" step=".01" value={weather[key]}
          aria-label={'Preview '+key} onChange={e=>setWeather({...weather,[key]:Number(e.target.value)})}/>
        <output>{key==='time'?new Date(Math.round(weather.time*1440)*60000).toISOString().slice(11,16):Math.round(weather[key]*100)+'%'}</output>
      </label>)}
      <button className="lighting-add" disabled={busy} onClick={()=>void apply(weather)}>Apply weather to map</button>
    </>}
  </details>;
}
