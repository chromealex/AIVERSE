import {useState} from 'react';
import {applyEdits,createScanner,getNodeValue,type Node} from 'jsonc-parser';
import {StudioAlert,StudioButton,StudioSelect} from '../ui';
import {EffectNumber,EffectRangeValue} from './EffectRangeEditor';

type Path=(string|number)[];
const countProperty={key:'count',label:'Particle count',min:0,max:2000,initial:1};
export default function EffectBursts({source,emitterIndex,emitter,schedule,patch,remove}:{
  source:string;emitterIndex:number;emitter:Record<string,unknown>;schedule?:Node;
  patch:(path:Path,value:unknown,source?:string)=>void;remove:(entries:Node[],position:number)=>void;
}){
  const [selection,select]=useState(0);
  const nodes=schedule?.children??[];
  const index=Math.min(selection,Math.max(0,nodes.length-1));
  const entry=nodes[index];
  const raw=entry&&getNodeValue(entry);
  const compact=Array.isArray(raw)&&raw.length===2&&raw.every(n=>typeof n==='number');
  const burst=compact?{time:raw[0],count:raw[1]}:entry?.type==='object'?raw:undefined;
  const path=['emitters',emitterIndex,'bursts'];
  function change(field:Path,value:unknown){
    const key=field[0];
    if(!compact){patch([...path,index,...field],value);return;}
    if(field.length===1&&(key==='time'||key==='count')&&typeof value==='number'){
      patch([...path,index,key==='time'?0:1],value);return;
    }
    // Retain the authored literals and their inline comments when a repeat or curve needs object syntax.
    const time=entry.children![0];
    const scan=createScanner(source,true);
    scan.setPosition(time.offset+time.length);scan.scan();
    const objectSource=applyEdits(source,[
      {offset:entry.offset,length:1,content:'{"time": '},
      {offset:scan.getTokenOffset(),length:1,content:', "count": '},
      {offset:entry.offset+entry.length-1,length:1,content:'}'}
    ]);
    patch([...path,index,...field],value,objectSource);
  }
  return <details className="effects-bursts"><summary>Scheduled bursts{nodes.length?' · '+nodes.length:''}</summary>
    {schedule&&schedule.type!=='array'?<StudioAlert tone="warning">Correct bursts in Effect source to edit this schedule.</StudioAlert>:<>
      <div className="au-row">
        {nodes.length>0&&<StudioSelect aria-label="Selected burst" value={index} onChange={e=>select(Number(e.target.value))}>
          {nodes.map((n,i)=>{
            const value=getNodeValue(n);
            const time=Array.isArray(value)?value[0]:value?.time;
            return <option key={i} value={i}>Burst {i+1} · {typeof time==='number'?time:0} s</option>;
          })}
        </StudioSelect>}
        <StudioButton onClick={()=>{
          if(schedule)patch([...path,nodes.length],{time:0,count:10});else patch(path,[{time:0,count:10}]);
          select(nodes.length);
        }}>Add burst</StudioButton>
        {entry&&<StudioButton onClick={()=>{
          if(nodes.length===1)patch(path,[]);else remove(nodes,index);
          select(Math.max(0,Math.min(index,nodes.length-2)));
        }}>Remove burst</StudioButton>}
      </div>
      {burst&&<div key={index}>
        <div className="effects-emitter-fields">
          <EffectNumber label="Start time (s)" min={0} max={600} value={burst.time??0} change={v=>change(['time'],v)}/>
          <EffectNumber label="Cycles (0 repeats forever)" min={0} max={1000} step={1} value={burst.cycles??1} change={v=>change(['cycles'],v)}/>
          <EffectNumber label="Interval (s)" min={0} max={600} value={burst.interval??0} change={v=>change(['interval'],v)}/>
          <label>Chance · {Math.round((burst.chance??1)*100)}%<input aria-label="Burst chance" type="range" min={0} max={1} step=".01"
            value={burst.chance??1} onChange={e=>change(['chance'],Number(e.target.value))}/></label>
        </div>
        <fieldset className="effects-key-list"><legend>Particles per burst</legend>
          <EffectRangeValue emitter={burst} p={countProperty} patch={change}/>
        </fieldset>
        {burst.cycles===0&&!(burst.interval>0)&&<StudioAlert tone="warning">Set a positive interval for a repeating burst.</StudioAlert>}
      </div>}
      {nodes.length>0&&emitter.loop===true&&!(Number(emitter.duration)>0)&&<StudioAlert tone="warning">Set the emitter duration to repeat this schedule.</StudioAlert>}
    </>}
  </details>;
}
