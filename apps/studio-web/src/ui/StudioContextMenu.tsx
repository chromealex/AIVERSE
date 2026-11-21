import { useEffect, useLayoutEffect, useRef, useState, type ReactNode } from 'react';
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
  onSelect: () => void;
}

/**
 * Shared right-click menu for every Studio panel. Positioned at the pointer and
 * kept inside the window; closes on outside click, Escape, scroll or blur.
 * Arrow keys move focus, Enter activates.
 */
export function StudioContextMenu({ x, y, items, onClose, label = 'Context menu' }: {
  x: number; y: number; items: StudioMenuItem[]; onClose: () => void; label?: string;
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
    const down = (event: PointerEvent) => { if (!ref.current?.contains(event.target as Node)) onClose(); };
    const key = (event: KeyboardEvent) => { if (event.key === 'Escape') { event.preventDefault(); onClose(); } };
    window.addEventListener('pointerdown', down, true);
    window.addEventListener('keydown', key);
    window.addEventListener('blur', onClose);
    window.addEventListener('wheel', onClose, { passive: true });
    return () => {
      window.removeEventListener('pointerdown', down, true);
      window.removeEventListener('keydown', key);
      window.removeEventListener('blur', onClose);
      window.removeEventListener('wheel', onClose);
    };
  }, [onClose]);
  return <div ref={ref} className="au-menu" role="menu" aria-label={label}
    style={{ left: pos.x, top: pos.y }}
    onContextMenu={event => event.preventDefault()}
    onKeyDown={event => {
      if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
      event.preventDefault();
      const buttons = [...(ref.current?.querySelectorAll<HTMLButtonElement>('button:not(:disabled)') ?? [])];
      const index = buttons.indexOf(document.activeElement as HTMLButtonElement);
      const next = buttons[(index + (event.key === 'ArrowDown' ? 1 : buttons.length - 1)) % buttons.length];
      next?.focus();
    }}>
    {items.map(item => <div key={item.id} className={item.separator ? 'au-menu__group' : undefined}>
      {item.caption ? <div className="au-menu__caption">{item.label}</div> : <button type="button" role="menuitem" className={'au-menu__item' + (item.danger ? ' is-danger' : '')}
        disabled={item.disabled} onClick={() => { onClose(); item.onSelect(); }}>
        <span className="au-menu__icon">{item.checked ? '✓' : item.icon}</span>
        <span className="au-menu__label">{item.label}</span>
        {item.shortcut && <kbd className="au-menu__shortcut">{item.shortcut}</kbd>}
      </button>}
    </div>)}
  </div>;
}
