import { useCallback, useEffect, useRef, useState, type KeyboardEvent } from 'react';
import { Bot, CheckCircle2, CircleAlert, MessageSquarePlus, RotateCcw, Send, Square, Wrench, Undo2, Redo2 } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioAgentInfo, StudioAgentChange, StudioSnapshot } from '../types';
import { StudioIconButton } from '../ui';
import { agentLineKind } from './agentLogic';
import './AgentPanel.css';
import { StudioLoader } from '../ui';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);

/**
 * In-editor agent: Claude Code driving this Studio through the aiverse MCP tools. Every map edit it makes is a
 * Owner-native History step when supported. Each action is linked to its own scope; bulk undo is map-only.
 */
export default function AgentPanel({ snapshot, commitSnapshot }: { snapshot: StudioSnapshot | null; commitSnapshot: (s: StudioSnapshot) => void }) {
  const projectPath = snapshot?.project.path ?? '';
  const [info, setInfo] = useState<StudioAgentInfo | null>(null);
  const [lines, setLines] = useState<string[]>([]);
  const [prompt, setPrompt] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [navigating,setNavigating]=useState(false);
  const navigation=useRef(false);
  const total = useRef(0);
  const scroller = useRef<HTMLDivElement>(null);
  const apply = useCallback((r: StudioAgentInfo, reset = false) => {
    setInfo(r);
    if (reset || r.total < total.current) { setLines(r.lines); }
    else if (r.lines.length) setLines(prev => [...prev, ...r.lines]);
    total.current = r.total;
  }, []);
  const poll = useCallback(async () => {
    try { apply(await invoke('studio.agentInfo', { from: total.current })); }
    catch (e) { setError(message(e)); }
  }, [apply]);
  useEffect(() => { total.current = 0; setLines([]); if (projectPath) void poll(); }, [projectPath, poll]);
  useEffect(() => {
    if (!info?.running) return;
    const t = window.setInterval(() => void poll(), 700);
    return () => window.clearInterval(t);
  }, [info?.running, poll]);
  useEffect(() => onNativeEvent(event => { if (event === 'agent.changed' || event === 'document.changed') void poll(); }), [poll]);
  useEffect(() => { if(projectPath)void poll(); },[snapshot,poll,projectPath]);
  useEffect(() => { const el = scroller.current; if (el) el.scrollTop = el.scrollHeight; }, [lines.length]);
  const send = async () => {
    const text = prompt.trim();
    if (!text || info?.running) return;
    setBusy(true);
    try { apply(await invoke('studio.agentStart', { prompt: text, from: total.current })); setPrompt(''); setError(''); }
    catch (e) { setError(message(e)); }
    finally { setBusy(false); }
  };
  const stop = async () => { try { apply(await invoke('studio.agentStop', { from: total.current })); } catch (e) { setError(message(e)); } };
  const reset = async () => {
    try { total.current = 0; apply(await invoke('studio.agentReset', { from: 0 }), true); } catch (e) { setError(message(e)); }
  };
  const navigate = async (change?:StudioAgentChange,direction?:'undo'|'redo') => {
    if(!info||info.running||navigation.current)return;
    if(change&&(!change.historyId||!change.historyVersion||
      (change.scope!=='map'&&change.scope!=='project'&&change.scope!=='assets')||
      (direction==='undo'?!change.canUndo:!change.canRedo)))return;
    if(!change&&(!info.historyVersion||info.startPosition<0))return;
    navigation.current=true;setNavigating(true);setError('');
    try{
      const result=change
        ? await invoke('studio.agentHistoryStep',{scope:change.scope as 'map'|'project'|'assets',
            historyId:change.historyId!,expectedVersion:change.historyVersion!,direction:direction!})
        : await invoke('studio.agentUndoAll',{expectedVersion:info.historyVersion!});
      commitSnapshot(result);void poll();
    }catch(e){setError(message(e));void poll();}
    finally{navigation.current=false;setNavigating(false);}
  };
  const onKey = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) { e.preventDefault(); void send(); }
  };
  if (!projectPath) return <div className="agent__empty">Open a project to work with the agent.</div>;
  const changes = info?.changes ?? [];
  const edits = changes.filter(c => c.ok);
  const canUndoAll = !!info?.historyVersion && info.startPosition >= 0 && edits.some(c => c.undoable) && !info.running && !navigating;
  return <div className="agent">
    <div className="agent__head">
      <Bot size={14}/>
      <span className={'agent__pill' + (info?.cli ? ' is-ok' : ' is-warn')} title={info?.cli || 'claude CLI not found'}>
        {info?.cli ? 'Claude Code' : 'claude not found'}</span>
      {!!info?.port && <span className="agent__meta" title="Local agent link (aiversemcp connects here)">port {info.port}{info.clients ? ` · ${info.clients} linked` : ''}</span>}
      <span className="agent__fill"/>
      <button type="button" className="au-button au-button--sm au-button--ghost" disabled={!canUndoAll}
        title="Undo the agent-only map suffix; other scopes must be undone individually" onClick={() => void navigate()}>
        <RotateCcw size={13}/>Undo map run</button>
      <button type="button" className="au-button au-button--sm au-button--ghost" disabled={!!info?.running} onClick={() => void reset()} title="Start a new conversation">
        <MessageSquarePlus size={13}/>New</button>
    </div>
    {error && <div className="agent__error" role="alert">{error}</div>}
    {lines.some(l => l.includes('Not logged in')) && !info?.running && <div className="agent__error" role="alert">
      Claude Code is not signed in on this Mac: open Terminal, run <code>claude</code>, type <code>/login</code>, then send again.</div>}
    <div className="agent__body">
      <div className="agent__log" ref={scroller} aria-live="polite">
        {!lines.length && <div className="agent__hint">
          Ask for a change to the open game — “put a gold mine near each start”, “make the river wider”, “add a fog of war”.
          Tracked edits link to Map, Project or Assets history. Undo and Redo are enabled only at the current boundary of that scope.</div>}
        {lines.map((line, i) => {
          const kind = agentLineKind(line);
          return <div key={i} className={'agent__line agent__line--' + kind}>
            {kind === 'tool' && <Wrench size={11}/>}
            {kind === 'done' && <CheckCircle2 size={11}/>}
            {kind === 'error' && <CircleAlert size={11}/>}
            <span>{kind === 'user' ? line.replace(/^› /, '') : kind === 'tool' ? line.replace(/^→ /, '') : line}</span>
          </div>;
        })}
        {info?.running && <div className="agent__line agent__line--status"><StudioLoader size={14}/><span>Working…</span></div>}
      </div>
      {changes.length > 0 && <aside className="agent__changes">
        <b>Changes · {edits.length}</b>
        {changes.map((c,i)=><div key={i} className={'agent__change-row'+(c.ok?'':' is-error')} title={c.error||c.args}>
          <div className="agent__change">
            {c.ok?<CheckCircle2 size={11}/>:<CircleAlert size={11}/>}<span title={c.op}>{c.op}</span>
          </div>
          <div className="agent__change-meta">
            <small>{c.scope==='map'?'Map':c.scope==='project'?'Project':c.scope==='assets'?'Assets':'Untracked'}
              {c.undoable?' · '+(c.applied?'Applied':'Undone'):c.ok?' · Not retained':' · Failed'}</small>
            {c.undoable&&<div className="agent__change-actions">
              <StudioIconButton size="sm" variant="ghost" aria-label={'Undo '+c.op} icon={<Undo2 size={13}/>}
                disabled={navigating||!c.canUndo||!!info?.running} onClick={()=>void navigate(c,'undo')}/>
              <StudioIconButton size="sm" variant="ghost" aria-label={'Redo '+c.op} icon={<Redo2 size={13}/>}
                disabled={navigating||!c.canRedo||!!info?.running} onClick={()=>void navigate(c,'redo')}/>
            </div>}
          </div>
        </div>)}
      </aside>}
    </div>
    <div className="agent__composer">
      <textarea className="au-input" rows={2} placeholder={info?.conversation ? 'Follow up… (⌘↩ to send)' : 'Ask the agent… (⌘↩ to send)'}
        value={prompt} onChange={e => setPrompt(e.target.value)} onKeyDown={onKey} disabled={!!info?.running}/>
      {info?.running
        ? <button type="button" className="au-button au-button--sm" onClick={() => void stop()}><Square size={12}/>Stop</button>
        : <button type="button" className="au-button au-button--sm au-button--primary" disabled={busy || !prompt.trim()} onClick={() => void send()}>
            {busy ? <StudioLoader size={16}/> : <Send size={13}/>}Send</button>}
    </div>
  </div>;
}
