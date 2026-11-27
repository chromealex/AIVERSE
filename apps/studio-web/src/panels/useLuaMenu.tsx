import { useRef, useState, type MouseEvent } from 'react';
import { invoke } from '../bridge';
import type { LuaMenuEntry, StudioSnapshot } from '../types';
import type { StudioPanel } from '../nestedDock';
import { extensionPanelFor } from './agentLogic';
import { StudioContextMenu, type StudioMenuItem } from '../ui/StudioContextMenu';

const menuItems = (entries: LuaMenuEntry[], window: string, element: string, select: (id: string) => void): StudioMenuItem[] => {
    const items: StudioMenuItem[] = [];
    let separator = false;
    for (const entry of entries) {
      if (!entry || entry.visible === false || (entry.scope && !['*', window].includes(entry.scope)) ||
          (entry.element && entry.element !== element)) continue;
      if (!entry.label && !entry.id) { separator = true; continue; }
      if (!entry.label || entry.label.length > 160 || (entry.id?.length ?? 0) > 128) continue;
      const children = Array.isArray(entry.children) ? menuItems(entry.children, window, element, select) : undefined;
      items.push({ id: entry.id || String(items.length), label: entry.label, separator, checked: entry.checked,
        disabled: entry.enabled === false, shortcut: entry.shortcut, children,
        onSelect: () => { if (entry.id) select(entry.id); } });
      separator = false;
    }
    return items;
  };

export function useLuaMenu(commitSnapshot: (s: StudioSnapshot) => void, showPanel: (p: StudioPanel) => void,
  setStatus: (s: string) => void, setError: (s: string) => void, afterAction?: () => void) {
  const [menu, setMenu] = useState<{ x: number; y: number; items: StudioMenuItem[] } | null>(null);
  const request = useRef(0);
  const close = () => { ++request.current; setMenu(null); };
  const dispatch = async (command: Record<string, unknown> | null, contextAction = false) => {
      if (command?.command === 'status' && command.text) setStatus(String(command.text));
      else if (contextAction && (command?.command === 'undo' || command?.command === 'redo')) {
        const game = await invoke('studio.gameState', {});
        if (!game.active && !game.loading)
          commitSnapshot(await invoke(command.command === 'undo' ? 'studio.undo' : 'studio.redo', {}));
      }
      else if (command?.command === 'focus_selection') await invoke('studio.viewportInput', { kind: 'focus' });
      else if (command?.command === 'clear_selection') commitSnapshot(await invoke('studio.select', { id: null }));
      else if (command?.command === 'select_entity' && command.entityId) commitSnapshot(await invoke('studio.select', { id: String(command.entityId) }));
      else if (command?.command === 'open_window') { const p = extensionPanelFor(String(command.id ?? '')); if (p) showPanel(p as StudioPanel); }
  };
  const open = async (event: MouseEvent, window: string, element: string, builtins: StudioMenuItem[] = []) => {
    event.preventDefault();
    event.stopPropagation();
    const x = event.clientX, y = event.clientY, version = ++request.current;
    setMenu(builtins.length ? { x, y, items: builtins } : null);
    try {
      const result = await invoke('studio.contextMenu', { window, element });
      if (version !== request.current) return;
      const actions = menuItems(result.items, window, element, id => {
        void invoke('studio.contextAction', { window, element, id }).then(async ({command}) => {
          await dispatch(command, true);
          afterAction?.();
        })
          .catch(error => setError(String(error)));
      });
      if (actions.length) setMenu({ x, y, items: [...builtins, ...actions] });
    } catch (error) { if (version === request.current) setError(String(error)); }
  };
  return { open, dispatch, popup: menu && <StudioContextMenu {...menu} onClose={close} /> };
}
