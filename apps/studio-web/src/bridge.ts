import { mockInvoke } from './mock';
import type { StudioMethod, StudioParams, StudioResult } from './types';

interface NativeRequest { id: number; method: string; params: unknown }
interface NativeReply { id?: number; result?: unknown; error?: string; event?: string; payload?: unknown }
type NativeHandler = { postMessage: (message: NativeRequest) => void };
declare global {
  interface Window {
    webkit?: { messageHandlers?: { aiverse?: NativeHandler } };
    __aiverseReceive?: (message: NativeReply) => void;
  }
}

const pending = new Map<number, {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  timer: ReturnType<typeof setTimeout> | null;
}>();
const listeners = new Set<(event: string, payload: unknown) => void>();
// Floating panel windows share the native bridge, and asynchronous replies
// reach every window, so each window draws request ids from its own range.
function firstRequestId(): number {
  try {
    const panel = new URLSearchParams(window.location?.search ?? '').get('panel');
    if (!panel) return 1;
    let hash = 0;
    for (const c of panel) hash = (hash * 31 + c.charCodeAt(0)) % 9000;
    return (hash + 1) * 1_000_000 + 1;
  } catch { return 1; }
}
let nextId = firstRequestId();

window.__aiverseReceive = (message: NativeReply) => {
  if (message && typeof message.event === 'string') {
    if (STICKY.has(message.event)) sticky.set(message.event, { payload: message.payload, at: Date.now() });
    listeners.forEach(listener => listener(message.event!, message.payload));
    return;
  }
  if (!message || typeof message.id !== 'number') return;
  const call = pending.get(message.id);
  if (!call) return;
  if (call.timer !== null) clearTimeout(call.timer);
  pending.delete(message.id);
  if (typeof message.error === 'string') call.reject(new Error(message.error));
  else call.resolve(message.result);
};

export const nativeConnected = (): boolean => !!window.webkit?.messageHandlers?.aiverse;
/** Native window chrome actions (no reply): drag the window from the title bar, zoom on double-click. */
export function nativeWindowAction(action: 'studio.windowDrag' | 'studio.windowZoom' | 'studio.windowMinimize' | 'studio.windowClose'): void {
  const native = window.webkit?.messageHandlers?.aiverse;
  try { native?.postMessage({ id: 0, method: action, params: {} }); }
  catch { /* not in the native host */ }
}
/** Start an edge resize. The button must still be down; the host runs the system size loop. */
export function nativeWindowResize(edge: 'left' | 'right' | 'top' | 'bottom' | 'top-left' | 'top-right' | 'bottom-left' | 'bottom-right'): void {
  const native = window.webkit?.messageHandlers?.aiverse;
  try { native?.postMessage({ id: 0, method: 'studio.windowResize', params: { edge } }); }
  catch { /* not in the native host */ }
}
/** Events the host may send while React is still mounting (the startup project opens on didFinishNavigation):
 *  a listener added within a few seconds still gets the latest one. */
const STICKY = new Set(['project.splash', 'project.loading']);
const sticky = new Map<string, { payload: unknown; at: number }>();
export function onNativeEvent(listener: (event: string, payload: unknown) => void): () => void {
  listeners.add(listener);
  const now = Date.now();
  for (const [event, last] of sticky) if (now - last.at < 4000) queueMicrotask(() => { if (listeners.has(listener)) listener(event, last.payload); });
  return () => listeners.delete(listener);
}
export async function invoke<M extends StudioMethod>(method: M, params: StudioParams[M]): Promise<StudioResult[M]> {
  const native = window.webkit?.messageHandlers?.aiverse;
  if (!native) return mockInvoke(method, params);
  return new Promise<StudioResult[M]>((resolve, reject) => {
    const id = nextId++;
    // Interactive native sheets wait for a person, including dirty-document prompts.
    const interactive = ['studio.openProject','studio.openMap','studio.newMap','studio.saveAs','studio.save','studio.assetImportPreview'].includes(method);
    // No RPC timeout: the host answers every request (with an error if it fails), and long operations
    // (generation, builds, a busy main thread while a project streams in) are legitimate. A very slow reply
    // only leaves a note in the host log; the call keeps waiting and resolves when the answer comes.
    const timer = interactive ? null : setTimeout(() => {
      try { native.postMessage({ id: 0, method: 'studio.webDiagnostic', params: { message: `slow native reply (>30 s): ${method}` } }); }
      catch { /* diagnostics only */ }
    }, 30000);
    pending.set(id, { resolve: resolve as (value: unknown) => void, reject, timer });
    try { native.postMessage({ id, method, params }); }
    catch (error) { if (timer !== null) clearTimeout(timer); pending.delete(id); reject(error); }
  });
}
