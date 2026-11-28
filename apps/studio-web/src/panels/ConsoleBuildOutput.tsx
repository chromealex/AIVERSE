import { useEffect, useMemo, useRef, useState } from 'react';
import { Copy, FolderOpen, Search } from 'lucide-react';
import { invoke } from '../bridge';
import { copyText, selectionIn } from '../clipboard';
import { useBuildTranscript } from './useBuildTranscript';
import { buildLineLevel } from './consoleLogic';

const pageSize = 120;

export default function BuildOutput({ project }: { project: string }) {
  const { state, error } = useBuildTranscript(project);
  const [filter, setFilter] = useState('All');
  const [query, setQuery] = useState('');
  const [page, setPage] = useState<number | null>(null);
  const [notice, setNotice] = useState('');
  const list = useRef<HTMLDivElement>(null);
  const follow = useRef(true);
  const lines = useMemo(() => {
    const values = state.text.split(/\r?\n/);
    if (!values.at(-1)) values.pop();
    return values.map((text, index) => ({ text, number: index + 1, level: buildLineLevel(text) }));
  }, [state.text]);
  const counts = useMemo(() => {
    const totals = { All: lines.length, Errors: 0, Warnings: 0 };
    for (const { level } of lines) {
      if (level === 'Errors' || level === 'Warnings') totals[level]++;
    }
    return totals;
  }, [lines]);
  const shown = useMemo(() => lines.filter(line => (filter === 'All' || line.level === filter)
    && line.text.toLowerCase().includes(query.toLowerCase())), [lines, filter, query]);
  const last = Math.max(0, Math.ceil(shown.length / pageSize) - 1);
  const current = Math.min(page ?? last, last);
  useEffect(() => {
    if (page === null && follow.current && list.current) list.current.scrollTop = list.current.scrollHeight;
  }, [shown.length, page]);
  useEffect(() => { setPage(null); follow.current = true; setNotice(''); }, [state.run]);
  const status = state.running ? 'Running' : !state.runs ? 'Ready' : state.exitCode === 0 ? 'Completed' : 'Failed';
  const copy = async (text: string) => setNotice(await copyText(text) ? 'Copied' : 'Copy failed');
  return <>
    <div className="console__bar transcript-tools">
      <label className="console__search"><Search size={14}/><input aria-label="Search build output" placeholder="Search output" value={query}
        onChange={e => { setQuery(e.target.value); setPage(0); }}/></label>
      {(['All', 'Errors', 'Warnings'] as const).map(level => <button type="button" key={level} className="console__level" aria-pressed={filter === level}
        onClick={() => { setFilter(level); setPage(level === 'All' ? null : 0); }}>{level} ({counts[level]})</button>)}
      <span className="console__fill"/>
      <button className="console__level" disabled={!state.text || state.next < state.size} onClick={() => void copy(state.text)}><Copy size={13}/>Copy full log</button>
      <button className="console__level" disabled={!project} onClick={() => void invoke('studio.buildReveal', { logFolder: true }).catch(e => setNotice(String(e)))}><FolderOpen size={14}/>Open log folder</button>
    </div>
    <div className="transcript-status"><strong>{state.label || 'Build output'}</strong><span data-phase={status}>{status}</span>
      <small>{shown.length} lines{state.runs > 0 && !state.running && ' · Exit '+state.exitCode}</small></div>
    {(error || notice) && <div className="console__empty" role="status">{error || notice}</div>}
    <div ref={list} className="console__list transcript-lines" tabIndex={0} aria-label="Build transcript"
      onKeyDown={e => { if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'c') {
        const selected = selectionIn(list.current); if (selected) { e.preventDefault(); void copy(selected); }
      } }}
      onScroll={e => { const el=e.currentTarget; follow.current=el.scrollHeight-el.scrollTop-el.clientHeight<24; }}>
      {shown.slice(current*pageSize,(current+1)*pageSize).map(line => <div className="transcript-line" key={line.number} data-level={line.level}><span>{line.number}</span><code>{line.text}</code></div>)}
      {!shown.length && <div className="console__empty">{lines.length ? 'No matching lines.' : 'Run a build or size estimate to see its output here.'}</div>}
    </div>
    {last > 0 && <div className="transcript-status"><button disabled={!current} onClick={() => setPage(current-1)}>Previous</button>
      <small>Page {current+1} of {last+1}</small><button disabled={current===last} onClick={() => setPage(current+1)}>Next</button>
      <button onClick={() => { setPage(null); follow.current=true; if(list.current)list.current.scrollTop=list.current.scrollHeight; }}>Follow latest</button></div>}
  </>;
}
