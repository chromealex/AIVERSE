import { useState } from 'react';
import type { StudioBuildingPreview } from '../../types';

interface Props {
  preview: StudioBuildingPreview;
  onSelectFeature: (index:number)=>void;
}

// A topology/placement inspector, not a fake 3D render. Socket positions
// come from native generateFunctionalBuilding() geometry, not inferred from
// the React form. Native Studio renders the actual model thumbnail/scene.
export default function BuildingPlanPreview({preview,onSelectFeature}:Props) {
  const [selected,setSelected]=useState('');
  const sockets=preview.sockets??[];
  const width=Math.max(preview.width,1),depth=Math.max(preview.depth,1);
  const margin=Math.max(2,Math.max(width,depth)*.08);
  const sx=-margin,sz=-margin;
  const planWidth=width+margin*2,planDepth=depth+margin*2;
  const selectedSocket=sockets.find(s=>s.socketId===selected);
  return <div className="abg__plan">
    <svg role="img" aria-label="Generated building footprint, rooms and actual opening anchors"
      viewBox={`${sx} ${sz} ${planWidth} ${planDepth}`}
      preserveAspectRatio="xMidYMid meet">
      <rect x={0} y={0} width={width} height={depth}
        fill="var(--au-panel-raised)" stroke="var(--au-text)" strokeWidth={.12}/>
      {Array.from({length:Math.max(0,Math.min(5,(preview as StudioBuildingPreview & {roomsX?:number}).roomsX??1)-1)},(_,i)=>
        <line key={'x'+i} x1={width*(i+1)/((preview as StudioBuildingPreview & {roomsX?:number}).roomsX??1)}
          x2={width*(i+1)/((preview as StudioBuildingPreview & {roomsX?:number}).roomsX??1)}
          y1={0} y2={depth} stroke="var(--au-line)" strokeDasharray=".35 .25" strokeWidth={.07}/>)}
      {sockets.map(socket=>{
        const [x,,z]=socket.outside;
        const isSelected=socket.socketId===selected;
        const radius=Math.max(.27,Math.min(1.5,Math.min(width,depth)*.035));
        return <g key={socket.socketId}>
          <circle cx={x} cy={z} r={radius} role="button" tabIndex={0}
            aria-label={`${socket.kind} ${socket.socketId}`}
            stroke={isSelected?'var(--au-accent)':'var(--au-text)'}
            strokeWidth={isSelected ? .17 : .09}
            fill={isSelected?'var(--au-accent)':'var(--au-panel-raised)'}
            onClick={()=>{setSelected(socket.socketId);onSelectFeature(socket.featureIndex);}}
            onKeyDown={e=>{
              if(e.key==='Enter'||e.key===' '){
                e.preventDefault();setSelected(socket.socketId);
                onSelectFeature(socket.featureIndex);
              }
            }}/>
        </g>;
      })}
    </svg>
    <div className="abg__plan-caption">
      <strong>Native structure plan</strong>
      <small>{preview.width.toFixed(1)} × {preview.depth.toFixed(1)} m · {sockets.length} sockets · {preview.solids??0} solids · {preview.connections??0} graph links</small>
      {selectedSocket&&<small>Selected: {selectedSocket.socketId} ({selectedSocket.kind}) · opening {selectedSocket.width.toFixed(1)}m, {Math.round(selectedSocket.open*100)}% initial open</small>}
      <small>Click an opening marker to edit its functional property. This is a top-down inspector; the real textured 3D asset appears in the native model thumbnail and Scene viewport after generation.</small>
    </div>
  </div>;
}
