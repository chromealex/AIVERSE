import { useCallback, useEffect, useState, type ReactNode } from 'react';
import { FilePlus2, RefreshCw } from 'lucide-react';
import { invoke } from '../bridge';
import type { LuaMenuEntry, StudioExtensionWidget, StudioExtensions, StudioSnapshot } from '../types';
import type { StudioPanel } from '../nestedDock';
import { extensionPanelFor } from './agentLogic';
import { StudioContextMenu, type StudioMenuItem } from '../ui/StudioContextMenu';
import './ToolPanel.css';
import './ProjectToolPanels.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);

/** Extensions: the project's editor/extensions.lua (declarative widgets, actions, Scene gizmos). */
export default function ExtensionsPanel({ snapshot, commitSnapshot, showPanel }: {
  snapshot: StudioSnapshot | null; commitSnapshot: (s: StudioSnapshot) => void; showPanel: (p: StudioPanel) => void;
}) {
  const path = snapshot?.project.path ?? '';
  const [ext, setExt] = useState<StudioExtensions | null>(null);
  const [values, setValues] = useState<Record<string, string | number | boolean>>({});
  const [status, setStatus] = useState('');
  const [error, setError] = useState('');
  const read = useCallback(() => void invoke('studio.extensions', {}).then(r => { setExt(r); setError(''); }).catch(e => setError(message(e))), []);
  useEffect(() => { if (path) read(); }, [path, snapshot, read]);
  const [menu, setMenu] = useState<{ x: number; y: number; items: StudioMenuItem[] } | null>(null);
  const run = async (id: string, value?: string | number | boolean, element?: string) => {
    try {
      const { command } = element !== undefined ? await invoke('studio.contextAction', { window: 'lua.widget', element, id }) : await invoke('studio.extensionAction', value === undefined ? { id } : { id, value });
      if (command?.command === 'status' && command.text) setStatus(String(command.text));
      else if (element !== undefined && (command?.command === 'undo' || command?.command === 'redo')) {
        const game = await invoke('studio.gameState', {});
        if (!game.active && !game.loading)
          commitSnapshot(await invoke(command.command === 'undo' ? 'studio.undo' : 'studio.redo', {}));
      }
      else if (command?.command === 'focus_selection') await invoke('studio.viewportInput', { kind: 'focus' });
      else if (command?.command === 'clear_selection') commitSnapshot(await invoke('studio.select', { id: null }));
      else if (command?.command === 'select_entity' && command.entityId) commitSnapshot(await invoke('studio.select', { id: String(command.entityId) }));
      else if (command?.command === 'open_window') { const p = extensionPanelFor(String(command.id ?? '')); if (p) showPanel(p as StudioPanel); }
      read();
    } catch (e) { setError(message(e)); }
  };
  const menuItems = (entries: LuaMenuEntry[], element: string): StudioMenuItem[] => {
    const items: StudioMenuItem[] = [];
    let separator = false;
    for (const entry of entries) {
      if (!entry || entry.visible === false || (entry.scope && !['*', 'lua.widget'].includes(entry.scope)) ||
          (entry.element && entry.element !== element)) continue;
      if (!entry.label && !entry.id) { separator = true; continue; }
      if (!entry.label || entry.label.length > 160 || (entry.id?.length ?? 0) > 128) continue;
      const children = Array.isArray(entry.children) ? menuItems(entry.children, element) : undefined;
      items.push({ id: entry.id || String(items.length), label: entry.label, separator, checked: entry.checked,
        disabled: entry.enabled === false, shortcut: entry.shortcut, children,
        onSelect: () => { setMenu(null); if (entry.id) void run(entry.id, undefined, element); } });
      separator = false;
    }
    return items;
  };
  const change = (id: string, value: string | number | boolean) => {
    setValues(previous => ({ ...previous, [id]: value }));
    void run(id, value);
  };
  const create = async () => { try { setExt(await invoke('studio.extensionCreate', {})); } catch (e) { setError(message(e)); } };
  if (!path) return <div className="tool__empty">Open a project to use its editor extensions.</div>;
  const widget = (w: StudioExtensionWidget, key: string, index: number): ReactNode => {
    const id = w.id ?? `item${index + 1}`, label = w.label ?? id;
    switch (w.kind) {
      case 'label': return <p key={key} data-lua-widget={id} className="ext__label">{label}</p>;
      case 'help': return <p key={key} data-lua-widget={id} className="ptp__hint">{label}</p>;
      case 'separator': return <h4 key={key} data-lua-widget={id}>{label}</h4>;
      case 'button': return <button key={key} data-lua-widget={id} type="button" className="au-button au-button--sm" onClick={() => void run(id)}>{label}</button>;
      case 'checkbox': {
        const v = (values[id] ?? !!w.value) as boolean;
        return <label key={key} data-lua-widget={id} className="ptp__check"><input type="checkbox" checked={v} onChange={e => change(id, e.currentTarget.checked)}/>{label}</label>;
      }
      case 'slider': case 'number': {
        const v = Number(values[id] ?? w.value ?? 0);
        return <label key={key} data-lua-widget={id} className="ptp__row"><span>{label}</span>
          <input type={w.kind === 'slider' ? 'range' : 'number'} min={w.min ?? (w.kind === 'slider' ? 0 : undefined)} max={w.max ?? (w.kind === 'slider' ? 1 : undefined)} step={w.step ?? (w.kind === 'slider' ? ((w.max ?? 1) - (w.min ?? 0)) / 100 : 0.1)}
            value={v} onChange={e => {
              const value = e.currentTarget.valueAsNumber;
              if (!Number.isFinite(value)) return;
              change(id, value);
            }}/>
          <output>{Math.round(v * 100) / 100}</output></label>;
      }
      case 'text': {
        const v = String(values[id] ?? w.value ?? '');
        return <label key={key} data-lua-widget={id} className="ptp__row"><span>{label}</span>
          <input className="au-input" value={v} onChange={e => change(id, e.currentTarget.value)}/></label>;
      }
      case 'group': case 'foldout': return <details key={key} data-lua-widget={id} open className="ext__group"><summary>{label}</summary>
        {(w.children ?? []).map((c, i) => widget(c, key + '.' + i, i))}</details>;
      case 'row': return <div key={key} data-lua-widget={id} className="ext__row">{(w.children ?? []).map((c, i) => widget(c, key + '.' + i, i))}</div>;
      default: return null;
    }
  };
  return <div className="ptp">
    <div className="ptp__edhead">
      <b>{ext?.path ?? 'editor/extensions.lua'}</b>
      {!ext?.exists && <button type="button" className="au-button au-button--sm" onClick={() => void create()}><FilePlus2 size={13}/>Create example</button>}
      <button type="button" className="au-button au-button--icon au-button--ghost au-button--sm" title="Reload" onClick={read}><RefreshCw size={13}/></button>
    </div>
    {(error || ext?.error) && <div className="tool__error" role="alert">Lua: {error || ext?.error}</div>}
    {status && <div className="ptp__status is-ok" role="status">{status}</div>}
    {menu && <StudioContextMenu {...menu} onClose={() => setMenu(null)} />}
    <div className="ptp__scroll" onContextMenu={event => {
      const element = (event.target as Element).closest<HTMLElement>('[data-lua-widget]')?.dataset.luaWidget;
      if (!element) return;
      event.preventDefault();
      const x = event.clientX, y = event.clientY;
      setMenu(null);
      void invoke('studio.contextMenu', { window: 'lua.widget', element }).then(result => {
        const items = menuItems(result.items, element);
        if (items.length) setMenu({ x, y, items });
      }).catch(e => setError(message(e)));
    }}>
      {!ext?.exists && <p className="ptp__hint">A project can add its own editor panel: editor/extensions.lua defines window(ctx) (widgets), on_action / on_change (commands) and scene_gizmos(ctx) (Scene shapes). Lua runs sandboxed and edits the editor only through commands.</p>}
      {(ext?.widgets ?? []).map((w, i) => widget(w, String(i), i))}
      {!!ext?.gizmos?.length && <p className="ptp__hint">{ext.gizmos.length} Scene gizmo{ext.gizmos.length > 1 ? 's' : ''} drawn</p>}
    </div>
  </div>;
}
