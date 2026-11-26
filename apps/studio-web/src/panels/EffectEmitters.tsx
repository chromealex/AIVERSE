import {useMemo,useState} from 'react';
import {applyEdits,createScanner,findNodeAtLocation,getNodeValue,modify,parseTree,type Node,type ParseError} from 'jsonc-parser';
import EffectLifetimeColor from './EffectLifetimeColor';
import EffectShape from './EffectShape';
import EffectBursts from './EffectBursts';
import EffectForceFields from './EffectForceFields';
import EffectRangeEditor,{EffectNumber} from './EffectRangeEditor';
import {StudioAlert,StudioButton,StudioInput,StudioSelect} from '../ui';

const emitterLimit=16;
const choices=[
  {key:'blend',label:'Blending',fallback:'additive',options:['alpha','additive','premultiplied','multiply','subtract','opaque']},
  {key:'align',label:'Alignment',fallback:'camera',options:['camera','velocity','ground','upright','world','local']},
  {key:'minQuality',label:'Minimum quality',fallback:'low',options:['off','low','medium','high']},
];

export default function EffectEmitters({source,edit,busy}:{source:string;edit:(draft:string)=>void;busy:boolean}){
  const [selection,select]=useState(0);
  const root=useMemo(()=>{
    const errors:ParseError[]=[];
    const tree=parseTree(source,errors,{allowTrailingComma:true});
    return errors.length?undefined:tree;
  },[source]);
  const array=root&&findNodeAtLocation(root,['emitters']);
  if(root?.type!=='object'||(array&&array.type!=='array'))
    return <StudioAlert tone="warning">Fix the effect source to edit emitters.</StudioAlert>;
  const nodes=array?.children??[];
  const index=Math.min(selection,Math.max(0,nodes.length-1));
  const node=nodes[index];
  const emitter=node?.type==='object'?getNodeValue(node):undefined;
  function update(base:string,path:(number|string)[],value:unknown){
    return applyEdits(base,modify(base,path,value,{formattingOptions:{tabSize:2,insertSpaces:true,eol:base.includes('\r\n')?'\r\n':'\n'}}));
  }
  function patch(path:(number|string)[],value:unknown,base=source){
    edit(update(base,path,value));
  }
  function duplicate(){
    const indent=source.slice(source.lastIndexOf('\n',node.offset-1)+1,node.offset).match(/^\s*/)?.[0]??'';
    const newline=source.includes('\r\n')?'\r\n':'\n';
    const raw=source.slice(node.offset,node.offset+node.length);
    edit(source.slice(0,node.offset)+raw+','+newline+indent+source.slice(node.offset));
    select(index+1);
  }
  function removeEntry(entries:Node[],position:number){
    // Remove only the node and one separator; comments on neighbouring entries survive.
    const node=entries[position];
    const previous=entries[position-1];
    const before=position===entries.length-1;
    const anchor=before?previous:node;
    const scanner=createScanner(source,true);
    scanner.setPosition(anchor.offset+anchor.length);
    scanner.scan();
    const comma=scanner.getTokenOffset();
    if(source[comma]!==',')return;
    edit(applyEdits(source,[{offset:node.offset,length:node.length,content:''},{offset:comma,length:1,content:''}]));

  }
  const fields=findNodeAtLocation(root,['forceFields']);
  return <><fieldset className="effects-emitters" disabled={busy}>
    <legend>Emitters</legend>
    <div className="au-row">
      {nodes.length>0&&<StudioSelect aria-label="Selected emitter" value={index} onChange={e=>select(Number(e.target.value))}>
        {nodes.map((n,i)=>{
          const texture=findNodeAtLocation(n,['texture'])?.value;
          return <option key={i} value={i}>Emitter {i+1}{typeof texture==='string'?' · '+texture:''}</option>;
        })}
      </StudioSelect>}
      <StudioButton disabled={nodes.length>=emitterLimit} onClick={()=>{
        patch(array?['emitters',nodes.length]:['emitters'],array?{burst:10}:[{burst:10}]);select(nodes.length);
      }}>Add emitter</StudioButton>
      <StudioButton disabled={!emitter||nodes.length>=emitterLimit} onClick={duplicate}>Duplicate</StudioButton>
      <StudioButton disabled={nodes.length<=1} onClick={()=>{removeEntry(nodes,index);select(Math.min(index,nodes.length-2));}}>Remove</StudioButton>
    </div>
    {emitter&&<div className="effects-emitter-fields">
      {choices.map(({key,label,options,fallback})=><label key={key}>{label}
        <StudioSelect value={emitter[key]??fallback} onChange={e=>patch(['emitters',index,key],e.target.value)}>
          {options.map(option=><option key={option}>{option}</option>)}
        </StudioSelect>
      </label>)}
      <label>Burst count<StudioInput type="number" min={0} max={2000} step={1} value={emitter.burst??0} onChange={e=>{
        const count=e.target.valueAsNumber;
        if(Number.isInteger(count)&&count>=0&&count<=2000)patch(['emitters',index,'burst'],count);
      }}/></label>
      <EffectNumber label="Emitter duration (s)" value={emitter.duration??0} min={0} max={600} change={value=>patch(['emitters',index,'duration'],value)}/>
      <EffectNumber label="External force multiplier" value={emitter.externalForces??0} min={0} max={100}
        change={value=>patch(['emitters',index,'externalForces'],value)}/>
      <label className="effects-emitter-loop"><input type="checkbox" checked={emitter.loop===true} onChange={e=>patch(['emitters',index,'loop'],e.target.checked)}/>Loop emission</label>
    </div>}
    {emitter&&<EffectShape key={'shape-'+index} emitter={emitter} patch={(path,value)=>{
      const base=path[0]==='shape'&&value==='mesh'&&!emitter.mesh?update(source,['emitters',index,'mesh'],'builtin:cube'):source;
      patch(['emitters',index,...path],value,base);
    }}/>}
    {emitter&&<EffectRangeEditor key={index} emitter={emitter} patch={(path,value)=>patch(['emitters',index,...path],value)}/>}
    {emitter&&<EffectLifetimeColor key={'color-'+index} emitter={emitter} patch={(path,value)=>patch(['emitters',index,...path],value)}/>}
    {emitter&&<EffectBursts key={'bursts-'+index} source={source} emitterIndex={index} emitter={emitter}
      schedule={findNodeAtLocation(node,['bursts'])} patch={patch} remove={removeEntry}/>}
  </fieldset>
    <EffectForceFields fields={fields} busy={busy} patch={patch} remove={position=>{
      const entries=fields?.children??[];
      if(entries.length===1)patch(['forceFields'],[]);
      else removeEntry(entries,position);
    }}/>
  </>;
}
