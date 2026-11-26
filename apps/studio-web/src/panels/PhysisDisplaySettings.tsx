import {useState} from 'react';
import type {StudioPhysisFrame} from '../types';
import {StudioButton,StudioSelect} from '../ui';

export default function PhysisDisplaySettings({settings,apply}:{
  settings:NonNullable<StudioPhysisFrame['presentation']>;
  apply:(quality:number,volume:number)=>void;
}){
  const [quality,setQuality]=useState(settings.quality);
  const [volume,setVolume]=useState(settings.volume);
  if(!settings.grass&&!settings.sound)return null;
  return <form aria-label="Nature display settings" className="physis-controls"
    onSubmit={event=>{event.preventDefault();apply(quality,volume);}}>
    {settings.grass&&<label>Grass quality<StudioSelect name="grassQuality" value={quality}
      onChange={event=>setQuality(Number(event.target.value))}>
      {['Low','Medium','High'].map((label,index)=><option key={label} value={index}>{label}</option>)}
    </StudioSelect></label>}
    {settings.sound&&<label>Volume · {Math.round(volume*100)}%
      <input type="range" min="0" max="100" step="1" name="previewVolume" value={Math.round(volume*100)}
        onChange={event=>setVolume(event.currentTarget.valueAsNumber/100)}/>
    </label>}
    <StudioButton type="submit" disabled={quality===settings.quality&&volume===settings.volume}>Apply settings</StudioButton>
  </form>;
}
