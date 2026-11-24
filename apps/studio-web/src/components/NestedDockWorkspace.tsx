import { useEffect, useRef, useState } from 'react';
import { AppWindow, Columns2, MoreVertical, Plus, Rows2, X } from 'lucide-react';
import { StudioContextMenu, type StudioMenuItem } from '../ui';
import type { Dispatch, DragEvent, PointerEvent as ReactPointerEvent, KeyboardEvent as ReactKeyboardEvent, ReactNode, SetStateAction } from 'react';
import { concealNativeViews, setDockDrag } from '../nativeOverlay';
import {
  STUDIO_PANELS, STUDIO_PANEL_TITLES, STUDIO_PANEL_HOME, activateTab, closeTab, defaultDockTree, dockStacks,
  moveTab, panelStack, reopenTab, resizeDockSplit, splitDockRoot, splitDockStack
} from '../nestedDock';
import type { DockEdge, DockNode, DockSplit, DockStack, DockTree, StudioPanel } from '../nestedDock';
import './NestedDockWorkspace.css';

const MIME = 'application/x-aiverse-studio-tab';
type DockUpdate = Dispatch<SetStateAction<DockTree>>;
interface Properties {
  tree: DockTree;
  setTree: DockUpdate;
  renderPanel: (panel: StudioPanel) => ReactNode;
  /** Undock into an OS window. ifOutside: a tab drag ended outside every drop target. */
  onFloat?: (panel: StudioPanel, ifOutside: boolean) => void;
  /** A tab dragged in from another Studio window was dropped here. */
  onAdopt?: (panel: StudioPanel) => void;
  /** Floating window: move a tab back into the main window. */
  onDockBack?: (panel: StudioPanel) => void;
  /** Floating window: no layout toolbar. */
  compact?: boolean;
}
interface BranchProperties extends Properties {
  node: DockNode;
  dragging: StudioPanel | null;
  setDragging: (panel: StudioPanel | null) => void;
  stacks: DockStack[];
  /** A tab from another Studio window is being dragged over this one. */
  foreign: boolean;
  setForeign: (value: boolean) => void;
  actionsPanel: StudioPanel | null;
  showActions: (panel: StudioPanel) => void;
  /** Pointer drag: HTML5 drag dies when the cursor crosses the native Scene window. */
  beginDrag: (event: ReactPointerEvent<HTMLElement>, panel: StudioPanel) => void;
}
const validPanel = (input: string): input is StudioPanel => (STUDIO_PANELS as readonly string[]).includes(input);
// Tabs also travel between Studio windows (same origin); text/plain is the fallback.
const TEXT_PREFIX = 'aiverse-panel:';
function draggedPanel(event: DragEvent<HTMLElement>, dragging: StudioPanel | null): StudioPanel | null {
  const data = event.dataTransfer.getData(MIME);
  if (validPanel(data)) return data;
  const text = event.dataTransfer.getData('text/plain');
  if (text.startsWith(TEXT_PREFIX) && validPanel(text.slice(TEXT_PREFIX.length))) return text.slice(TEXT_PREFIX.length) as StudioPanel;
  return dragging;
}
// Only the tab MIME (never plain text: asset drags carry text/plain too).
const carriesTab = (event: DragEvent<HTMLElement>) => Array.from(event.dataTransfer.types).includes(MIME);
function DockTabs({ node, tree, setTree, renderPanel, dragging, setDragging,
                    stacks, onFloat, onAdopt, onDockBack, foreign, setForeign, beginDrag }: BranchProperties & { node: DockStack }) {
  const [hover, setHover] = useState<DockEdge | 'center' | null>(null);
  const [menu, setMenu] = useState<{ x: number; y: number; panel: StudioPanel } | null>(null);
  // Unity-style dock menu (also on tab right-click): close, split, move to another area.
  const dockMenu = (panel: StudioPanel): StudioMenuItem[] => {
    const many = node.tabs.length > 1;
    const split = (edge: DockEdge, label: string, icon: ReactNode, separator = false): StudioMenuItem =>
      ({ id: 'split-' + edge, label, icon, separator, disabled: !many,
         onSelect: () => setTree(t => splitDockStack(t, panel, node.id, edge)) });
    return [
      ...(onFloat ? [{ id: 'float', label: 'Open in New Window', icon: <AppWindow size={13}/>,
        onSelect: () => onFloat(panel, false) } as StudioMenuItem] : []),
      ...(onDockBack ? [{ id: 'dock-back', label: 'Move to Main Window', icon: <AppWindow size={13}/>,
        onSelect: () => onDockBack(panel) } as StudioMenuItem] : []),
      { id: 'close', label: 'Close ' + STUDIO_PANEL_TITLES[panel], icon: <X size={13}/>, onSelect: () => setTree(t => closeTab(t, panel)) },
      { id: 'add', label: 'Add Tab', icon: <Plus size={13}/>, separator: true, onSelect: () => {},
        children: STUDIO_PANELS.filter(candidate => !node.tabs.includes(candidate))
          .map(candidate => ({ id: candidate, label: STUDIO_PANEL_TITLES[candidate], onSelect: () => setTree(t => moveTab(t, candidate, node.id)) }))
          .sort((a, b) => a.label.localeCompare(b.label)) },
      split('right', 'Split Right', <Columns2 size={13}/>, true),
      split('left', 'Split Left', <Columns2 size={13}/>),
      split('bottom', 'Split Down', <Rows2 size={13}/>),
      split('top', 'Split Up', <Rows2 size={13}/>),
      ...stacks.filter(stack => stack.id !== node.id).map((stack, index) => ({
        id: 'move-' + stack.id, separator: index === 0,
        label: 'Move to ' + (stack.tabs[0] ? STUDIO_PANEL_TITLES[stack.tabs[0]] + ' area' : 'empty area'),
        onSelect: () => setTree(t => moveTab(t, panel, stack.id)) })),
    ];
  };
  const transfer = (event: DragEvent<HTMLElement>, edge?: DockEdge, before?: StudioPanel) => {
    event.preventDefault();
    event.stopPropagation();
    const panel = draggedPanel(event, dragging);
    setForeign(false);
    if (!panel) return;
    const adopted = panelStack(tree, panel) === null; // came from another Studio window
    setTree(current => edge ? splitDockStack(current, panel, node.id, edge)
      : moveTab(current, panel, node.id, before));
    setDragging(null);
    setHover(null);
    if (adopted) onAdopt?.(panel);
  };
  const showEdges = foreign || (dragging !== null && (panelStack(tree, dragging) !== node.id || node.tabs.length > 1));
  return <section className="nested-stack" aria-label={'Dock '+node.id} data-stack={node.id}
    onDragOver={event => {event.preventDefault();event.dataTransfer.dropEffect='move';}}
    onDrop={event => transfer(event)}>
    <div className="nested-tabs" role="tablist" aria-label={'Tabs '+node.id}
      onWheel={event=>{if(Math.abs(event.deltaY)>Math.abs(event.deltaX))event.currentTarget.scrollLeft+=event.deltaY;}}>
      {node.tabs.map(panel => <div key={panel} className={'nested-tab'+(node.active===panel?' nested-active':'')}
        data-dock="tab" data-stack={node.id} data-before={panel}
        ref={node.active===panel ? revealTab : undefined}
        onMouseDown={event=>{
          // Middle click closes the tab. preventDefault stops the browser autoscroll cursor.
          if(event.button!==1)return;
          event.preventDefault();
          event.stopPropagation();
          setTree(t=>closeTab(t,panel));
        }}
        onAuxClick={event=>{if(event.button===1)event.preventDefault();}}
        onPointerDown={event=>beginDrag(event, panel)}
        onContextMenu={event=>{event.preventDefault();setMenu({x:event.clientX,y:event.clientY,panel});}}>
        <span role="tab" aria-selected={node.active===panel} tabIndex={0}
          onClick={()=>setTree(t=>activateTab(t,node.id,panel))}
          onKeyDown={event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();setTree(t=>activateTab(t,node.id,panel));}}}>{STUDIO_PANEL_TITLES[panel]}</span>
        <button className="nested-tab-close" title={'Close '+STUDIO_PANEL_TITLES[panel]}
          aria-label={'Close '+STUDIO_PANEL_TITLES[panel]} onPointerDown={event=>event.stopPropagation()}
          onClick={()=>setTree(t=>closeTab(t,panel))}>×</button>
      </div>)}
      <span className="nested-tabs-fill"/>
      {node.active && <button type="button" className="nested-stack-menu" draggable={false}
        aria-label="Dock area menu" title="Dock area menu"
        onClick={event=>{const r=event.currentTarget.getBoundingClientRect();
          setMenu({x:r.right-180,y:r.bottom+2,panel:node.active!});}}><MoreVertical size={13}/></button>}
    </div>
    {menu && <StudioContextMenu x={menu.x} y={menu.y} label="Dock actions"
      items={dockMenu(menu.panel)} onClose={()=>setMenu(null)}/>}
    <div className="nested-stack-body" role="tabpanel" aria-label={node.active ? STUDIO_PANEL_TITLES[node.active] : 'Empty dock'}>
      {node.active ? renderPanel(node.active) : <div className="nested-empty">Drop a panel here</div>}
      {showEdges && <div className="nested-drop-zones">
        {(['left','right','top','bottom'] as const).map(edge=><div key={edge}
          data-dock="edge" data-edge={edge} data-stack={node.id}
          className={'nested-drop-edge nested-drop-'+edge+(hover===edge?' hovered':'')}>{edge}</div>)}
        <div data-dock="center" data-stack={node.id}
          className={'nested-drop-center'+(hover==='center'?' hovered':'')}>Tab</div>
      </div>}
    </div>
  </section>;
}
function DockSplitNode({ node, tree, setTree, renderPanel, dragging, setDragging,
                         stacks, actionsPanel, showActions, onFloat, onAdopt, onDockBack, foreign, setForeign, beginDrag }: BranchProperties & { node: DockSplit }) {
  const horizontal=node.axis==='horizontal';
  const adjust=(ratio:number)=>setTree(t=>resizeDockSplit(t,node.id,ratio));
  const resize=(event: ReactPointerEvent<HTMLDivElement>)=>{
    event.preventDefault();
    const element=event.currentTarget.parentElement;
    if(!element)return;
    const rect=element.getBoundingClientRect();
    const move=(e: PointerEvent)=>{
      const value=horizontal ? (e.clientX-rect.left)/rect.width : (e.clientY-rect.top)/rect.height;
      adjust(value);
    };
    const stop=()=>{
      window.removeEventListener('pointermove',move);
      window.removeEventListener('pointerup',stop);
      window.removeEventListener('pointercancel',stop);
    };
    window.addEventListener('pointermove',move);
    window.addEventListener('pointerup',stop);
    window.addEventListener('pointercancel',stop);
  };
  const resizeKey=(event: ReactKeyboardEvent<HTMLDivElement>)=>{
    const positive=horizontal?'ArrowRight':'ArrowDown';
    const negative=horizontal?'ArrowLeft':'ArrowUp';
    if(event.key===positive || event.key===negative){
      event.preventDefault();
      adjust(node.ratio+(event.key===positive?1:-1)*(event.shiftKey?.04:.015));
    }else if(event.key==='Home'||event.key==='End'){
      event.preventDefault();adjust(event.key==='Home'?.12:.88);
    }
  };
  const shared={tree,setTree,renderPanel,dragging,setDragging,stacks,actionsPanel,showActions,onFloat,onAdopt,onDockBack,foreign,setForeign,beginDrag};
  return <div className={'nested-split '+(horizontal?'nested-horizontal':'nested-vertical')}>
    <div className="nested-child" style={{flex:node.ratio+' 1 0%'}}>
      <DockBranch node={node.first} {...shared}/>
    </div>
    <div role="separator" aria-label={'Resize '+node.id} aria-orientation={horizontal?'vertical':'horizontal'}
      className="nested-divider" tabIndex={0} aria-valuemin={12} aria-valuemax={88}
      aria-valuenow={Math.round(node.ratio*100)} onKeyDown={resizeKey} onPointerDown={resize}/>
    <div className="nested-child" style={{flex:(1-node.ratio)+' 1 0%'}}>
      <DockBranch node={node.second} {...shared}/>
    </div>
  </div>;
}
function DockBranch(props: BranchProperties) {
  return props.node.kind==='stack' ? <DockTabs {...props} node={props.node}/>
    : <DockSplitNode {...props} node={props.node}/>;
}
/** Keep the active tab visible in an overflowing tab strip (a panel opened from the palette or Window menu). */
function revealTab(el: HTMLDivElement | null) { el?.scrollIntoView({ block: 'nearest', inline: 'nearest' }); }

interface DockHit { kind: string; edge?: DockEdge; stack?: string; before?: string }

/** Drop target under the pointer. elementFromPoint misses once the cursor is over the native Scene window. */
function dockHit(x: number, y: number): DockHit | null {
  const inside = (element: HTMLElement) => {
    const rect = element.getBoundingClientRect();
    return rect.width >= 1 && rect.height >= 1 && x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
  };
  let root: HTMLElement | null = null;
  let best: HTMLElement | null = null;
  let bestArea = Infinity;
  for (const element of document.querySelectorAll<HTMLElement>('[data-dock]')) {
    const kind = element.dataset.dock;
    if (!kind || !inside(element)) continue;
    if (kind === 'root') { root = element; continue; }
    if (kind !== 'edge' && kind !== 'center' && kind !== 'tab') continue;
    const rect = element.getBoundingClientRect();
    const area = rect.width * rect.height;
    if (area < bestArea) { best = element; bestArea = area; }
  }
  const chosen = root ?? best;
  if (chosen) return {
    kind: chosen.dataset.dock || '',
    edge: chosen.dataset.edge as DockEdge | undefined,
    stack: chosen.dataset.stack,
    before: chosen.dataset.before,
  };
  const body = document.querySelector('.nested-workspace-body');
  if (body instanceof HTMLElement && inside(body)) {
    const rect = body.getBoundingClientRect();
    const margin = 28;
    if (x < rect.left + margin) return { kind: 'root', edge: 'left' };
    if (x > rect.right - margin) return { kind: 'root', edge: 'right' };
    if (y < rect.top + margin) return { kind: 'root', edge: 'top' };
    if (y > rect.bottom - margin) return { kind: 'root', edge: 'bottom' };
  }
  for (const element of document.querySelectorAll<HTMLElement>('.nested-stack[data-stack]')) {
    if (!inside(element)) continue;
    const rect = element.getBoundingClientRect();
    const stack = element.dataset.stack;
    if (y < rect.top + 40) return { kind: 'center', stack };
    const rx = (x - rect.left) / Math.max(1, rect.width);
    const ry = (y - rect.top) / Math.max(1, rect.height);
    if (rx < 0.28) return { kind: 'edge', edge: 'left', stack };
    if (rx > 0.72) return { kind: 'edge', edge: 'right', stack };
    if (ry < 0.28) return { kind: 'edge', edge: 'top', stack };
    if (ry > 0.78) return { kind: 'edge', edge: 'bottom', stack };
    return { kind: 'center', stack };
  }
  return null;
}

export default function NestedDockWorkspace({ tree, setTree, renderPanel, onFloat, onAdopt, onDockBack, compact }: Properties) {
  const [dragging,setDragging]=useState<StudioPanel|null>(null);
  const [foreign,setForeign]=useState(false);
  const foreignTimer=useRef(0);
  const [actionsPanel,setActionsPanel]=useState<StudioPanel|null>(null);
  const stacks=dockStacks(tree);
  const [rootHover,setRootHover]=useState<DockEdge|null>(null);
  const currentStackId=actionsPanel ? panelStack(tree,actionsPanel) : null;
  const currentStack=stacks.find(stack=>stack.id===currentStackId);
  const docking=dragging!==null || foreign;
  useEffect(()=>{ setDockDrag(docking); },[docking]);
  useEffect(()=>()=>setDockDrag(false),[]);
  const ghost=useRef<HTMLDivElement>(null);
  const point=useRef({x:0,y:0});
  useEffect(()=>{
    if(!dragging || !ghost.current) return;
    ghost.current.style.transform=`translate(${point.current.x+14}px,${point.current.y+14}px)`;
  },[dragging]);
  const beginDrag=(event: ReactPointerEvent<HTMLElement>, panel: StudioPanel)=>{
    if(event.button!==0)return;
    const origin = event.target instanceof Element ? event.target : (event.target as Node).parentElement;
    if(!origin || origin.closest('.nested-tab-close,button,a,input'))return;
    // A browser drag on the label ends the gesture before it moves. Pointer capture is taken
    // only after the drag has started, so the tab can leave the window; a rejected capture
    // must not cancel the in-window drag (mousemove on the page still tracks that).
    event.preventDefault();
    const startX=event.clientX, startY=event.clientY;
    const tab=event.currentTarget;
    const pointerId=event.pointerId;
    let moved=false;
    let done=false;
    let captured=false;
    const track=(x:number,y:number)=>{
      point.current={x,y};
      const chip=ghost.current;
      if(chip) chip.style.transform=`translate(${x+14}px,${y+14}px)`;
      const hit=dockHit(x,y);
      document.querySelectorAll('[data-dock].hovered').forEach(node=>node.classList.remove('hovered'));
      if(!hit)return;
      const zone=[...document.querySelectorAll<HTMLElement>('[data-dock]')].find(element=>{
        const rect=element.getBoundingClientRect();
        return element.dataset.dock===hit.kind && (element.dataset.stack||'')===(hit.stack||'') &&
          (element.dataset.edge||'')===(hit.edge||'') && x>=rect.left && x<rect.right && y>=rect.top && y<rect.bottom;
      });
      zone?.classList.add('hovered');
    };
    const finish=(x:number,y:number)=>{
      if(done)return;
      done=true;
      window.removeEventListener('pointermove', move, true);
      window.removeEventListener('mousemove', move, true);
      window.removeEventListener('pointerup', finishPointer, true);
      window.removeEventListener('mouseup', finishPointer, true);
      document.querySelectorAll('[data-dock].hovered').forEach(node=>node.classList.remove('hovered'));
      setDockDrag(false);
      setDragging(null);
      if(!moved){
        const stack=panelStack(tree, panel);
        if(stack) setTree(current=>activateTab(current, stack, panel));
        return;
      }
      const hit=dockHit(x,y);
      if(!hit){
        if(x<=0||y<=0||x>=window.innerWidth||y>=window.innerHeight) onFloat?.(panel, true);
        return;
      }
      const adopted=panelStack(tree, panel)===null;
      setTree(current=>{
        if(hit.kind==='root' && hit.edge) return splitDockRoot(current, panel, hit.edge);
        if(hit.kind==='edge' && hit.edge && hit.stack) return splitDockStack(current, panel, hit.stack, hit.edge);
        if(hit.stack) return moveTab(current, panel, hit.stack, hit.before && validPanel(hit.before) ? hit.before : undefined);
        return current;
      });
      if(adopted) onAdopt?.(panel);
    };
    const move=(ev: PointerEvent | MouseEvent)=>{
      if(!moved && Math.hypot(ev.clientX-startX, ev.clientY-startY)<3)return;
      if(!moved){
        moved=true; concealNativeViews(); setDragging(panel);
        if(!captured && tab instanceof HTMLElement){
          captured=true;
          try { tab.setPointerCapture(pointerId); } catch { /* the page still tracks the drag inside the window */ }
        }
      }
      track(ev.clientX, ev.clientY);
    };
    const finishPointer=(ev: PointerEvent | MouseEvent)=>finish(ev.clientX, ev.clientY);
    window.addEventListener('pointermove', move, true);
    window.addEventListener('mousemove', move, true);
    window.addEventListener('pointerup', finishPointer, true);
    window.addEventListener('mouseup', finishPointer, true);
  };
  const dropOnRoot=(event: DragEvent<HTMLElement>, edge: DockEdge)=>{
    event.preventDefault();
    event.stopPropagation();
    const panel=draggedPanel(event, dragging);
    setForeign(false);
    setRootHover(null);
    setDragging(null);
    if(!panel)return;
    const adopted=panelStack(tree, panel)===null;
    setTree(current=>splitDockRoot(current, panel, edge));
    if(adopted)onAdopt?.(panel);
  };
  useEffect(()=>{
    if(!actionsPanel)return;
    const dismiss=(event:KeyboardEvent)=>{if(event.key==='Escape')setActionsPanel(null);};
    window.addEventListener('keydown',dismiss);
    return ()=>window.removeEventListener('keydown',dismiss);
  },[actionsPanel]);
  const applyAction=(update:(next:DockTree)=>DockTree)=>{
    setTree(update);
    setActionsPanel(null);
  };
  return <div className="nested-workspace-root"
    onDragEnter={event=>{if(dragging===null && carriesTab(event))setForeign(true);}}
    onDragOver={()=>{ // a foreign drag that ends without a drop here stops sending dragover
      if(!foreign)return;
      window.clearTimeout(foreignTimer.current);
      foreignTimer.current=window.setTimeout(()=>setForeign(false),400);
    }}
    onDragLeave={event=>{ // WebKit gives no relatedTarget: leaving means crossing the window edge
      if(event.clientX<=0||event.clientY<=0||event.clientX>=window.innerWidth||event.clientY>=window.innerHeight)setForeign(false);
    }}
    onDrop={()=>setForeign(false)}>
    {!compact && <div className={"nested-toolbar"+(actionsPanel?" nested-toolbar--actions":"")}>
      {actionsPanel && currentStackId ? <div className="nested-action-bar" role="toolbar"
          aria-label={'Panel actions for '+STUDIO_PANEL_TITLES[actionsPanel]}>
        <strong className="nested-action-title">{STUDIO_PANEL_TITLES[actionsPanel]}</strong>
        <span className="nested-action-label">Move to</span>
        {stacks.filter(stack=>stack.id!==currentStackId).map(stack=>{
          const label=stack.tabs[0] ? STUDIO_PANEL_TITLES[stack.tabs[0]] : 'Empty area';
          return <button key={stack.id} type="button" title={'Move to '+label}
            onClick={()=>applyAction(t=>moveTab(t,actionsPanel,stack.id))}>{label}</button>;
        })}
        <span className="nested-action-label">Split</span>
        {(['left','right','top','bottom'] as DockEdge[]).map(edge=>
          <button key={edge} type="button"
            disabled={!currentStack || currentStack.tabs.length<=1}
            onClick={()=>applyAction(t=>splitDockStack(t,actionsPanel,currentStackId,edge))}>
            {edge}
          </button>)}
        <button type="button" className="nested-action-danger"
          onClick={()=>applyAction(t=>closeTab(t,actionsPanel))}>Close panel</button>
        <button type="button" className="nested-action-done"
          onClick={()=>setActionsPanel(null)}>Done</button>
      </div> : <>
        <details className="nested-panels-list">
          <summary>Panels</summary>
          <div className="nested-panels-popover">
            {STUDIO_PANELS.map(panel=><label key={panel}>
              <input type="checkbox" checked={panelStack(tree,panel)!==null}
                onChange={event=>setTree(t=>event.target.checked
                  ?reopenTab(t,panel,STUDIO_PANEL_HOME[panel]):closeTab(t,panel))}/>
              {STUDIO_PANEL_TITLES[panel]}
            </label>)}
          </div>
        </details>
        <button onClick={()=>setTree(defaultDockTree())}>Reset layout</button>
        <span>Drag tabs to rearrange or out of the window to float them; ⋮ for dock actions</span>
      </>}
    </div>}
    <div className="nested-workspace-body">
    <DockBranch node={tree.root} tree={tree} setTree={setTree} renderPanel={renderPanel} onFloat={onFloat}
      onAdopt={onAdopt} onDockBack={onDockBack} foreign={foreign} setForeign={setForeign}
      dragging={dragging} setDragging={setDragging} stacks={stacks} beginDrag={beginDrag}
      actionsPanel={actionsPanel} showActions={panel=>setActionsPanel(current=>
        current===panel?null:panel)}/>
    {docking && <div className="nested-root-zones" data-native-overlay="">
      {(['left','right','top','bottom'] as const).map(edge=><div key={edge}
        data-dock="root" data-edge={edge}
        className={'nested-root-edge nested-root-'+edge+(rootHover===edge?' hovered':'')}
        title={'Dock to the '+edge+' of the window'}
        onDragOver={event=>{event.preventDefault();event.stopPropagation();event.dataTransfer.dropEffect='move';setRootHover(edge);}}
        onDragLeave={()=>setRootHover(null)} onDrop={event=>dropOnRoot(event,edge)}/>)}
    </div>}
    {dragging && <div ref={ghost} className="nested-drag-ghost">{STUDIO_PANEL_TITLES[dragging]}</div>}
    </div>
  </div>;
}
