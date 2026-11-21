import {useCallback,useEffect,useState} from 'react';
import type {StudioSnapshot,WindState,WindSettings,WindDebugView,WindMask} from '../../types';
import {invoke,nativeConnected,onNativeEvent} from '../../bridge';
import {StudioPanel,StudioSection,StudioAlert,StudioEmptyState,StudioField,StudioInput,StudioSelect,StudioCheckbox,StudioBadge,StudioButton} from '../../ui';
import './WindPanel.css';

/** «Ветер»: preview wind for every model with hierarchical wind masks
 * (<model>.wind.json) and rig debug views. Session-only; never writes files. */
const DEBUG:Array<[WindDebugView,string]>=[['off','Выкл'],['parts','Части'],['depth','Уровни иерархии'],['weight','Сила качания'],['skeleton','Скелет']];

function MaskCurve({mask}:{mask:WindMask}){
  const points=mask.samples.map((v,i)=>`${(i/(mask.samples.length-1))*100},${(1-v)*40}`).join(' ');
  return <svg className="wind-curve" viewBox="0 0 100 40" preserveAspectRatio="none" role="img" aria-label={'Маска '+mask.name}>
    <polyline points={points} fill="none" stroke="currentColor" strokeWidth="1.5" vectorEffect="non-scaling-stroke"/>
  </svg>;
}

export default function WindPanel({snapshot}:{snapshot:StudioSnapshot|null}){
  const [state,setState]=useState<WindState|null>(null);
  const [busy,setBusy]=useState(false),[error,setError]=useState('');
  const projectLoaded=Boolean(snapshot?.project.path);
  const reload=useCallback(async()=>{try{setState(await invoke('studio.wind',{}));setError('');}catch(e){setError(String(e));}},[]);
  useEffect(()=>{void reload();},[reload,snapshot?.project.path,snapshot?.scene.name]);
  useEffect(()=>onNativeEvent(event=>{if(event==='document.changed'||event==='workspace.changed')void reload();}),[reload]);
  const apply=async(patch:Partial<WindSettings>)=>{
    if(Object.values(patch).some(v=>typeof v==='number'&&!Number.isFinite(v)))return;
    setBusy(true);
    try{setState(await invoke('studio.windSet',patch));setError('');}catch(e){setError(String(e));}finally{setBusy(false);}
  };
  const s=state?.settings;
  const native=nativeConnected();
  const number=(key:'speed'|'direction'|'gust'|'scale',label:string,min:number,max:number,step:number,hint?:string)=>
    <StudioField label={label} htmlFor={'wind-'+key} hint={hint}>
      <StudioInput id={'wind-'+key} type="range" min={min} max={max} step={step} disabled={!native||busy||!s}
        value={s?.[key]??0} onChange={e=>void apply({[key]:e.currentTarget.valueAsNumber})}/>
    </StudioField>;
  return <StudioPanel className="studio-panel-wind" title="Ветер" eyebrow="WORLD / WIND" subtitle="Иерархические маски качания"
    actions={<StudioBadge tone={state?.scene.riggedInstances?'success':'neutral'}>{state?.scene.riggedInstances??0} объектов с ригом</StudioBadge>}>
    <div className="au-stack">
      {error&&<StudioAlert title="Ошибка">{error}</StudioAlert>}
      {!projectLoaded?<StudioEmptyState title="Нет проекта" description="Откройте проект в нативной Studio."/>:<>
      <StudioSection title="Предпросмотр ветра">
        <StudioCheckbox label="Ветер в сцене" checked={s?.enabled??false} disabled={!native||busy||!s} onChange={e=>void apply({enabled:e.currentTarget.checked})}/>
        {number('speed',`Скорость: ${(s?.speed??0).toFixed(1)} м/с`,0,30,.5)}
        {number('direction',`Куда дует: ${Math.round(s?.direction??0)}° (0 = +X, 90 = +Z)`,-180,180,5)}
        {number('gust',`Порывы: ${Math.round((s?.gust??0)*100)}%`,0,1,.05)}
        {number('scale',`Амплитуда: ${((s?.scale??0)*100).toFixed(1)} см на м/с`,0,.15,.005,'Смещение кончика части с маской 1 и гибкостью 1')}
      </StudioSection>
      <StudioSection title="Отладочный вид">
        <StudioField label="Показать" htmlFor="wind-debug">
          <StudioSelect id="wind-debug" value={s?.debug??'off'} disabled={!native||busy||!s} onChange={e=>void apply({debug:e.currentTarget.value as WindDebugView})}>
            {DEBUG.map(([id,label])=><option key={id} value={id}>{label}</option>)}
          </StudioSelect>
        </StudioField>
        <p className="wind-legend">{s?.debug==='depth'?'Ствол — коричневый, ветки — жёлтый, веточки — зелёный, листья — голубой; яркость = значение маски.':
          s?.debug==='weight'?'Синий — неподвижно, зелёный — средне, красный — максимальное качание.':
          s?.debug==='parts'?'Каждая часть своим цветом, яркость растёт к кончику по её маске.':
          s?.debug==='skeleton'?'Линии от точки крепления к кончику каждой части, качаются как на GPU.':'Обычные материалы.'}</p>
        {state&&<p className="wind-legend">Частей в сцене: {state.scene.riggedParts??0} · моделей: {state.scene.models??0}</p>}
      </StudioSection>
      <StudioSection title={`Маски проекта (${state?.library.uniqueMasks??0} уникальных)`} actions={<StudioButton disabled={busy} onClick={()=>void reload()}>Перечитать</StudioButton>}>
        {(state?.library.warnings??[]).map(w=><StudioAlert key={w} title="wind/*.json">{w}</StudioAlert>)}
        <div className="wind-masks">{(state?.library.masks??[]).map(m=><div className="wind-mask" key={m.name}>
          <MaskCurve mask={m}/><span>{m.name}</span><small>#{m.index}</small></div>)}</div>
        <p className="wind-legend">Свои маски — в wind/masks.json, профили — в wind/profiles.json, привязка к модели — в &lt;модель&gt;.wind.json рядом с файлом модели. Одинаковые кривые хранятся один раз.</p>
      </StudioSection>
      {(state?.library.profiles.length??0)>0&&<StudioSection title="Профили">
        {state!.library.profiles.map(p=><div key={p.name} className="wind-profile"><strong>{p.name}</strong>
          {p.levels.map((l,i)=><span key={i}>L{i}: {l.mask} · жёсткость {l.stiffness.toFixed(2)} · гибкость {l.flex.toFixed(2)}</span>)}</div>)}
      </StudioSection>}
      </>}
    </div>
  </StudioPanel>;
}
