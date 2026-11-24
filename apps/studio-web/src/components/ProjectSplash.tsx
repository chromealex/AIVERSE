import { useEffect, useRef, useState } from 'react';
import { Check } from 'lucide-react';
import { onNativeEvent } from '../bridge';
import type { ProjectLoadState } from './ProjectLoading';
import { pickSky, splashStages } from './projectSplashLogic';
import SplashArt from './SplashArt';
import skyDawn from '../assets/splash/sky-dawn.jpg';
import skyGolden from '../assets/splash/sky-golden.jpg';
import skyDay from '../assets/splash/sky-day.jpg';
import skyDusk from '../assets/splash/sky-dusk.jpg';
import skyStorm from '../assets/splash/sky-storm.jpg';
import './ProjectSplash.css';
import { StudioLoader } from '../ui';

/** Engine renders of the PhysicalSky (build/claude/studio-sky-shots.command): the default background when a game has no splash art. */
const SKIES = [skyDawn, skyGolden, skyDay, skyDusk, skyStorm];

export interface ProjectSplashInfo { path: string; name: string; module?: string; maps?: number; background?: string; art?: string; logo?: string; holdMs?: number }

const TIPS = [
  '⌘1…⌘7 switch workspaces; each remembers its own layout',
  'In World, a Brush sculpts, paints or scatters objects in the Scene; [ ] resize it',
  '⌥-Play starts the game from the Scene camera',
  'Keep play changes copies positions from the running game back to the map',
  'The Agent panel edits the project with the same tools as the panels; every change is one undo step',
  '⌘K finds any command, panel, object or asset',
  'Drag a tab out of the window to float it, onto another screen too',
];

/** Full-window splash while a project opens: the game's art and name, staged progress, a rotating tip. */
export default function ProjectSplash({ state, onClose, onHub }: { state: ProjectLoadState | null; onClose: () => void; onHub: () => void }) {
  const [info, setInfo] = useState<ProjectSplashInfo | null>(null);
  const [dismissed, setDismissed] = useState(false);
  const [elapsed, setElapsed] = useState(0);
  const [tip, setTip] = useState(() => Math.floor(Math.random() * TIPS.length));
  useEffect(() => onNativeEvent((event, payload) => {
    if (event === 'project.splash' && payload && typeof payload === 'object') {
      const p = payload as ProjectSplashInfo;
      setInfo(p); setDismissed(false); setHoldUntil(p.holdMs ? performance.now() + p.holdMs : 0);
    }
  }), []);
  // Host automation (AIVERSE_STUDIO_SPLASH_HOLD) keeps the finished splash up for a screenshot.
  const [holdUntil, setHoldUntil] = useState(0);
  const [, setTick] = useState(0);
  useEffect(() => {
    if (!holdUntil) return;
    const t = window.setTimeout(() => setTick(i => i + 1), Math.max(0, holdUntil - performance.now()) + 20);
    return () => window.clearTimeout(t);
  }, [holdUntil]);
  const lastState = useRef<ProjectLoadState | null>(null);
  if (state) lastState.current = state;
  const holding = holdUntil > performance.now();
  if (!holding && !state) lastState.current = null;
  state = state ?? (holding ? lastState.current : null);
  const active = !!state && !!info && !dismissed && (state.phase !== 'done' || holding);
  useEffect(() => {
    if (!active) return;
    const started = performance.now();
    const t = window.setInterval(() => setElapsed((performance.now() - started) / 1000), 200);
    const r = window.setInterval(() => setTip(i => (i + 1) % TIPS.length), 4500);
    return () => { window.clearInterval(t); window.clearInterval(r); };
  }, [active, info?.path]);
  if (!active || !state || !info) return null;
  const stages = splashStages(state);
  const failed = state.phase === 'error';
  const style = info.background && /^#[0-9a-f]{3,8}$/i.test(info.background) ? { backgroundColor: info.background } : undefined;
  return <div className={'psplash' + (failed ? ' is-error' : '')} style={style} data-native-overlay role="dialog" aria-modal="true" aria-label={'Opening ' + info.name}>
    {info.art ? <div className="psplash__art" style={{ backgroundImage: `url(${info.art})` }}/>
      : SKIES.length ? <div className="psplash__art is-sky" style={{ backgroundImage: `url(${SKIES[pickSky(info.name, SKIES.length)]})` }}/>
      : <SplashArt seed={info.name}/>}
    <div className="psplash__shade"/>
    <div className="psplash__brand">AIVERSE</div>
    {info.logo && <img className="psplash__logo" src={info.logo} alt=""/>}
    <div className="psplash__title">
      <small>{failed ? 'Could not open the project' : 'Opening project'}</small>
      <h1>{info.name}</h1>
      <p>{info.path}{info.module ? ' · ' + info.module.toUpperCase() : ''}{info.maps ? ` · ${info.maps} map${info.maps > 1 ? 's' : ''}` : ''}</p>
    </div>
    <div className="psplash__foot">
      {failed ? <div className="psplash__error">
        <p>{state.message}</p>
        <div><button type="button" className="au-button au-button--sm" onClick={() => { onClose(); onHub(); }}>Back to hub</button>
          <button type="button" className="au-button au-button--sm au-button--ghost" onClick={onClose}>Close</button></div>
      </div> : <>
        <div className="psplash__stages">
          {stages.steps.map(s => <span key={s.label} className={'is-' + s.state}>
            {s.state === 'done' ? <Check size={11}/> : s.state === 'active' ? <StudioLoader size={14}/> : null}{s.label}</span>)}
          <em>{elapsed.toFixed(1)} s</em>
        </div>
        <div className="psplash__bar" role="progressbar" aria-valuemin={0} aria-valuemax={100} aria-valuenow={Math.round(stages.ratio * 100)}>
          <span style={{ width: (stages.ratio * 100).toFixed(1) + '%' }}/></div>
        <div className="psplash__meta">
          <span>{stages.detail}</span>
          <span className="psplash__tip">Tip: {TIPS[tip]}</span>
        </div>
        {state.phase !== 'project' && <button type="button" className="psplash__continue" onClick={() => setDismissed(true)}>Continue while loading</button>}
      </>}
    </div>
  </div>;
}
