import { useEffect, useState } from 'react';
import { invoke, nativeConnected, onNativeEvent } from '../bridge';
import { StudioSection, StudioField, StudioSelect, StudioInput, StudioAlert } from '../ui';
import type { RenderLightingSettings } from '../types';

export default function RenderLightingControls({ projectPath }: { projectPath: string }) {
  const [settings, setSettings] = useState<RenderLightingSettings | null>(null);
  const [error, setError] = useState('');
  const [status, setStatus] = useState('Waiting for native viewport…');
  const [busy, setBusy] = useState(false);
  const [revision, setRevision] = useState(0);
  useEffect(() => onNativeEvent((event, payload) => {
    if (event === 'lighting.status' && payload && typeof payload === 'object' && 'message' in payload)
      setStatus(String(payload.message));
    if (event === 'document.changed' || event === 'workspace.changed') setRevision(n => n + 1);
  }), []);
  useEffect(() => {
    let cancelled = false;
    setSettings(null);
    if (!projectPath || !nativeConnected()) return;
    void invoke('studio.renderLighting', {}).then(value => {
      if (!cancelled) { setSettings(value); setError(''); }
    }).catch(reason => { if (!cancelled) setError(String(reason)); });
    return () => { cancelled = true; };
  }, [projectPath, revision]);
  const update = async (patch: Partial<RenderLightingSettings>) => {
    if (busy || !settings) return;
    setBusy(true);
    try { setSettings(await invoke('studio.renderLightingSet', { patch })); setError(''); }
    catch (reason) { setError(String(reason)); }
    finally { setBusy(false); }
  };
  return <StudioSection title="GI and direct lighting">
    {error && <StudioAlert tone="danger">{error}</StudioAlert>}
    {!settings ? <StudioAlert>Open a native project to configure lighting.</StudioAlert> : <>
      <StudioField label="Indirect lighting" htmlFor="render-indirect">
        <StudioSelect id="render-indirect" value={settings.indirect} disabled={busy}
          onChange={event => void update({ indirect: event.target.value as RenderLightingSettings['indirect'] })}>
          <option value="off">Off</option><option value="auto">Automatic GPU / CPU</option>
          <option value="cpu">CPU probes</option><option value="gpu">GPU probes</option>
        </StudioSelect>
      </StudioField>
      <StudioField label="Direct lighting" htmlFor="render-direct">
        <StudioSelect id="render-direct" value={settings.direct} disabled={busy}
          onChange={event => void update({ direct: event.target.value as RenderLightingSettings['direct'] })}>
          <option value="legacy">Standard</option><option value="luma">Luma</option>
        </StudioSelect>
      </StudioField>
      <StudioField label="Luma quality" htmlFor="render-quality">
        <StudioSelect id="render-quality" value={settings.quality} disabled={busy}
          onChange={event => void update({ quality: event.target.value as RenderLightingSettings['quality'] })}>
          <option value="balanced">Balanced</option><option value="high">High</option><option value="ultra">Ultra</option>
        </StudioSelect>
      </StudioField>
      {([['probes','Probes per axis',4,16,1],['layers','Probe layers',2,8,1],
        ['spacing','Probe spacing',.25,64,.25],['samples','Rays per probe',8,64,1],
        ['distance','Ray distance',1,256,1],['cpuBudgetMs','CPU budget (ms)',.1,20,.1]] as const).map(([key,label,min,max,step]) =>
        <StudioField key={key} label={label} htmlFor={'render-'+key}>
          <StudioInput key={String(settings[key])} id={'render-'+key} type="number" defaultValue={settings[key]}
            min={min} max={max} step={step} disabled={busy || settings.indirect === 'off'}
            onBlur={event => {
              const value=event.target.valueAsNumber;
              if (Number.isFinite(value) && value !== settings[key]) void update({ [key]: value });
            }} onKeyDown={event => { if(event.key === 'Enter') event.currentTarget.blur(); }}/>
        </StudioField>)}
      <StudioAlert>{status}</StudioAlert>
      <StudioAlert>Settings are stored with the map. Use Save in the main toolbar.</StudioAlert>
    </>}
  </StudioSection>;
}
