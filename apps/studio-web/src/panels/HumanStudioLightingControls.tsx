// Studio light for the Character Creator: preset cards apply on click,
// rotation and brightness apply on release. Preview only, never baked.
import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { HumanStudioLightingInfo, HumanStudioLightingSettings } from '../types';
import { HcCard, HcNote, HcSlider, HcSwitch } from './HumanCreatorUi';

const defaults: HumanStudioLightingSettings = { preset: 'neutral', rotation: 0, gain: 1, backdrop: true };
// Swatch per preset id (key/fill tint), so the cards read at a glance.
const SWATCH: Record<string, string> = {
  neutral: 'linear-gradient(135deg,#f3efe8,#8c98a8)', portrait: 'linear-gradient(135deg,#ffe2c8,#b9a4a0)',
  warm: 'linear-gradient(135deg,#ffb066,#4a2a2a)', cool: 'linear-gradient(135deg,#d9ecff,#6d86a8)',
  dramatic: 'linear-gradient(135deg,#ffffff,#111318 60%)', rim: 'radial-gradient(circle at 80% 30%,#fff,#1b2230 55%)',
  gel: 'linear-gradient(135deg,#ff4fa3,#3c7bff)', catalog: 'linear-gradient(135deg,#ffffff,#dfe5ec)' };

export default function HumanStudioLightingControls() {
  const [info, setInfo] = useState<HumanStudioLightingInfo | null>(null), [draft, setDraft] = useState(defaults);
  const [busy, setBusy] = useState(false), [error, setError] = useState('');
  useEffect(() => {
    let live = true;
    void invoke('studio.humanStudioLighting', {}).then(i => { if (live) { setInfo(i); setDraft(i.settings); } })
      .catch(e => { if (live) setError(String(e)); });
    return () => { live = false; };
  }, []);
  const apply = async (settings: HumanStudioLightingSettings) => {
    setDraft(settings); setBusy(true); setError('');
    try { const i = await invoke('studio.humanStudioLighting', { settings }); setInfo(i); setDraft(i.settings); }
    catch (e) { setError(String(e)); } finally { setBusy(false); }
  };
  const off = busy || !info;
  return <>
    {error && <HcNote tone="error">{error}</HcNote>}
    <HcCard title="Свет" hint="Клик применяет пресет. Свет действует только в превью и не запекается в текстуры"
      actions={<button type="button" className="hc-reset" title="Сбросить свет" disabled={off} onClick={() => void apply(defaults)}>↺</button>}>
      <div className="hc-light-grid">{info?.presets.map(p => <button key={p.id} type="button" className="hc-light"
        aria-pressed={draft.preset === p.id} disabled={off} onClick={() => void apply({ ...draft, preset: p.id })}>
        <span className="hc-light__swatch" style={{ background: SWATCH[p.id] ?? '#556' }}/>
        <span>{p.label.replace(/^\d+\s*·\s*/, '')}</span></button>)}</div>
    </HcCard>
    <HcCard title="Настройка">
      <HcSlider id="hc-light-rot" label="Поворот света" min={-180} max={180} step={1} value={draft.rotation} disabled={off}
        format={v => Math.round(v) + '°'} onChange={v => setDraft({ ...draft, rotation: v })} onCommit={() => void apply(draft)}/>
      <HcSlider id="hc-light-gain" label="Яркость" min={.25} max={2} step={.01} value={draft.gain} disabled={off}
        format={v => '×' + v.toFixed(2)} onChange={v => setDraft({ ...draft, gain: v })} onCommit={() => void apply(draft)}/>
      <HcSwitch label="Циклорама" hint="Плавный переход пола в стену" checked={draft.backdrop} disabled={off} onChange={v => void apply({ ...draft, backdrop: v })}/>
    </HcCard>
  </>;
}
