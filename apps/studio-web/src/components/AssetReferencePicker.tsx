import { useEffect, useMemo, useState } from 'react';
import type { DragEvent } from 'react';
import { StudioDialog } from '../ui';
import { FolderSearch, Search, X } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import { assetFileName, assetKind, assetFolders, assetParent, safeAssetPath } from '../assetTree';
import type { StudioAsset, StudioAssetList } from '../types';
import './AssetReferencePicker.css';

export const STUDIO_ASSET_DRAG_MIME = 'application/x-aiverse-content-path';

export interface AssetReferencePickerProps {
  value: string;
  onChange: (value: string) => void;
  disabled?: boolean;
  label: string;
  /** Only matching kinds are selectable; omit for all files. */
  kinds?: ReadonlyArray<ReturnType<typeof assetKind>>;
  allowClear?: boolean;
  /** Folder references use content-relative paths, not file GUIDs. */
  folderMode?: boolean;
}

/** Engine built-in meshes offered by model pickers (no file in content/). */
export const BUILTIN_MESHES: ReadonlyArray<{ value: string; label: string }> = [
  { value: 'builtin:cube', label: 'Cube' }, { value: 'builtin:sphere', label: 'Sphere' }];
const builtinLabel = (value: string) => {
  const item = BUILTIN_MESHES.find(entry => entry.value === value);
  return item ? item.label + ' (built-in)' : value;
};
const emptyCatalog: StudioAssetList = { root: 'content', files: [], folders: [] };
const normalized = (path: string) =>
  safeAssetPath(path.startsWith('content/') ? path.slice('content/'.length) : path);

/** Asset reference editor shared by any inspector/panel, never a filesystem picker. */
export default function AssetReferencePicker({
  value, onChange, disabled=false, label, kinds, allowClear=true, folderMode=false
}: AssetReferencePickerProps) {
  const [open, setOpen] = useState(false);
  const [search, setSearch] = useState('');
  const [catalog, setCatalog] = useState<StudioAssetList>(emptyCatalog);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);
  const [version, setVersion] = useState(0);

  useEffect(() => onNativeEvent((event) => {
    if (event === 'workspace.changed' || event === 'document.changed') setVersion(n => n + 1);
  }), []);

  // A GUID reference is shown by its content path (GUID stays in the tooltip).
  const [names, setNames] = useState<Record<string, string>>({});
  useEffect(() => {
    if (folderMode || !value || names[value] !== undefined || !/^[0-9a-f]{16,64}$/i.test(value)) return;
    let alive = true;
    void invoke('studio.assets', {}).then(result => {
      if (!alive) return;
      const found = result.files.find(item => item.guid === value);
      setNames(old => ({ ...old, [value]: found ? found.path : '' }));
    }).catch(() => {});
    return () => { alive = false; };
  }, [value, names, folderMode]);
  const shown = (!folderMode && value && names[value]) || value;
  const folderPaths = (list: StudioAssetList) => new Set([
    ...(list.folders??[]).map(item=>normalized(item.path)),
    ...assetFolders(list.files).map(item=>item.path)
  ].filter((path):path is string=>Boolean(path)));
  const folders = useMemo(()=>folderMode?[...folderPaths(catalog)].filter(path=>path.toLowerCase().includes(search.toLowerCase())).sort((a,b)=>a.localeCompare(b)).slice(0,500):[],[folderMode,catalog,search]);

  useEffect(() => {
    if (!open) return;
    let alive = true;
    setLoading(true);
    void invoke('studio.assets', {}).then(result => {
      if (alive) { setCatalog(result); setError(''); }
    }).catch(cause => {
      if (alive) setError(String(cause instanceof Error ? cause.message : cause));
    }).finally(() => { if (alive) setLoading(false); });
    return () => { alive = false; };
  }, [open, version]);

  const permitted = (asset: StudioAsset) => (!kinds?.length || kinds.includes(assetKind(asset.path))) &&
    (!kinds?.includes('image') || kinds.length !== 1 || !asset.importer || asset.importer === 'texture');
  const files = useMemo(() => catalog.files.filter(asset => permitted(asset) &&
    (asset.path.toLocaleLowerCase().includes(search.toLocaleLowerCase()) ||
     asset.guid?.toLocaleLowerCase().includes(search.toLocaleLowerCase())))
    .sort((a,b) => a.path.localeCompare(b.path)).slice(0, 500),
  [catalog, kinds, search]);

  const choose = (asset: StudioAsset) => {
    if (disabled || folderMode || !permitted(asset)) return;
    if (!asset.guid) { setError('Asset is missing a GUID. Refresh the project Assets catalog.'); return; }
    onChange(asset.guid);
    setError('');
    setOpen(false);
  };
  const drop = (event: DragEvent<HTMLElement>) => {
    if (disabled || !event.dataTransfer.types.includes(STUDIO_ASSET_DRAG_MIME)) return;
    const path = normalized(event.dataTransfer.getData(STUDIO_ASSET_DRAG_MIME));
    event.preventDefault();
    event.stopPropagation();
    if (!path) return;
    // Recheck against authoritative catalog on every drop: stale React state
    // must not assign deleted files or accidentally replace GUIDs with paths.
    void invoke('studio.assets', {}).then(result => {
      setCatalog(result);
      const resolved = result.files.find(item => item.path === path);
      if (folderMode) {
        const valid=folderPaths(result);
        const folder=valid.has(path)?path:resolved&&assetKind(resolved.path)==='image'?assetParent(path):'';
        if(folder&&valid.has(folder)){onChange(folder);setOpen(false);setError('');}
        else setError('Drop a content folder or a texture inside a content folder.');
        return;
      }
      if (resolved && permitted(resolved) && resolved.guid) {
        onChange(resolved.guid);
        setError('');
        setOpen(false);
      } else setError('Dropped asset is not present in project/content or has an incompatible type.');
    }).catch(cause => setError(String(cause instanceof Error ? cause.message : cause)));
  };
  const dragOver = (event: DragEvent<HTMLElement>) => {
    if (!disabled && event.dataTransfer.types.includes(STUDIO_ASSET_DRAG_MIME)) {
      event.preventDefault();
      event.stopPropagation();
      event.dataTransfer.dropEffect = 'copy';
    }
  };

  return <div className="studio-asset-reference" onDragOver={dragOver} onDrop={drop}>
    <div className="studio-asset-reference__control">
      <input className="au-input" value={builtinLabel(shown)} readOnly aria-label={label}
        placeholder={folderMode?'Default (drop a folder or texture)':'None (drop an asset)'} title={value} disabled={disabled}/>
      <button type="button" disabled={disabled} title="Choose from Assets" aria-label={'Choose '+label}
        onClick={() => { setSearch(''); setOpen(true); }}><FolderSearch size={16}/></button>
      {allowClear && value && !value.startsWith('builtin:') && <button type="button" disabled={disabled} onClick={() => {setError('');onChange('');}}
        aria-label={'Clear '+label}><X size={14}/></button>}
    </div>
    {error && !open && <p role="alert">{error}</p>}
    {open && <StudioDialog className="studio-asset-reference__overlay" label={'Choose '+label}
      onDismiss={() => setOpen(false)}>
      <section className="studio-asset-reference__dialog">
        <header><strong>Choose {label}</strong>
          <button type="button" aria-label="Close asset chooser" onClick={() => setOpen(false)}><X size={18}/></button>
        </header>
        <label className="studio-asset-reference__search"><Search size={16}/>
          <input autoFocus value={search} onChange={event => setSearch(event.target.value)}
            placeholder={folderMode?'Search folder path…':'Search name, path or GUID…'} aria-label="Search assets"/>
        </label>
        <div className="studio-asset-reference__results" role="listbox" aria-label="Available assets">
          {loading && <p>Loading assets…</p>}
          {error && <p role="alert">{error}</p>}
          {!loading && !error && (folderMode?!folders.length:!files.length && !kinds?.includes('model')) && <p>No matching assets.</p>}
          {folderMode && !loading && folders.map(path=><button type="button" key={path} role="option" aria-selected={value===path} onClick={()=>{if(!disabled){onChange(path);setOpen(false);}}}><span>{assetFileName(path)}</span><small>{path}</small></button>)}
          {!folderMode && kinds?.includes('model') && BUILTIN_MESHES
            .filter(item => item.label.toLocaleLowerCase().includes(search.toLocaleLowerCase()))
            .map(item => <button type="button" key={item.value} role="option" aria-selected={value === item.value}
              onClick={() => { if (!disabled) { onChange(item.value); setOpen(false); } }}>
              <span>{item.label}</span><small>Built-in mesh</small>
            </button>)}
          {!folderMode && !loading && files.map(asset => <button type="button" key={asset.path}
            role="option" aria-selected={value === asset.guid || value === asset.path || value === 'content/' + asset.path}
            onClick={() => choose(asset)}>
            <span>{assetFileName(asset.path)}</span>
            <small>{asset.path}{asset.guid ? ' · '+asset.guid : ''}</small>
          </button>)}
        </div>
        <footer>{folderMode?'Choose a content folder or drop a texture to select its parent folder.':kinds?.includes('model') ? 'Built-in meshes or models under project/content.' :
          'Only assets under project/content are selectable.'} Drag from Assets to assign.</footer>
      </section>
    </StudioDialog>}
  </div>;
}
