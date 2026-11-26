import {useState} from 'react';
import type {StudioPhysisGrid} from '../types';
import {StudioButton,StudioSelect} from '../ui';

export default function PhysisPreviewGrid({grid,apply}:{
  grid:StudioPhysisGrid;
  apply:(meters:number,humidity:number,shoreSand:number)=>void;
}){
  const [meters,setMeters]=useState(grid.requestedMeters);
  const [humidity,setHumidity]=useState(grid.humidity);
  const [shoreSand,setShoreSand]=useState(grid.shoreSand);
  return <form aria-label="Simulation grid" onSubmit={event=>{event.preventDefault();apply(meters,humidity,shoreSand);}}>
    <h4>Simulation grid</h4>
    <p>{grid.width} × {grid.height} cells · {grid.meters} m per cell · {Math.round(grid.baseHumidity*100)}% initial humidity</p>
    <div className="physis-controls">
      <label>Cell size<StudioSelect name="cellMeters" value={meters} onChange={event=>setMeters(Number(event.target.value))}>
        {grid.mapAvailable&&<option value={0}>Use map grid and climate</option>}
        {Array.from({length:64},(_,i)=><option key={i+1} value={i+1}>{i+1} m</option>)}
      </StudioSelect></label>
      {meters!==0&&<label>Initial humidity · {Math.round(humidity*100)}%
        <input type="range" name="humidity" min="0" max="100" step="1" value={Math.round(humidity*100)}
          onChange={event=>setHumidity(event.currentTarget.valueAsNumber/100)}/>
      </label>}
      <label>Bare shore · {shoreSand} m
        <input type="range" name="shoreSand" min="0" max="40" step="0.1" value={shoreSand}
          onChange={event=>setShoreSand(event.currentTarget.valueAsNumber)}/>
      </label>
    </div>
    <StudioButton type="submit" disabled={meters===grid.requestedMeters&&humidity===grid.humidity&&shoreSand===grid.shoreSand}>Rebuild preview</StudioButton>
  </form>;
}
