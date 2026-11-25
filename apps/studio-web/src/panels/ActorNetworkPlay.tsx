import { useState } from 'react';
import { invoke } from '../bridge';
import { StudioButton, StudioField, StudioInput, StudioSection } from '../ui';

export function ActorNetworkPlay({project}:{project:string}) {
  const [address,setAddress]=useState('udp://127.0.0.1:27100');
  const [busy,setBusy]=useState(false);
  const [message,setMessage]=useState('');
  const [error,setError]=useState('');
  async function launch(mode:'listen'|'connect') {
    setBusy(true);setMessage('');setError('');
    try {
      await invoke('studio.networkPlay',{project,mode,address:mode==='connect'?address.trim():undefined});
      setMessage(mode==='listen'?'Host window launched.':'Client window launched.');
    } catch(e) { setError(e instanceof Error?e.message:String(e)); }
    finally { setBusy(false); }
  }
  return <div className="actor-simulation">
    <StudioSection title="Host a session">
      <p>Open the saved map in a game window. Players can join on UDP port 27100 or WebSocket port 27101.</p>
      <StudioButton disabled={busy} onClick={()=>void launch('listen')}>Start host</StudioButton>
    </StudioSection>
    <StudioSection title="Join a session">
      <form onSubmit={e=>{e.preventDefault();void launch('connect');}}>
        <StudioField label="Server address" htmlFor="actor-server"><StudioInput id="actor-server" value={address} onChange={e=>setAddress(e.target.value)} spellCheck={false}/></StudioField>
        <StudioButton type="submit" disabled={busy||!address.trim()}>Connect</StudioButton>
      </form>
    </StudioSection>
    {message&&<p role="status">{message}</p>}
    {error&&<p role="alert">{error}</p>}
  </div>;
}
