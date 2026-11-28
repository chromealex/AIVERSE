/** Scene/Live view preference for panels in this window. The game session owns the selected entity. */
import { useEffect, useState } from 'react';

export interface LiveRef { epoch: string; ref: string; refs?: string[]; names?: Record<string,string> }
let showLive = true;
const listeners = new Set<() => void>();
const notify = () => listeners.forEach(l => l());
export function setShowLive(on: boolean) { if (showLive !== on) { showLive = on; notify(); } }
export function useLivePlay(): { showLive: boolean } {
  const [, tick] = useState(0);
  useEffect(() => { const l = () => tick(x => x + 1); listeners.add(l); return () => { listeners.delete(l); }; }, []);
  return { showLive };
}
/** Start each Play session on the live view. */
export function resetLive() { showLive = true; notify(); }
