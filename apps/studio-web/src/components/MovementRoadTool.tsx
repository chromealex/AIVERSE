import { useEffect, useState } from 'react';
import { Route } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import type { StudioGraphDocumentScope } from '../types';

export default function MovementRoadTool({scope}: {scope:StudioGraphDocumentScope}) {
  const [pick, setPick] = useState<{token:string;scope:StudioGraphDocumentScope} | null>(null);
  const [hasOrigin, setHasOrigin] = useState(false);
  const [error, setError] = useState('');
  useEffect(() => {
    if (!pick) return;
    let current = true;
    const stop = onNativeEvent((event, payload) => {
      const result = payload as {token?:string;hasOrigin?:boolean;error?:string} | null;
      if (result?.token !== pick.token) return;
      if (event === 'roadPick.changed') setHasOrigin(!!result.hasOrigin);
      if (event === 'roadPick.finished') {
        setError(result.error ?? '');
        setPick(null);
      }
    });
    const escape = (event:KeyboardEvent) => { if (event.key === 'Escape') setPick(null); };
    window.addEventListener('keydown', escape);
    void invoke('studio.roadPick',{...pick,armed:true}).catch(e => {
      if (current) { setError(String(e)); setPick(null); }
    });
    return () => {
      current = false;
      stop();
      window.removeEventListener('keydown', escape);
      void invoke('studio.roadPick',{token:pick.token,armed:false}).catch(() => {});
    };
  }, [pick]);
  return <>
    <button type="button" className={'viewport-control'+(pick?' selected':'')} aria-label="Movement roads" aria-pressed={!!pick}
      title="Connect or disconnect objects with movement roads" onClick={() => {
        setError(''); setHasOrigin(false);
        setPick(pick ? null : {token:'road-'+Date.now()+'-'+Math.random().toString(36).slice(2),scope});
      }}><Route size={14}/></button>
    {pick && <span className="road-pick-guide" role="status">{hasOrigin ? 'Pick endpoint · Shift to continue' : 'Pick first object'}</span>}
    {error && <span className="road-pick-guide" role="alert">{error}</span>}
  </>;
}
