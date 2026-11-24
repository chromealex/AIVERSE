import { useEffect, useRef, useState } from 'react';
import { ArrowLeft, Clock, FolderOpen, Package, Plus, Search, Sparkles } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioLoader } from '../ui';
import type { StudioHub, StudioSnapshot, StudioTemplate } from '../types';
import './ProjectHub.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const ago = (t: number) => {
  if (!t) return '';
  const d = Date.now() / 1000 - t;
  return d < 3600 ? Math.max(1, Math.round(d / 60)) + ' min ago' : d < 86400 ? Math.round(d / 3600) + ' h ago' : Math.round(d / 86400) + ' d ago';
};
const mb = (n?: number) => !n ? '' : n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : Math.max(1, Math.round(n / 1024)) + ' KB';

function TemplateCard({ t, on, pick }: { t: StudioTemplate; on: boolean; pick: () => void }) {
  return <button type="button" className={'hub__tmpl' + (on ? ' is-on' : '') + (t.preview ? ' has-preview' : '')} onClick={pick} title={t.description}>
    {t.preview && <img src={t.preview} alt="" loading="lazy"/>}
    <b>{t.name}</b>
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
  const [query, setQuery] = useState('');
  const [found, setFound] = useState<StudioTemplate[] | null>(null);
  const [catalogError, setCatalogError] = useState('');
  const searchSeq = useRef(0);
  useEffect(() => {
    void invoke('studio.hub', {}).then(h => {
      setHub(h);
      // the hub opened from the package cache; refresh the official templates from the catalog
      void invoke('studio.hubTemplates', {}).then(r => {
        if (r.templates.length) setHub(cur => cur ? { ...cur, templates: r.templates } : cur);
        setCatalogError(r.error ?? '');
      }).catch(() => undefined);
    }).catch(e => setError(message(e)));
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
    return () => clearTimeout(timer);
  }, [browse, query]);
  const done = (r: { snapshot?: StudioSnapshot | null; cancelled?: boolean }) => { if (r.snapshot) onOpened(r.snapshot); };
  const open = async (dir: string) => {
    setBusy(dir);
    try { done(await invoke('studio.hubOpen', { dir })); setError(''); } catch (e) { setError(message(e)); } finally { setBusy(''); }
  };
  const create = async () => {
    if (!templ || !name.trim()) return;
    setBusy('create');
    try { done(await invoke('studio.hubCreate', { template: templ.id, name: name.trim() })); setError(''); } catch (e) { setError(message(e)); } finally { setBusy(''); }
  };
  const pick = (t: StudioTemplate) => { setTempl(t); if (!name) setName(t.id === 'empty' ? 'My Game' : 'My ' + t.name); };
  const recent = (hub?.recent ?? []).filter(p => p.exists).slice(0, compact ? 5 : 12);
  const createForm = templ && <>
    <form className="hub__create" onSubmit={e => { e.preventDefault(); void create(); }}>
      <input className="au-input" autoFocus value={name} onChange={e => setName(e.target.value)} placeholder="Project name"/>
      <button type="submit" className="au-button au-button--sm au-button--primary" disabled={!name.trim() || !!busy}>
        {busy === 'create' ? <StudioLoader size={14}/> : <Plus size={13}/>}Create…</button>
    </form>
    <p className="hub__muted">{templ.source === 'catalog'
      ? `“${templ.name}” ${templ.version ?? ''} is downloaded from the package catalog into a folder you choose (default ${hub?.defaultLocation}).`
      : `A copy of the template goes to a folder you choose (default ${hub?.defaultLocation}); the template itself is never changed.`}</p>
  </>;
  return <div className={'hub' + (compact ? ' hub--compact' : '')}>
    <header className="hub__head">
      <h2>AIVERSE</h2>
      <button type="button" className="au-button au-button--sm" onClick={openOther}><FolderOpen size={14}/>Open…</button>
    </header>
    {error && <div className="hub__error" role="alert">{error}</div>}
    <div className="hub__cols">
      <section>
        <h3><Clock size={13}/>Recent</h3>
        {!hub && <p className="hub__muted"><StudioLoader size={22} label="Loading…"/></p>}
        {hub && !recent.length && <p className="hub__muted">No recent projects yet.</p>}
        {recent.map(p => <button key={p.dir} type="button" className="hub__item" disabled={!!busy} onClick={() => void open(p.dir)} title={p.dir}>
          <b>{p.name || p.dir.split('/').pop()}</b><span>{p.dir}</span><small>{ago(p.lastOpened)}</small>
          {busy === p.dir && <StudioLoader size={14}/>}</button>)}
      </section>
      {!browse ? <section>
        <h3><Sparkles size={13}/>New from template</h3>
        <div className="hub__templates">
          {(hub?.templates ?? []).map(t => <TemplateCard key={t.id} t={t} on={templ?.id === t.id} pick={() => pick(t)}/>)}
        </div>
        <button type="button" className="au-button au-button--sm hub__more" onClick={() => { setBrowse(true); setTempl(null); }}>
          <Package size={13}/>More templates from the catalog…</button>
        {createForm}
      </section> : <section>
        <h3><button type="button" className="hub__back" onClick={() => { setBrowse(false); setTempl(null); }} aria-label="Back"><ArrowLeft size={13}/></button>
          <Package size={13}/>Template catalog</h3>
        <label className="hub__search"><Search size={13}/>
          <input className="au-input" autoFocus value={query} onChange={e => setQuery(e.target.value)} placeholder="Search templates: genre, mechanic, name"/></label>
        {catalogError && <p className="hub__error">{catalogError}</p>}
        {!found && <p className="hub__muted"><StudioLoader size={18} label="Searching the catalog…"/></p>}
        {found && !found.length && <p className="hub__muted">Nothing found.</p>}
        <div className="hub__templates">
          {(found ?? []).map(t => <TemplateCard key={t.id} t={t} on={templ?.id === t.id} pick={() => pick(t)}/>)}
        </div>
        {createForm}
      </section>}
    </div>
  </div>;
}
