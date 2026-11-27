import { useEffect, useMemo, useRef, useState } from 'react';
import { Pause, Play, SkipBack } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioSnapshot } from '../types';
import type { StudioPanel } from '../nestedDock';
import { STUDIO_PANEL_TITLES } from '../nestedDock';
import { clipDuration, clipKeyTimes, projectSkeleton, type AnimDoc, type SampledNode } from './animLogic';
import './ToolPanel.css';
import './ProjectToolPanels.css';
import './AnimationTimeline.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const op = async (name: string, params: Record<string, unknown> = {}) => (await invoke('studio.op', { op: name, params })).result as any;

/** Animation: clips of the project's authoring models (modeling/*.model.json) on a timeline with a live skeleton preview. */
export default function AnimationTimeline({ snapshot, showPanel, route }: { route:import('../types').AnimationAuthoringRoute; snapshot: StudioSnapshot | null; showPanel: (p: StudioPanel) => void }) {
  const path = snapshot?.project.path ?? '';
  const [models, setModels] = useState<string[]>([]);
  const [model, setModel] = useState('');
  const [loaded, setLoaded] = useState<{model:string; revision:number; document:AnimDoc} | null>(null);
  const doc = loaded?.model === model ? loaded.document : null;
  const [clip, setClip] = useState('');
  const [selection, setSelection] = useState<{channel:number; key:number} | null>(null);
  useEffect(() => setSelection(null), [model, clip, path]);
  const [time, setTime] = useState(0);
  const [playing, setPlaying] = useState(false);
  const [pose, setPose] = useState<SampledNode[]>([]);
  const [view, setView] = useState<'front' | 'side'>('front');
  const [error, setError] = useState('');
  const requestPose = useRef<((time: number) => void) | null>(null);
  useEffect(() => {
    if (!path) return;
    void op('modeling_list').then(r => { const m: string[] = r.models ?? []; setModels(m); setModel(x => x || m[0] || ''); }).catch(e => setError(message(e)));
  }, [path]);
  useEffect(() => {
    setLoaded(null); setPlaying(false); setClip(''); setTime(0); setError('');
    if (!model) return;
    let live=true;
    void op('animation_get', { name: model }).then(r => {
      if(!live)return;
      const d = r.document as AnimDoc; setLoaded({model,revision:r.revision,document:d}); setClip(d.clips?.[0]?.id ?? ''); setTime(0); setError('');
    }).catch(e => {if(live)setError(message(e));});
    return ()=>{live=false;};
  }, [model]);
  useEffect(()=>{
    const target=route.request;
    if(target?.tab==='clips'&&target.project===path){
      setModel(target.name);setPlaying(false);setTime(0);route.consume?.();
    }
  },[route.request,path]);
  const current = doc?.clips?.find(c => c.id === clip);
  const channel = selection && current?.channels[selection.channel];
  const selectedValue = channel && selection ? channel.values?.[channel.interpolation === 'cubic' ? selection.key * 3 + 1 : selection.key] : undefined;
  const duration = current ? clipDuration(current) : 0;
  useEffect(() => {
    setPose([]);
    if (!doc || !clip) return;
    let active = true, sampling = false;
    let pending: number | undefined;
    requestPose.current = async target => {
      pending = target;
      if (sampling) return;
      sampling = true;
      while (active && pending !== undefined) {
        const sampleTime = pending;
        pending = undefined;
        try {
          const result = await op('animation_sample', { name: model, clip, time: sampleTime });
          if (active) { setPose(result.nodes ?? []); setError(''); }
        } catch (e) {
          if (active) { setError(message(e)); setPlaying(false); }
        }
      }
      sampling = false;
    };
    return () => { active = false; requestPose.current = null; };
  }, [model, clip, doc]);
  useEffect(() => { requestPose.current?.(time); }, [model, clip, time, doc]);
  useEffect(() => {
    if (!playing || duration <= 0) return;
    let last = performance.now(), raf = 0;
    const tick = (now: number) => {
      const dt = (now - last) / 1000; last = now;
      setTime(t => (t + dt) % duration);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [playing, duration]);
  const skeleton = useMemo(() => projectSkeleton(doc, pose, view, 260, 200), [doc, pose, view]);
  const keys = current ? clipKeyTimes(current) : [];
  const [saving, setSaving] = useState(false);
  async function writeKey(action: 'save' | 'add' | 'delete', form: HTMLFormElement) {
    if (!loaded || !current || !channel || !selection || !selectedValue || saving) return;
    const { key } = selection;
    const times = [...(channel.times ?? [])];
    const values = [...(channel.values ?? [])];
    const stride = channel.interpolation === 'cubic' ? 3 : 1;
    let next = key;
    if (action === 'save') {
      const fields = new FormData(form);
      const target = Number(fields.get('time'));
      if ((key > 0 && target <= times[key - 1]) || (key + 1 < times.length && target >= times[key + 1])) {
        setError('Key time must fall between its neighboring keys.'); return;
      }
      times[key] = target;
      values[key * stride + (stride === 3 ? 1 : 0)] = selectedValue.map((_, i) => Number(fields.get('v' + i)));
    } else if (action === 'add') {
      next = times.length;
      times.push(Number((times[next - 1] + 0.1).toFixed(6)));
      values.push(...values.slice(key * stride, (key + 1) * stride));
    } else {
      if (times.length < 2) return;
      times.splice(key, 1); values.splice(key * stride, stride);
      next = Math.min(key, times.length - 1);
    }
    const channels = current.channels.map((entry, index) => index === selection.channel ? {...entry, times, values} : entry);
    const value = {...current, channels, duration: Math.max(current.duration ?? 0, ...times)};
    setSaving(true); setPlaying(false); setError('');
    try {
      const response = await invoke('studio.op', {expectedProject:path, op:'animation_edit', params:{
        name:model, expectedRevision:loaded.revision, commands:[{op:'put', collection:'clips', id:clip, value}]
      }});
      const result = response.result as {document:AnimDoc; revision:number; error?:string};
      if (result.error) throw new Error(result.error);
      setLoaded(previous => previous === loaded ? {model, revision:result.revision, document:result.document} : previous);
      setSelection({channel:selection.channel, key:next}); setTime(times[next]);
    } catch (e) { setError(message(e)); } finally { setSaving(false); }
  }
  if (!path) return <div className="tool__empty">Open a project to preview its animations.</div>;
  return <div className="ptp anim">
    <div className="ptp__edhead">
      <div className="tbrush__chips">{models.map(m => <button key={m} type="button" className={m === model ? 'is-on' : ''} onClick={() => setModel(m)}>{m}</button>)}</div>
      {!models.length && <span className="ptp__meta">No authoring models (modeling/*.model.json) — create one in Modeling.</span>}
    </div>
    {error && <div className="tool__error" role="alert">{error}</div>}
    {doc && <div className="anim__body">
      <div className="anim__preview">
        <svg viewBox="0 0 260 200" role="img" aria-label="Skeleton preview">
          <line x1={0} x2={260} y1={skeleton.ground} y2={skeleton.ground} className="anim__ground"/>
          {skeleton.bones.map((b, i) => <line key={i} x1={b[0]} y1={b[1]} x2={b[2]} y2={b[3]} className="anim__bone"/>)}
          {skeleton.joints.map((j, i) => <circle key={i} cx={j[0]} cy={j[1]} r={2.5} className="anim__joint"><title>{j[2]}</title></circle>)}
        </svg>
        <div className="ptp__seg anim__view">
          {(['front', 'side'] as const).map(v => <button key={v} type="button" className={view === v ? 'is-on' : ''} onClick={() => setView(v)}>{v}</button>)}
        </div>
      </div>
      <div className="anim__side">
        <div className="tbrush__chips">{(doc.clips ?? []).map(c => <button key={c.id} type="button" className={c.id === clip ? 'is-on' : ''}
          onClick={() => { setClip(c.id); setTime(0); }}>{c.id}</button>)}</div>
        {!doc.clips?.length && <p className="ptp__hint">This model has no clips yet (Animation Clips ▸ Generate).</p>}
        {current && <>
          <div className="anim__transport">
            <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="To start" onClick={() => setTime(0)}><SkipBack size={13}/></button>
            <button type="button" className="au-button au-button--sm au-button--primary" onClick={() => setPlaying(!playing)}>{playing ? <Pause size={13}/> : <Play size={13}/>}{playing ? 'Pause' : 'Play'}</button>
            <span className="ptp__meta">{time.toFixed(2)} / {duration.toFixed(2)} s · {current.channels.length} channels · {keys.length} keys</span>
          </div>
          <div className="anim__track">
            <input type="range" aria-label="Clip time" aria-valuetext={time.toFixed(2)+' seconds'}
              title="Clip time · Shift + arrow to jump between keys" min={0} max={Math.max(duration, 0.01)}
              step={0.01} value={time}
              onKeyDown={event => {
                if (!event.shiftKey || (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight')) return;
                event.preventDefault();
                const next = event.key === 'ArrowRight'
                  ? keys.find(key => key > time)
                  : keys.slice().reverse().find(key => key < time);
                if (next !== undefined) { setPlaying(false); setTime(next); }
              }}
              onChange={e => { setPlaying(false); setTime(Number(e.target.value)); }}/>
            <div className="anim__keys">{keys.map((k, i) => <i key={i} style={{ left: (duration ? k / duration : 0) * 100 + '%' }} onClick={() => { setPlaying(false); setTime(k); }}/>)}</div>
          </div>
          <div className="anim__channels">{current.channels.map((c, i) => <div key={i} className="anim__channel">
            <span title={c.node + '.' + (c.path ?? 'rotation')}>{c.node}<small>.{c.path ?? 'rotation'}</small></span>
            <div>{(c.times ?? []).map((t: number, k: number) => <button key={k} type="button"
              aria-label={c.node + '.' + (c.path ?? 'rotation') + ' key ' + (k + 1) + ' at ' + t + ' s'}
              aria-pressed={selection?.channel === i && selection.key === k}
              style={{ left: (duration ? t / duration : 0) * 100 + '%' }}
              onClick={() => { setPlaying(false); setTime(t); setSelection({channel:i, key:k}); }}/>)}</div>
          </div>)}</div>
          {channel && selection && <output className="ptp__meta">{channel.node}.{channel.path ?? 'rotation'} · Key {selection.key + 1} · {channel.times?.[selection.key]} s · {selectedValue?.join(', ') ?? '—'}</output>}
          {channel && selection && selectedValue && <form className="anim__edit"
            key={[model, clip, selection.channel, selection.key, loaded?.revision].join('/')}
            onSubmit={event => {event.preventDefault(); void writeKey('save', event.currentTarget);}}>
            <fieldset disabled={saving}><legend>Edit selected key</legend>
              <label>Time (s)<input type="number" name="time" required min="0" step="any" defaultValue={channel.times?.[selection.key]}/></label>
              {selectedValue.map((v,i) => <label key={i}>{['X','Y','Z','W'][i] ?? i+1}<input type="number" name={'v'+i} required step="any" defaultValue={v}/></label>)}
              <button className="au-button au-button--sm" type="submit">Save key</button>
              <button className="au-button au-button--sm" type="button" onClick={e => void writeKey('add', e.currentTarget.form!)}>Append key</button>
              <button className="au-button au-button--sm" type="button" disabled={(channel.times?.length ?? 0)<2} onClick={e => void writeKey('delete', e.currentTarget.form!)}>Delete key</button>
            </fieldset>
          </form>}
        </>}
      </div>
    </div>}
    <div className="anim__links">
      <button type="button" className="au-button au-button--sm au-button--ghost" disabled={!doc}
        onClick={() => void invoke('studio.op', {expectedProject:path, op:'animation_editor', params:{name:model,tab:'graph'}})
          .catch(e => setError(message(e)))}>Open model graph</button>
      {(['anim_assets', 'anim_graph', 'anim_rig', 'model_anim'] as const).map(p =>
        <button key={p} type="button" className="au-button au-button--sm au-button--ghost" onClick={() => showPanel(p)}>{STUDIO_PANEL_TITLES[p]}</button>)}
    </div>
  </div>;
}
