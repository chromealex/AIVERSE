import { useCallback, useEffect, useMemo, useState } from 'react';
import { CloudSun, Crosshair, Lightbulb, Plus, RefreshCw, Trash2, X } from 'lucide-react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import PropertyField from '../components/PropertyField';
import { LIGHTING_KEYS, LIGHT_EDITABLE_KEYS, lightingProperties, lightColor, lightProperties } from './lightingSchema';
import type { LightEditableKey, LightingScalarKey, LightingState } from './lightingSchema';
import {hexToRgb,rgbToHex} from './humanCreatorSchema';
import './LightingPanel.css';
import LightingWeatherPreview from './LightingWeatherPreview';
import RenderLightingControls from './RenderLightingControls';

interface LightingPanelProps { dirty: boolean; saveMap: () => Promise<void>; openScene: () => void; projectPath: string; documentEpoch?: string; selectedId: string | null; onSelect: (id: string | null) => Promise<void> }
const empty: LightingState = { lighting: null, lights: [] };

export default function LightingPanel({ dirty, saveMap, openScene, projectPath, documentEpoch, selectedId, onSelect }: LightingPanelProps) {
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
    // Keep the scroll position and focused field on same-document refreshes.
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
    void perform(() => invoke('studio.lightingSet', { key: key as LightingScalarKey, value, expectedDocumentEpoch:documentEpoch }));
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
        <div className="lighting-section-heading lighting-list-heading">
          <span>Environment</span>
          {state.lighting && <button className="lighting-add" disabled={busy || !available || !nativeConnected()}
            title="Remove all map lighting overrides" onClick={()=>void perform(()=>invoke('studio.lightingSet',{key:'remove',value:true,expectedDocumentEpoch:documentEpoch}))}>
            Use project lighting
          </button>}
        </div>
        <div className="lighting-fields">
          <label className="property-row"><span>Palette preset</span>
            <select aria-label="Palette preset" value={state.lighting?.preset ?? ''} disabled={busy || !available || !nativeConnected()}
              onChange={event=>{const value=event.target.value;void perform(()=>invoke('studio.lightingSet',{key:'preset',value,expectedDocumentEpoch:documentEpoch}));}}>
              <option value="">Project palette</option>
              {['Desert','Snow','Volcanic','Overcast','Cave'].map(label=><option key={label} value={label.toLowerCase()}>{label}</option>)}
            </select>
          </label>
          <p className="property-description">Empty fields use the palette or project settings.</p>
          {settings.map(property => <PropertyField key={property.key} property={state.lighting?.[property.key as LightingScalarKey] === undefined ? {...property,mixed:true} : property}
            disabled={busy || !available || !nativeConnected()} onChange={value => editSetting(property.key, value)}/>)}
        </div>
        {nativeConnected() && documentEpoch && <LightingWeatherPreview epoch={documentEpoch} lighting={state.lighting}
          busy={busy} apply={value=>perform(()=>invoke('studio.lightingSet',{key:'weather',value,expectedDocumentEpoch:documentEpoch}))}/>}
        <RenderLightingControls projectPath={projectPath}/>
        <div className="lighting-section-heading lighting-list-heading">
          <span>Local lights ({state.lights.length})</span>
        </div>
        <div className="lighting-placement">
          <button className="lighting-add" disabled={busy || !available || !nativeConnected()}
            title="Place a light at the Scene camera target, snapped to half a unit"
            onClick={() => void perform(() => invoke('studio.lightAdd', {atSceneTarget:true, expectedDocumentEpoch:documentEpoch}), true)}>
            <Crosshair size={14}/> Add at Scene target
          </button>
          <button className="lighting-add" disabled={busy || !available || !nativeConnected()}
            title="Add light at map center / FPS origin"
            onClick={() => void perform(() => invoke('studio.lightAdd', {expectedDocumentEpoch:documentEpoch}), true)}>
            <Plus size={14}/> Add at center
          </button>
        </div>
        {state.lights.length === 0 ? <p className="lighting-state">No authored lights in this map.</p> :
          <>
            <div className="lighting-light-list" role="listbox" aria-label="Map lights">
              {state.lights.map((light, index) =>
                <button key={light.entityId ?? index} type="button" role="option" aria-selected={selected === index}
                  className={'lighting-light-item' + (selected === index ? ' active' : '')}
                  onClick={() => { if (light.entityId) void onSelect(`light:${light.entityId}`); setConfirmDelete(false); }}>
                  <Lightbulb size={15} color={rgbToHex(lightColor(light))}/><span>{light.name || (light.type === 'spot' ? 'Spot' : 'Point') + ' #' + (index + 1)}</span>
                  <small>{index + 1}</small>
                </button>)}
            </div>
            {activeLight && <div className="lighting-fields lighting-light-fields">
              <button className="lighting-add" disabled={busy || !selectedId || !documentEpoch || !nativeConnected()}
                onClick={async () => {
                  if(!selectedId || !documentEpoch)return;
                  try {
                    await invoke('studio.viewportInput',{kind:'focusLight',entityId:selectedId,expectedDocumentEpoch:documentEpoch});
                    openScene();
                  } catch(err) { setError(err instanceof Error ? err.message : String(err)); }
                }}><Crosshair size={14}/> Show in Scene</button>
              <label className="property-row"><span>Light color</span>
                <span className="lighting-color">
                  <input type="color" aria-label="Light color" value={rgbToHex(lightColor(activeLight))}
                    disabled={busy || !available || !nativeConnected()} onChange={event=>{
                      const value=hexToRgb(event.target.value);
                      if(value) void perform(()=>invoke('studio.lightUpdate',{index:selected,entityId:selectedId ?? undefined,key:'color',value}));
                    }}/>
                  <code>{rgbToHex(lightColor(activeLight)).toUpperCase()}</code>
                </span>
              </label>
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
    <div className="panel-footer">
      <button className="au-button au-button--sm" disabled={!dirty||busy||loading||!available||!nativeConnected()} onClick={async()=>{
        setBusy(true);setError('');
        try{await saveMap();}catch(e){setError(e instanceof Error?e.message:String(e));}
        finally{setBusy(false);}
      }}>Save map</button>
    </div>
  </section>;
}
