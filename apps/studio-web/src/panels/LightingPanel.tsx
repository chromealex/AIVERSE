import { useCallback, useEffect, useMemo, useState } from 'react';
import { CloudSun, Lightbulb, Plus, RefreshCw, Trash2, X } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import PropertyField from '../components/PropertyField';
import { LIGHTING_KEYS, LIGHT_EDITABLE_KEYS, lightingProperties, lightProperties } from './lightingSchema';
import type { LightEditableKey, LightingScalarKey, LightingState } from './lightingSchema';
import './LightingPanel.css';
import RenderLightingControls from './RenderLightingControls';

interface LightingPanelProps { projectPath: string; selectedId: string | null; onSelect: (id: string | null) => Promise<void> }
const empty: LightingState = { lighting: null, lights: [] };

export default function LightingPanel({ projectPath, selectedId, onSelect }: LightingPanelProps) {
  const [state, setState] = useState<LightingState>(empty);
  const [loading, setLoading] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const selected = state.lights.findIndex(light => Boolean(light.entityId) && `light:${light.entityId}` === selectedId);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [reloadIndex, setReloadIndex] = useState(0);
  const available = !nativeConnected() || Boolean(projectPath);
  const reload = useCallback(() => setReloadIndex(n => n + 1), []);
  useEffect(() => onNativeEvent(event => {
    if (event === 'workspace.changed' || event === 'document.changed') reload();
  }), [reload]);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError('');
    if (!available) {
      setState(empty);
      setLoading(false);
      return;
    }
    void invoke('studio.lighting', {})
      .then(next => { if (!cancelled) setState(next); })
      .catch(err => { if (!cancelled) setError(err instanceof Error ? err.message : String(err)); })
      .finally(() => { if (!cancelled) setLoading(false); });
    return () => { cancelled = true; };
  }, [available, projectPath, reloadIndex]);

  const perform = async (action: () => Promise<LightingState>, selectAdded = false) => {
    if (busy || loading || !available) return;
    setBusy(true);
    setError('');
    try {
      const result = await action();
      setState(result);
      if (selectAdded) {
        const added = result.lights.at(-1);
        if (added?.entityId) await onSelect(`light:${added.entityId}`);
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
    } finally { setBusy(false); }
  };
  const settings = useMemo(() => lightingProperties(state), [state]);
  const activeLight = state.lights[selected];
  const lightFields = useMemo(() => activeLight ? lightProperties(activeLight) : [], [activeLight]);
  const editSetting = (key: string, value: string | number | boolean) => {
    if (typeof value !== 'number' || !(LIGHTING_KEYS as readonly string[]).includes(key)) return;
    void perform(() => invoke('studio.lightingSet', { key: key as LightingScalarKey, value }));
  };
  const editLight = (key: string, value: string | number | boolean) => {
    if (!(LIGHT_EDITABLE_KEYS as readonly string[]).includes(key) || !activeLight) return;
    void perform(() => invoke('studio.lightUpdate', { index: selected, entityId: selectedId ?? undefined, key: key as LightEditableKey, value }));
  };
  const deleteLight = () => {
    if (!confirmDelete) { setConfirmDelete(true); return; }
    setConfirmDelete(false);
    void perform(() => invoke('studio.lightDelete', { index: selected, entityId: selectedId ?? undefined }));
  };

  return <section className="lighting-editor">
    <header className="lighting-heading">
      <span><CloudSun size={16}/> Map Lighting</span>
      <button className="icon-button" aria-label="Reload lighting" title="Reload lighting"
        disabled={loading || busy} onClick={reload}><RefreshCw size={16}/></button>
    </header>
    {!nativeConnected() && <div className="lighting-notice">DEMO — no native scene or project lighting</div>}
    {!available && <div className="lighting-notice">Open a project to edit map lighting.</div>}
    {error && <p role="alert" className="lighting-error">{error}</p>}
    {loading ? <div className="lighting-state">Loading map lighting…</div> :
      <div className="lighting-scroll">
        <div className="lighting-section-heading">Environment</div>
        <div className="lighting-fields">
          {settings.map(property => <PropertyField key={property.key} property={property}
            disabled={busy || !available || !nativeConnected()} onChange={value => editSetting(property.key, value)}/>)}
        </div>
        <RenderLightingControls projectPath={projectPath}/>
        <div className="lighting-section-heading lighting-list-heading">
          <span>Local lights ({state.lights.length})</span>
          <button className="lighting-add" disabled={busy || !available || !nativeConnected()}
            title="Add light at map center / FPS origin"
            onClick={() => void perform(() => invoke('studio.lightAdd', {}), true)}>
            <Plus size={14}/> Add
          </button>
        </div>
        {state.lights.length === 0 ? <p className="lighting-state">No authored lights in this map.</p> :
          <>
            <div className="lighting-light-list" role="listbox" aria-label="Map lights">
              {state.lights.map((light, index) =>
                <button key={light.entityId ?? index} type="button" role="option" aria-selected={selected === index}
                  className={'lighting-light-item' + (selected === index ? ' active' : '')}
                  onClick={() => { if (light.entityId) void onSelect(`light:${light.entityId}`); setConfirmDelete(false); }}>
                  <Lightbulb size={15}/><span>{light.name || (light.type === 'spot' ? 'Spot' : 'Point') + ' #' + (index + 1)}</span>
                  <small>{index + 1}</small>
                </button>)}
            </div>
            {activeLight && <div className="lighting-fields lighting-light-fields">
              {lightFields.map(property => <PropertyField key={selected + ':' + property.key}
                property={property} disabled={busy || !available || !nativeConnected()}
                onChange={value => editLight(property.key, value)}/>)}
              <div className="lighting-delete-actions">
                <button disabled={busy || !available || !nativeConnected()}
                  className={'lighting-delete' + (confirmDelete ? ' pending' : '')}
                  onClick={deleteLight}><Trash2 size={14}/>{confirmDelete ? 'Confirm delete' : 'Delete light'}</button>
                {confirmDelete && <button className="lighting-cancel" onClick={() => setConfirmDelete(false)}>
                  <X size={14}/> Cancel</button>}
              </div>
            </div>}
          </>}
      </div>}
    <div className="panel-footer">Authoring data · use Save in the main toolbar to persist</div>
  </section>;
}
