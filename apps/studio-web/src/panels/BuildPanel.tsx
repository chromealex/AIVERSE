import { useCallback, useEffect, useRef, useState } from 'react';
import { CheckCircle2, CircleAlert, FolderOpen, Hammer, Ruler, Save, ScrollText, XCircle } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioBuildEstimate, StudioBuildInfo, StudioSnapshot, StudioToolchain } from '../types';
import { StudioAlert, StudioBadge, StudioButton, StudioCheckbox, StudioField, StudioInput, StudioSelect, StudioLoader } from '../ui';
import { buildErrors, buildPhase, buildSteps, formatBytes, missingToolchain, runTarget } from './buildLogic';
import { useBuildTranscript } from './useBuildTranscript';
import './BuildPanel.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const MAX_LOG = 400_000; // characters kept in the panel (the full log is in the log file)

function TextureSizeSelect({path,rule,disabled,onChange}: {
  path:string;rule?:{maxSize?:number;scale?:number};disabled:boolean;onChange:(size:number|null)=>void;
}) {
  const current=rule?.maxSize ?? rule?.scale;
  const sizes=[0.25,0.5,256,512,1024,2048];
  if (current!=null && !sizes.includes(current)) sizes.push(current);
  return <StudioSelect aria-label={'Texture size for '+path} disabled={disabled} value={current ?? ''}
    onChange={e=>onChange(e.currentTarget.value==='' ? null : Number(e.currentTarget.value))}>
    <option value="">Default</option>
    {sizes.map(size=><option key={size} value={size}>{size<1 ? (100*size)+'%' : size+' px'}</option>)}
  </StudioSelect>;
}

function PackageInventory({files,rules,scopeLabel,disabled,saveLevel,setExcluded,saveSize}: {
  files:NonNullable<StudioBuildEstimate['files']>;rules:NonNullable<StudioBuildInfo['rules']>;
  saveSize:(path:string,original:string,size:number|null)=>void;
  setExcluded:(path:string,original:string,exclude:boolean)=>void;
  scopeLabel:string;disabled:boolean;saveLevel:(path:string,original:string,level:number|null)=>void;
}) {
  const byPath = new Map(rules.map(entry=>[entry.rule.match,entry]));
  const [query,setQuery] = useState('');
  const [page,setPage] = useState(0);
  const filtered = files.filter(file=>file.path.toLowerCase().includes(query.trim().toLowerCase()));
  const pageSize = 25;
  const last = Math.max(0,Math.ceil(filtered.length/pageSize)-1);
  const current = Math.min(page,last);
  return <details className="build__card build__files">
    <summary>Packaged files ({files.length})</summary>
    <StudioField label="Filter by path" htmlFor="build-file-filter">
      <StudioInput id="build-file-filter" type="search" value={query} onChange={e=>{setQuery(e.target.value);setPage(0);}}/>
    </StudioField>
    {filtered.length ? <table aria-label="Packaged files"><thead><tr><th>Path</th><th>Original</th><th>Packed</th><th>Codec</th><th>Compression · {scopeLabel}</th><th>Texture size</th><th>Exclude</th></tr></thead>
      <tbody>{filtered.slice(current*pageSize,(current+1)*pageSize).map(file=><tr key={file.path}><td>{file.path}</td>
        <td>{formatBytes(file.rawBytes)}</td><td>{formatBytes(file.storedBytes)}</td><td>{file.codec}</td><td>
          <StudioSelect aria-label={'Compression for '+file.path} disabled={disabled || /[*?]/.test(file.path)}
            value={byPath.get(file.path)?.rule.level ?? ''} onChange={e=>saveLevel(file.path,byPath.get(file.path)?.original ?? '',e.target.value==='' ? null : Number(e.target.value))}>
            <option value="">Default</option><option value={0}>Store</option>
            {Array.from({length:22},(_,i)=><option key={i} value={i+1}>zstd {i+1}</option>)}
          </StudioSelect></td><td>
          {/\.(png|jpe?g|tga|bmp)$/i.test(file.path) && !file.path.startsWith('_atlas/') &&
            <TextureSizeSelect path={file.path} rule={byPath.get(file.path)?.rule} disabled={disabled || /[*?]/.test(file.path)}
              onChange={size=>saveSize(file.path,byPath.get(file.path)?.original ?? '',size)}/>}
        </td><td>
          <input type="checkbox" aria-label={'Exclude '+file.path} checked={!!byPath.get(file.path)?.rule.exclude}
            disabled={disabled || /[*?]/.test(file.path) || file.path==='game.json' || file.path.startsWith('engine/') || file.path.startsWith('_atlas/')}
            onChange={e=>setExcluded(file.path,byPath.get(file.path)?.original ?? '',e.currentTarget.checked)}/>
        </td></tr>)}</tbody>
    </table> : <p>No matching files.</p>}
    {last>0 && <nav aria-label="Packaged file pages">
      <StudioButton disabled={!current} onClick={()=>setPage(current-1)}>Previous</StudioButton>
      <span> {current+1} / {last+1} </span>
      <StudioButton disabled={current===last} onClick={()=>setPage(current+1)}>Next</StudioButton>
    </nav>}
  </details>;
}

function PackageLimit({field, original, inherited, disabled, save}: {
  field:'baseSize'|'chunkSize';original:string|number|null;inherited:string;disabled:boolean;save:(bytes:number|null)=>void;
}) {
  const initial = String(original ?? '');
  const [draft,setDraft] = useState(initial);
  const value = draft.trim();
  const match = /^(\d+(?:\.\d+)?|\.\d+)\s*([KMG]?)B?$/i.exec(value);
  const bytes = match ? Number(match[1]) * 1024 ** ' KMG'.indexOf(match[2].toUpperCase() || ' ') : NaN;
  const valid = !value || (Number.isSafeInteger(bytes) && (field === 'baseSize' || bytes > 0));
  return <StudioField label={field === 'baseSize' ? 'Base install limit' : 'Download package size'} htmlFor={'build-'+field}>
    <StudioInput id={'build-'+field} value={draft} disabled={disabled} placeholder={'Project default · '+inherited}
      aria-invalid={!valid} onChange={e=>setDraft(e.target.value)}/>
    {!valid && <small>Enter a size in bytes, KB, MB or GB.</small>}
    {draft !== initial && <StudioButton disabled={disabled || !valid} onClick={()=>save(value ? bytes : null)}>
      {value ? 'Apply size' : 'Use project default'}
    </StudioButton>}
  </StudioField>;
}

/** Publish: package the open game for a platform with aiversebuild (background run, streamed log). */
export default function BuildPanel({ snapshot, commitSnapshot }: {
  snapshot: StudioSnapshot | null; commitSnapshot: (next: StudioSnapshot) => void;
}) {
  const projectPath = snapshot?.project.path ?? '';
  const [info, setInfo] = useState<StudioBuildInfo | null>(null);
  const [target, setTarget] = useState(() => window.localStorage.getItem('aiverse.studio.build.target') || 'macos');
  const [config, setConfig] = useState<'Release' | 'Debug'>('Release');
  const [compressionDraft, setCompressionDraft] = useState<number | null>(null);
  useEffect(() => setCompressionDraft(null), [projectPath, info?.compression]);
  const [ruleScope,setRuleScope] = useState<'project'|'target'>('project');
  const [savingAtlas, setSavingAtlas] = useState(false);
  const [autoEstimate,setAutoEstimate] = useState(true);
  const [pendingEstimate,setPendingEstimate] = useState<{project:string}|null>(null);
  const queueEstimate = () => { if (autoEstimate) setPendingEstimate({project:projectPath}); };
  useEffect(()=>setPendingEstimate(null),[projectPath]);
  const [audio, setAudio] = useState('auto');
  const [installMissing, setInstallMissing] = useState(false);
  const [toolchain, setToolchain] = useState<StudioToolchain | null>(null);
  const [checking, setChecking] = useState(false);
  const [estimate, setEstimate] = useState<StudioBuildEstimate | null>(null);
  const {state,reload:reloadTranscript,error:transcriptError}=useBuildTranscript(projectPath,MAX_LOG);
  const log=state.text;
  const [error, setError] = useState('');
  const [startedAt, setStartedAt] = useState(0);
  const [now, setNow] = useState(Date.now());
  const logBox = useRef<HTMLPreElement>(null);
  const follow = useRef(true);

  const refreshInfo = useCallback(async () => {
    if (!projectPath) return;
    try { const next = await invoke('studio.buildInfo', {}); setInfo(next); }
    catch (e) { setError(message(e)); }
  }, [projectPath]);
  useEffect(() => { setInfo(null); void refreshInfo(); }, [refreshInfo]);
  useEffect(() => { window.localStorage.setItem('aiverse.studio.build.target', target); }, [target]);
  // Saved project history can change manifest identity without changing the project path.
  useEffect(() => { if (snapshot?.project.historyVersion) void refreshInfo(); }, [snapshot?.project.historyVersion, refreshInfo]);

  // Toolchain of the selected platform (aiversebuild --check-toolchain, about a second).
  useEffect(() => {
    if (!projectPath || !info?.toolFound) { setToolchain(null); return; }
    let live = true;
    setChecking(true); setToolchain(null);
    void invoke('studio.buildToolchain', { target }).then(t => { if (live) setToolchain(t); })
      .catch(e => { if (live) setError(message(e)); }).finally(() => { if (live) setChecking(false); });
    return () => { live = false; };
  }, [projectPath, target, info?.toolFound, state.runs]);
  useEffect(() => {
    if (!projectPath) return;
    let live = true;
    setEstimate(null);
    void invoke('studio.buildEstimate', { target }).then(r => { if (live) setEstimate(r); }).catch(() => {});
    return () => { live = false; };
  }, [projectPath, target, state.runs, snapshot?.project.historyVersion]);

  useEffect(()=>{if(state.runs||state.running)void refreshInfo();},[state.runs,state.running,refreshInfo]);
  useEffect(() => {
    if (!state.running) return;
    const t = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(t);
  }, [state.running]);
  useEffect(() => { if (follow.current && logBox.current) logBox.current.scrollTop = logBox.current.scrollHeight; }, [log]);

  const start = useCallback(async (kind: 'build' | 'estimate', withInstall = installMissing) => {
    setPendingEstimate(null);
    setError('');
    try {
      follow.current = true;
      await invoke('studio.buildStart', { target, config, audio, installMissing: withInstall, estimate: kind === 'estimate' });
      setStartedAt(Date.now()); setNow(Date.now());
      reloadTranscript(kind === 'estimate' ? 'estimate' : 'build-'+target);
    } catch (e) { setError(message(e)); }
  },[target,config,audio,installMissing,reloadTranscript]);
  useEffect(()=>{
    if (!autoEstimate || pendingEstimate?.project!==projectPath || !info?.toolFound ||
        info.project!==projectPath || info.dirty || state.running || savingAtlas) return;
    const timer=window.setTimeout(()=>void start('estimate',false),800);
    return ()=>window.clearTimeout(timer);
  },[autoEstimate,pendingEstimate,projectPath,info?.project,info?.toolFound,info?.dirty,state.running,savingAtlas,start]);
  const save = async () => {
    try { commitSnapshot(await invoke('studio.save', {})); await refreshInfo(); }
    catch (e) { setError(message(e)); }
  };
  const reveal = (params: { target?: string; log?: boolean }) =>
    void invoke('studio.buildReveal', params).catch(e => setError(message(e)));

  if (!projectPath) return <div className="build__empty">Open a project to build it.</div>;
  const phase = buildPhase(state.running, state.runs, state.exitCode, log);
  const steps = buildSteps(log);
  const errors = phase === 'failed' || phase === 'missing-toolchain' ? buildErrors(log) : [];
  const missing = missingToolchain(log);
  const run = runTarget(state.label);
  const elapsed = startedAt ? Math.max(0, Math.round((now - startedAt) / 1000)) : 0;
  const selected = info?.targets.find(t => t.id === target);
  const rules = (ruleScope === 'project' ? info?.rules : selected?.rules) ?? [];
  const pack = estimate?.pack as { packBytes?: number; rawBytes?: number; entries?: number; duplicates?: number } | undefined;
  const saveAtlas = async (action: () => Promise<StudioSnapshot>) => {
    setSavingAtlas(true);setError('');
    try { commitSnapshot(await action()); queueEstimate(); }
    catch (err) { setError(message(err)); }
    finally { await refreshInfo();setSavingAtlas(false); }
  };
  const atlasDisabled = state.running || savingAtlas || info?.project !== projectPath;
  const setExcluded = (path:string,original:string,exclude:boolean) =>
    void saveAtlas(()=>invoke('studio.buildFileExclusion',{project:projectPath,target,scope:ruleScope,path,original,exclude}));
  const compression = compressionDraft ?? info?.compression ?? 0;
  const compressionLabel = compression ? String(compression) : 'Auto (' + (config === 'Debug' ? 3 : 19) + ')';
  const atlas = estimate?.atlas;
  const pages = info?.atlasPages;
  const inheritedPage = typeof pages === 'number' ? pages : pages?.default ?? (['web','android'].includes(target) ? 2048 : 4096);
  const pageOverride = pages && typeof pages === 'object' ? pages[target] : undefined;
  const pageSizes = [1024,2048,4096,8192];
  return <div className="build">
    <aside className="build__side">
      {info && <section className="build__identity" aria-label="Game">
        <b>{info.identity.name || snapshot?.project.name}</b>
        <span>{info.identity.id || 'no id'}</span>
        <span>v{info.identity.version || '0.0.0'} · build {info.identity.build}{info.identity.orientation ? ' · ' + info.identity.orientation : ''}</span>
      </section>}
      <div className="build__label">Platform</div>
      <div role="listbox" aria-label="Build platform" className="build__targets">
        {(info?.targets ?? []).map(t => <button key={t.id} type="button" role="option" aria-selected={t.id === target}
          className={'build__target' + (t.id === target ? ' is-active' : '')} onClick={() => { if (t.id!==target) { setTarget(t.id);queueEstimate(); } }} disabled={state.running}>
          <span>{t.name}</span>{t.built && <StudioBadge tone="success">built</StudioBadge>}</button>)}
      </div>
      <StudioField label="Configuration" htmlFor="build-config">
        <StudioSelect id="build-config" value={config} disabled={state.running} onChange={e => { setConfig(e.target.value as 'Release' | 'Debug');queueEstimate(); }}>
          <option value="Release">Release</option><option value="Debug">Debug</option>
        </StudioSelect>
      </StudioField>
      <StudioField label={'Pack compression · ' + compressionLabel} htmlFor="build-compression">
        <input id="build-compression" type="range" min={0} max={22} step={1} value={compression}
          aria-valuetext={compressionLabel} disabled={atlasDisabled}
          onChange={e => setCompressionDraft(Number(e.currentTarget.value))}/>
      </StudioField>
      {compressionDraft != null && compression !== (info?.compression ?? 0) &&
        <StudioButton disabled={atlasDisabled} onClick={() => void saveAtlas(() =>
          invoke('studio.buildCompression',{project:projectPath,original:info?.compression ?? 0,level:compression}))}>
          Apply compression
        </StudioButton>}
      <StudioCheckbox label="Pack texture atlases" checked={info?.atlasEnabled ?? true} disabled={atlasDisabled}
        onChange={e => {
          const enabled = e.currentTarget.checked;
          void saveAtlas(() => invoke('studio.buildAtlasEnabled',{project:projectPath,original:info?.atlasEnabled ?? true,enabled}));
        }}/>
      <StudioField label="Atlas page limit" htmlFor="build-atlas-page">
        <StudioSelect id="build-atlas-page" value={pageOverride ?? ''} disabled={atlasDisabled || info?.atlasEnabled === false}
          onChange={e => {
            const value = e.currentTarget.value;
            void saveAtlas(() => invoke('studio.buildAtlasPage',{project:projectPath,original:pages ?? null,target,size:value ? Number(value) : null}));
          }}>
          <option value="">Project default ({inheritedPage} px)</option>
          {pageOverride != null && !pageSizes.includes(pageOverride) && <option value={pageOverride}>{pageOverride} px</option>}
          {pageSizes.map(size => <option key={size} value={size}>{size} px</option>)}
        </StudioSelect>
      </StudioField>
      <StudioField label="Content delivery" htmlFor="build-split-mode">
        <StudioSelect id="build-split-mode" disabled={atlasDisabled || !selected}
          value={selected?.split == null ? '' : String(selected.split)} onChange={e => {
            const mode = e.currentTarget.value;
            void saveAtlas(() => invoke('studio.buildSplitMode',{project:projectPath,target,original:selected?.split ?? null,mode:mode === '' ? null : mode === 'true'}));
          }}>
          <option value="">Project default ({info?.splitDefault ? 'download packages' : 'single package'})</option>
          <option value="false">Single package</option>
          <option value="true">Base install and download packages</option>
        </StudioSelect>
      </StudioField>
      {selected && (selected.split ?? info?.splitDefault) && (['baseSize','chunkSize'] as const).map(field =>
        <PackageLimit key={JSON.stringify([projectPath,target,field,selected[field]])} field={field} original={selected[field] ?? null}
          inherited={String(info?.[field] ?? (field === 'chunkSize' ? '8 MB' : 'unlimited'))} disabled={atlasDisabled}
          save={bytes=>void saveAtlas(()=>invoke('studio.buildSplitLimit',{project:projectPath,target,field,original:selected[field] ?? null,bytes}))}/>)}
      <StudioField label="Audio profile" htmlFor="build-audio">
        <StudioSelect id="build-audio" value={audio} disabled={state.running} onChange={e => { setAudio(e.target.value);queueEstimate(); }}>
          <option value="auto">Platform default</option><option value="desktop">Desktop</option>
          <option value="mobile">Mobile</option><option value="web">Web</option><option value="off">Off</option>
        </StudioSelect>
      </StudioField>
      <StudioCheckbox label="Estimate size after changes" checked={autoEstimate}
        onChange={e=>{setAutoEstimate(e.currentTarget.checked);setPendingEstimate(null);}}/>
      <StudioCheckbox label="Install missing tools first" checked={installMissing} disabled={state.running}
        onChange={e => setInstallMissing(e.target.checked)}/>
      <div className="build__actions">
        <StudioButton variant="primary" disabled={!info?.toolFound || state.running || !!info?.dirty} onClick={() => void start('build')}>
          {state.running && !run.estimate ? <StudioLoader size={17}/> : <Hammer size={14}/>}Build {selected?.name ?? target}</StudioButton>
        <StudioButton disabled={!info?.toolFound || state.running || !!info?.dirty} onClick={() => void start('estimate')}
          title="Pack the content only and report the size (no app build)"><Ruler size={14}/>Estimate size</StudioButton>
        <StudioButton disabled={!selected?.built} onClick={() => reveal({ target })}><FolderOpen size={14}/>Show build</StudioButton>
      </div>
      {info && <p className="build__out" title={info.outDir}>Output: {info.outDir}</p>}
    </aside>
    <section className="build__main" aria-label="Build status">
      {(error||transcriptError) && <StudioAlert tone="danger">{error || transcriptError}{error && <button type="button" className="build__dismiss" aria-label="Dismiss" onClick={() => setError('')}>×</button>}</StudioAlert>}
      {info && !info.toolFound && <StudioAlert tone="danger" title="aiversebuild not found">
        The packager is built with Studio (CMake target aiversebuild). Rebuild Studio, or set AIVERSE_BUILD_TOOL.</StudioAlert>}
      {info?.dirty && <StudioAlert tone="warning" title="Unsaved changes">A build packs the files on disk.
        <StudioButton size="sm" onClick={() => void save()}><Save size={13}/>Save and continue</StudioButton></StudioAlert>}
      <div className={'build__status build__status--' + phase} role="status">
        {phase === 'running' ? <StudioLoader size={19}/> : phase === 'ok' ? <CheckCircle2 size={16}/> :
          phase === 'idle' ? <Hammer size={16}/> : phase === 'missing-toolchain' ? <CircleAlert size={16}/> : <XCircle size={16}/>}
        <b>{phase === 'running' ? (run.estimate ? 'Estimating size' : 'Building ' + (run.target || target)) :
            phase === 'ok' ? (run.estimate ? 'Estimate ready' : 'Build succeeded') :
            phase === 'failed' ? 'Build failed' : phase === 'missing-toolchain' ? 'Missing tools' : 'Ready'}</b>
        {steps.length > 0 && <span className="build__step">{steps[steps.length - 1]}</span>}
        {phase === 'running' && elapsed > 0 && <span className="build__time">{Math.floor(elapsed / 60)}:{String(elapsed % 60).padStart(2, '0')}</span>}
        {phase !== 'running' && phase !== 'idle' && <span className="build__time">exit {state.exitCode}</span>}
      </div>
      {steps.length > 0 && <ol className="build__steps" aria-label="Build stages">
        {steps.slice(-8).map((s, i, all) => <li key={i + s} className={i === all.length - 1 && phase === 'running' ? 'is-current' : ''}>{s}</li>)}
      </ol>}
      {errors.length > 0 && <StudioAlert tone="danger" title="Errors">{errors.map((e, i) => <div key={i}>{e}</div>)}</StudioAlert>}
      {missing && phase === 'missing-toolchain' && <StudioAlert tone="warning" title={'Missing for ' + missing.target + ': ' + missing.ids.join(', ')}>
        <StudioButton size="sm" onClick={() => void start('build', true)}>Install and build</StudioButton></StudioAlert>}
      <div className="build__cards">
        <section className="build__card" aria-label="Toolchain">
          <h3>Toolchain · {selected?.name ?? target}{checking && <StudioLoader size={15}/>}</h3>
          {toolchain && !toolchain.hostSupported && <p className="build__note">{toolchain.hostNote || 'Not buildable on this computer'}</p>}
          {toolchain?.requirements.map(r => <div key={r.id} className={'build__req' + (r.found ? ' is-found' : r.required ? ' is-missing' : '')}>
            {r.found ? <CheckCircle2 size={13}/> : r.required ? <XCircle size={13}/> : <CircleAlert size={13}/>}
            <span>{r.name}</span><small>{r.found ? (r.version || r.path || 'found') : (r.autoInstall ? 'installable' + (r.size ? ' · ' + r.size : '') : r.manual || r.install || 'install manually')}</small>
          </div>)}
          {toolchain?.ok && <p className="build__note">Everything needed is installed.</p>}
        </section>
        <section className="build__card" aria-label="Size estimate">
          <h3>Size · {selected?.name ?? target}</h3>
          {pack ? <>
            <div className="build__size"><b>{formatBytes(pack.packBytes)}</b><span>content pack · {formatBytes(pack.rawBytes)} raw · {pack.entries ?? 0} files</span></div>
            <ul className="build__heavy">{estimate!.heaviest.slice(0, 8).map((f, i) =>
              <li key={i}><span title={f.path}>{f.path ?? f.file}</span><small>{formatBytes(f.storedBytes)}</small></li>)}</ul>
          </> : <p className="build__note">Run <b>Estimate size</b> to pack the content and see what is heavy.</p>}
        </section>
        <details className="build__card build__files">
          <summary>Configured file rules</summary>
          <StudioField label="Rule scope" htmlFor="build-rule-scope">
            <StudioSelect id="build-rule-scope" value={ruleScope} onChange={e=>setRuleScope(e.target.value==='target'?'target':'project')}>
              <option value="project">All platforms</option><option value="target">{selected?.name ?? target} only</option>
            </StudioSelect>
          </StudioField>
          {rules.length ? <table aria-label="Configured file rules"><thead><tr><th>Path or pattern</th><th>Settings</th><th>Action</th></tr></thead>
            <tbody>{rules.map(({rule,original},index)=><tr key={index}><td>{rule.match}</td><td>{[
              rule.exclude && 'Excluded',rule.level==null ? '' : rule.level===0 ? 'Store' : 'zstd '+rule.level,
              rule.maxSize && rule.maxSize+' px',rule.scale && (100*rule.scale)+'%',
            ].filter(Boolean).join(' · ')}</td><td>
              {rule.exclude && !/[*?]/.test(rule.match) && <StudioButton disabled={atlasDisabled}
                onClick={()=>setExcluded(rule.match,original,false)}>Include file</StudioButton>}
              <StudioButton disabled={atlasDisabled} aria-label={'Remove rule for '+rule.match}
              onClick={()=>void saveAtlas(()=>invoke('studio.buildRemoveFileRule',{project:projectPath,target,scope:ruleScope,index,original}))}>Remove</StudioButton></td></tr>)}</tbody>
          </table> : <p>No rules for this scope.</p>}
        </details>
        {estimate?.files && <PackageInventory key={JSON.stringify([projectPath,target])} files={estimate.files}
          rules={rules} scopeLabel={ruleScope==='project' ? 'all platforms' : selected?.name ?? target} disabled={atlasDisabled}
          saveSize={(path,original,size)=>void saveAtlas(()=>invoke('studio.buildFileTextureSize',{project:projectPath,target,scope:ruleScope,path,original,size}))}
          setExcluded={setExcluded} saveLevel={(path,original,level)=>void saveAtlas(()=>invoke('studio.buildFileCompression',{project:projectPath,target,scope:ruleScope,path,original,level}))}/>}
        {estimate?.split && <section className="build__card" aria-label="Downloadable content">
          <h3>Downloadable content</h3>
          <div className="build__size"><b>{formatBytes(estimate.split.baseBytes)}</b><span>installed with the app</span></div>
          <p>{formatBytes(estimate.split.totalBytes)} including all downloads</p>
          {estimate.split.chunks.length > 0 ? <details className="build__files">
            <summary>Download packages ({estimate.split.chunks.length})</summary>
            <table aria-label="Download packages"><thead><tr><th>Package</th><th>Files</th><th>Size</th></tr></thead>
              <tbody>{estimate.split.chunks.map(chunk => <tr key={chunk.file}><td>{chunk.file}</td><td>{chunk.files}</td><td>{formatBytes(chunk.bytes)}</td></tr>)}</tbody>
            </table>
          </details> : <p className="build__note">All content fits in the base package.</p>}
        </section>}
        {!!estimate?.fileRules?.length && <details className="build__card build__files">
          <summary>Applied file rules ({estimate.fileRules.length})</summary>
          <table aria-label="Applied file rules">
            <thead><tr><th>File</th><th>Change</th><th>Before</th><th>After</th></tr></thead>
            <tbody>{estimate.fileRules.map((rule,i) => <tr key={i}>
              <td>{rule.path}</td><td>{rule.what}</td>
              <td>{rule.before != null ? formatBytes(rule.before) : '—'}</td>
              <td>{rule.after != null ? formatBytes(rule.after) : '—'}</td>
            </tr>)}</tbody>
          </table>
        </details>}
        {atlas?.enabled && <section className="build__card" aria-label="Texture atlas report">
          <h3>Texture atlases</h3>
          <p>{atlas.atlased ?? 0} textures · {atlas.pages ?? 0} pages · {atlas.occupancyPercent ?? 0}% filled</p>
          {typeof atlas.bytesSaved === 'number' && <p>Pack {atlas.bytesSaved < 0 ? 'increase' : 'savings'} · {formatBytes(Math.abs(atlas.bytesSaved))}</p>}
          {atlas.gpuBytesBefore != null && atlas.gpuBytesAfter != null &&
            <p>GPU memory · {formatBytes(atlas.gpuBytesBefore)} → {formatBytes(atlas.gpuBytesAfter)}</p>}
          {atlas.textureBindsBefore != null && atlas.textureBindsAfter != null &&
            <p>Texture bindings · {atlas.textureBindsBefore} → {atlas.textureBindsAfter}</p>}
          {!!atlas.excluded?.length && <details><summary>Excluded textures ({atlas.excluded.length})</summary>
            <ul>{atlas.excluded.map(item => <li key={item.path}><strong>{item.path}</strong> · {item.reason}
              {!!item.fixMaxSize && <span> · Suggested maximum {item.fixMaxSize} px</span>}</li>)}</ul>
            {atlas.excluded.some(item => (item.fixMaxSize ?? 0)>0) &&
              <StudioButton disabled={atlasDisabled || estimate?.fixRun == null} onClick={async () => {
                if (estimate?.fixRun == null || !estimate.fixToken) return;
                setSavingAtlas(true);setError('');
                try {
                  commitSnapshot(await invoke('studio.buildAtlasFixSizes',{project:projectPath,target,run:estimate.fixRun,token:estimate.fixToken}));
                  await start('estimate');
                } catch (err) {setError(message(err));}
                finally {setSavingAtlas(false);}
              }}>Apply suggested sizes for {selected?.name ?? target}</StudioButton>}
          </details>}
        </section>}
      </div>
      <div className="build__loghead"><ScrollText size={13}/><span>Log</span>
        <button type="button" disabled={!state.runs} onClick={() => reveal({ log: true })}>Show log file</button></div>
      <pre ref={logBox} className="build__log" aria-label="Build log" tabIndex={0}
        onScroll={e => { const el = e.currentTarget; follow.current = el.scrollHeight - el.scrollTop - el.clientHeight < 24; }}>
        {log || 'No build yet.'}</pre>
    </section>
  </div>;
}
