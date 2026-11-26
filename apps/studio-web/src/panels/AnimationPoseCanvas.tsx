import {useState} from 'react';
import GraphCanvas from '../graphs/GraphCanvas';
import {animationProjection} from '../graphs/nativeGraphAdapters';
import type {PortRef} from '../graphs/graphGeometry';
import type {Positioned} from '../graphs/graphSelection';

interface Props {
 graph:{nodes:{id:string;kind:string;position?:number[];graph?:string}[];output:string};
 selection:string[];
 busy:boolean;
 select:(ids:string[])=>void;
 move:(nodes:Positioned[])=>void;
 connect:(from:PortRef,to:PortRef)=>void;
}

export default function AnimationPoseCanvas({graph,selection,busy,select,move,connect}:Props){
 const [pending,setPending]=useState<PortRef|null>(null);
 const projection=animationProjection(graph,'pose');
 const positions=graph.nodes.map((n,i)=>({x:n.position?.[0]??24+190*(i%3),y:n.position?.[1]??24+90*Math.floor(i/3)}));
 // Legacy layouts allow negative coordinates. Translate only while displaying them.
 const left=Math.min(0,...positions.map(p=>p.x))-24;
 const top=Math.min(0,...positions.map(p=>p.y))-24;
 const nodes=projection.nodes.map((n,i)=>{
  const source=graph.nodes[i],label=source.kind==='subgraph'&&source.graph?source.graph:n.label;
  return {...n,x:positions[i].x-left,y:positions[i].y-top,label:label+(n.id===graph.output?' · output':'')};
 });
 const moveNodes=(updates:Positioned[])=>move(updates.map(n=>({...n,x:Math.round(n.x+left),y:Math.round(n.y+top)})));
 const wire=(from:PortRef,to:PortRef)=>{setPending(null);if(from.port==='pose')connect(from,to);};
 return <GraphCanvas label="Pose node connections" enhancedNavigation typedPorts disabled={busy}
  view={{nodes,links:projection.links}} moveLimit={Number.MAX_SAFE_INTEGER}
  selected={selection.at(-1)??''} selection={selection} onSelection={select} onSelect={id=>select(id?[id]:[])}
  pendingOutput={pending} onOutput={setPending} onInput={to=>{if(pending)wire(pending,to);}} onConnect={wire} onCancel={()=>setPending(null)}
  onMove={(id,x,y)=>moveNodes([{id,x,y}])} onMoveGroup={moveNodes}/>;
}
