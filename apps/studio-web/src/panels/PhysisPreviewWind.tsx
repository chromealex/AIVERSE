import {useState} from 'react';
import {StudioButton} from '../ui';

export default function PhysisPreviewWind({wind,apply}:{
  wind:{speed:number;direction:number};
  apply:(speed:number,direction:number)=>void;
}){
  const [speed,setSpeed]=useState(wind.speed);
  const [direction,setDirection]=useState(wind.direction);
  return <form aria-label="Prevailing wind" onSubmit={event=>{event.preventDefault();apply(speed,direction);}}>
    <h4>Prevailing wind</h4>
    <div className="physis-controls">
      <label>Speed · {speed.toFixed(1)} m/s
        <input name="windSpeed" type="range" min="0" max="30" step="0.1" value={speed}
          onChange={event=>setSpeed(event.currentTarget.valueAsNumber)}/>
      </label>
      <label>Direction · {direction}°
        <input name="windDirection" type="range" min="-180" max="180" step="1" value={direction}
          onChange={event=>setDirection(event.currentTarget.valueAsNumber)}/>
      </label>
    </div>
    <StudioButton type="submit" disabled={speed===wind.speed&&direction===wind.direction}>Apply wind</StudioButton>
  </form>;
}
