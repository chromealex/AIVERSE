import { useLayoutEffect, useRef, useState } from 'react';
import { ChevronDown } from 'lucide-react';
import { WORKSPACES, WORKSPACE_INFO, type StudioWorkspace } from '../workspaces';
import { fitWorkspaceTabs, workspaceTabBudget } from '../workspaceTabs';
import { StudioContextMenu, type StudioMenuItem } from '../ui';

const windowsHost = typeof navigator !== 'undefined' && /Win/i.test(navigator.platform + navigator.userAgent);

/** Workspace switcher in the title bar. Tabs that do not fit move into a menu instead of being clipped. */
export default function WorkspaceTabs({ value, onChange }: { value: StudioWorkspace; onChange: (id: StudioWorkspace) => void }) {
  const slot = useRef<HTMLDivElement>(null);
  const probe = useRef<HTMLDivElement>(null);
  const more = useRef<HTMLButtonElement>(null);
  const [shown, setShown] = useState<StudioWorkspace[]>([...WORKSPACES]);
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);

  useLayoutEffect(() => {
    const host = slot.current;
    const measure = probe.current;
    const side = host?.parentElement;
    if (!host || !measure || !side) return;
    const fit = () => {
      const buttons = [...measure.querySelectorAll<HTMLButtonElement>('button')];
      const moreButton = buttons.find(button => button.dataset.more === '1');
      const tabs = buttons.filter(button => button.dataset.more !== '1');
      const widths = tabs.map(button => button.offsetWidth);
      const indexes = fitWorkspaceTabs(widths, workspaceTabBudget(host), WORKSPACES.indexOf(value), moreButton?.offsetWidth || 36);
      const next = indexes.map(index => WORKSPACES[index]);
      setShown(prev => prev.length === next.length && prev.every((id, index) => id === next[index]) ? prev : next);
    };
    const observer = new ResizeObserver(fit);
    observer.observe(side);
    observer.observe(measure);
    fit();
    return () => observer.disconnect();
  }, [value]);

  const hidden = WORKSPACES.filter(id => !shown.includes(id));
  const items: StudioMenuItem[] = hidden.map(id => ({
    id,
    label: WORKSPACE_INFO[id].title,
    shortcut: (windowsHost ? 'Ctrl+' : '⌘') + WORKSPACE_INFO[id].key,
    checked: id === value,
    onSelect: () => onChange(id),
  }));
  const activeHidden = hidden.includes(value);

  return <div className="workspace-tabs-slot" ref={slot}>
    <div className="workspace-tabs" role="tablist" aria-label="Workspaces">
      <div className="workspace-tabs__probe" ref={probe} aria-hidden="true">
        {WORKSPACES.map(id => <button key={id} type="button" tabIndex={-1} className="workspace-tabs__tab">{WORKSPACE_INFO[id].title}</button>)}
        <button type="button" tabIndex={-1} className="workspace-tabs__tab workspace-tabs__more" data-more="1"><ChevronDown size={14}/></button>
      </div>
      {shown.map(id => <button key={id} type="button" role="tab" aria-selected={value === id}
        className={'workspace-tabs__tab' + (value === id ? ' is-active' : '')}
        title={WORKSPACE_INFO[id].purpose + ' (⌘' + WORKSPACE_INFO[id].key + ')'}
        onClick={() => onChange(id)}>{WORKSPACE_INFO[id].title}</button>)}
      {hidden.length > 0 && <button ref={more} type="button"
        className={'workspace-tabs__tab workspace-tabs__more' + (activeHidden ? ' is-active' : '')}
        aria-haspopup="menu" aria-expanded={menu !== null}
        aria-label={activeHidden ? WORKSPACE_INFO[value].title : 'More workspaces'}
        title="More workspaces"
        onClick={() => {
          const rect = more.current?.getBoundingClientRect();
          if (!rect) return;
          setMenu(current => current ? null : { x: rect.left, y: rect.bottom + 4 });
        }}><ChevronDown size={14}/></button>}
      {menu && <StudioContextMenu x={menu.x} y={menu.y} items={items} label="Workspaces" anchor={more.current}
        onClose={() => setMenu(null)}/>}
    </div>
  </div>;
}
