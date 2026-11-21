import { useState } from 'react';
import type { Dispatch, DragEvent, PointerEvent as ReactPointerEvent, ReactNode, SetStateAction } from 'react';
import { GripVertical, LayoutPanelLeft, X } from 'lucide-react';
import {
  DOCK_AREAS, DOCK_PANELS, PANEL_TITLES, closeDockPanel, defaultDockLayout,
  dockAreaOf, moveDockPanel, normalizeDockLayout, openDockPanel, setActiveDockPanel
} from '../dockLayout';
import type { DockArea, DockLayout, DockPanel } from '../dockLayout';
import './DockWorkspace.css';

type UpdateDock = Dispatch<SetStateAction<DockLayout>>;
const DRAG_MIME = 'application/x-aiverse-studio-panel';
const AREA_NAMES: Record<DockArea, string> = {
  left: 'Left', center: 'Center', right: 'Right', bottom: 'Bottom'
};
interface Props {
  layout: DockLayout;
  setLayout: UpdateDock;
  renderPanel: (panel: DockPanel) => ReactNode;
}

interface SlotProps {
  area: DockArea;
  layout: DockLayout;
  setLayout: UpdateDock;
  renderPanel: Props['renderPanel'];
  hovered: boolean;
  setHovered: (area: DockArea | null) => void;
}
function DockSlot({ area, layout, setLayout, renderPanel, hovered, setHovered }: SlotProps) {
  const tabs = layout.areas[area];
  const active = layout.active[area];
  const drop = (event: DragEvent<HTMLElement>, before?: DockPanel) => {
    event.preventDefault();
    setHovered(null);
    const panel = event.dataTransfer.getData(DRAG_MIME);
    if (!(DOCK_PANELS as readonly string[]).includes(panel)) return;
    setLayout(current => moveDockPanel(current, panel as DockPanel, area, before));
  };
  return <section className={'dock-slot dock-' + area + (hovered ? ' dock-drop-hover' : '')}
    aria-label={AREA_NAMES[area] + ' dock'}
    onDragEnter={event => { event.preventDefault(); setHovered(area); }}
    onDragOver={event => { event.preventDefault(); event.dataTransfer.dropEffect = 'move'; }}
    onDragLeave={event => {
      if (!(event.relatedTarget instanceof Node) || !event.currentTarget.contains(event.relatedTarget))
        setHovered(null);
    }}
    onDrop={event => drop(event)}>
    <div className="dock-tabs" role="tablist" aria-label={AREA_NAMES[area] + ' tabs'}>
      <span className="dock-grip" title="Drag tabs to another dock"><GripVertical size={14}/></span>
      {tabs.map(panel => <div className={'dock-tab' + (panel === active ? ' dock-tab-active' : '')}
        key={panel} draggable
        onDragStart={event => {
          event.dataTransfer.setData(DRAG_MIME, panel);
          event.dataTransfer.effectAllowed = 'move';
        }}
        onDragEnd={() => setHovered(null)}
        onDrop={event => { event.stopPropagation(); drop(event, panel); }}
        onDragOver={event => { event.preventDefault(); event.stopPropagation(); event.dataTransfer.dropEffect = 'move'; }}>
        <button role="tab" aria-selected={panel === active} className="dock-tab-label"
          title={PANEL_TITLES[panel]}
          onClick={() => setLayout(current => setActiveDockPanel(current, area, panel))}>
          {PANEL_TITLES[panel]}
        </button>
        <button className="dock-close" aria-label={'Close ' + PANEL_TITLES[panel]}
          title={'Close ' + PANEL_TITLES[panel]}
          onClick={() => setLayout(current => closeDockPanel(current, panel))}><X size={12}/></button>
      </div>)}
      {active && <select className="dock-move" aria-label={'Move ' + PANEL_TITLES[active] + ' panel'}
        value={area}
        onChange={event => setLayout(current => moveDockPanel(current, active, event.target.value as DockArea))}>
        {DOCK_AREAS.map(zone => <option key={zone} value={zone}>{AREA_NAMES[zone]}</option>)}
      </select>}
    </div>
    <div className="dock-body" role="tabpanel" aria-label={active ? PANEL_TITLES[active] : 'Empty dock'}>
      {active ? renderPanel(active) :
        <div className="dock-empty"><span>Drop a panel here</span></div>}
    </div>
  </section>;
}

export default function DockWorkspace({ layout, setLayout, renderPanel }: Props) {
  const [hovered, setHovered] = useState<DockArea | null>(null);
  const safe = normalizeDockLayout(layout);
  type ResizeField = 'leftSize' | 'rightSize' | 'bottomSize';
  const updateSize = (field: ResizeField, proposed: number) => {
    const bounds = field === 'bottomSize' ? [110, 480] : [180, 600];
    const value = Math.min(bounds[1], Math.max(bounds[0], Math.round(proposed)));
    setLayout(current => ({ ...current, [field]: value }));
  };
  const keyboardResize = (field: ResizeField, event: React.KeyboardEvent<HTMLDivElement>) => {
    const horizontal = field !== 'bottomSize';
    const positive = horizontal ? 'ArrowRight' : 'ArrowUp';
    const negative = horizontal ? 'ArrowLeft' : 'ArrowDown';
    const sign = field === 'rightSize' ? -1 : 1;
    if (event.key === positive || event.key === negative) {
      event.preventDefault();
      const direction = event.key === positive ? sign : -sign;
      updateSize(field, safe[field] + direction * (event.shiftKey ? 32 : 8));
    } else if (event.key === 'Home') {
      event.preventDefault();
      updateSize(field, field === 'bottomSize' ? 110 : 180);
    } else if (event.key === 'End') {
      event.preventDefault();
      updateSize(field, field === 'bottomSize' ? 480 : 600);
    }
  };
  const resize = (field: ResizeField, event: ReactPointerEvent<HTMLDivElement>) => {
    event.preventDefault();
    const startX = event.clientX, startY = event.clientY;
    const original = safe[field];
    const move = (next: PointerEvent) => {
      const delta = field === 'leftSize' ? next.clientX - startX :
        field === 'rightSize' ? startX - next.clientX : startY - next.clientY;
      updateSize(field, original + delta);
    };
    const stop = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', stop);
      window.removeEventListener('pointercancel', stop);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', stop);
    window.addEventListener('pointercancel', stop);
  };
  const slot = (area: DockArea) => <DockSlot key={area} area={area} layout={safe}
    setLayout={setLayout} renderPanel={renderPanel} hovered={hovered === area} setHovered={setHovered}/>;

  const leftWidth = safe.areas.left.length ? safe.leftSize : 44;
  const rightWidth = safe.areas.right.length ? safe.rightSize : 44;
  const bottomHeight = safe.areas.bottom.length ? safe.bottomSize : 43;

  return <div className="dock-root">
    <div className="dock-global-toolbar">
      <details className="dock-panels-menu">
        <summary><LayoutPanelLeft size={14}/> Panels</summary>
        <div className="dock-panel-chooser">
          {DOCK_PANELS.map(panel => <label key={panel}>
            <input type="checkbox" checked={dockAreaOf(safe, panel) !== null}
              onChange={event => setLayout(current => event.target.checked
                ? openDockPanel(current, panel) : closeDockPanel(current, panel))}/>
            {PANEL_TITLES[panel]}
          </label>)}
        </div>
      </details>
      <button className="dock-reset" onClick={() => setLayout(defaultDockLayout())}>Reset layout</button>
      <span className="dock-hint">Drag a tab or use its location menu to move panels</span>
    </div>
    <div className="dock-workspace" style={{
      gridTemplateColumns: leftWidth + 'px 6px minmax(200px,1fr) 6px ' + rightWidth + 'px',
      gridTemplateRows: 'minmax(170px,1fr) 6px ' + bottomHeight + 'px'
    }}>
      {slot('left')}
      <div className="dock-resizer dock-resize-left" role="separator"
        aria-label="Resize left dock" aria-orientation="vertical" tabIndex={0}
        aria-valuemin={180} aria-valuemax={600} aria-valuenow={safe.leftSize}
        onKeyDown={event => keyboardResize('leftSize', event)}
        onPointerDown={event => resize('leftSize', event)}/>
      {slot('center')}
      <div className="dock-resizer dock-resize-right" role="separator"
        aria-label="Resize right dock" aria-orientation="vertical" tabIndex={0}
        aria-valuemin={180} aria-valuemax={600} aria-valuenow={safe.rightSize}
        onKeyDown={event => keyboardResize('rightSize', event)}
        onPointerDown={event => resize('rightSize', event)}/>
      {slot('right')}
      <div className="dock-resizer dock-resize-bottom" role="separator"
        aria-label="Resize bottom dock" aria-orientation="horizontal" tabIndex={0}
        aria-valuemin={110} aria-valuemax={480} aria-valuenow={safe.bottomSize}
        onKeyDown={event => keyboardResize('bottomSize', event)}
        onPointerDown={event => resize('bottomSize', event)}/>
      {slot('bottom')}
    </div>
  </div>;
}
