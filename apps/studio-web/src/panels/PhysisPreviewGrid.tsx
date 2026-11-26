import {useState} from 'react';
import type {StudioPhysisGrid} from '../types';
import {StudioButton,StudioSelect} from '../ui';

export default function PhysisPreviewGrid({grid,apply}:{
  grid:StudioPhysisGrid;
  apply:(meters:number,humidity:number)=>void;
}){
  const [meters,setMeters]=useState(grid.requestedMeters);
  const [humidity,setHumidity]=useState(grid.humidity);
  return <form aria-label="Simulation grid" onSubmit={event=>{event.preventDefault();apply(meters,humidity);}}>
    <h4>Simulation grid</h4>
    <p>{grid.width} × {grid.height} cells · {grid.meters} m per cell · {Math.round(grid.baseHumidity*100)}% initial humidity</p>
    <div className="physis-controls">
      <label>Cell size<StudioSelect name="cellMeters" value={meters} onChange={event=>setMeters(Number(event.target.value))}>
        {grid.mapAvailable&&<option value={0}>Use map grid and climate</option>}
        {Array.from({length:16},(_,i)=><option key={i+1} value={i+1}>{i+1} m</option>)}
      </StudioSelect></label>
      {meters!==0&&<label>Initial humidity · {Math.round(humidity*100)}%
        <input type="range" name="humidity" min="0" max="100" step="1" value={Math.round(humidity*100)}
          onChange={event=>setHumidity(event.currentTarget.valueAsNumber/100)}/>
      </label>}
    </div>
    <StudioButton type="submit" disabled={meters===grid.requestedMeters&&humidity===grid.humidity}>Rebuild preview</StudioButton>
  </form>;
}
