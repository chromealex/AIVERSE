import { useState } from 'react';
import { Bot, Brush, LayoutGrid, Play, Rocket, X } from 'lucide-react';
import { StudioDialog } from '../ui';
import './WelcomeTour.css';

const KEY = 'aiverse.studio.tour.v1';
export function tourSeen(): boolean { try { return window.localStorage.getItem(KEY) === '1'; } catch { return true; } }
function markSeen() { try { window.localStorage.setItem(KEY, '1'); } catch { /* private mode */ } }

const STEPS = [
  { icon: LayoutGrid, title: 'Workspaces for each kind of work',
    text: 'Scene, World, Look, Animate, Character, Logic and Publish (⌘1…⌘7) each bring their panels. Drag tabs to rearrange or out of the window to float them; Window ▸ Reset Layout brings a workspace back.' },
  { icon: Brush, title: 'Edit the world in the Scene',
    text: 'Q W E R pick Hand, Move, Rotate and Scale. In World, the Terrain, Tiles, Fields, Roads and Water panels have a Brush: drag in the Scene to sculpt, paint, scatter objects or draw a road or river — each stroke is one History step. [ ] change its size, Esc stops it.' },
  { icon: Play, title: 'Play in place',
    text: 'Play (⌘P) runs the saved map in the Game panel on the chosen device; ⌥-Play starts from the Scene camera. While playing, Hierarchy and Inspector show the live game, and Keep play changes copies positions back to the map.' },
  { icon: Bot, title: 'Ask the agent',
    text: 'The Agent panel runs Claude Code on this project through the same tools as the panels. Every change it makes is a History step and is listed so you can jump back or undo all of it.' },
  { icon: Rocket, title: 'Publish',
    text: 'Publish ▸ Build checks the toolchain, estimates the size and builds for each platform; Branding, Asset Audit and Frame Capture help before release. ⌘K finds any command, panel, object or asset.' },
];

/** First-run tour of the Studio (shown once; Window ▸ Welcome Tour shows it again). */
export default function WelcomeTour({ onClose }: { onClose: () => void }) {
  const [step, setStep] = useState(0);
  const close = () => { markSeen(); onClose(); };
  const s = STEPS[step], Icon = s.icon;
  return <StudioDialog className="tour" label="Welcome to AIVERSE" onDismiss={close}>
    <div className="tour__card">
      <button type="button" className="tour__close" aria-label="Close tour" onClick={close}><X size={14}/></button>
      <div className="tour__icon"><Icon size={26} strokeWidth={1.5}/></div>
      <small>Welcome to AIVERSE · {step + 1} / {STEPS.length}</small>
      <h2>{s.title}</h2>
      <p>{s.text}</p>
      <div className="tour__dots">{STEPS.map((_, i) => <button key={i} type="button" aria-label={'Step ' + (i + 1)} className={i === step ? 'is-on' : ''} onClick={() => setStep(i)}/>)}</div>
      <div className="tour__actions">
        <button type="button" className="au-button au-button--sm au-button--ghost" onClick={close}>Skip</button>
        {step > 0 && <button type="button" className="au-button au-button--sm" onClick={() => setStep(step - 1)}>Back</button>}
        <button type="button" className="au-button au-button--sm au-button--primary" onClick={() => step + 1 < STEPS.length ? setStep(step + 1) : close()}>
          {step + 1 < STEPS.length ? 'Next' : 'Start'}</button>
      </div>
    </div>
  </StudioDialog>;
}
