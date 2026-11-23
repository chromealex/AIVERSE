import {StudioButton,StudioInput,StudioSelect,StudioSection} from '../ui';
import {animationComparisons,animationScalarNames,editAnimationCondition,setAnimationScalar,object} from './nativeGraphAdapters';
import type {GraphObject} from './nativeGraphAdapters';

export function AnimationParameters({doc,disabled,onEdit}:{doc:GraphObject;disabled:boolean;onEdit:(doc:GraphObject)=>void}){
 const names=animationScalarNames(doc);
 return <StudioSection title="Scalar parameters">
  {!names.length&&<span>No declared scalar parameters.</span>}
  {names.map(name=>{
   const value=(doc.parameters as GraphObject)[name];
   return <label className="au-row" key={name}><span>{name}</span>
    {typeof value==='boolean'?<StudioSelect aria-label={name} disabled={disabled} value={String(value)}
      onChange={e=>onEdit(setAnimationScalar(doc,name,e.target.value==='true'))}><option value="true">true</option><option value="false">false</option></StudioSelect>:
     <StudioInput aria-label={name} disabled={disabled} type="number" step="any" value={Number(value)} onChange={e=>{if(e.target.value.trim())onEdit(setAnimationScalar(doc,name,Number(e.target.value)))}}/>}
   </label>;
  })}
 </StudioSection>;
}
export function AnimationConditions({doc,state,index,transition,disabled,onEdit}:{doc:GraphObject;state:string;index:number;transition:GraphObject;disabled:boolean;onEdit:(doc:GraphObject)=>void}){
 const names=animationScalarNames(doc);
 const conditions=Array.isArray(transition.when)?transition.when:[];
 const edit=(i:number,patch:GraphObject|null)=>onEdit(editAnimationCondition(doc,state,index,i,patch));
 return <StudioSection title="All conditions (AND)">
  {!conditions.length&&<span>Unconditional transition.</span>}
  {conditions.map((c,i)=>object(c)&&<div className="au-row" key={i} style={{flexWrap:'wrap'}}>
   <StudioSelect aria-label={'Condition '+(i+1)+' parameter'} disabled={disabled} value={String(c.parameter??'')}
    onChange={e=>edit(i,{parameter:e.target.value,op:typeof c.op==='string'?c.op:'eq',value:typeof c.value==='number'?c.value:1})}>
    {!names.includes(String(c.parameter))&&<option value={String(c.parameter??'')}>Unknown: {String(c.parameter??'')}</option>}
    {names.map(n=><option key={n} value={n}>{n}</option>)}
   </StudioSelect>
   <StudioSelect aria-label={'Condition '+(i+1)+' comparison'} disabled={disabled} value={String(c.op??'eq')}
    onChange={e=>edit(i,{op:e.target.value,value:typeof c.value==='number'?c.value:1})}>
    {animationComparisons.map(op=><option key={op} value={op}>{op}</option>)}
   </StudioSelect>
   <StudioInput aria-label={'Condition '+(i+1)+' value'} disabled={disabled} type="number" step="any" value={typeof c.value==='number'?c.value:1}
    onChange={e=>{if(e.target.value.trim())edit(i,{op:typeof c.op==='string'?c.op:'eq',value:Number(e.target.value)})}}/>
   <StudioButton size="sm" disabled={disabled} onClick={()=>edit(i,null)}>Remove</StudioButton>
  </div>)}
  <StudioButton size="sm" disabled={disabled||!names.length||conditions.length>=32}
   onClick={()=>edit(conditions.length,{parameter:names[0],op:'eq',value:1})}>Add condition</StudioButton>
 </StudioSection>;
}
