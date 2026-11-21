import {useCallback,useEffect,useState} from 'react';
import type {StudioSnapshot,NereidFlowState,NereidFlowInput} from '../../types';
import {invoke,onNativeEvent,nativeConnected} from '../../bridge';
import {StudioPanel,StudioSection,StudioAlert,StudioEmptyState,StudioField,StudioInput,StudioSelect,StudioButton} from '../../ui';
import './NereidFlowPanel.css';
const initial:NereidFlowInput={id:0,mode:'automatic',slopeDegrees:1,downhillHeadingDegrees:180,responseSeconds:5,maxSpeed:10,flow:[0,0,0]};
type Scalar='slopeDegrees'|'downhillHeadingDegrees'|'responseSeconds'|'maxSpeed';
export default function NereidFlowPanel({snapshot}:{snapshot:StudioSnapshot|null}){
 const [data,setData]=useState<NereidFlowState>({surfaces:[]});const [draft,setDraft]=useState(initial);
 const [busy,setBusy]=useState(false),[dirty,setDirty]=useState(false),[error,setError]=useState('');
 const choose=useCallback((state:NereidFlowState,id:number)=>{const s=state.surfaces.find(s=>s.id===id)??state.surfaces.find(s=>s.shape==='river');if(!s){setDraft(initial);return;}setDraft({id:s.id,mode:s.grade&&s.grade.automaticCurrent!==false?'automatic':'manual',slopeDegrees:s.grade?.slopeDegrees??0,downhillHeadingDegrees:s.grade?.downhillHeadingDegrees??180,responseSeconds:s.grade?.responseSeconds??5,maxSpeed:s.grade?.maxSpeed??10,flow:s.flow??[0,0,0]});setDirty(false);},[]);
 const reload=useCallback(async()=>{if(!snapshot?.project.path)return;setBusy(true);try{const state=await invoke('studio.nereidFlow',{});setData(state);choose(state,draft.id);setError('');}catch(e){setError(String(e));}finally{setBusy(false);}},[snapshot?.project.path,draft.id,choose]);
 useEffect(()=>{void reload();},[snapshot?.project.path,snapshot?.scene.name]);
 useEffect(()=>onNativeEvent(event=>{if(!dirty&&(event==='document.changed'||event==='workspace.changed'))void reload();}),[reload,dirty]);
 const update=(patch:Partial<NereidFlowInput>)=>{setDraft(v=>({...v,...patch}));setDirty(true);};
 const apply=async()=>{setBusy(true);try{const state=await invoke('studio.nereidFlowSet',draft);setData(state);choose(state,draft.id);setError('');}catch(e){setError(String(e));}finally{setBusy(false);}};
 const number=(key:Scalar,label:string,min:number,max:number,step:number)=><StudioField label={label} htmlFor={'water-'+key}><StudioInput id={'water-'+key} type="number" value={draft[key]} min={min} max={max} step={step} disabled={busy} onChange={e=>update({[key]:e.currentTarget.valueAsNumber})}/></StudioField>;
 const resolved=data.computed?.find(s=>s.id===draft.id)?.flow;
 return <StudioPanel title="NEREID Water" eyebrow="WORLD / WATER" subtitle="Уклон и течение"><div className="au-stack">
 {error&&<StudioAlert title="Ошибка NEREID">{error}</StudioAlert>}
 {!draft.id?<StudioEmptyState title="Нет рек NEREID" description="Откройте карту с рекой NEREID в нативном редакторе."/>:<>
 <StudioField label="Река" htmlFor="water-id"><StudioSelect id="water-id" disabled={busy||dirty} value={draft.id} onChange={e=>choose(data,Number(e.currentTarget.value))}>{data.surfaces.filter(s=>s.shape==='river').map(s=><option key={s.id} value={s.id}>Река {s.id}</option>)}</StudioSelect></StudioField>
 <StudioSection title="Поверхность">{number('slopeDegrees','Уклон, °',-20,20,.1)}{number('downhillHeadingDegrees','Направление вниз, ° (0 = +Z)',0,360,1)}</StudioSection>
 <StudioSection title="Течение"><StudioField label="Режим" htmlFor="water-mode"><StudioSelect id="water-mode" value={draft.mode} disabled={busy} onChange={e=>update({mode:e.currentTarget.value as NereidFlowInput['mode']})}><option value="automatic">По уклону</option><option value="manual">Вручную</option></StudioSelect></StudioField>
 {draft.mode==='automatic'?<>{number('responseSeconds','Время сопротивления, с',.1,60,.1)}{number('maxSpeed','Предел скорости, м/с',.1,100,.1)}</>:[0,1,2].map(axis=><StudioField key={axis} label={'Скорость '+['X','Y','Z'][axis]+', м/с'} htmlFor={'water-v'+axis}><StudioInput id={'water-v'+axis} type="number" min={-1000} max={1000} step={.1} disabled={busy} value={draft.flow[axis]} onChange={e=>{const flow=[...draft.flow];flow[axis]=e.currentTarget.valueAsNumber;update({flow});}}/></StudioField>)}
 {resolved&&<p>Применено: {resolved.map(v=>v.toFixed(2)).join(' / ')} м/с</p>}
 <StudioAlert title="Русло Terra">Уклон меняет поверхность воды. Готовый рельеф должен соответствовать уклону; применение параметров воды не перестраивает террейн.</StudioAlert>
 </StudioSection><div className="au-row"><StudioButton disabled={busy||!dirty||!nativeConnected()||![draft.slopeDegrees,draft.downhillHeadingDegrees,draft.responseSeconds,draft.maxSpeed,...draft.flow].every(Number.isFinite)} onClick={()=>void apply()}>Применить</StudioButton><StudioButton disabled={busy} onClick={()=>void reload()}>Перечитать</StudioButton></div></>}
 </div></StudioPanel>;
}
