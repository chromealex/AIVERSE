import { useEffect, useRef, useState } from 'react';
import { Check, Clock, Folder, FolderOpen, Package, Plus, Search, X } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioLoader } from '../ui';
import type { StudioHub, StudioSnapshot, StudioTemplate } from '../types';
import GameWizard from './GameWizard';
import './ProjectHub.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const ago = (t: number) => {
  if (!t) return '';
  const d = Date.now() / 1000 - t;
  return d < 3600 ? Math.max(1, Math.round(d / 60)) + ' min ago' : d < 86400 ? Math.round(d / 3600) + ' h ago' : Math.round(d / 86400) + ' d ago';
};
const mb = (n?: number) => !n ? '' : n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';

function TemplateCard({ t, on, pick }: { t: StudioTemplate; on: boolean; pick: () => void }) {
  return <button type="button" className={'hub__tmpl' + (on ? ' is-on' : '') + (t.preview ? ' has-preview' : '')} onClick={pick} aria-pressed={on} aria-label={t.name} title={t.description}>
    {t.preview && <img src={t.preview} alt="" loading="lazy"/>}
    <b>{t.name}{on && <Check size={14}/>}</b>
    <span>{t.id === 'empty' ? 'minimal' : t.genre}{t.maps ? ` · ${t.maps} map${t.maps > 1 ? 's' : ''}` : ''}
      {t.source === 'catalog' && t.size ? ` · ${mb(t.size)}` : ''}</span>
    {t.source === 'catalog' && !t.official && <small className="hub__badge">community</small>}
  </button>;
}

/** Project hub: recent projects, a new project from a template (folder chosen in macOS), Open… for anything else.
 *  Templates: the official ones of the package catalog (AIVERSE-PACKAGES) first; "More templates" searches
 *  every template package of the catalog. */
export default function ProjectHub({ onOpened, openOther, compact }: {
  onOpened: (s: StudioSnapshot) => void; openOther: () => void; compact?: boolean;
}) {
  const [hub, setHub] = useState<StudioHub | null>(null);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState('');
  const [templ, setTempl] = useState<StudioTemplate | null>(null);
  const [name, setName] = useState('');
  const [browse, setBrowse] = useState(false);
  const [page, setPage] = useState<'projects' | 'templates' | 'wizard'>('projects');
  const [wizardVisited, setWizardVisited] = useState(false);
  const [projectQuery, setProjectQuery] = useState('');
  const [query, setQuery] = useState('');
  const [found, setFound] = useState<StudioTemplate[] | null>(null);
  const [catalogError, setCatalogError] = useState('');
  const searchSeq = useRef(0);
  useEffect(() => {
    let active = true;
    void invoke('studio.hub', {}).then(h => {
      if (!active) return;
      setHub(h);
      // the hub opened from the package cache; refresh the official templates from the catalog
      void invoke('studio.hubTemplates', {}).then(r => {
        if (!active) return;
        if (r.templates.length) setHub(cur => cur ? { ...cur, templates: r.templates } : cur);
      }).catch(() => undefined);
    }).catch(e => { if (active) setError(message(e)); });
    return () => { active = false; };
  }, []);
  useEffect(() => {
    if (!browse) return;
    const seq = ++searchSeq.current;
    setFound(null);
    const timer = setTimeout(() => {
      void invoke('studio.hubTemplates', { all: true, query }).then(r => {
        if (seq !== searchSeq.current) return;
        setFound(r.templates); setCatalogError(r.error ?? '');
      }).catch(e => { if (seq === searchSeq.current) { setFound([]); setCatalogError(message(e)); } });
    }, query ? 250 : 0);
    return () => { clearTimeout(timer); ++searchSeq.current; };
  }, [browse, query]);
  const done = (r: { snapshot?: StudioSnapshot | null; cancelled?: boolean }) => { if (r.snapshot) onOpened(r.snapshot); };
  const open = async (dir: string) => {
    setBusy(dir);
    try { done(await invoke('studio.hubOpen', { dir })); setError(''); } catch (e) { setError(message(e)); } finally { setBusy(''); }
  };
  const forget = async (dir: string) => {
    setBusy(dir);
    try {
      await invoke('studio.hubForget', { dir });
      setHub(current => current && { ...current, recent: current.recent.filter(project => project.dir !== dir) });
      setError('');
    } catch (e) { setError(message(e)); } finally { setBusy(''); }
  };
  const create = async () => {
    if (!templ || !name.trim()) return;
    setBusy('create');
    try { done(await invoke('studio.hubCreate', { template: templ.id, name: name.trim() })); setError(''); } catch (e) { setError(message(e)); } finally { setBusy(''); }
  };
  const suggestedName = (t: StudioTemplate) => t.id === 'empty' ? 'My Game' : 'My ' + t.name;
  const pick = (t: StudioTemplate) => {
    if (!name.trim() || (templ && name === suggestedName(templ))) setName(suggestedName(t));
    setTempl(t);
  };
  const needle = projectQuery.trim().toLowerCase();
  const recent = hub?.recent.filter(p => (p.name + ' ' + p.dir).toLowerCase().includes(needle)) ?? [];
  return <div className={'hub' + (compact ? ' hub--compact' : '')}>
    <header className="hub__head">
      <h2>Project Hub</h2>
      <button type="button" className="au-button" onClick={openOther} disabled={!!busy}><FolderOpen size={15}/>Open folder…</button>
    </header>
    <nav className="hub__nav" aria-label="Project Hub pages">
      <button aria-current={page === 'projects' ? 'page' : undefined} disabled={!!busy} onClick={() => setPage('projects')}><Clock size={15}/>Recent projects</button>
      <button aria-current={page === 'templates' ? 'page' : undefined} disabled={!!busy} onClick={() => setPage('templates')}><Plus size={15}/>New project</button>
      <button aria-current={page === 'wizard' ? 'page' : undefined} disabled={!!busy} onClick={() => { setWizardVisited(true); setPage('wizard'); }}>Game wizard</button>
    </nav>
    {error && <p className="hub__error" role="alert">{error}</p>}
    <div className="hub__body">
    {wizardVisited && <div hidden={page !== 'wizard'}><GameWizard onOpened={onOpened} onBusy={active => setBusy(active ? 'wizard' : '')}/></div>}
    {page === 'wizard' ? null : page === 'projects' ? <section aria-label="Recent projects">
      <label className="hub__search"><Search size={15}/><input type="search" aria-label="Search recent projects" placeholder="Find by name or folder" value={projectQuery} onChange={e => setProjectQuery(e.target.value)}/></label>
      {!hub && !error && <StudioLoader size={20} label="Loading recent projects…"/>}
      {hub && !recent.length && <p>{needle ? 'No projects match your search.' : 'Your recently opened projects will appear here.'}</p>}
      {recent.map(p => <div key={p.dir} className="hub__row"><button type="button" className="hub__item" disabled={!!busy || !p.exists} onClick={() => void open(p.dir)} title={p.dir}>
        <Folder size={20}/><span><b>{p.name || p.dir.split('/').pop()}</b><small>{p.dir}</small></span>
        <small>{busy === p.dir ? <StudioLoader size={14}/> : p.exists ? ago(p.lastOpened) : 'Folder missing'}</small>
      </button>
        <button type="button" className="hub__forget" disabled={!!busy} aria-label={`Remove ${p.name || p.dir} from recent projects`}
          title="Remove from recent projects" onClick={() => void forget(p.dir)}><X size={15}/></button>
      </div>)}
    </section> : <section className="hub__workspace" aria-label="Create a project">
      <div><div className="hub__subhead"><h3>{browse ? 'Template catalog' : 'Project templates'}</h3>
        <button type="button" className="au-button au-button--sm" disabled={!!busy} onClick={() => setBrowse(!browse)}><Package size={14}/>{browse ? 'Featured templates' : 'Browse catalog'}</button></div>
        {browse && <label className="hub__search"><Search size={15}/><input type="search" aria-label="Search templates" placeholder="Search by name, genre or mechanic" value={query} onChange={e => setQuery(e.target.value)}/></label>}
        {browse && catalogError && <p className="hub__error" role="alert">{catalogError}</p>}
        {(browse ? !found : !hub) && <StudioLoader size={18} label="Loading templates…"/>}
        {browse && found?.length === 0 && <p>No matching templates.</p>}
        <fieldset className="hub__templates" aria-label="Choose a template" disabled={!!busy}>
          {(browse ? found ?? [] : hub?.templates ?? []).map(t => <TemplateCard key={t.id} t={t} on={templ?.id === t.id} pick={() => pick(t)}/>)}
        </fieldset>
      </div>
      <aside className="hub__details"><h3>{templ?.name || 'Project setup'}</h3>
        {templ ? <form onSubmit={e => { e.preventDefault(); void create(); }}>
          {templ.description && <p>{templ.description}</p>}
          <label>Project name<input className="au-input" value={name} onChange={e => setName(e.target.value)} disabled={!!busy} required/></label>
          <p>{templ.source === 'catalog' ? 'Download this template into a new project folder.' : 'Create a new project folder from this template.'} Choose the parent folder in the next step.</p>
          {hub?.defaultLocation && <small>Default parent folder<br/>{hub.defaultLocation}</small>}
          <button type="submit" className="au-button au-button--primary" disabled={!name.trim() || !!busy}>{busy === 'create' ? <StudioLoader size={15}/> : <FolderOpen size={15}/>}Choose folder…</button>
        </form> : <p>Select a template to name your project and choose its folder.</p>}
      </aside>
    </section>}
    </div>
  </div>;
}
