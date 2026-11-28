import type { StudioScriptMessage } from '../types';
import { useEffect, useMemo, useRef, useState } from 'react';
import { CircleX, Copy, Info, Search, Trash2, TriangleAlert } from 'lucide-react';
import { invoke } from '../bridge';
import { copyText, selectionIn } from '../clipboard';
import type { StudioLog } from '../types';
import { filterLogs, levelCounts, logParts, logSource, logSources, type ConsoleLevel } from './consoleLogic';
import BuildOutput from './ConsoleBuildOutput';
import './ConsolePanel.css';

/** Console: editor/agent activity with level and source filters; entity ids and files are links. */
export default function ConsolePanel({ logs, select, project, scriptErrors, openScript }: {
  openScript:(path:string,line:number)=>void; scriptErrors?: StudioScriptMessage[]; project: string; logs: StudioLog[]; select: (id: string | null) => Promise<void>;
}) {
  const [mode,setMode] = useState('Activity');
  const [level, setLevel] = useState<ConsoleLevel>('all');
  const [source, setSource] = useState('');
  const [query, setQuery] = useState('');
  const [cleared, setCleared] = useState<StudioLog | null>(null);
  const [copied, setCopied] = useState('');
  const list = useRef<HTMLDivElement>(null);
  const follow = useRef(true);
  // The status bar's problem counter opens the Console on errors.
  useEffect(() => {
    const open = (e: Event) => { const l = (e as CustomEvent<ConsoleLevel>).detail; if (l) { setMode('Activity'); setLevel(l); } };
    window.addEventListener('studio:console-filter', open);
    return () => window.removeEventListener('studio:console-filter', open);
  }, []);
  const visible = useMemo(() => {
    const start = cleared ? logs.lastIndexOf(cleared) + 1 : 0;
    const activity = logs.slice(Math.max(0, start));
    for (const { tick, text, source } of scriptErrors ?? []) {
      activity.push({level:'error', source:source==='view'?'Lua view':'Lua', time:String(tick), text:`Tick ${tick} · ${text}`});
    }
    return activity;
  }, [logs, cleared, scriptErrors]);
  const counts = levelCounts(visible);
  const shown = filterLogs(visible, { level, source, query });
  useEffect(() => { if (follow.current && list.current) list.current.scrollTop = list.current.scrollHeight; }, [shown.length]);
  const openFile = (path: string) => {
    window.dispatchEvent(new CustomEvent('studio:show-panel', { detail: 'assets' }));
    window.setTimeout(() => window.dispatchEvent(new CustomEvent('studio:reveal-asset', { detail: { path } })), 0);
  };
  const lineText = (l: StudioLog) => `[${logSource(l)}] ${l.text}`;
  const copy = (text: string, what: string) => void copyText(text).then(ok => {
    setCopied(ok ? what : 'Copy failed');
    window.setTimeout(() => setCopied(''), 1500);
  });
  // Cmd/Ctrl+C copies the selected console text (also where the host has no Edit menu), Cmd/Ctrl+A selects all lines.
  const onKeyDown = (e: React.KeyboardEvent<HTMLDivElement>) => {
    if (!(e.metaKey || e.ctrlKey) || e.altKey) return;
    const key = e.key.toLowerCase();
    if (key === 'c') {
      const text = selectionIn(list.current);
      if (text) { e.preventDefault(); copy(text, 'Selection copied'); }
    } else if (key === 'a' && list.current) {
      e.preventDefault();
      window.getSelection()?.selectAllChildren(list.current);
    }
  };
  const openEntity = (id: string) => void select(id).then(() => invoke('studio.viewportInput', { kind: 'focus' })).catch(console.warn);
  const levelButton = (id: ConsoleLevel, label: string, icon: React.ReactNode, count?: number) =>
    <button type="button" className={'console__level' + (level === id ? ' is-on' : '')} aria-label={label || id} aria-pressed={level === id}
      onClick={() => setLevel(level === id && id !== 'all' ? 'all' : id)}>{icon}{label}{count !== undefined && <b>{count}</b>}</button>;
  return <div className="console">
    <nav className="console__views" aria-label="Console views">{['Activity','Build output'].map(name => <button type="button" key={name} aria-pressed={mode === name} onClick={() => setMode(name)}>{name}</button>)}</nav>
    {mode === 'Build output' ? <BuildOutput project={project}/> : <>
    <div className="console__bar" role="toolbar" aria-label="Console filters">
      {levelButton('all', 'All', null)}
      {levelButton('info', '', <Info size={12}/>, counts.info)}
      {levelButton('warning', '', <TriangleAlert size={12}/>, counts.warning)}
      {levelButton('error', '', <CircleX size={12}/>, counts.error)}
      <select aria-label="Source" value={source} onChange={e => setSource(e.target.value)}>
        <option value="">All sources</option>
        {logSources(visible).map(s => <option key={s} value={s}>{s}</option>)}
      </select>
      <label className="console__search"><Search size={12} aria-hidden="true"/>
        <input value={query} onChange={e => setQuery(e.target.value)} placeholder="Filter" aria-label="Filter log"/></label>
      <span className="console__fill"/>
      {copied && <span className="console__copied" role="status">{copied}</span>}
      <button type="button" className="console__icon" title="Copy the selection, or every shown line"
        onClick={() => { const sel = selectionIn(list.current); copy(sel || shown.map(lineText).join('\n'), sel ? 'Selection copied' : `${shown.length} lines copied`); }}><Copy size={13}/></button>
      <button type="button" className="console__icon" title="Clear" onClick={() => setCleared(logs[logs.length - 1] ?? null)}><Trash2 size={13}/></button>
    </div>
    <div ref={list} className="console__list log-list au-selectable" role="log" aria-live="polite" tabIndex={0} onKeyDown={onKeyDown}
      onScroll={e => { const el = e.currentTarget; follow.current = el.scrollHeight - el.scrollTop - el.clientHeight < 20; }}>
      {shown.map((line, i) => <div className={'log-row log-' + line.level} key={i}>
        <span>{logSource(line)}</span>
        <p>{logParts(line.text).map((part, j) => part.kind === 'text' ? <span key={j} className="console__text">{part.text}</span>
          : <button key={j} type="button" className="console__link" title={part.kind === 'entity' ? 'Select and frame' : part.kind === 'script' ? 'Open in Scripts' : 'Show in Assets'}
              onClick={() => part.kind === 'entity' ? openEntity(part.id) : part.kind === 'script' ? openScript(part.path,part.line) : openFile(part.path)}>{part.text}</button>)}</p>
        <button type="button" className="console__rowcopy" title="Copy this line" aria-label="Copy this line"
          onClick={() => copy(lineText(line), 'Line copied')}><Copy size={11}/></button>
      </div>)}
      {!shown.length && <div className="console__empty">{visible.length ? 'Nothing matches the filter.' : 'No messages.'}</div>}
    </div>
    </>}
  </div>;
}
