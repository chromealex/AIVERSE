import { useCallback, useEffect, useMemo, useState } from 'react';
import type { StudioSnapshot } from '../../types';
import { Package, RefreshCw, Search } from 'lucide-react';
import {
  StudioPanel, StudioSection, StudioBadge, StudioAlert, StudioEmptyState, StudioButton,
  StudioInput, StudioSelect, StudioListRow, StudioLoader
} from '../../ui';
import { invoke } from '../../bridge';
import './CatalogPanel.css';

/** One line of package_search. */
interface CatalogHit { id: string; kind?: string; category?: string; title?: string; latest?: string; size?: number; cached?: boolean }
/** package_info: the manifest plus versions.json of the package. */
interface CatalogInfo {
  id: string; title?: string; summary?: string; description?: string; license?: string; kind?: string;
  category?: string; latest?: string; minEngine?: string; previews?: string[]; readme?: string;
  dependencies?: Record<string, string>; install?: { mount?: string };
  versions?: Record<string, { size?: number; minEngine?: string }>;
  [key: string]: unknown;
}
/** package_status: what the open project has. */
interface ProjectPackage { id: string; wanted?: string | null; locked?: string | null; files?: number; missing?: number | string; changed?: number | null }

const CATEGORIES = ['', 'materials', 'textures', 'models', 'templates', 'audio', 'vfx', 'ui', 'scripts', 'plugins', 'tools'];

function size(n?: number): string {
  if (!n) return '';
  return n >= 1048576 ? `${(n / 1048576).toFixed(n >= 104857600 ? 0 : 1)} MB` : `${Math.max(1, Math.round(n / 1024))} KB`;
}

async function op<T>(name: string, params: Record<string, unknown>): Promise<T> {
  return (await invoke('studio.op', { op: name, params })).result as T;
}

/** Package catalog (AIVERSE-PACKAGES): search, details, install into the open project in one click.
 *  Every action runs the Workspace package_* ops (tools/pkg/aiverse_pkg.py); React never writes files. */
export default function CatalogPanel({ snapshot }: { snapshot: StudioSnapshot | null }) {
  const projectLoaded = Boolean(snapshot?.project.path);
  const [query, setQuery] = useState('');
  const [category, setCategory] = useState('');
  const [hits, setHits] = useState<CatalogHit[] | null>(null);
  const [installed, setInstalled] = useState<Record<string, ProjectPackage>>({});
  const [selected, setSelected] = useState<string | null>(null);
  const [info, setInfo] = useState<CatalogInfo | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [note, setNote] = useState<string | null>(null);

  const refreshInstalled = useCallback(async () => {
    if (!projectLoaded) { setInstalled({}); return; }
    try {
      const r = await op<{ packages?: ProjectPackage[] }>('package_status', {});
      const map: Record<string, ProjectPackage> = {};
      for (const p of r.packages ?? []) map[p.id] = p;
      setInstalled(map);
    } catch { setInstalled({}); }
  }, [projectLoaded]);

  const search = useCallback(async () => {
    setBusy('search'); setError(null);
    try {
      const params: Record<string, unknown> = { query };
      if (category) params.category = category;
      setHits(await op<CatalogHit[]>('package_search', params));
    } catch (e) { setError((e as Error).message); setHits([]); }
    finally { setBusy(null); }
  }, [query, category]);

  useEffect(() => { if (projectLoaded) { void search(); void refreshInstalled(); } }, [projectLoaded, category]);   // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (!selected) { setInfo(null); return; }
    let live = true;
    op<CatalogInfo>('package_info', { id: selected }).then(r => { if (live) setInfo(r); }).catch(e => { if (live) setError((e as Error).message); });
    return () => { live = false; };
  }, [selected]);

  const act = async (kind: 'add' | 'remove' | 'install', id?: string) => {
    setBusy(kind + (id ?? '')); setError(null); setNote(null);
    try {
      if (kind === 'add') {
        const r = await op<{ version: string; files: number; dependencies?: { id: string }[] }>('package_add', { id });
        const deps = (r.dependencies ?? []).map(d => d.id);
        setNote(`${id} ${r.version} installed: ${r.files} files${deps.length ? `, with ${deps.join(', ')}` : ''}.`);
      } else if (kind === 'remove') {
        const r = await op<{ removedFiles: number }>('package_remove', { id });
        setNote(`${id} removed (${r.removedFiles} files).`);
      } else {
        await op('package_install', {});
        setNote('Every package of the project was restored from aiverse.lock.');
      }
      await refreshInstalled();
    } catch (e) { setError((e as Error).message); }
    finally { setBusy(null); }
  };

  const problems = useMemo(() => Object.values(installed).filter(p => (Number(p.missing) || 0) > 0 || (p.changed ?? 0) > 0 || !p.locked), [installed]);
  const canInstall = (h: { kind?: string }) => h.kind === 'content';

  return <StudioPanel className="studio-panel-catalog" title="Catalog"
    eyebrow="PACKAGES"
    subtitle={snapshot?.project.name ?? 'No project open'}
    actions={<StudioBadge tone={projectLoaded ? 'success' : 'neutral'}>
      {projectLoaded ? `${Object.keys(installed).length} installed` : 'No project'}
    </StudioBadge>}>
    <div className="au-stack">
      {!projectLoaded && <StudioEmptyState icon={<Package size={22}/>} title="Open a project"
        description="Packages are installed into the open project: its game.json lists them and aiverse.lock pins their files."/>}
      {projectLoaded && <>
        <div className="catalog-search">
          <StudioInput value={query} placeholder="Search: rocks, terrain, pbr…" aria-label="Search the catalog"
            onChange={e => setQuery(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') void search(); }}/>
          <StudioSelect value={category} aria-label="Category" onChange={e => setCategory(e.target.value)}>
            {CATEGORIES.map(c => <option key={c} value={c}>{c || 'All'}</option>)}
          </StudioSelect>
          <StudioButton variant="primary" loading={busy === 'search'} onClick={() => void search()}><Search size={14}/> Search</StudioButton>
        </div>
        {error && <StudioAlert tone="danger" title="Catalog">{error}</StudioAlert>}
        {note && <StudioAlert tone="success">{note}</StudioAlert>}
        {problems.length > 0 && <StudioAlert tone="warning" title="Project packages need attention">
          {problems.map(p => p.id).join(', ')}: missing or changed files.{' '}
          <StudioButton size="sm" loading={busy === 'install'} onClick={() => void act('install')}><RefreshCw size={12}/> Restore from aiverse.lock</StudioButton>
        </StudioAlert>}
        <StudioSection title={hits ? `Packages (${hits.length})` : 'Packages'}>
          {hits === null && <StudioLoader size={22} label="Loading the catalog"/>}
          {hits && hits.length === 0 && <p className="catalog-muted">Nothing found.</p>}
          <div className="catalog-list">
            {hits?.map(h => {
              const have = installed[h.id];
              return <StudioListRow key={h.id} selected={selected === h.id} onClick={() => setSelected(h.id)}
                label={h.title ?? h.id}
                meta={<span className="catalog-meta">
                  <span>{h.category}</span><span>{size(h.size)}</span>
                  {have ? <StudioBadge tone="success">{have.locked ?? 'installed'}</StudioBadge> : <span>v{h.latest}</span>}
                </span>}/>;
            })}
          </div>
        </StudioSection>
        {info && <StudioSection title={info.title ?? info.id} actions={
          installed[info.id]
            ? <StudioButton variant="danger" size="sm" loading={busy === 'remove' + info.id} onClick={() => void act('remove', info.id)}>Remove</StudioButton>
            : canInstall(info) && <StudioButton variant="primary" size="sm" loading={busy === 'add' + info.id} onClick={() => void act('add', info.id)}>Install</StudioButton>}>
          {info.previews?.[0] && <img className="catalog-preview" src={info.previews[0]} alt="" loading="lazy"/>}
          {info.summary && <p>{info.summary}</p>}
          {info.description && <p className="catalog-muted">{info.description}</p>}
          <dl className="catalog-facts">
            <dt>Version</dt><dd>{info.latest}{info.minEngine ? ` · engine ${info.minEngine}+` : ''}</dd>
            <dt>Size</dt><dd>{size(info.latest ? info.versions?.[info.latest]?.size : undefined)}</dd>
            <dt>Licence</dt><dd>{info.license}</dd>
            {info.install?.mount && <><dt>Installs to</dt><dd><code>{info.install.mount}</code></dd></>}
            {info.dependencies && Object.keys(info.dependencies).length > 0 && <><dt>Needs</dt><dd>{Object.keys(info.dependencies).join(', ')} (installed with it)</dd></>}
          </dl>
          {!canInstall(info) && <p className="catalog-muted">This is a {info.kind} package: tools fetch it into the package cache, it is not copied into projects.</p>}
        </StudioSection>}
      </>}
    </div>
  </StudioPanel>;
}
