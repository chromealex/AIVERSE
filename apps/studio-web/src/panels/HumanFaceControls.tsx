import {useEffect,useRef,useState} from 'react';
import type {HumanFaceShape,HumanLookInfo,HumanBuildStatus} from '../types';
import {invoke} from '../bridge';
import AssetReferencePicker from '../components/AssetReferencePicker';
import {guessFaceMeshes} from './humanCreatorSchema';
import {HcCard,HcNote} from './HumanCreatorUi';
import {StudioField,StudioInput,StudioSelect,StudioButton,StudioAlert} from '../ui';
const neutral=():HumanFaceShape=>({version:1,meshes:[],values:{},landmarks:[]});
const markers:[number,number,number][]=[[.3,.62,.65],[.7,.62,.65],[.5,.48,.9],[.35,.32,.7],[.65,.32,.7],[.5,.08,.55]];
export default function HumanFaceControls({guid,face,schema,onChange,onImported}:{guid:string;face:HumanFaceShape|null;schema:HumanLookInfo['faceSchema'];onChange:(f:HumanFaceShape|null)=>void;onImported:(guid:string)=>void}) {
 const draft=face??neutral();
 const [meshes,setMeshes]=useState<string[]>([]),[bones,setBones]=useState<string[]>([]);
 const [history,setHistory]=useState<(HumanFaceShape|null)[]>([]),[error,setError]=useState('');
 const [scan,setScan]=useState(''),[head,setHead]=useState(''),[bone,setBone]=useState(''),[name,setName]=useState('imported_character');
 const [hidden,setHidden]=useState<string[]>([]),[distance,setDistance]=useState(.1),[strength,setStrength]=useState(.5);
 const [job,setJob]=useState<HumanBuildStatus|null>(null),[busy,setBusy]=useState(false);
 const [photo,setPhoto]=useState(''),[profile,setProfile]=useState(false),[active,setActive]=useState(0);
 const live=useRef(true);useEffect(()=>{live.current=true;return ()=>{live.current=false;};},[]);
 useEffect(()=>()=>{if(photo)URL.revokeObjectURL(photo);},[photo]);
 const edit=(next:HumanFaceShape|null)=>{setHistory(h=>[...h.slice(-31),face]);onChange(next);};
 const run=async(fn:()=>Promise<void>)=>{setBusy(true);setError('');try{await fn();}catch(e){if(live.current)setError(String(e));}finally{if(live.current)setBusy(false);}};
 useEffect(()=>{if(!job?.running)return;let valid=true;const timer=setInterval(()=>{void invoke('studio.humanCharacterBuildStatus',{}).then(result=>{if(!valid)return;setJob(result);if(result.error)setError(result.error);if(!result.running&&result.face)edit(result.face);}).catch(e=>{if(valid)setError(String(e));});},1000);return ()=>{valid=false;clearInterval(timer);};},[job?.running]);
 const disabled=busy||!!job?.running;
 return <>
  {error&&<HcNote tone="error">{error}</HcNote>}
  <HcCard title="Части головы" hint={draft.meshes.length?`Выбрано: ${draft.meshes.length} — деформируются вместе`:"Отметьте кожу лица, глаза, брови, ресницы и волосы"} actions={<div className="hc-row"><StudioButton size="sm" disabled={disabled||!history.length} onClick={()=>{onChange(history[history.length-1]);setHistory(history.slice(0,-1));}}>Отменить</StudioButton><StudioButton size="sm" disabled={disabled||!face} onClick={()=>edit({...draft,values:{},landmarks:[]})}>Сбросить</StudioButton></div>}>
  <StudioButton disabled={disabled||!guid} onClick={()=>void run(async()=>{const i=await invoke('studio.humanWardrobeInfo',{guid});if(live.current){setMeshes(i.meshes);setBones(i.bones);if(!face?.meshes.length){const auto=guessFaceMeshes(i.meshes);if(auto.length)edit({...draft,meshes:auto});}}})}>Загрузить меши головы и кости</StudioButton>
  <div className="hc-meshes">{meshes.map((mesh,i)=><StudioField key={mesh} htmlFor={'face-mesh-'+i} label={mesh}><StudioInput id={'face-mesh-'+i} type="checkbox" disabled={disabled} checked={draft.meshes.includes(mesh)} onChange={()=>{const selected=draft.meshes.includes(mesh)?draft.meshes.filter(m=>m!==mesh):[...draft.meshes,mesh];edit(selected.length?{...draft,meshes:selected}:null);}}/></StudioField>)}</div>
  </HcCard>

  <details className="hc-more"><summary>Восстановить форму из файла рецепта</summary><StudioField htmlFor="face-recipe" label="Восстановить форму из source.json / face.json" hint="Рецепт относится к исходной модели; не применяйте его повторно к уже запечённому GLB."><StudioInput id="face-recipe" type="file" accept=".json,application/json" disabled={disabled} onChange={e=>{const file=e.target.files?.[0];if(!file)return;void run(async()=>{if(file.size>512*1024)throw Error('Рецепт превышает 512 KiB');const parsed=JSON.parse(await file.text());const f=parsed.face??parsed;const vec=(v:unknown)=>Array.isArray(v)&&v.length===3&&v.every(n=>typeof n==='number'&&Number.isFinite(n)&&n>=-1&&n<=2);if(f.version!==1||!Array.isArray(f.meshes)||!f.meshes.length||f.meshes.length>16||!f.meshes.every((m:unknown)=>typeof m==='string'&&m.length>0&&m.length<=256)||!f.values||typeof f.values!=='object'||Array.isArray(f.values)||!Object.entries(f.values).every(([k,v])=>schema.some(s=>s.key===k&&typeof v==='number'&&Number.isFinite(v)&&v>=s.min&&v<=s.max))||!Array.isArray(f.landmarks)||f.landmarks.length>256||!f.landmarks.every((m:HumanFaceShape['landmarks'][number])=>m&&vec(m.from)&&vec(m.to)&&typeof m.radius==='number'&&m.radius>=.01&&m.radius<=1&&typeof m.weight==='number'&&m.weight>=0&&m.weight<=1))throw Error('Некорректный рецепт лица');if(live.current)edit(f);});}}/></StudioField></details>
  <HcCard title="Пропорции" hint="Точные значения — круг смешивания пишет сюда же">{schema.map(f=><StudioField key={f.key} label={f.label} htmlFor={'face-'+f.key}><StudioInput id={'face-'+f.key} type="range" min={f.min} max={f.max} step={.001} disabled={disabled||!face} value={draft.values[f.key]??0} onChange={e=>edit({...draft,values:{...draft.values,[f.key]:Number(e.target.value)}})}/><output>{(draft.values[f.key]??0).toFixed(3)}</output></StudioField>)}</HcCard>
  <HcCard title="Импорт лица" hint="Подгоните шаблон по фото или 3D-скану, либо поставьте отдельную голову на тело"><details className="hc-more"><summary>По фото · ручные точки</summary>
   <StudioAlert>Выберите кадр с плотно обрезанной головой. Это ручная деформация шаблона: фронт меняет X/Y, профиль — Z/Y. Невидимая глубина, текстура и геометрия из фото автоматически не восстанавливаются. Фото служит только локальным ориентиром.</StudioAlert>
   <StudioField htmlFor="face-photo" label="Фото"><StudioInput id="face-photo" type="file" accept="image/png,image/jpeg,image/webp" onChange={e=>{const f=e.target.files?.[0];if(f){if(f.size>20*1024*1024){setError('Фото превышает 20 MiB');return;}setPhoto(URL.createObjectURL(f));}}}/></StudioField>
   <StudioField htmlFor="face-photo-plane" label="Ракурс"><StudioSelect id="face-photo-plane" value={profile?'profile':'front'} onChange={e=>setProfile(e.target.value==='profile')}><option value="front">Фронт · X/Y</option><option value="profile">Профиль · Z/Y, нос справа</option></StudioSelect></StudioField>
   <StudioButton disabled={disabled||!face} onClick={()=>edit({...draft,landmarks:markers.map(p=>({from:[...p],to:[...p],radius:.22,weight:1}))})}>Создать 6 опорных точек</StudioButton>
   <StudioField htmlFor="face-point" label="Активная точка"><StudioSelect id="face-point" value={active} onChange={e=>setActive(Number(e.target.value))}>{draft.landmarks.map((_,i)=><option key={i} value={i}>{i+1}</option>)}</StudioSelect></StudioField>
   {photo&&<svg viewBox="0 0 100 100" role="img" aria-label="Фото: щёлкните для перемещения выбранной точки" onClick={e=>{if(disabled||!draft.landmarks[active])return;const r=e.currentTarget.getBoundingClientRect();const x=Math.max(0,Math.min(1,(e.clientX-r.left)/r.width)),y=1-Math.max(0,Math.min(1,(e.clientY-r.top)/r.height));edit({...draft,landmarks:draft.landmarks.map((m,i)=>{if(i!==active)return m;const to:[number,number,number]=[...m.to];to[profile?2:0]=x;to[1]=y;return {...m,to};})});}}>
    <image href={photo} width="100" height="100" preserveAspectRatio="none"/>
    {draft.landmarks.map((m,i)=><g key={i}><circle cx={m.to[profile?2:0]*100} cy={(1-m.to[1])*100} r={i===active?2:1} fill="var(--au-accent)" stroke="var(--au-text)" strokeWidth=".3"/><text x={m.to[profile?2:0]*100+2} y={(1-m.to[1])*100} fontSize="4" fill="var(--au-text)">{i+1}</text></g>)}
   </svg>}
   <StudioAlert>Точки можно редактировать с клавиатуры в нормализованных координатах головы.</StudioAlert>
   {draft.landmarks[active]&&draft.landmarks[active].to.map((v,axis)=><StudioField key={axis} htmlFor={'face-point-'+axis} label={'XYZ'[axis]}><StudioInput id={'face-point-'+axis} type="number" min={-1} max={2} step={.01} value={v} disabled={disabled} onChange={e=>{const n=e.target.valueAsNumber;if(!Number.isFinite(n)||n< -1||n>2)return;edit({...draft,landmarks:draft.landmarks.map((m,i)=>{const to:[number,number,number]=[...m.to];to[axis]=n;return i===active?{...m,to}:m;})});}}/></StudioField>)}
  </details>
  <details className="hc-more"><summary>По 3D-скану</summary>
   <StudioAlert>Скан и шаблон должны быть заранее совмещены по масштабу и координатам. Выполняется ограниченная подгонка по ближайшим вершинам; автоматической регистрации скана нет.</StudioAlert>
   <AssetReferencePicker value={scan} kinds={['model']} label="Модель скана" disabled={disabled} onChange={setScan}/>
   <StudioField htmlFor="face-distance" label="Максимальное расстояние (единицы модели)"><StudioInput id="face-distance" type="number" min={.001} max={1} step={.01} value={distance} onChange={e=>{if(Number.isFinite(e.target.valueAsNumber))setDistance(e.target.valueAsNumber);}}/></StudioField>
   <StudioField htmlFor="face-strength" label="Сила подгонки"><StudioInput id="face-strength" type="range" min={0} max={1} step={.01} value={strength} onChange={e=>setStrength(Number(e.target.value))}/></StudioField>
   <StudioButton disabled={disabled||!guid||!scan||!face} onClick={()=>void run(async()=>{setJob(await invoke('studio.humanFaceFitScan',{guid,scanGuid:scan,face:face!,distance,strength}));})}>Подогнать шаблон</StudioButton>
  </details>
  <details className="hc-more"><summary>Отдельная голова на тело</summary>
   <StudioAlert>Выбранная модель персонажа используется как тело. Голова сохраняет собственный скелет, морфы и материалы. Требуется общее исходное положение и масштаб; сварка шеи и автоматический ретаргет не выполняются.</StudioAlert>
   <AssetReferencePicker value={head} kinds={['model']} label="Отдельная голова" disabled={disabled} onChange={setHead}/>
   <StudioField htmlFor="face-socket" label="Кость тела для крепления"><StudioSelect id="face-socket" value={bone} onChange={e=>setBone(e.target.value)}><option value="">Без крепления</option>{bones.map((b,i)=><option key={i}>{b}</option>)}</StudioSelect></StudioField>
   {meshes.map((m,i)=><StudioField key={m} htmlFor={'face-hide-'+i} label={'Скрыть исходный '+m}><StudioInput id={'face-hide-'+i} type="checkbox" checked={hidden.includes(m)} onChange={()=>setHidden(hidden.includes(m)?hidden.filter(v=>v!==m):[...hidden,m])}/></StudioField>)}
   <StudioField htmlFor="face-import-name" label="Имя нового персонажа"><StudioInput id="face-import-name" value={name} onChange={e=>setName(e.target.value)}/></StudioField>
   <StudioButton disabled={disabled||!guid||!head||!(/^[A-Za-z0-9_-]{1,64}$/).test(name)} onClick={()=>void run(async()=>{setJob(await invoke('studio.humanHeadBodyImport',{guid,headGuid:head,bone,hiddenMeshes:hidden,name}));})}>Импортировать голову и тело</StudioButton>
  </details></HcCard>
  {job?.running&&<StudioAlert>Обработка персонажа…</StudioAlert>}
  {job?.path&&<StudioAlert tone="success">{job.path}</StudioAlert>}
  {job?.guid&&<StudioButton onClick={()=>onImported(job.guid!)}>Редактировать импортированного персонажа</StudioButton>}
 </>;
}
