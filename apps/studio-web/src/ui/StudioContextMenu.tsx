import { useEffect, useLayoutEffect, useRef, useState, type ReactNode } from 'react';
import { createPortal } from 'react-dom';
import './StudioContextMenu.css';

export interface StudioMenuItem {
  id: string;
  label: string;
  icon?: ReactNode;
  shortcut?: string;
  disabled?: boolean;
  danger?: boolean;
  /** Visual separator above this item. */
  separator?: boolean;
  /** Shows a check mark (toggle items such as open panels). */
  checked?: boolean;
  /** Non-interactive section caption. */
  caption?: boolean;
  /** Nested menu, opened beside this row (Add Tab, Delete Layout). */
  children?: StudioMenuItem[];
  onSelect: () => void;
}

function menuElement(target: EventTarget | null): Element | null {
  const node = target instanceof Element ? target : target instanceof Node ? target.parentElement : null;
  return node?.closest('[data-au-menu]') ?? null;
}

/**
 * Shared right-click menu for every Studio panel. Positioned at the pointer and
 * kept inside the window; closes on outside click, Escape, scroll or blur.
 * Arrow keys move focus, Enter activates.
 * `anchor` is the button that opened the menu: a press on it does not count as outside.
 */
export function StudioContextMenu({ x, y, items, onClose, label = 'Context menu', anchor = null, minWidth }: {
  x: number; y: number; items: StudioMenuItem[]; onClose: () => void; label?: string;
  anchor?: HTMLElement | null; minWidth?: number;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState({ x, y });
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    setPos({ x: Math.max(4, Math.min(x, window.innerWidth - r.width - 4)),
      y: Math.max(4, Math.min(y, window.innerHeight - r.height - 4)) });
    el.querySelector<HTMLButtonElement>('button:not(:disabled)')?.focus();
  }, [x, y]);
  useEffect(() => {
    const down = (event: PointerEvent) => {
      if (menuElement(event.target)) return;
      if (anchor && event.target instanceof Node && anchor.contains(event.target)) return;
      onClose();
    };
    const key = (event: KeyboardEvent) => { if (event.key === 'Escape') { event.preventDefault(); onClose(); } };
    // A long menu (Window) scrolls itself. Wheel over the menu must not dismiss it.
    const wheel = (event: WheelEvent) => { if (!menuElement(event.target)) onClose(); };
    window.addEventListener('pointerdown', down, true);
    window.addEventListener('keydown', key);
    window.addEventListener('blur', onClose);
    window.addEventListener('wheel', wheel, { passive: true });
    return () => {
      window.removeEventListener('pointerdown', down, true);
      window.removeEventListener('keydown', key);
      window.removeEventListener('blur', onClose);
      window.removeEventListener('wheel', wheel);
    };
  }, [onClose, anchor]);
  return <div ref={ref} className="au-menu" role="menu" aria-label={label} data-au-menu=""
    style={{ left: pos.x, top: pos.y, minWidth }}
    onContextMenu={event => event.preventDefault()}
    onKeyDown={event => {
      if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
      event.preventDefault();
      const buttons = [...(ref.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? [])];
      const index = buttons.indexOf(document.activeElement as HTMLButtonElement);
      const next = buttons[(index + (event.key === 'ArrowDown' ? 1 : buttons.length - 1)) % buttons.length];
      next?.focus();
    }}>
    <MenuRows items={items} onClose={onClose}/>
  </div>;
}

function MenuRows({ items, onClose }: { items: StudioMenuItem[]; onClose: () => void }) {
  const [fly, setFly] = useState<{ id: string; rect: DOMRect } | null>(null);
  const timer = useRef<number | null>(null);
  const clearTimer = () => { if (timer.current !== null) { window.clearTimeout(timer.current); timer.current = null; } };
  useEffect(() => () => clearTimer(), []);
  const item = fly ? items.find(row => row.id === fly.id) : undefined;
  return <>
    {items.map(row => <div key={row.id}
      className={'au-menu__row' + (row.separator ? ' au-menu__group' : '')}
      onMouseEnter={event => {
        clearTimer();
        setFly(row.children ? { id: row.id, rect: event.currentTarget.getBoundingClientRect() } : null);
      }}
      onMouseLeave={() => {
        if (!row.children) return;
        clearTimer();
        timer.current = window.setTimeout(() => setFly(current => current?.id === row.id ? null : current), 180);
      }}>
      {row.caption ? <div className="au-menu__caption">{row.label}</div> : <button type="button" role="menuitem"
        aria-haspopup={row.children ? 'menu' : undefined}
        aria-expanded={row.children ? fly?.id === row.id : undefined}
        className={'au-menu__item' + (row.danger ? ' is-danger' : '')}
        disabled={row.disabled}
        onClick={event => {
          if (row.children) {
            const parent = event.currentTarget.parentElement;
            if (parent) setFly({ id: row.id, rect: parent.getBoundingClientRect() });
            return;
          }
          onClose();
          row.onSelect();
        }}>
        <span className="au-menu__icon">{row.checked ? '✓' : row.icon}</span>
        <span className="au-menu__label">{row.label}</span>
        {row.children ? <span className="au-menu__shortcut" aria-hidden="true">›</span>
          : row.shortcut && <kbd className="au-menu__shortcut">{row.shortcut}</kbd>}
      </button>}
    </div>)}
    {item?.children && fly && <Flyout anchor={fly.rect} label={item.label} items={item.children} onClose={onClose}
      onEnter={clearTimer} onLeave={() => {
        clearTimer();
        timer.current = window.setTimeout(() => setFly(null), 180);
      }}/>}
  </>;
}

/** Drawn on document.body so a scrolling menu cannot clip it. */
function Flyout({ anchor, label, items, onClose, onEnter, onLeave }: {
  anchor: DOMRect; label: string; items: StudioMenuItem[]; onClose: () => void;
  onEnter: () => void; onLeave: () => void;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const [pos, setPos] = useState({ x: anchor.right - 8, y: anchor.top });
  useLayoutEffect(() => {
    const el = ref.current;
    if (!el) return;
    const width = el.offsetWidth;
    const height = el.offsetHeight;
    let x = anchor.right - 8;
    if (x + width > window.innerWidth - 4) x = Math.max(4, anchor.left - width + 8);
    let y = anchor.top;
    if (y + height > window.innerHeight - 4) y = Math.max(4, window.innerHeight - height - 4);
    setPos({ x, y });
  }, [anchor, items]);
  return createPortal(
    <div ref={ref} className="au-menu au-menu--sub" role="menu" aria-label={label} data-au-menu=""
      style={{ left: pos.x, top: pos.y }}
      onMouseEnter={onEnter} onMouseLeave={onLeave}
      onContextMenu={event => event.preventDefault()}>
      <MenuRows items={items} onClose={onClose}/>
    </div>, document.body);
}
