import { useCallback, useEffect, useState, type ReactNode } from 'react';
import { FilePlus2, RefreshCw } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioExtensionWidget, StudioExtensions, StudioSnapshot } from '../types';
import type { StudioPanel } from '../nestedDock';
import { extensionPanelFor } from './agentLogic';
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
  const run = async (id: string, value?: string | number | boolean) => {
    try {
      const { command } = await invoke('studio.extensionAction', value === undefined ? { id } : { id, value });
      if (command?.command === 'status' && command.text) setStatus(String(command.text));
      else if (command?.command === 'focus_selection') await invoke('studio.viewportInput', { kind: 'focus' });
      else if (command?.command === 'clear_selection') commitSnapshot(await invoke('studio.select', { id: null }));
      else if (command?.command === 'select_entity' && command.entityId) commitSnapshot(await invoke('studio.select', { id: String(command.entityId) }));
      else if (command?.command === 'open_window') { const p = extensionPanelFor(String(command.id ?? '')); if (p) showPanel(p as StudioPanel); }
      read();
    } catch (e) { setError(message(e)); }
  };
  const change = (id: string, value: string | number | boolean) => {
    setValues(previous => ({ ...previous, [id]: value }));
    void run(id, value);
  };
  const create = async () => { try { setExt(await invoke('studio.extensionCreate', {})); } catch (e) { setError(message(e)); } };
  if (!path) return <div className="tool__empty">Open a project to use its editor extensions.</div>;
  const widget = (w: StudioExtensionWidget, key: string): ReactNode => {
    const id = w.id ?? key, label = w.label ?? id;
    switch (w.kind) {
      case 'label': return <p key={key} className="ext__label">{label}</p>;
      case 'help': return <p key={key} className="ptp__hint">{label}</p>;
      case 'separator': return <h4 key={key}>{label}</h4>;
      case 'button': return <button key={key} type="button" className="au-button au-button--sm" onClick={() => void run(id)}>{label}</button>;
      case 'checkbox': {
        const v = (values[id] ?? !!w.value) as boolean;
        return <label key={key} className="ptp__check"><input type="checkbox" checked={v} onChange={e => change(id, e.currentTarget.checked)}/>{label}</label>;
      }
      case 'slider': case 'number': {
        const v = Number(values[id] ?? w.value ?? 0);
        return <label key={key} className="ptp__row"><span>{label}</span>
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
        return <label key={key} className="ptp__row"><span>{label}</span>
          <input className="au-input" value={v} onChange={e => change(id, e.currentTarget.value)}/></label>;
      }
      case 'group': case 'foldout': return <details key={key} open className="ext__group"><summary>{label}</summary>
        {(w.children ?? []).map((c, i) => widget(c, key + '.' + i))}</details>;
      case 'row': return <div key={key} className="ext__row">{(w.children ?? []).map((c, i) => widget(c, key + '.' + i))}</div>;
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
    <div className="ptp__scroll">
      {!ext?.exists && <p className="ptp__hint">A project can add its own editor panel: editor/extensions.lua defines window(ctx) (widgets), on_action / on_change (commands) and scene_gizmos(ctx) (Scene shapes). Lua runs sandboxed and edits the editor only through commands.</p>}
      {(ext?.widgets ?? []).map((w, i) => widget(w, String(i)))}
      {!!ext?.gizmos?.length && <p className="ptp__hint">{ext.gizmos.length} Scene gizmo{ext.gizmos.length > 1 ? 's' : ''} drawn</p>}
    </div>
  </div>;
}
