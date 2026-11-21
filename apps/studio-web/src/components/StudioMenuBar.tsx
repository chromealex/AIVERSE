import { useRef, useState } from 'react';
import { StudioContextMenu, type StudioMenuItem } from '../ui';
import './StudioMenuBar.css';

export interface StudioMenu { id: string; label: string; items: () => StudioMenuItem[] }

/** Application menu bar (File / Edit / Window…) inside the HTML title bar. */
export default function StudioMenuBar({ menus }: { menus: StudioMenu[] }) {
  const [open, setOpen] = useState<{ id: string; x: number; y: number } | null>(null);
  const refs = useRef<Record<string, HTMLButtonElement | null>>({});
  const show = (id: string) => {
    const r = refs.current[id]?.getBoundingClientRect();
    if (r) setOpen({ id, x: r.left, y: r.bottom + 2 });
  };
  const current = menus.find(menu => menu.id === open?.id);
  return <nav className="studio-menubar" aria-label="Application menu">
    {menus.map(menu => <button key={menu.id} type="button" ref={el => { refs.current[menu.id] = el; }}
      className={'studio-menubar__item' + (open?.id === menu.id ? ' is-open' : '')}
      aria-haspopup="menu" aria-expanded={open?.id === menu.id}
      onMouseDown={event => { event.stopPropagation(); }}
      onClick={() => open?.id === menu.id ? setOpen(null) : show(menu.id)}
      onMouseEnter={() => { if (open && open.id !== menu.id) show(menu.id); }}>{menu.label}</button>)}
    {open && current && <StudioContextMenu key={open.id} x={open.x} y={open.y} label={current.label}
      items={current.items()} onClose={() => setOpen(null)}/>}
  </nav>;
}
