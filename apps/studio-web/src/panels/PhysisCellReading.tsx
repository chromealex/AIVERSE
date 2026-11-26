import {useEffect,useState} from 'react';
import {invoke} from '../bridge';
import {StudioButton} from '../ui';

type Sample={active:boolean;available:boolean;tick:number;wind:number[];gravity:number[]};
export default function PhysisCellReading({projectPath,x,z,tick,label,value,refresh}:{
  projectPath:string;x:number;z:number;tick:number;label:string;value:number|null;refresh:()=>void
}){
  const [sample,setSample]=useState<Sample>();
  const [error,setError]=useState('');
  useEffect(()=>{
    if(value===null)return;
    let current=true;
    invoke('studio.op',{op:'physis_demo',params:{action:'probe',cellX:x,cellZ:z},expectedProject:projectPath})
      .then(reply=>{if(current)setSample(reply.result as Sample);})
      .catch(e=>{if(current)setError(String(e));});
    return ()=>{current=false;};
  },[projectPath,x,z,tick,value]);
  return <section className="physis-cell" aria-label="Selected cell">
    <h4>Cell ({x}, {z})</h4>
    {value===null?<p>Outside grid</p>:<>
      <p>{label} · {value.toFixed(4)}</p>
      {error?<p role="alert">{error}</p>:!sample?<p role="status">Reading cell…</p>:
        !sample.active||sample.tick!==tick?<StudioButton onClick={refresh}>Refresh field map</StudioButton>:
        !sample.available?<p>Cell sample unavailable</p>:
        <table aria-label="Field vectors"><thead><tr><th scope="col">Field</th>{['X','Y','Z'].map(axis=><th key={axis} scope="col">{axis}</th>)}</tr></thead>
          <tbody>{([['Wind (m/s)',sample.wind],['Gravity (m/s²)',sample.gravity]] as const).map(([name,vector])=>
            <tr key={name}><th scope="row">{name}</th>{vector.map((n,i)=><td key={i}>{n.toFixed(2)}</td>)}</tr>)}</tbody>
        </table>}
    </>}
  </section>;
}
