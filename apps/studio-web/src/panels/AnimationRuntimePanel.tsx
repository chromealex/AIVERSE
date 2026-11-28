import {useEffect,useState} from 'react';
import {invoke} from '../bridge';
import type {RuntimeAnimationRow} from '../types';

export default function AnimationRuntimePanel({project,rig}:{project:string;rig:boolean}){
 const [rows,setRows]=useState<RuntimeAnimationRow[]|null>(null);
 const [error,setError]=useState('');
 useEffect(()=>{
  let active=true,timer=0;
  async function refresh(){
   try{
    const next=await invoke('studio.animationRuntime',{project,panel:rig?'rig':'graph'});
    if(active){setRows(next);setError('');}
   }catch(e){if(active){setError(String(e));setRows(null);}}
   if(active)timer=window.setTimeout(refresh,1000);
  }
  setRows(null);setError('');
  if(project)void refresh();
  return ()=>{active=false;window.clearTimeout(timer);};
 },[project,rig]);
 if(error)return <p role="alert">{error}</p>;
 if(!project)return <p>Open a project to inspect live entities.</p>;
 if(!rows)return <p role="status">Reading play-test animations…</p>;
 if(!rows.length)return <p>No animated entities in the current play-test.</p>;
 return <div className="anim-runtime"><table aria-label="Live animation entities">
  <thead><tr><th>Entity</th><th>Climbing</th><th>Attached</th><th>Planted feet</th><th>Stepping feet</th><th>Foot error</th><th>Penetration</th><th>Body position</th><th>Surface normal</th><th>Error</th></tr></thead>
  <tbody>{rows.map(row=><tr key={row.entityId}>
   <th scope="row">{row.entityId}</th><td>{row.climbing?'Yes':'No'}</td><td>{row.attached?'Yes':'No'}</td>
   <td>{row.stanceFeet}</td><td>{row.steppingFeet}</td><td>{row.footError}</td><td>{row.penetration}</td>
   <td>{row.bodyPosition.join(', ')}</td><td>{row.surfaceNormal.join(', ')}</td><td>{row.error||'—'}</td>
  </tr>)}</tbody>
 </table></div>;
}
