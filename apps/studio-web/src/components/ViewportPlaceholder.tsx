import { useCallback, useEffect, useRef, useState } from 'react';
import { shortcutKey } from '../keyboard';
import { Box } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import { SCENE_TOOL_KEYS } from './SceneToolbar';
import SceneAxisGizmo from './SceneAxisGizmo';
import { brushPixels, onSculptBrush, sculptBrush, strokeSpacing } from '../panels/sculptBrushStore';
import { onReferenceView, referenceView } from '../panels/referenceStore';
import { forgetViewportRect, overlayBlocks, viewportRectChanged, watchOverlays } from '../nativeOverlay';
import {
  DEFAULT_SCENE_CAMERA, dragZoomAmount, flyStep, isSceneCameraState, navigationFor, stepFlySpeed,
  type SceneCameraState, type SceneNavigation
} from '../panels/sceneCamera';

type DragMode = Exclude<SceneNavigation, 'select'> | 'select' | 'handle';
const FLY_KEYS = new Set(['w', 'a', 's', 'd', 'q', 'e']);
// Unity also moves the Scene camera with arrow keys without flythrough mode.
const ARROW_KEYS: Record<string, string> = { arrowup: 'w', arrowdown: 's', arrowleft: 'a', arrowright: 'd' };

/** Layout source of truth for a real native GPU surface. Never streams raster frames to JS. */
export default function ViewportPlaceholder({ title, active }: { title: string; active: boolean }) {
  const element = useRef<HTMLElement>(null);
  const drag = useRef<{ x: number; y: number; startX: number; startY: number; moved: boolean; mode: DragMode; button: number } | null>(null);
  const pendingDelta = useRef<{ x: number; y: number; raf: number; mode: Exclude<DragMode, 'select' | 'handle'> }>({ x: 0, y: 0, raf: 0, mode: 'orbit' });
  const flying = useRef(false);
  const handleInFlight = useRef(false);
  const flySpeedRef = useRef(1);
  const autoIso = useRef(false);
  const [flySpeed, setFlySpeed] = useState(1);
  const [speedVisible, setSpeedVisible] = useState(false);
  const speedTimer = useRef(0);
  const [camera, setCamera] = useState<SceneCameraState>(DEFAULT_SCENE_CAMERA);
  // Character Creator sculpt: drag paints strokes, a circle shows the brush.
  const [brush, setBrushView] = useState(sculptBrush());
  const [cursor, setCursor] = useState<{ x: number; y: number } | null>(null);
  const sculptDrag = useRef<{ x: number; y: number; busy: boolean; queued: { x: number; y: number } | null } | null>(null);
  useEffect(() => onSculptBrush(setBrushView), []);
  const [reference, setReference] = useState(referenceView());
  useEffect(() => onReferenceView(setReference), []);
  const cameraRef = useRef(camera);
  cameraRef.current = camera;
  const native = nativeConnected();
  const [gpuReady, setGpuReady] = useState(false);
  const [navigationActive, setNavigationActive] = useState(false);
  const [gpuError, setGpuError] = useState('');
  useEffect(() => {
    if (!active) { setGpuReady(false); setGpuError(''); }
  }, [active]);
  useEffect(() => {
    if (!native) return;
    return onNativeEvent((event, payload) => {
      if (event === 'viewport.ready') { setGpuReady(true); setGpuError(''); }
      if (event === 'viewport.camera' && isSceneCameraState(payload)) setCamera(payload);
      if (event === 'viewport.error') {
        setGpuReady(false);
        setGpuError(typeof payload === 'object' && payload !== null && 'message' in payload
          ? String(payload.message) : 'Native renderer could not initialize');
      }
    });
  }, [native]);

  useEffect(() => {
    if (!native) return;
    const host = element.current;
    if (!host) return;
    let frame = 0;
    let closed = false;
    const update = () => {
      if (frame) cancelAnimationFrame(frame);
      frame = requestAnimationFrame(() => {
        frame = 0;
        if (closed) return;
        const bounds = host.getBoundingClientRect();
        const rect = {
          x: bounds.left, y: bounds.top,
          width: bounds.width, height: bounds.height,
          dpr: window.devicePixelRatio || 1,
          // Hidden while an HTML dialog/menu overlaps it: the Metal view would cover it.
          visible: active && document.visibilityState === 'visible' &&
                   bounds.width > 16 && bounds.height > 16 && !overlayBlocks(bounds)
        };
        if (!viewportRectChanged('scene', rect)) return;
        void invoke('studio.viewportRect', { id: 'scene', ...rect })
          .catch(error => { forgetViewportRect('scene'); console.warn('AIVERSE viewport layout:', error); });
      });
    };
    const stopOverlays = watchOverlays(update);
    const observer = new ResizeObserver(update);
    observer.observe(host);
    window.addEventListener('resize', update);
    window.addEventListener('scroll', update, true);
    document.addEventListener('visibilitychange', update);
    update();
    return () => {
      closed = true;
      if (frame) cancelAnimationFrame(frame);
      observer.disconnect();
      stopOverlays();
      window.removeEventListener('resize', update);
      window.removeEventListener('scroll', update, true);
      document.removeEventListener('visibilitychange', update);
      // On tab switch/unmount, remove native surface even if no further resize event occurs.
      forgetViewportRect('scene');
      void invoke('studio.viewportRect', {
        id: 'scene', x: 0, y: 0, width: 0, height: 0, dpr: 1, visible: false
      }).catch(() => {});
    };
  }, [active, native]);

  const send = useCallback((params: Parameters<typeof invoke<'studio.viewportInput'>>[1]) =>
    invoke('studio.viewportInput', params).catch(console.warn), []);
  const leaveAutoIso = useCallback(() => {
    // Unity: rotating away from a gizmo-snapped axis view returns to perspective.
    if (autoIso.current && cameraRef.current.ortho) void send({ kind: 'projection', ortho: false });
    autoIso.current = false;
  }, [send]);
  const showSpeed = (speed: number) => {
    flySpeedRef.current = speed; setFlySpeed(speed); setSpeedVisible(true);
    window.clearTimeout(speedTimer.current);
    speedTimer.current = window.setTimeout(() => setSpeedVisible(false), 1200);
  };
  useEffect(() => () => window.clearTimeout(speedTimer.current), []);

  // Editor-only navigation (Unity scheme). WASD/QE move only while RMB flythrough is
  // held; arrow keys always move. Held keys move continuously, independently of OS
  // keyboard repeat; pending bridge calls are not allowed to pile up.
  useEffect(() => {
    if (!active || !native || !gpuReady) return;
    const keys=new Set<string>();
    let boost=false;
    let frame=0;
    let lastTick=0;
    let heldSince=0;
    let inFlight=false;
    const focused=()=>!!element.current?.matches(':focus-within');
    const clear=()=>{keys.clear();boost=false;lastTick=0;heldSince=0;};
    const keydown=(event:KeyboardEvent)=>{
      if (!focused() || event.metaKey || event.ctrlKey) return;
      const target=event.target;
      if (target instanceof HTMLElement &&
          target.closest('input, textarea, select, [contenteditable="true"]')) return;
      const key=shortcutKey(event);
      boost=event.shiftKey;
      if (event.altKey) return;
      if (!flying.current && !event.repeat && !event.shiftKey && SCENE_TOOL_KEYS[key]) {
        event.preventDefault();
        window.dispatchEvent(new CustomEvent('studio:scene-tool', { detail: SCENE_TOOL_KEYS[key] }));
      } else if (key==='f' && !event.repeat && !flying.current) {
        event.preventDefault(); void send({kind:'focus'});
      } else if (key==='home' && !event.repeat) {
        event.preventDefault(); autoIso.current=false; void send({kind:'reset'});
      } else if ((flying.current && FLY_KEYS.has(key)) || ARROW_KEYS[key]) {
        event.preventDefault();
        if (keys.size===0) heldSince=performance.now();
        keys.add(key);
      }
    };
    const keyup=(event:KeyboardEvent)=>{
      const key=shortcutKey(event);
      keys.delete(key);
      boost=event.shiftKey;
      if (keys.size===0) heldSince=0;
    };
    const blur=()=>clear();
    const visibility=()=>{if(document.hidden)clear();};
    const tick=(now:number)=>{
      frame=requestAnimationFrame(tick);
      if (!focused() || document.hidden || keys.size===0) {lastTick=0;return;}
      // Fly keys stop as soon as the right button is released; arrows always move.
      if (!flying.current) for (const key of [...keys]) if (!ARROW_KEYS[key]) keys.delete(key);
      if (keys.size===0) {heldSince=0;return;}
      const directions=new Set([...keys].map(key=>ARROW_KEYS[key] ?? key));
      if (lastTick && now-lastTick<32) return;
      const elapsed=lastTick?Math.min(80,now-lastTick):33;
      lastTick=now;
      if(inFlight)return;
      const step=flyStep(directions,{elapsedMs:elapsed,heldMs:heldSince?now-heldSince:0,speed:flySpeedRef.current,boost});
      if (!step.right && !step.up && !step.forward) return;
      inFlight=true;
      void send({kind:'move',...step}).finally(()=>{inFlight=false;});
    };
    window.addEventListener('keydown',keydown);
    window.addEventListener('keyup',keyup);
    window.addEventListener('blur',blur);
    document.addEventListener('visibilitychange',visibility);
    frame=requestAnimationFrame(tick);
    return ()=>{
      cancelAnimationFrame(frame);
      clear();
      window.removeEventListener('keydown',keydown);
      window.removeEventListener('keyup',keyup);
      window.removeEventListener('blur',blur);
      document.removeEventListener('visibilitychange',visibility);
    };
  },[active,native,gpuReady,send]);
  const composite = () => native && document.documentElement.classList.contains('native-composite');
  const sendDelta = (dx: number, dy: number, mode: Exclude<DragMode, 'select' | 'handle'>) => {
    const state = pendingDelta.current;
    if (state.mode !== mode && (state.x || state.y)) {
      const x=state.x,y=state.y; state.x=state.y=0;
      void flushDelta(state.mode,x,y);
    }
    state.mode = mode;
    state.x += dx;
    state.y += dy;
    if (state.raf) return;
    state.raf = requestAnimationFrame(() => {
      state.raf = 0;
      const x = state.x, y = state.y;
      state.x = state.y = 0;
      if (x !== 0 || y !== 0) void flushDelta(state.mode, x, y);
    });
  };
  const flushDelta = (mode: Exclude<DragMode, 'select' | 'handle'>, x: number, y: number) =>
    mode === 'zoom' ? send({ kind: 'zoom', amount: Math.max(-900, Math.min(900, dragZoomAmount(x, y))) })
      : send({ kind: mode, dx: Math.max(-999, Math.min(999, x)), dy: Math.max(-999, Math.min(999, y)) });
  const endDrag = () => {
    drag.current=null;
    flying.current=false;
    setNavigationActive(false);
  };
  // One sculpt stroke per call; while the native side is busy only the latest
  // cursor position is kept, so a fast drag never floods the bridge.
  const stroke = (x: number, y: number, b: DOMRect) => {
    const sd=sculptDrag.current;
    if (sd?.busy) { sd.queued={ x, y }; return; }
    if (sd) sd.busy=true;
    void send({kind:'pick', x, y, width:b.width, height:b.height}).finally(() => {
      const cur=sculptDrag.current;
      if (!cur) return;
      cur.busy=false;
      const q=cur.queued; cur.queued=null;
      if (q) stroke(q.x,q.y,b);
    });
  };
  const navigationReady = native && active && gpuReady;
  return <section ref={element} className={'viewport' + (native && active && gpuReady ? ' viewport-native' : '')}
    aria-label={native && active ? 'Native Scene viewport' : 'Viewport placeholder'}
    tabIndex={0}
    onContextMenu={event=>event.preventDefault()}
    onPointerDown={event => {
      if (!composite() || !active || !gpuReady) return;
      event.currentTarget.focus();
      const mode=navigationFor(event.button,{alt:event.altKey,meta:event.metaKey});
      if (!mode || drag.current) return;
      if (mode==='select' && sculptBrush().armed) {
        const b=event.currentTarget.getBoundingClientRect();
        sculptDrag.current={ x: event.clientX, y: event.clientY, busy: false, queued: null };
        event.currentTarget.setPointerCapture(event.pointerId);
        stroke(event.clientX-b.left,event.clientY-b.top,b);
        return;
      }
      if (mode!=='select') leaveAutoIso();
      if (mode==='look') flying.current=true;
      drag.current = { x: event.clientX, y: event.clientY, startX: event.clientX, startY: event.clientY,
        moved: false, mode, button:event.button };
      if (mode==='select') {
        // Move tool: a press on a translate handle turns this drag into an axis move.
        const bounds=event.currentTarget.getBoundingClientRect();
        const state=drag.current;
        void invoke('studio.viewportInput',{kind:'handle',phase:'down',x:event.clientX-bounds.left,
          y:event.clientY-bounds.top,width:bounds.width,height:bounds.height}).then(result=>{
          if (result.hit && drag.current===state) { state.mode='handle'; setNavigationActive(true); }
        }).catch(console.warn);
      }
      if (mode!=='select') setNavigationActive(true);
      event.currentTarget.setPointerCapture(event.pointerId);
    }}
    onPointerMove={event => {
      if (brush.armed) {
        const b=event.currentTarget.getBoundingClientRect();
        setCursor({ x: event.clientX-b.left, y: event.clientY-b.top });
        const sd=sculptDrag.current;
        if (sd && Math.hypot(event.clientX-sd.x,event.clientY-sd.y)>=strokeSpacing(brushPixels(brush.radius,b.height))) {
          sd.x=event.clientX; sd.y=event.clientY;
          stroke(event.clientX-b.left,event.clientY-b.top,b);
        }
      }
      const state=drag.current;
      if (!state || !composite() || !gpuReady) return;
      const dx=event.clientX-state.x,dy=event.clientY-state.y;
      state.x=event.clientX; state.y=event.clientY;
      if (Math.hypot(event.clientX-state.startX,event.clientY-state.startY)>4) state.moved=true;
      if (state.mode==='handle') {
        if (handleInFlight.current) return;
        const bounds=event.currentTarget.getBoundingClientRect();
        handleInFlight.current=true;
        void invoke('studio.viewportInput',{kind:'handle',phase:'drag',x:event.clientX-bounds.left,
          y:event.clientY-bounds.top,width:bounds.width,height:bounds.height,invertSnap:event.ctrlKey||event.metaKey})
          .catch(console.warn).finally(()=>{handleInFlight.current=false;});
      } else if (state.moved && state.mode!=='select') sendDelta(dx,dy,state.mode);
    }}
    onPointerUp={event => {
      if (sculptDrag.current) {
        sculptDrag.current=null;
        if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
        return;
      }
      const state=drag.current;
      if (!state || event.button!==state.button) return;
      endDrag();
      if (!composite() || !gpuReady) return;
      if (event.currentTarget.hasPointerCapture(event.pointerId)) event.currentTarget.releasePointerCapture(event.pointerId);
      if (state.mode==='handle') {
        const bounds=event.currentTarget.getBoundingClientRect();
        void invoke('studio.viewportInput',{kind:'handle',phase:'up',x:event.clientX-bounds.left,
          y:event.clientY-bounds.top,width:bounds.width,height:bounds.height}).catch(console.warn);
        return;
      }
      if (!state.moved && state.mode==='select') {
        const bounds=event.currentTarget.getBoundingClientRect();
        void send({kind:'pick', x:event.clientX-bounds.left,y:event.clientY-bounds.top,width:bounds.width,height:bounds.height});
      }
    }}
    onPointerLeave={() => setCursor(null)}
    onPointerCancel={() => { sculptDrag.current=null; endDrag(); }}
    onLostPointerCapture={() => { if (drag.current) endDrag(); }}
    onWheel={event => {
      if (!composite() || !active || !gpuReady) return;
      if (flying.current) { showSpeed(stepFlySpeed(flySpeedRef.current, event.deltaY)); return; }
      // Trackpad pinch arrives as ctrl+wheel in WebKit.
      const amount=-event.deltaY*(event.ctrlKey?0.6:0.12);
      void send({kind:'zoom',amount:Math.max(-900,Math.min(900,amount))});
    }}>
    {navigationReady && <div className="studio-scene-nav-help" aria-hidden="true">
      {navigationActive?(flying.current?'Flythrough · WASD move · Q/E down/up · Shift faster · Wheel speed':'Navigating Scene')
        :'RMB fly (WASD/QE) · Alt+LMB orbit · MMB / Alt+Cmd+LMB pan · Wheel / Alt+RMB zoom · F focus'}
    </div>}
    {navigationReady && reference.src && reference.visible && <img className="studio-reference-photo" src={reference.src} alt="" aria-hidden="true"
      style={{ opacity: reference.opacity, height: (reference.scale * 100) + '%',
        transform: `translate(calc(-50% + ${reference.x * 50}%), calc(-50% + ${reference.y * 50}%))${reference.mirror ? ' scaleX(-1)' : ''}` }}/>}
    {navigationReady && brush.armed && cursor && (() => {
      const h=element.current?.getBoundingClientRect().height ?? 600, r=brushPixels(brush.radius,h);
      return <div className="studio-sculpt-brush" aria-hidden="true"
        style={{ left: cursor.x-r, top: cursor.y-r, width: r*2, height: r*2 }}/>;
    })()}
    {navigationReady && speedVisible && <div className="studio-scene-fly-speed" role="status">
      Camera speed ×{flySpeed.toFixed(flySpeed < 1 ? 2 : 1)}</div>}
    {navigationReady && <SceneAxisGizmo camera={camera}
      onView={view => {
        // Unity: snapping to an axis view switches to Iso; orbiting away restores Persp.
        autoIso.current = !cameraRef.current.ortho || autoIso.current;
        void send({kind:'view',...view});
        void send({kind:'projection',ortho:true});
      }}
      onToggleProjection={() => { autoIso.current = false; void send({kind:'projection',ortho:!cameraRef.current.ortho}); }}/>}
    <div className="viewport-grid"/>
    <div className="viewport-placeholder">
      <div className="viewport-cube"><Box size={44} strokeWidth={1.1}/></div>
      <h2>{title}</h2>
      <p>{native ? gpuError ? 'Native renderer error: ' + gpuError : 'Starting AIVERSE native GPU renderer…' :
        'The native AIVERSE renderer is not connected to the browser preview.'}</p>
      <span className="viewport-tag">Metal · Vulkan · GLES</span>
    </div>
  </section>;
}
