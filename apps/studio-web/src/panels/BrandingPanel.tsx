import { useEffect, useId, useState, type ReactNode } from 'react';
import { invoke } from '../bridge';
import './BrandingPanel.css';

const platforms = ['iOS', 'macOS', 'Android', 'Windows', 'Web', 'Linux'] as const;
type Platform = typeof platforms[number];
type Block = Record<string, unknown>;
interface Branding { icons: unknown; iconsVersion: string; icon?: string; resolved: {iconMaster: string}; warnings: string[] }
interface Variants { variants: { label: string; size: number; png: string }[]; warnings: string[] }

function object(value: unknown): Block {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Block : {};
}
function atPath(base: Block, path: string) {
  return path.split('.').reduce<unknown>((value, key) => object(value)[key], base);
}
function writePath(base: Block, path: string[], value: unknown) {
  const [key, ...rest] = path;
  if (rest.length) {
    const old = base[key];
    const child: Block = typeof old === 'string' ? {image: old} : {...object(old)};
    writePath(child, rest, value);
    if (Object.keys(child).length) base[key] = child;
    else delete base[key];
  } else if (value === null || value === '') delete base[key];
  else base[key] = value;
}

async function op<T>(project: string, name: string, params: Block = {}) {
  const reply = await invoke('studio.op', { op: name, expectedProject: project, params });
  return reply.result as T;
}

export default function BrandingPanel({ project, advanced }: {project: string; advanced: ReactNode}) {
  const [section, setSection] = useState<'icons'|'advanced'>('icons');
  const [platform, setPlatform] = useState<Platform>('iOS');
  const [data, setData] = useState<Branding>();
  const [preview, setPreview] = useState<Variants>();
  const [fields, setFields] = useState<Block>({});
  const [images, setImages] = useState<string[]>([]);
  const [revision, setRevision] = useState(0);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const [previewError, setPreviewError] = useState('');
  const imageList = useId();
  const dirty = Object.keys(fields).length > 0;
  useEffect(() => {
    if (!project || section !== 'icons') return;
    let live = true;
    setData(undefined); setError('');
    void op<Branding>(project, 'branding_get').then(value => {
      if (live) setData(value);
    }).catch(e => { if (live) setError(String(e)); });
    return () => { live = false; };
  }, [project, section, revision]);
  useEffect(() => {
    if (!project) return;
    let live = true;
    void invoke('studio.assets', {}).then(({files}) => {
      if (live) setImages(files.filter(f => /\.(png|jpe?g|tga|bmp)$/i.test(f.path)).map(f => 'content/' + f.path));
    }).catch(() => {});
    return () => { live = false; };
  }, [project, revision]);
  useEffect(() => {
    if (!data || section !== 'icons') return;
    let live = true;
    setPreview(undefined); setPreviewError('');
    void op<Variants>(project, 'icons_preview', {platform}).then(value => {
      if (!value?.variants?.length) throw new Error('No icon variants returned');
      if (live) setPreview(value);
    }).catch(e => { if (live) setPreviewError(String(e)); });
    return () => { live = false; };
  }, [project, platform, data, section]);

  const original: Block = typeof data?.icons === 'string' ? {default: data.icons} : object(data?.icons);
  const read = (path: string) => path in fields ? fields[path] : atPath(original, path);
  function edit(path: string, value: unknown) {
    setFields(previous => ({...previous, [path]: value}));
  }
  function imageField(label: string, key: string, placeholder = 'Use master artwork') {
    const path = typeof atPath(original, key) === 'object' && atPath(original, key) !== null ? key + '.image' : key;
    const value = read(path);
    return <label>{label}<input list={imageList} value={typeof value === 'string' ? value : ''}
      placeholder={placeholder} onChange={e => edit(path, e.target.value)}/></label>;
  }
  function numberField(label: string, path: string, max: number) {
    const value = Number(read(path) ?? 0);
    return <label>{label}<span className="brand-range"><input type="range" min="0" max={max} step=".01" value={value}
      onChange={e => edit(path, Number(e.target.value))}/><output>{Math.round(100 * value)}%</output></span></label>;
  }
  function colorField(label: string, path: string) {
    const raw = read(path);
    const value = typeof raw === 'string' ? raw : raw == null ? '' : JSON.stringify(raw);
    return <label>{label}<span className="brand-color">
      <input type="color" aria-label={label+' colour picker'} value={/^#[a-f0-9]{6}$/i.test(value) ? value : '#000000'}
        onChange={e => edit(path, e.target.value)}/>
      <input aria-label={label} placeholder="Automatic" value={value} onChange={e => edit(path,e.target.value)}/>
    </span></label>;
  }
  async function save() {
    if (!data || saving) return;
    setSaving(true); setError('');
    const icons = {...original};
    for (const [path, value] of Object.entries(fields)) writePath(icons, path.split('.'), value);
    try {
      await op(project, 'branding_set', {icons, expectedIconsVersion: data.iconsVersion});
      setFields({}); setRevision(r => r + 1);
    } catch (e) { setError(String(e)); }
    finally { setSaving(false); }
  }
  if (!project) return <div className="brand-page">Open a project to edit its branding.</div>;
  return <div className="brand-page">
    <header><h2>Branding</h2><nav aria-label="Branding sections">
      <button aria-pressed={section === 'icons'} onClick={() => setSection('icons')}>App icon</button>
      <button aria-pressed={section === 'advanced'} disabled={dirty || saving} onClick={() => setSection('advanced')}>Splash &amp; advanced</button>
    </nav></header>
    {section === 'advanced' ? advanced : <>
      {error && <p role="alert">{error}</p>}
      {data ? <div className="brand-content"><section className="brand-settings">
        <div className="brand-heading"><h3>Master artwork</h3><small>1024 × 1024 or larger</small></div>
        <datalist id={imageList}>{images.map(path => <option key={path} value={path}/>)}</datalist>
        <fieldset disabled={saving}>
          {imageField('Source image', 'default', data.icon || data.resolved.iconMaster)}
          <div className="brand-columns">{numberField('Padding', 'padding', .45)}{numberField('Corner radius', 'cornerRadius', .5)}</div>
          {colorField('Opaque icon background', 'background')}
        </fieldset>
        <h3>Platform variants</h3>
        <nav aria-label="Icon platform">{platforms.map(p => <button key={p} aria-pressed={platform === p} onClick={() => setPlatform(p)}>{p}</button>)}</nav>
        <fieldset disabled={saving}>
          {platform === 'Android' ? <div className="brand-columns">
            {(['legacy','round','foreground','monochrome'] as const).map(k => <div key={k}>{imageField(k.charAt(0).toUpperCase()+k.slice(1), 'android.'+k)}</div>)}
            {colorField('Adaptive background', 'android.background')}
          </div> : imageField(platform + ' artwork', platform.toLowerCase())}
        </fieldset>
        <div className="brand-actions"><button disabled={!dirty || saving} onClick={() => void save()}>{saving ? 'Saving…' : 'Save icon settings'}</button>
          <button disabled={saving} onClick={() => { setFields({}); setRevision(r => r + 1); }}>{dirty ? 'Discard edits & reload' : 'Reload'}</button></div>
        </section><section className="brand-preview"><div className="brand-heading"><h3>Build preview</h3><small>{dirty ? 'Showing saved artwork' : platform}</small></div>
        {previewError ? <p role="alert">{previewError}</p> : !preview ? <p role="status">Rendering variants…</p> :
          <div className="brand-grid">{preview.variants.map(v => <figure key={v.label}><div><img src={'data:image/png;base64,'+v.png} width={v.size} height={v.size} alt={platform+' '+v.label}/></div><figcaption>{/^\d+$/.test(v.label) ? v.size+' × '+v.size : v.label}</figcaption></figure>)}</div>}
        {Array.from(new Set([...data.warnings,...(preview?.warnings ?? [])])).map(w => <p key={w} className="brand-warning">{w}</p>)}
      </section></div> : !error ? <p role="status">Loading icon settings…</p> : <button onClick={() => setRevision(r => r+1)}>Retry</button>}
    </>}
  </div>;
}
