import { useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { ChevronDown, LayoutGrid } from 'lucide-react';
import {
  deleteLayoutPreset, factoryPresets, layoutDownloadName, parseLayoutFile, readSavedPresets,
  resetWorkspaceLayouts, saveLayoutPreset, serializeLayoutFile, type LayoutPreset,
} from '../layoutPresets.ts';
import type { DockTree } from '../nestedDock.ts';
import { StudioContextMenu, type StudioMenuItem } from '../ui';
import { WORKSPACE_INFO, workspaceDockTree, type StudioWorkspace } from '../workspaces.ts';
import './LayoutMenu.css';

function download(filename: string, text: string) {
  const blob = new Blob([text], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.rel = 'noopener';
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
}

/** Compact Layout menu in the title bar. Scale settings stay on the gear. */
export default function LayoutMenu({ workspace, dock, onApply, onError }: {
  workspace: StudioWorkspace;
  dock: DockTree;
  onApply: (tree: DockTree) => void;
  onError: (message: string) => void;
}) {
  const buttonRef = useRef<HTMLButtonElement>(null);
  const fileRef = useRef<HTMLInputElement>(null);
  const [open, setOpen] = useState(false);
  const [naming, setNaming] = useState(false);
  const [name, setName] = useState('');
  const [presets, setPresets] = useState<LayoutPreset[]>(() => readSavedPresets(null));
  const [box, setBox] = useState({ x: 0, y: 0 });
  const rect = open ? buttonRef.current?.getBoundingClientRect() : null;
  const saved = presets.filter(preset => preset.workspace === workspace);
  const placeDialog = () => {
    const r = buttonRef.current?.getBoundingClientRect();
    if (r) setBox({ x: Math.max(8, r.right - 240), y: r.bottom + 4 });
  };
  const items: StudioMenuItem[] = [
    ...factoryPresets(workspace).map(preset => ({
      id: preset.id, label: preset.title, onSelect: () => onApply(preset.tree),
    })),
    ...saved.map(preset => ({
      id: preset.id, label: preset.title, onSelect: () => onApply(preset.tree),
    })),
    { id: 'save', label: 'Save Layout…', separator: true, onSelect: () => { setName(''); placeDialog(); setNaming(true); } },
    { id: 'save-file', label: 'Save Layout to File…', onSelect: () => {
      const title = WORKSPACE_INFO[workspace].title + ' layout';
      download(layoutDownloadName(title), serializeLayoutFile(title, workspace, dock));
    } },
    { id: 'load-file', label: 'Load Layout from File…', onSelect: () => fileRef.current?.click() },
    { id: 'delete', label: 'Delete Layout', onSelect: () => {}, children: saved.length
      ? saved.map(preset => ({
        id: 'del-' + preset.id, label: preset.title,
        onSelect: () => setPresets(deleteLayoutPreset(preset.id)),
      }))
      : [{ id: 'del-none', label: 'No saved layouts', disabled: true, onSelect: () => {} }] },
    { id: 'reset', label: 'Reset All Layouts', onSelect: () => {
      resetWorkspaceLayouts();
      onApply(workspaceDockTree(workspace));
    } },
  ];
  return <>
    <button ref={buttonRef} type="button" className="layout-menu-button" aria-haspopup="menu"
      aria-expanded={open} aria-label="Layout" title="Layout"
      onMouseDown={event => event.stopPropagation()}
      onClick={() => setOpen(value => !value)}>
      <LayoutGrid size={14}/><span>Layout</span><ChevronDown size={13}/>
    </button>
    <input ref={fileRef} type="file" accept="application/json,.json" hidden
      onChange={event => {
        const file = event.target.files?.[0] ?? null;
        event.target.value = '';
        if (!file) return;
        void file.text().then(text => {
          const parsed = parseLayoutFile(text);
          if (!parsed) { onError('This file is not a layout.'); return; }
          onApply(parsed.tree);
        }).catch(() => onError('This file is not a layout.'));
      }}/>
    {open && rect && <StudioContextMenu x={rect.right - 248} y={rect.bottom + 2} anchor={buttonRef.current}
      minWidth={248} label="Layout" items={items} onClose={() => setOpen(false)}/>}
    {naming && createPortal(<div className="layout-name-backdrop" onMouseDown={() => setNaming(false)}>
      <form className="layout-name" role="dialog" aria-label="Save Layout" style={{ left: box.x, top: box.y }}
        onMouseDown={event => event.stopPropagation()}
        onSubmit={event => {
          event.preventDefault();
          if (!name.trim()) return;
          setPresets(saveLayoutPreset(name, workspace, dock));
          setNaming(false);
        }}>
        <strong>Save Layout</strong>
        <input autoFocus value={name} maxLength={40} placeholder="Name" aria-label="Layout name"
          onChange={event => setName(event.target.value)}
          onKeyDown={event => { if (event.key === 'Escape') { event.preventDefault(); setNaming(false); } }}/>
        <div className="layout-name__actions">
          <button type="button" onClick={() => setNaming(false)}>Cancel</button>
          <button type="submit" disabled={!name.trim()}>Save</button>
        </div>
      </form>
    </div>, document.body)}
  </>;
}
