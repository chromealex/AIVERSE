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
  const [floor,setFloor]=useState(0);
  const [selectedInterior,setSelectedInterior]=useState('');
  const selectedFloor=Math.min(floor,Math.max(0,(preview.floors??1)-1));
  const mask=preview.floorFootprints?.[selectedFloor]??preview.footprint;
  const sockets=(preview.sockets??[]).filter(s=>s.floor===undefined||s.floor===selectedFloor);
  const width=Math.max(preview.width,1),depth=Math.max(preview.depth,1);
  const margin=Math.max(2,Math.max(width,depth)*.08);
  const sx=-margin,sz=-margin;
  const planWidth=width+margin*2,planDepth=depth+margin*2;
  const selectedSocket=sockets.find(s=>s.socketId===selected);
  const selectedFurniture=preview.furniture?.find(item=>item.id===selectedInterior&&item.floor===selectedFloor);
  const selectedLight=preview.interiorLights?.find(item=>item.id===selectedInterior&&item.floor===selectedFloor);
  const selectedInteraction=preview.furnitureSockets?.find(item=>item.id===selectedInterior&&item.floor===selectedFloor);
  const inspect=(id:string)=>({role:'button',tabIndex:0,onClick:()=>setSelectedInterior(id),onKeyDown:(e:import('react').KeyboardEvent<SVGElement>)=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();setSelectedInterior(id);}}});
  return <div className="abg__plan">
    {(preview.floors??1)>1&&<label>Plan floor <select value={selectedFloor} onChange={e=>{setFloor(Number(e.target.value));setSelected('');setSelectedInterior('');}}>{Array.from({length:preview.floors??1},(_,i)=><option key={i} value={i}>{i}</option>)}</select></label>}
    <svg role="img" aria-label="Generated building footprint, rooms and actual opening anchors"
      viewBox={`${sx} ${sz} ${planWidth} ${planDepth}`}
      preserveAspectRatio="xMidYMid meet">
      {preview.footprintPolygon?.length?<polygon points={preview.footprintPolygon.map(p=>p.join(',')).join(' ')} fill="var(--au-panel-raised)" stroke="var(--au-text)" strokeWidth={.12}/>:mask?.length?mask.flatMap((row,z)=>[...row].map((cell,x)=>cell==='1'?
        <rect key={x+':'+z} x={width*x/(preview.roomsX??1)} y={depth*z/(preview.roomsZ??1)}
          width={width/(preview.roomsX??1)} height={depth/(preview.roomsZ??1)}
          fill="var(--au-panel-raised)" stroke="var(--au-text)" strokeWidth={.12}/>:null)):<>
      <rect x={0} y={0} width={width} height={depth}
        fill="var(--au-panel-raised)" stroke="var(--au-text)" strokeWidth={.12}/>
      {Array.from({length:Math.max(0,Math.min(5,preview.roomsX??1)-1)},(_,i)=>
        <line key={'x'+i} x1={width*(i+1)/(preview.roomsX??1)}
          x2={width*(i+1)/(preview.roomsX??1)}
          y1={0} y2={depth} stroke="var(--au-line)" strokeDasharray=".35 .25" strokeWidth={.07}/>)}
      {Array.from({length:Math.max(0,Math.min(5,(preview.roomsZ??1)-1))},(_,i)=>
        <line key={'z'+i} x1={0} x2={width}
          y1={depth*(i+1)/(preview.roomsZ??1)}
          y2={depth*(i+1)/(preview.roomsZ??1)}
          stroke="var(--au-line)" strokeDasharray=".35 .25" strokeWidth={.07}/>)}</>}
      {(preview.terraces??[]).filter(t=>t.floor===selectedFloor).map(t=>
        <rect key={'deck-'+t.x+':'+t.z} x={width*t.x/(preview.roomsX??1)} y={depth*t.z/(preview.roomsZ??1)}
          width={width/(preview.roomsX??1)} height={depth/(preview.roomsZ??1)}
          fill="var(--au-accent)" fillOpacity={.16} stroke="var(--au-accent)" strokeWidth={.1} strokeDasharray=".25 .18"/>)}
      {(preview.balconies??[]).filter(t=>t.floor===selectedFloor).map(t=>
        <rect key={'balcony-'+t.socketId} x={t.x0} y={t.z0} width={t.x1-t.x0} height={t.z1-t.z0}
          fill="var(--au-accent)" fillOpacity={.28} stroke="var(--au-accent)" strokeWidth={.1}/>)}
      {selectedFloor===(preview.floors??1)-1&&(preview.roofSections??[]).map((r,i)=>
        <g key={'roof-'+i} pointerEvents="none">
          <rect x={r.x0} y={r.z0} width={r.x1-r.x0} height={r.z1-r.z0} fill="none" stroke="var(--au-accent)" strokeDasharray=".3 .2" strokeWidth={.13}/>
        </g>)}
      {(preview.furniture??[]).filter(item=>item.floor===selectedFloor).map(item=>
        <rect key={item.id} {...inspect(item.id)} aria-label={item.kind+" "+item.id} x={item.x0} y={item.z0} width={item.x1-item.x0} height={item.z1-item.z0}
          fill="var(--au-accent)" fillOpacity={selectedInterior===item.id ? .9 : .55} stroke="var(--au-text)" strokeWidth={.06}><title>{item.kind} · {item.roomId}</title></rect>)}
      {(preview.interiorLights??[]).filter(i=>i.floor===selectedFloor).map(i=><circle key={i.id} {...inspect(i.id)} aria-label={i.id} cx={i.x} cy={i.z} r={selectedInterior===i.id ? .38 : .25} fill="#e8bd65" stroke="var(--au-text)" strokeWidth={.06}><title>{i.id} · {i.intensity} cd</title></circle>)}
      {(preview.interiorDecor??[]).filter(i=>i.floor===selectedFloor).map(i=><rect key={i.id} x={i.x-.12} y={i.z-.12} width={.24} height={.24} fill="var(--au-text)"><title>{i.id}</title></rect>)}
      {(preview.furnitureSockets??[]).filter(i=>i.floor===selectedFloor).map(i=>
        <circle key={i.id} {...inspect(i.id)} aria-label={i.action+" "+i.id} cx={i.x} cy={i.z} r={selectedInterior===i.id ? .3 : .18} fill="none" stroke="var(--au-accent)" strokeWidth={.1}><title>{i.action} · {i.furnitureId}</title></circle>)}
      {(preview.removedDamage??[]).map((r,i)=>
        <rect key={'damage-'+i} x={r.minimum[0]} y={r.minimum[2]} width={Math.max(.1,r.maximum[0]-r.minimum[0])}
          height={Math.max(.1,r.maximum[2]-r.minimum[2])} fill="#c16b50" fillOpacity={.35} stroke="#c16b50" strokeWidth={.1}>
          <title>{r.kind} damage · Y {r.minimum[1]}–{r.maximum[1]}m</title></rect>)}
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
      <small>{preview.width.toFixed(1)} × {preview.depth.toFixed(1)} m · {preview.floors??1} floors · {sockets.length} sockets · {preview.solids??0} solids · {preview.connections??0} graph links</small>
      {!!preview.roomUses?.length&&<small>{preview.roomUses.filter(r=>r.roomId.startsWith('room/'+selectedFloor+'/')).map(r=>r.roomId.split('/').slice(2).join(',')+': '+r.use).join(' · ')}</small>}
      {!!preview.furnitureSockets?.length&&<small>{preview.furnitureSockets.filter(s=>s.floor===selectedFloor).length} furniture interaction sockets</small>}
      {!!preview.furniture?.length&&<small>{preview.furniture.filter(f=>f.floor===selectedFloor).length} static furniture items</small>}
      {!!preview.removedDamage?.length&&<small>{preview.removedDamage.length} removed solid pieces (all heights overlaid)</small>}
      {!!preview.roofSections?.length&&<small>{preview.roofSections.length} composed upper roof sections</small>}
      {!!preview.balconies?.length&&<small>{preview.balconies.filter(t=>t.floor===selectedFloor).length} projecting balconies</small>}
      {!!(preview.terraces?.length||preview.balconies?.length)&&<small>{(preview.terraces??[]).filter(t=>t.floor===selectedFloor).length} terrace cells · {(preview.terraceAccess??[]).filter(t=>t.floor===selectedFloor).length} authored terrace access links</small>}
      {selectedFurniture&&<small>Furniture: {selectedFurniture.kind} · {selectedFurniture.roomId} · {(selectedFurniture.x1-selectedFurniture.x0).toFixed(2)} × {(selectedFurniture.z1-selectedFurniture.z0).toFixed(2)} m · {(preview.furnitureSockets??[]).filter(s=>s.furnitureId===selectedFurniture.id).map(s=>s.action).join(', ')||'no interaction sockets'}</small>}
      {selectedLight&&<small>Light: {selectedLight.roomId} · {selectedLight.intensity.toFixed(1)} cd · {selectedLight.range.toFixed(1)} m range</small>}
      {selectedInteraction&&<small>Interaction: {selectedInteraction.action} · {selectedInteraction.furnitureId} · Y {selectedInteraction.y.toFixed(2)} m</small>}
      {selectedSocket&&<small>Selected: {selectedSocket.socketId} ({selectedSocket.kind}) · opening {selectedSocket.width.toFixed(1)}m, {Math.round(selectedSocket.open*100)}% initial open</small>}
      <small>Click an opening marker to edit its functional property. This is a top-down inspector; the real textured 3D asset appears in the native model thumbnail and Scene viewport after generation.</small>
    </div>
  </div>;
}
