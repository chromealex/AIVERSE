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
  const [doc, setDoc] = useState<AnimDoc | null>(null);
  const [clip, setClip] = useState('');
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
    if (!model) { setDoc(null); return; }
    let live=true;
    void op('animation_get', { name: model }).then(r => {
      if(!live)return;
      const d = r.document as AnimDoc; setDoc(d); setClip(d.clips?.[0]?.id ?? ''); setTime(0); setError('');
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
  const duration = current ? clipDuration(current) : 0;
  useEffect(() => {
    setPose([]);
    if (!model || !clip) return;
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
  }, [model, clip]);
  useEffect(() => { requestPose.current?.(time); }, [model, clip, time]);
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
          <div className="anim__channels">{current.channels.slice(0, 40).map((c, i) => <div key={i} className="anim__channel">
            <span title={c.node + '.' + (c.path ?? 'rotation')}>{c.node}<small>.{c.path ?? 'rotation'}</small></span>
            <div>{(c.times ?? []).map((t: number, k: number) => <i key={k} style={{ left: (duration ? t / duration : 0) * 100 + '%' }}/>)}</div>
          </div>)}</div>
        </>}
      </div>
    </div>}
    <div className="anim__links">
      {(['anim_assets', 'anim_graph', 'anim_rig', 'model_anim'] as const).map(p =>
        <button key={p} type="button" className="au-button au-button--sm au-button--ghost" onClick={() => showPanel(p)}>{STUDIO_PANEL_TITLES[p]}</button>)}
    </div>
  </div>;
}
