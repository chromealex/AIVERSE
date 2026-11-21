import {useEffect,useState,useId,cloneElement,type ReactElement} from 'react';
import type {HumanSocket,HumanFaceShape,HumanLook,HumanWardrobe,HumanWardrobeItem,HumanBuildStatus} from '../types';
import {invoke} from '../bridge';
import AssetReferencePicker from '../components/AssetReferencePicker';
import {StudioField,StudioInput,StudioSelect,StudioButton,StudioAlert} from '../ui';
import {HcCard,HcHeader,HcNote,HcSwitch} from './HumanCreatorUi';
import {hexToRgb,rgbToHex} from './humanCreatorSchema';
import {useOutfit,type Outfit} from './useOutfit';
import {Shirt} from 'lucide-react';
const SLOT_LABELS:Record<string,string>={top:'Верх',bottom:'Низ',feet:'Обувь',head:'Голова',neck:'Шея',wrist_left:'Левое запястье',wrist_right:'Правое запястье',ears:'Уши',accessory_1:'Аксессуар',accessory_2:'Аксессуар'};
const slotLabel=(slot:string)=>SLOT_LABELS[slot]??slot;
const assetLabel=(path:string)=>path?path.split('/').pop()!.replace(/\.(gltf|glb|fbx)$/i,'').replace(/([a-z])([A-Z])/g,'$1 $2'):'Без модели';
const slots=['top','bottom','feet','head','neck','wrist_left','wrist_right','ears','accessory_1','accessory_2'];
export default function HumanWardrobeControls({guid,look,face,onShow,outfit}:{guid:string;look:HumanLook;face:HumanFaceShape|null;onShow?:(guid:string)=>void;outfit?:Outfit}) {
 const own=useOutfit(outfit?'':guid);const o=outfit??own;
 const {recipe,setRecipe,tryOn,setTryOn,trying}=o;
 const [history,setHistory]=useState<HumanWardrobe[]>([]);
 const [info,setInfo]=useState<{meshes:string[];bones:string[];materials:string[]}|null>(null);
 const [busy,setBusy]=useState(false),[error,setError]=useState(''),[name,setName]=useState('character_assembled');
 const [size,setSize]=useState(1024),[job,setJob]=useState<HumanBuildStatus|null>(null);
 const edit=(next:HumanWardrobe)=>{setHistory(h=>[...h.slice(-31),recipe]);setRecipe(next);};
 const socketEdit=(index:number,patch:Partial<HumanSocket>)=>edit({...recipe,sockets:(recipe.sockets??[]).map((v,i)=>i===index?{...v,...patch}:v)});
 const itemEdit=(index:number,patch:Partial<HumanWardrobeItem>)=>edit({...recipe,items:recipe.items.map((v,i)=>i===index?{...v,...patch}:v)});
 const run=async(fn:()=>Promise<void>)=>{setBusy(true);setError('');try{await fn();}catch(e){setError(String(e));}finally{setBusy(false);}};
 useEffect(()=>{if(!job?.running)return;let live=true;const timer=setInterval(()=>{void invoke('studio.humanCharacterBuildStatus',{}).then(result=>{if(live){setJob(result);if(result.error)setError(result.error);}}).catch(e=>{if(live)setError(String(e));});},1000);return ()=>{live=false;clearInterval(timer);};},[job?.running]);
 useEffect(()=>{let live=true;void invoke('studio.humanCharacterBuildStatus',{}).then(result=>{if(live)setJob(result);}).catch(()=>{});return ()=>{live=false;};},[]);
 const disabled=busy||!!job?.running;
 const [paths,setPaths]=useState<Record<string,string>>({});
 useEffect(()=>{if(!recipe.items.some(i=>i.asset&&!paths[i.asset]))return;let live=true;void invoke('studio.assets',{}).then(r=>{if(live)setPaths(Object.fromEntries(r.files.filter(f=>f.guid).map(f=>[f.guid!,f.path])));}).catch(()=>{});return ()=>{live=false;};},[recipe.items,paths]);
 // Live try-on: every change of the outfit re-assembles the preview (small
 // textures, nothing written to disk) until the switch is turned off.
 const [placed,setPlaced]=useState('');
 const toggle=(values:string[],value:string)=>values.includes(value)?values.filter(v=>v!==value):[...values,value];
 return <>
  <HcHeader title="Одежда" hint="Наденьте комплект модели или добавьте вещи — с примеркой они сразу видны во вьюпорте. Сборка запекает итог в новый ассет."/>
  {(error||o.error)&&<HcNote tone="error">{error||o.error}</HcNote>}
  <HcCard title="Комплект" hint={recipe.items.length?recipe.items.filter(i=>i.enabled).length+' из '+recipe.items.length+' вещей надето':'Загрузите комплект, который лежит рядом с моделью'}
   actions={<StudioButton size="sm" disabled={disabled||!history.length} onClick={()=>{setRecipe(history[history.length-1]);setHistory(history.slice(0,-1));}}>Отменить</StudioButton>}>
   <StudioButton variant={recipe.items.length?'secondary':'primary'} disabled={disabled||!guid} onClick={()=>void run(async()=>{setInfo(await invoke('studio.humanWardrobeInfo',{guid}));edit(await invoke('studio.humanWardrobeFromAsset',{guid}));})}><Shirt size={14}/> Загрузить комплект модели</StudioButton>
   {!guid&&<HcNote tone="warn">Сначала выберите персонажа в «Пресетах».</HcNote>}
   {recipe.items.length>0&&<HcSwitch label="Примерять во вьюпорте" hint={trying?'Обновляю примерку…':'Текстуры 256 px, на диск ничего не пишется'} checked={tryOn} disabled={!guid} onChange={setTryOn}/>}
   <div className="hc-items">
  {info&&<details><summary>Скрыть части модели под одеждой</summary>
   {info.meshes.map(mesh=><WardrobeField key={mesh} label={mesh}><StudioInput type="checkbox" aria-label={'Показывать '+mesh} checked={!recipe.hiddenMeshes.includes(mesh)} disabled={disabled} onChange={()=>edit({...recipe,hiddenMeshes:toggle(recipe.hiddenMeshes,mesh)})}/></WardrobeField>)}
  </details>}
  <details><summary>Точки крепления (сокеты)</summary><div className="au-stack">
   {(recipe.sockets??[]).map((socket,index)=><div className="au-stack" key={index}>
    <WardrobeField label="Имя сокета"><StudioInput value={socket.name} disabled={disabled} onChange={e=>{const next=e.target.value;edit({...recipe,sockets:(recipe.sockets??[]).map((v,i)=>i===index?{...v,name:next}:v),items:recipe.items.map(v=>v.socket===socket.name?{...v,socket:next}:v)});}}/></WardrobeField>
    <WardrobeField label="Родительская кость"><StudioSelect value={socket.bone} disabled={disabled} onChange={e=>socketEdit(index,{bone:e.target.value})}><option value="">Выберите кость</option>{info?.bones.map((bone,i)=><option key={i}>{bone}</option>)}</StudioSelect></WardrobeField>
    {(['translation','rotation','scale'] as const).map(field=><details key={field}><summary>{{translation:'Положение сокета XYZ',rotation:'Поворот сокета XYZ (радианы)',scale:'Масштаб сокета XYZ'}[field]}</summary>{socket[field].map((value,axis)=><WardrobeField key={axis} label={'XYZ'[axis]}><StudioInput type="number" step={.01} value={value} disabled={disabled} onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n)&&Math.abs(n)<=100&&(field!=='scale'||n>.001))socketEdit(index,{[field]:socket[field].map((v,i)=>i===axis?n:v)});}}/></WardrobeField>)}</details>)}
    <StudioButton disabled={disabled||recipe.items.some(i=>i.socket===socket.name)} onClick={()=>edit({...recipe,sockets:(recipe.sockets??[]).filter((_,i)=>i!==index)})}>Удалить неиспользуемый сокет</StudioButton>
   </div>)}
   <StudioButton disabled={disabled||!info||(recipe.sockets?.length??0)>=128} onClick={()=>edit({...recipe,sockets:[...(recipe.sockets??[]),{name:'socket_'+Date.now(),bone:'',translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1]}]})}>Добавить сокет</StudioButton>
  </div></details>
  {recipe.items.map((item,index)=><div key={item.id} className={'hc-item'+(item.enabled?'':' hc-item--off')}>
   <div className="hc-item__row">
    <label className="hc-item__toggle" title={item.enabled?'Снять':'Надеть'}><input type="checkbox" role="switch" aria-label={(item.enabled?'Снять ':'Надеть ')+assetLabel(paths[item.asset]??item.asset)} checked={item.enabled} disabled={disabled} onChange={e=>itemEdit(index,{enabled:e.target.checked})}/><i aria-hidden="true"/></label>
    <div className="hc-item__name"><strong>{assetLabel(paths[item.asset]??'')}</strong><small>{slotLabel(item.slot)}{item.finish==='source'?' · металл/украшение':''}</small></div>
    <label className="hc-item__color" title="Цвет вещи"><input type="color" aria-label={'Цвет '+assetLabel(paths[item.asset]??'')} disabled={disabled}
     value={rgbToHex((item.tint??[1,1,1]).map(v=>Math.min(1,v)))}
     onChange={e=>{const rgb=hexToRgb(e.target.value);if(rgb)itemEdit(index,{tint:rgb});}}/></label>
    {item.tint&&<button type="button" className="hc-reset" title="Исходный цвет" disabled={disabled} onClick={()=>itemEdit(index,{tint:undefined})}>↺</button>}
   </div>
   <details className="hc-item__more"><summary>Настроить крепление</summary><div className="au-stack">
   <AssetReferencePicker value={item.asset} kinds={['model']} label="SKM / меш / аксессуар" disabled={disabled} onChange={asset=>itemEdit(index,{asset})}/>
   <WardrobeField label="Слот" hint="Произвольное имя. Для нескольких креплений на одной кости используйте разные слоты."><StudioInput value={item.slot} disabled={disabled} onChange={e=>itemEdit(index,{slot:e.target.value})}/></WardrobeField>
   <WardrobeField label="Режим скелета"><StudioSelect value={item.independentRig?'separate':'shared'} disabled={disabled} onChange={e=>itemEdit(index,e.target.value==='separate'?{independentRig:true,attachmentRule:'keep_world'}:{independentRig:false,attachmentRule:undefined,socket:'',bone:'',translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1]})}><option value="shared">Общий · совместимая одежда</option><option value="separate">Отдельный · крепление любого SKM</option></StudioSelect></WardrobeField>
   {item.independentRig&&<WardrobeField label="Правило крепления"><StudioSelect value={item.attachmentRule??''} disabled={disabled} onChange={e=>itemEdit(index,{attachmentRule:e.target.value?e.target.value as 'keep_world'|'snap':undefined})}><option value="">Исходная привязка</option><option value="keep_world">Сохранить исходное положение</option><option value="snap">Совместить с костью / сокетом</option></StudioSelect></WardrobeField>}
   <WardrobeField label="Именованный сокет"><StudioSelect value={item.socket??''} disabled={disabled} onChange={e=>itemEdit(index,{socket:e.target.value,bone:'',...(e.target.value?{independentRig:true,attachmentRule:item.attachmentRule??'keep_world'}:{})})}><option value="">Напрямую к кости</option>{(recipe.sockets??[]).map((socket,i)=><option key={i} value={socket.name}>{socket.name}</option>)}</StudioSelect></WardrobeField>
   <WardrobeField label="Материал"><StudioSelect value={item.finish} disabled={disabled} onChange={e=>itemEdit(index,{finish:e.target.value as 'cloth'|'source'})}><option value="cloth">Ткань · настройки Clothing</option><option value="source">Исходный PBR · металл, украшения</option></StudioSelect></WardrobeField>
   <WardrobeField label="Кость крепления"><StudioSelect value={item.bone} disabled={disabled||!!item.socket} onChange={e=>itemEdit(index,{bone:e.target.value,...(e.target.value?{independentRig:true,attachmentRule:item.attachmentRule??'snap'}:{})})}><option value="">Исходный скелет / мировое положение</option>{info?.bones.map((bone,i)=><option key={i}>{bone}</option>)}</StudioSelect></WardrobeField>
   {(['translation','rotation','scale'] as const).map(field=><details key={field}><summary>{{translation:'Смещение XYZ (единицы кости)',rotation:'Поворот XYZ (радианы)',scale:'Масштаб XYZ'}[field]}</summary>{item[field].map((value,axis)=><WardrobeField key={axis} label={'XYZ'[axis]}><StudioInput type="number" step={.01} value={value} disabled={disabled} onChange={e=>{const n=e.target.valueAsNumber;if(Number.isFinite(n)&&Math.abs(n)<=100&&(field!=='scale'||n>.001))itemEdit(index,{[field]:item[field].map((v,i)=>i===axis?n:v)});}}/></WardrobeField>)}</details>)}
   <details><summary>Скрывать меши при надевании</summary>{info?.meshes.map(mesh=><WardrobeField key={mesh} label={mesh}><StudioInput type="checkbox" aria-label={'Скрывать '+mesh} checked={item.hideMeshes.includes(mesh)} disabled={disabled} onChange={()=>itemEdit(index,{hideMeshes:toggle(item.hideMeshes,mesh)})}/></WardrobeField>)}</details>
   {item.masks.map((mask,mi)=><div className="au-stack" key={mi}>
    <WardrobeField label="Материал тела для маски"><StudioSelect value={mask.material} disabled={disabled} onChange={e=>itemEdit(index,{masks:item.masks.map((m,i)=>i===mi?{...m,material:e.target.value}:m)})}><option value="">Выберите материал тела</option>{info?.materials.map((m,i)=><option key={i}>{m}</option>)}</StudioSelect></WardrobeField>
    <AssetReferencePicker value={mask.texture} kinds={['image']} label="Маска видимости · чёрное скрыто, белое видно" disabled={disabled} onChange={texture=>itemEdit(index,{masks:item.masks.map((m,i)=>i===mi?{...m,texture}:m)})}/>
    <StudioButton disabled={disabled} onClick={()=>itemEdit(index,{masks:item.masks.filter((_,i)=>i!==mi)})}>Убрать маску</StudioButton>
   </div>)}
   <div className="au-row"><StudioButton disabled={disabled||!info||item.masks.length>=16} onClick={()=>itemEdit(index,{masks:[...item.masks,{material:'',texture:''}]})}>Добавить маску тела</StudioButton><StudioButton disabled={disabled} onClick={()=>edit({...recipe,items:recipe.items.filter((_,i)=>i!==index)})}>Убрать вещь</StudioButton></div>
  </div></details></div>)}
   </div>
  <div className="au-row"><StudioButton disabled={disabled||!info||recipe.items.length>=32} onClick={()=>edit({...recipe,items:[...recipe.items,{id:'item_'+Date.now(),asset:'',slot:slots.find(s=>!recipe.items.some(i=>i.enabled&&i.slot===s))??'attachment_'+Date.now(),enabled:true,bone:'',translation:[0,0,0],rotation:[0,0,0],scale:[1,1,1],finish:'cloth',hideMeshes:[],masks:[]}]})}>Добавить одежду / аксессуар</StudioButton></div>
  </HcCard>
  <HcCard title="Собрать персонажа" hint="Одежда, форма лица и внешность запекаются в новый ассет content/Characters">
  <WardrobeField label="Имя собранного персонажа" hint="Новый ассет в content/Characters. Существующие сборки не перезаписываются."><StudioInput value={name} disabled={disabled} onChange={e=>setName(e.target.value)}/></WardrobeField>
  <WardrobeField label="Максимальный размер запекаемых текстур"><StudioSelect value={size} disabled={disabled} onChange={e=>setSize(Number(e.target.value))}>{[256,512,1024,2048,4096].map(n=><option key={n}>{n}</option>)}</StudioSelect></WardrobeField>
  <StudioButton variant="primary" disabled={disabled||!guid||!(/^[A-Za-z0-9_-]{1,64}$/).test(name)} onClick={()=>void run(async()=>{setJob(await invoke('studio.humanCharacterBuild',{guid,look,wardrobe:recipe,name,textureSize:size,face}));})}>{job?.running?'Собирается…':'Собрать персонажа'}</StudioButton>
  {job?.running&&<StudioAlert>Импорт, запекание материалов и масок, сборка мешей, проверка повторным импортом GLB…</StudioAlert>}
  {job?.path&&<><StudioAlert tone="success">{job.path} · {job.meshes} мешей · {job.materials} материалов · {job.vertices} вершин. Рецепт сохранён рядом в source.json.</StudioAlert>
   {job.lodTriangles&&<LodBudget tris={job.lodTriangles} bytes={job.bytes??0} textureSize={job.textureSize??size}/>}
   <StudioButton disabled={disabled||!job.guid} onClick={()=>void run(async()=>{const current=await invoke('studio.humanAppearanceInfo',{});await invoke('studio.humanAppearanceApply',{guid:job.guid!,revision:current.preview.revision,face:null,look:{version:1,values:{},materials:{}}});onShow?.(job.guid!);})}>Показать собранного персонажа</StudioButton>
   <StudioButton disabled={disabled} title="Добавить тип юнита с этой моделью в rules.json и поставить его на карту" onClick={()=>void run(async()=>{const r=await invoke('studio.humanPlaceInGame',{type:name.replace(/[^A-Za-z0-9_]/g,'_').replace(/^(\d)/,'_$1'),path:job.path!,name});setPlaced(r.placed?'Тип «'+r.type+'» добавлен в правила и поставлен в центр карты':'Тип «'+r.type+'» добавлен в правила — поставьте его на карту из палитры');})}>В игру</StudioButton>
   {placed&&<HcNote>{placed}</HcNote>}</>}
  </HcCard>
 </>;
}

// Game budget: triangles per LOD against common targets for a hero
// character, plus file size and baked texture size.
function LodBudget({tris,bytes,textureSize}:{tris:number[];bytes:number;textureSize:number}) {
 const limits=[100000,50000,20000,8000,3000];
 const fmt=(n:number)=>n>=1000?(n/1000).toFixed(n>=10000?0:1)+'k':String(n);
 return <div className="hc-budget"><strong>Бюджет для игры</strong>
  {tris.map((t,i)=>{const limit=limits[Math.min(i,limits.length-1)],pct=Math.min(100,t/limit*100);
   return <div key={i} className={'hc-budget__row'+(t>limit?' hc-budget__row--over':'')}><span>LOD{i}</span><i style={{['--hc-fill' as string]:pct+'%'}}/><span>{fmt(t)} / {fmt(limit)} тр.</span></div>;})}
  {tris.length<2&&<small className="hc-hint">Нет LOD-уровней: на расстоянии персонаж будет рисоваться в полной детализации.</small>}
  <small className="hc-hint">Файл {(bytes/1048576).toFixed(1)} МБ · текстуры до {textureSize} px{textureSize>2048?' — тяжело для толпы':''}</small>
 </div>;
}

function WardrobeField({label,hint,children}:{label:string;hint?:string;children:ReactElement<{id?:string}>}) {
 const id=useId();return <StudioField label={label} hint={hint} htmlFor={id}>{cloneElement(children,{id})}</StudioField>;
}
