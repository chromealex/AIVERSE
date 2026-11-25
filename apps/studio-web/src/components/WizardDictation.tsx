import { useEffect, useRef, useState } from 'react';
import { Mic, Square } from 'lucide-react';
import { invoke } from '../bridge';
import type { StudioResult } from '../types';

type State = StudioResult['studio.wizardDictation'];
interface Props {
  field: string; language: string; onText: (text: string) => void; onBusy: (value: boolean) => void;
}

export default function WizardDictation({ field, language, onText, onBusy }: Props) {
  const [state, setState] = useState<State>();
  const [error, setError] = useState('');
  const active = useRef('');
  const stop = useRef(false);
  const timer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);
  const callbacks = useRef({ onText, onBusy });
  callbacks.current = { onText, onBusy };
  useEffect(() => {
    let mounted = true;
    void invoke('studio.wizardDictation', { action: 'available' }).then(value => {
      if (mounted) setState(value);
    }).catch(e => { if (mounted) setError(String(e)); });
    return () => {
      mounted = false;
      clearTimeout(timer.current);
      if (active.current) {
        void invoke('studio.wizardDictation', { action: 'cancel', owner: active.current }).catch(() => {});
        active.current = '';
        callbacks.current.onBusy(false);
      }
    };
  }, []);
  function start() {
    if (active.current) return;
    const owner = Array.from(crypto.getRandomValues(new Uint32Array(4))).join('-');
    active.current = owner; stop.current = false; setError('');
    callbacks.current.onBusy(true);
    setState({ supported: true, phase: 'starting' });
    async function update(first = false) {
      try {
        const action = first ? 'start' : stop.current ? 'stop' : 'poll';
        stop.current = false;
        const next = await invoke('studio.wizardDictation', { action, owner, language });
        if (active.current !== owner) return;
        setState(next);
        if (next.phase === 'done' || next.phase === 'idle') {
          active.current = '';
          callbacks.current.onBusy(false);
          if (next.text) callbacks.current.onText(next.text);
          if (next.error) setError(next.error);
          return;
        }
        timer.current = setTimeout(() => void update(), 250);
      } catch (e) {
        if (active.current !== owner) return;
        void invoke('studio.wizardDictation', { action: 'cancel', owner }).catch(() => {});
        active.current = ''; callbacks.current.onBusy(false);
        setState({ supported: true, phase: 'idle' }); setError(String(e));
      }
    }
    void update(true);
  }
  const busy = state && ['starting', 'listening', 'finishing'].includes(state.phase);
  return <div className="wizard__dictation">
    {state?.supported && <button type="button" className="au-button au-button--sm" disabled={state.phase === 'finishing'}
      aria-label={busy ? 'Stop dictation' : 'Dictate ' + field}
      onClick={() => { if (busy) stop.current = true; else start(); }}>
      {busy ? <Square size={12}/> : <Mic size={14}/>}
      {busy ? 'Stop dictation' : 'Dictate'}
    </button>}
    {state && !state.supported && <small>Use system dictation in this text field.</small>}
    {busy && <span role="status">{state.phase === 'starting' ? 'Starting microphone…' :
      state.phase === 'finishing' ? 'Finishing transcript…' : 'Listening…'}</span>}
    {busy && state.text && <p>{state.text}</p>}
    {error && <p className="hub__error" role="alert">{error}</p>}
  </div>;
}
