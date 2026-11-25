import { useCallback, useEffect, useRef, useState, type KeyboardEvent } from 'react';
import { Bot, CheckCircle2, CircleAlert, MessageSquarePlus, RotateCcw, Send, Square, Wrench, Undo2, Redo2 } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioAgentInfo, StudioAgentChange, StudioAgentBatch, StudioSnapshot } from '../types';
import { StudioIconButton, StudioButton, StudioAlert } from '../ui';
import { copyText } from '../clipboard';
import { agentLineKind } from './agentLogic';
import './AgentPanel.css';
import { StudioLoader } from '../ui';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);

/**
 * In-editor agent: Claude Code driving this Studio through the aiverse MCP tools. Every map edit it makes is a
 * Owner-native History step when supported. Reviewed batches coordinate supported map, project and metadata edits.
 */
export default function AgentPanel({ snapshot, commitSnapshot }: { snapshot: StudioSnapshot | null; commitSnapshot: (s: StudioSnapshot) => void }) {
  const projectPath = snapshot?.project.path ?? '';
  const [info, setInfo] = useState<StudioAgentInfo | null>(null);
  const [lines, setLines] = useState<string[]>([]);
  const [prompt, setPrompt] = useState('');
  const [copied, setCopied] = useState(false);
  const [editingBrief, setEditingBrief] = useState(false);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [navigating,setNavigating]=useState(false);
  const [batchReview,setBatchReview]=useState<StudioAgentBatch|null>(null);
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
  useEffect(() => { total.current = 0; setLines([]);setBatchReview(null); if (projectPath) void poll(); }, [projectPath, poll]);
  useEffect(()=>{if(batchReview&&((batchReview.direction==='undo'?info?.batchUndo?.token:info?.batchRedo?.token)!==batchReview.token||info?.running))setBatchReview(null);},[info,batchReview]);
  useEffect(() => {
    if (!info?.running) return;
    const t = window.setInterval(() => void poll(), 700);
    return () => window.clearInterval(t);
  }, [info?.running, poll]);
  useEffect(() => onNativeEvent(event => { if (event === 'agent.changed' || event === 'document.changed') void poll(); }), [poll]);
  useEffect(() => { if(projectPath)void poll(); },[snapshot,poll,projectPath]);
  useEffect(() => { const el = scroller.current; if (el) el.scrollTop = el.scrollHeight; }, [lines.length]);
  const composer = useRef<HTMLTextAreaElement>(null);
  const promptBytes = new TextEncoder().encode(prompt.trim()).length;
  const loadBrief = async () => {
    setBusy(true);
    setError('');
    try {
      const response = await invoke('studio.agentInfo', { from: total.current, briefForProject: projectPath });
      if (!response.wizardBrief) throw new Error('Wizard brief unavailable');
      setCopied(false);
      setEditingBrief(true);
      setPrompt(response.wizardBrief);
      requestAnimationFrame(() => {
        const field = composer.current;
        if (field) {
          field.focus();
          field.setSelectionRange(0, 0);
          field.scrollTop = 0;
        }
      });
    } catch (e) { setError(message(e)); }
    finally { setBusy(false); }
  };
  const copyBrief = async () => {
    const success = await copyText(prompt);
    setCopied(success);
    setError(success ? '' : 'Select the draft and copy it with the system shortcut.');
  };
  const send = async () => {
    const text = prompt.trim();
    if (!text || busy || !info?.cli || !info.mcp || promptBytes > 20000 || info.running) return;
    setBusy(true);
    try { apply(await invoke('studio.agentStart', { prompt: text, from: total.current })); setPrompt(''); setEditingBrief(false); setError(''); }
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
  const reviewBatch=async(direction:'undo'|'redo')=>{
    if(navigation.current||info?.running)return;
    navigation.current=true;setNavigating(true);setError('');
    try{
      const review=await invoke('studio.agentBatchPreview',{direction});
      if(!review.available||!review.token)throw new Error(review.reason||'History batch unavailable');
      setBatchReview(review);
    }catch(e){setError(message(e));}
    finally{navigation.current=false;setNavigating(false);}
  };
  const applyBatch=async()=>{
    if(navigation.current||!batchReview?.token||info?.running)return;
    navigation.current=true;setNavigating(true);setError('');
    try{
      const result=await invoke('studio.agentBatchApply',{direction:batchReview.direction,token:batchReview.token,confirmed:true});
      setBatchReview(null);commitSnapshot(result);void poll();
    }catch(e){setBatchReview(null);setError(message(e));void poll();}
    finally{navigation.current=false;setNavigating(false);}
  };
  const onKey = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) { e.preventDefault(); void send(); }
  };
  if (!projectPath) return <div className="agent__empty">Open a project to work with the agent.</div>;
  const changes = info?.changes ?? [];
  const edits = changes.filter(c => c.ok);
  const canUndoAll = !!info?.historyVersion && info.startPosition >= 0 && edits.some(c => c.undoable) && !info.running && !navigating;
  return <div className={editingBrief && !lines.length ? "agent agent--draft" : "agent"}>
    <div className="agent__head">
      <Bot size={14}/>
      <span className={'agent__pill' + (info?.cli ? ' is-ok' : ' is-warn')} title={info?.cli || 'claude CLI not found'}>
        {info?.cli ? 'Claude Code' : 'claude not found'}</span>
      {!!info?.port && <span className="agent__meta" title="Local agent link (aiversemcp connects here)">port {info.port}{info.clients ? ` · ${info.clients} linked` : ''}</span>}
      <span className="agent__fill"/>
      <StudioButton size="sm" variant="ghost" disabled={navigating||!info?.batchUndo?.available} title={info?.batchUndo?.reason||'Review all tracked edits'} onClick={()=>void reviewBatch('undo')}><Undo2 size={13}/>Undo batch</StudioButton>
      <StudioButton size="sm" variant="ghost" disabled={navigating||!info?.batchRedo?.available} title={info?.batchRedo?.reason||'Review all tracked edits'} onClick={()=>void reviewBatch('redo')}><Redo2 size={13}/>Redo batch</StudioButton>
      <button type="button" className="au-button au-button--sm au-button--ghost" disabled={!canUndoAll}
        title="Undo the agent-only map suffix; other scopes must be undone individually" onClick={() => void navigate()}>
        <RotateCcw size={13}/>Undo map run</button>
      <button type="button" className="au-button au-button--sm au-button--ghost" disabled={!!info?.running} onClick={() => void reset()} title="Start a new conversation">
        <MessageSquarePlus size={13}/>New</button>
    </div>
    {batchReview&&<div className="agent__batch-review">
      <StudioAlert tone="warning" title={batchReview.direction==='undo'?'Review batch undo':'Review batch redo'}>
        {batchReview.steps?.length??0} tracked steps. The file transaction covers saved documents and supported asset relocations. Overlapping paths or changed references are refused; map changes remain unsaved until you save the map.
      </StudioAlert>
      <div className="agent__batch-scopes">{(['map','project','assets'] as const).map(scope=><span key={scope}>{scope}: {batchReview.steps?.filter(s=>s.scope===scope).length??0}</span>)}</div>
      <div className="agent__batch-actions"><StudioButton size="sm" disabled={navigating} onClick={()=>setBatchReview(null)}>Cancel</StudioButton>
        <StudioButton size="sm" variant="primary" disabled={navigating||!!info?.running} onClick={()=>void applyBatch()}>Confirm {batchReview.direction}</StudioButton></div>
    </div>}
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
    {info?.hasWizardBrief && !prompt && !info.running && <div className="agent__brief">
      <div><strong>Game wizard brief</strong><small>WIZARD_TASK.md</small></div>
      <StudioButton size="sm" disabled={busy} onClick={() => void loadBrief()}>Load draft</StudioButton>
    </div>}
    {promptBytes > 20000 && <div className="agent__error" role="status">Shorten the draft to 20,000 UTF-8 bytes to send ({promptBytes.toLocaleString()} bytes).</div>}
    {editingBrief && <div className="agent__draft-title"><strong>Game wizard brief</strong><small>WIZARD_TASK.md</small></div>}
    <div className="agent__composer">
      <textarea ref={composer} aria-label="Agent request" className="au-input" rows={prompt.includes('\n') ? 5 : 2} placeholder={info?.conversation ? 'Follow up… (⌘↩ to send)' : 'Ask the agent… (⌘↩ to send)'}
        value={prompt} onChange={e => { setPrompt(e.target.value); setCopied(false); if (!e.target.value) setEditingBrief(false); }} onKeyDown={onKey} disabled={busy || !!info?.running}/>
      <div className="agent__composer-actions">
      {editingBrief && <StudioButton size="sm" onClick={() => void copyBrief()}>{copied ? 'Copied' : 'Copy draft'}</StudioButton>}
      {info?.running
        ? <button type="button" className="au-button au-button--sm" onClick={() => void stop()}><Square size={12}/>Stop</button>
        : <button type="button" className="au-button au-button--sm au-button--primary" title={!info?.cli ? 'Claude Code CLI is unavailable' : !info.mcp ? 'Studio MCP executable is unavailable' : undefined}
            disabled={busy || !info?.cli || !info.mcp || !prompt.trim() || promptBytes > 20000} onClick={() => void send()}>
            {busy ? <StudioLoader size={16}/> : <Send size={13}/>}Send</button>}
      </div>
    </div>
  </div>;
}
