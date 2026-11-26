import {useEffect, useState} from 'react';
import {invoke} from '../bridge';
import type {StaticGeometryReport} from '../types';

export default function StaticGeometryDiagnostics({project}:{project:string}) {
  const [open,setOpen]=useState(false);
  const [report,setReport]=useState<StaticGeometryReport|null|undefined>();
  const [error,setError]=useState('');
  useEffect(()=>{
    if(!open)return;
    let live=true;
    let timer:ReturnType<typeof setTimeout>;
    async function refresh(){
      try {
        const state=await invoke('studio.gameState',{geometryProject:project});
        if(live){setReport(state.geometry??null);setError('');}
      } catch(e){
        if(live){setReport(undefined);setError(String(e));}
      }
      if(live)timer=setTimeout(refresh,1200);
    }
    void refresh();
    return ()=>{live=false;clearTimeout(timer);};
  },[open,project]);
  return <details onToggle={e=>{
    setOpen(e.currentTarget.open);
    if(!e.currentTarget.open){setReport(undefined);setError('');}
  }}>
    <summary>Game rendering decisions</summary>
    {error?<p role="alert">{error}</p>:report?<><dl>
      <dt>STRATA rendering</dt><dd>{report.enabled?'Enabled':'Disabled'}</dd>
      <dt>Loaded instances</dt><dd>{report.instances}</dd>
      <dt>STRATA sources</dt><dd>{report.autoStrataSources}</dd>
      <dt>LOD sources</dt><dd>{report.autoLodSources}</dd>
      <dt>Cooked sources</dt><dd>{report.cookedSources}</dd>
      <dt>Stale sources</dt><dd>{report.cookStaleSources}</dd>
    </dl>{report.select.length>0&&<ul>{report.select.map(line=><li key={line}>{line}</li>)}</ul>}</>:
      <p role="status">{report===null?'Play a map to inspect its rendering decisions.':'Reading the game renderer…'}</p>}
  </details>;
}
