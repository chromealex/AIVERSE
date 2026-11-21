import { useState, type KeyboardEvent } from 'react';
import { alignedViewName, gizmoArms, viewForAxis, type GizmoArm, type SceneAxis, type SceneCameraState } from '../panels/sceneCamera';
import './SceneAxisGizmo.css';

const SIZE = 96;
const C = SIZE / 2;
const R = 31;
const LABEL: Record<SceneAxis, string> = { x: 'X', y: 'Y', z: 'Z' };

/**
 * Unity-style Scene orientation gizmo. Pure presentation of the native camera
 * state; clicking an arm requests that axis view, the centre toggles Persp/Iso.
 */
export default function SceneAxisGizmo({ camera, disabled, onView, onToggleProjection }: {
  camera: SceneCameraState;
  disabled?: boolean;
  onView: (view: { yaw: number; pitch: number }, axis: SceneAxis, sign: 1 | -1) => void;
  onToggleProjection: () => void;
}) {
  const [hover, setHover] = useState<string | null>(null);
  const arms = gizmoArms(camera.yaw, camera.pitch, R);
  const aligned = alignedViewName(camera.yaw, camera.pitch);
  const caption = (aligned ? aligned + ' · ' : '') + (camera.ortho ? 'Iso' : 'Persp');
  const activate = (arm: GizmoArm) => {
    if (!disabled) onView(viewForAxis(arm.axis, arm.sign), arm.axis, arm.sign);
  };
  const key = (event: KeyboardEvent, action: () => void) => {
    if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); action(); }
  };
  // Pointer events never reach the viewport navigation handlers.
  const stop = (event: { stopPropagation: () => void }) => event.stopPropagation();
  return <div className={'scene-axis-gizmo' + (disabled ? ' is-disabled' : '')}
    onPointerDown={stop} onPointerUp={stop} onPointerMove={stop} onWheel={stop} onContextMenu={stop}>
    <svg width={SIZE} height={SIZE} viewBox={`0 0 ${SIZE} ${SIZE}`} role="group" aria-label="Scene orientation">
      <circle className="scene-axis-gizmo__backdrop" cx={C} cy={C} r={R + 11}/>
      {arms.map(arm => {
        const id = arm.axis + arm.sign;
        const length = Math.hypot(arm.x, arm.y);
        const ux = length > 1e-3 ? arm.x / length : 0, uy = length > 1e-3 ? arm.y / length : 0;
        const tipX = C + arm.x, tipY = C + arm.y;
        // Fixed-size cone at the arm tip, shortened with the arm's foreshortening.
        const coneLength = Math.min(length, Math.max(7, 15 * length / R));
        const baseX = tipX - ux * coneLength, baseY = tipY - uy * coneLength;
        const half = 6;
        const cone = `${tipX},${tipY} ${baseX - uy * half},${baseY + ux * half} ${baseX + uy * half},${baseY - ux * half}`;
        const cls = 'scene-axis-gizmo__arm axis-' + arm.axis + (arm.sign < 0 ? ' is-negative' : '') +
          (hover === id ? ' is-hover' : '');
        const target = viewForAxis(arm.axis, arm.sign);
        const name = alignedViewName(target.yaw, target.pitch);
        const pointing = length <= 4; // arm points straight at / away from the viewer
        return <g key={id} className={cls} role="button" tabIndex={disabled ? -1 : 0}
          aria-label={`${name} view (${arm.sign > 0 ? '+' : '−'}${LABEL[arm.axis]})`}
          onPointerEnter={() => setHover(id)} onPointerLeave={() => setHover(null)}
          onClick={() => activate(arm)} onKeyDown={event => key(event, () => activate(arm))}>
          <title>{`${name} view`}</title>
          {/* Generous invisible hit area around the cone. */}
          <circle className="scene-axis-gizmo__hit" cx={pointing ? tipX : tipX - ux * coneLength * 0.5}
            cy={pointing ? tipY : tipY - uy * coneLength * 0.5} r={10}/>
          {!pointing && <line className="scene-axis-gizmo__stem" x1={C} y1={C} x2={baseX} y2={baseY}/>}
          {pointing
            ? <circle className="scene-axis-gizmo__cone" cx={tipX} cy={tipY} r={half + 1}/>
            : <polygon className="scene-axis-gizmo__cone" points={cone}/>}
          {arm.sign > 0 && <text className="scene-axis-gizmo__label"
            x={pointing ? tipX + 11 : tipX + ux * 9} y={(pointing ? tipY - 9 : tipY + uy * 9) + 3.5}>{LABEL[arm.axis]}</text>}
        </g>;
      })}
      <g className="scene-axis-gizmo__center" role="button" tabIndex={disabled ? -1 : 0}
        aria-label={camera.ortho ? 'Switch to perspective projection' : 'Switch to isometric (orthographic) projection'}
        onClick={() => { if (!disabled) onToggleProjection(); }}
        onKeyDown={event => key(event, () => { if (!disabled) onToggleProjection(); })}>
        <title>{camera.ortho ? 'Perspective' : 'Isometric'}</title>
        <rect x={C - 6.5} y={C - 6.5} width={13} height={13} rx={2.5}/>
      </g>
    </svg>
    <button type="button" className="scene-axis-gizmo__caption" disabled={disabled}
      onClick={onToggleProjection} title="Toggle perspective / isometric">{caption}</button>
  </div>;
}
