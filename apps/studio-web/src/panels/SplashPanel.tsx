import { useEffect, useId, useRef, useState } from 'react';
import { invoke } from '../bridge';

type Block = Record<string, unknown>;
type Settings = { splash: unknown; splashVersion: string; splashDefaults: Block; portrait: boolean;
  resolved: { splashEnabled: boolean; splashMinimumSeconds?: number; splashSequenceSeconds?: number; nativeLaunchImage?: string; nativeLaunchBackground?: string } };
const obj = (value: unknown): Block => typeof value === 'object' && value !== null && !Array.isArray(value) ? value as Block : {};
const format = (value: unknown) => JSON.stringify(value ?? null, null, 2);
async function request<T>(project: string, op: string, params: Block = {}) {
  return (await invoke('studio.op', { op, params, expectedProject: project })).result as T;
}

function Timeline({ project, settings }: {project:string; settings:Settings}) {
  const [start, seek] = useState(0);
  const [time, setTime] = useState(0);
  const [playing, play] = useState(false);
  const [restart, restartClock] = useState(0);
  const [frame, setFrame] = useState('');
  const [warnings, setWarnings] = useState<string[]>([]);
  const [error, setError] = useState('');
  const current = useRef(0);
  const duration = Math.max(2, Math.max(settings.resolved.splashSequenceSeconds ?? 0, settings.resolved.splashMinimumSeconds ?? 0) + 1);
  useEffect(() => {
    let live = true;
    let timer: ReturnType<typeof setTimeout>;
    let clockStart: number | undefined;
    async function batch(at: number) {
      const times = [at];
      if (playing) while (times.length < 8 && times[times.length - 1] < duration)
        times.push(Math.min(duration, at + times.length / 10));
      try {
        const r = await request<{frames:string[];warnings:string[]}>(project, 'splash_preview', {times, frames:true,
          width:settings.portrait ? 180 : 320, height:settings.portrait ? 320 : 180});
        if (!live) return;
        if (r.frames.length !== times.length) throw new Error('Incomplete splash preview');
        clockStart ??= performance.now();
        setError(''); setWarnings(r.warnings);
        function show(index:number) {
          if (!live) return;
          current.current = times[index]; setTime(times[index]); setFrame(r.frames[index]);
          if (!playing) return;
          if (times[index] >= duration) { seek(duration); play(false); return; }
          const nextTime = Math.min(duration, times[index]+0.1);
          const deadline = clockStart! + (nextTime-start)*1000;
          timer = setTimeout(() => {
            if (index+1 < times.length) show(index+1);
            else void batch(Math.min(duration, Math.max(nextTime, start+(performance.now()-clockStart!)/1000)));
          }, Math.max(0, deadline-performance.now()));
        }
        show(0);
      } catch (e) { if (live) { setError(String(e)); seek(current.current); play(false); } }
    }
    timer = setTimeout(() => void batch(start), playing ? 0 : 90);
    return () => { live = false; clearTimeout(timer); };
  }, [project, settings, start, playing, duration, restart]);
  useEffect(() => {
    const pause = () => { if (document.hidden) { seek(current.current); play(false); } };
    document.addEventListener('visibilitychange',pause);
    return () => document.removeEventListener('visibilitychange',pause);
  }, []);
  return <section className="brand-preview"><div className="brand-heading"><h3>Loading preview</h3><small>Saved settings</small></div>
    <div className={'splash-stage'+(settings.portrait?' portrait':'')}>{frame ? <img src={'data:image/png;base64,'+frame} alt="Loading screen at the selected time"/> : <span>Rendering…</span>}</div>
    <label>Timeline <input type="range" min={0} max={duration} step=".01" value={time}
      onChange={e => {const t=Number(e.target.value);play(false);setTime(t);seek(t);}}/></label>
    <div className="splash-transport"><button onClick={() => {seek(playing ? current.current : time >= duration ? 0 : time);play(!playing);}}>{playing?'Pause':'Play'}</button>
      <button onClick={() => {seek(0);setTime(0);restartClock(n=>n+1);play(true);}}>Restart</button><output>{time.toFixed(2)} / {duration.toFixed(2)} s</output></div>
    {error && <p role="alert">{error}</p>}{warnings.map((w,i)=><p key={i} className="brand-warning">{w}</p>)}
  </section>;
}

export default function SplashPanel({project,images,lockTabs}: {project:string;images:string[];lockTabs:(locked:boolean)=>void}) {
  const imageList = useId();
  const [settings, setSettings] = useState<Settings>();
  const [draft, setDraft] = useState('');
  const [revision, reload] = useState(0);
  const [error, setError] = useState('');
  const [saving, setSaving] = useState(false);
  const dirty = !!settings && draft !== format(settings.splash);
  useEffect(()=>{lockTabs(dirty || saving);return ()=>lockTabs(false);},[dirty,saving,lockTabs]);
  useEffect(()=>{
    let live=true;
    setSettings(undefined);setError('');
    void request<Settings>(project,'branding_get').then(r=>{if(live){setSettings(r);setDraft(format(r.splash));}}).catch(e=>{if(live)setError(String(e));});
    return ()=>{live=false;};
  },[project,revision]);
  if (!settings) return error ? <p role="alert">{error} <button onClick={()=>reload(r=>r+1)}>Retry</button></p> : <p>Loading splash settings…</p>;
  let raw:unknown, invalid=false;
  try {raw=JSON.parse(draft);} catch {invalid=true;}
  const disabled = raw === false || raw === 'off' || raw === 'none';
  const mode = disabled ? 'off' : raw && typeof raw === 'object' ? 'custom' : 'engine';
  const block=obj(raw);
  // Keep inherited settings as overrides when editing the engine sequence.
  const base:Block = mode === 'engine' ? {base:'engine'} : block;
  const effective = mode === 'engine' || (block.base === 'engine' || block.base === 'default') ? obj(settings.splashDefaults) : {};
  const show = (key:string) => block[key] ?? effective[key];
  const progress = {...obj(effective.progress), ...obj(block.progress)};
  const showBar = typeof show('progress') === 'boolean' ? Boolean(show('progress')) : progress.show !== false;
  function change(key:string,value:unknown) {
    const next={...base};
    if(value==='' || value===undefined) delete next[key]; else next[key]=value;
    setDraft(format(next));
  }
  function field(label:string,key:string,type='text',placeholder='') {
    const value=show(key);
    return <label>{label}<input list={key==='image'?imageList:undefined} type={type} step={type==='number'?'.05':undefined} min={type==='number'?0:undefined}
      value={typeof value==='number'||typeof value==='string'?value:''} placeholder={placeholder}
      onChange={e=>change(key,type==='number' && e.target.value!=='' ? Number(e.target.value) : e.target.value)}/></label>;
  }
  async function save() {
    setSaving(true);setError('');
    try {await request(project,'branding_set',{splash:raw,expectedSplashVersion:settings!.splashVersion});reload(r=>r+1);}
    catch(e){setError(String(e));}finally{setSaving(false);}
  }
  return <div className="brand-content"><section className="brand-settings"><h3>Loading screen</h3>
    <datalist id={imageList}>{images.map(path=><option key={path} value={path}/>)}</datalist>
    <fieldset disabled={saving}>
      <label>Screen mode<select aria-label="Screen mode" value={mode} onChange={e=>setDraft(format(e.target.value==='off'?false:e.target.value==='engine'?null:{background:'#101923',minSeconds:2}))}>
        <option value="engine">Engine default</option><option value="custom">Project settings</option><option value="off">Disabled</option></select></label>
      {!disabled && !invalid && <>{field('Background','background','text','#101923')}{field('Background image','image','text','Project image path')}
        <label>Image fit<select aria-label="Image fit" value={String(show('fit')??'cover')} onChange={e=>change('fit',e.target.value)}>{['cover','contain','center','stretch'].map(v=><option key={v}>{v}</option>)}</select></label>
        <div className="brand-columns">{field('Minimum seconds','minSeconds','number')}{field('Maximum seconds','maxSeconds','number')}</div>
        {([['skippable','Allow skip',true],['loop','Loop during loading',false]] as const).map(([key,label,def])=>
          <label className="splash-check" key={key}><input type="checkbox" checked={Boolean(show(key)??def)} onChange={e=>change(key,e.target.checked)}/>{label}</label>)}
        <label className="splash-check"><input type="checkbox" checked={showBar}
          onChange={e=>change('progress',{...obj(block.progress),show:e.target.checked})}/>Show loading bar</label>
        <label>Loading bar colour<input placeholder="Default" value={String(progress.color??'')} onChange={e=>{const next={...obj(block.progress),show:showBar};change('progress',{...next,color:e.target.value||undefined});}}/></label>
        <details><summary>Native launch screen</summary>{['image','background','androidIcon'].map((k,i)=><label key={k}>{['Launch image','Launch background','Android launch icon'][i]}<input
          list={k==='image'||k==='androidIcon'?imageList:undefined} value={String(obj(block.native)[k]??'')} placeholder="Inherit" onChange={e=>change('native',{...obj(block.native),[k]:e.target.value||undefined})}/></label>)}
          <label>Launch image scale<input type="number" min="0.05" max="1" step="0.05" placeholder="0.4" value={typeof obj(block.native).scale==='number'?Number(obj(block.native).scale):''} onChange={e=>change('native',{...obj(block.native),scale:e.target.value===''?undefined:Number(e.target.value)})}/></label></details>
      </>}
      <details><summary>Sequence JSON</summary><label>Logos, frames and animation<textarea rows={10} value={draft} onChange={e=>setDraft(e.target.value)}/></label></details>
    </fieldset>
    {invalid&&<p role="alert">Enter valid JSON to save the sequence.</p>}{error&&<p role="alert">{error}</p>}
    <div className="brand-actions"><button disabled={!dirty||invalid||saving} onClick={()=>void save()}>Save splash settings</button><button disabled={saving} onClick={()=>reload(r=>r+1)}>{dirty?'Discard edits & reload':'Reload'}</button></div>
    </section>{settings.resolved.splashEnabled?<Timeline project={project} settings={settings}/>:<section><h3>Loading preview</h3><p>The saved project has its splash screen disabled.</p></section>}</div>;
}
