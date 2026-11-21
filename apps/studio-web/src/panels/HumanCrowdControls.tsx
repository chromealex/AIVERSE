// Crowd from one character: N variants of skin, hair, eyes and body (locked
// parts stay), saved as looks <name>_v1..vN; optionally each is baked into its
// own character asset and added to the game as a unit type.
import { useState } from 'react';
import { Users } from 'lucide-react';
import { invoke } from '../bridge';
import type { HumanBuildStatus, HumanFaceShape, HumanLook, HumanLookInfo } from '../types';
import { StudioButton, StudioCheckbox } from '../ui';
import { HcCard, HcNote, HcSlider } from './HumanCreatorUi';
import { randomLook, type LookPart } from './humanCreatorSchema';
import type { Outfit } from './useOutfit';

const wait = (ms: number) => new Promise(r => setTimeout(r, ms));
export default function HumanCrowdControls({ info, draft, face, guid, name, locked, outfit, onSaved }:
  { info: HumanLookInfo | null; draft: HumanLook; face: HumanFaceShape | null; guid: string; name: string; locked: LookPart[];
    outfit: Outfit; onSaved: (looks: string[]) => void }) {
  const [count, setCount] = useState(4), [bake, setBake] = useState(false);
  const [busy, setBusy] = useState(false), [status, setStatus] = useState(''), [error, setError] = useState('');
  const valid = /^[A-Za-z0-9_-]{1,58}$/.test(name);
  const run = async () => {
    setBusy(true); setError('');
    try {
      const base = Object.fromEntries((info?.schema ?? []).map(f => [f.key, draft.values[f.key] ?? f.initial]));
      for (let i = 1; i <= count; ++i) {
        const variant = name + '_v' + i;
        const look: HumanLook = { ...draft, values: { ...draft.values, ...randomLook(base, locked) } };
        setStatus(`Вариант ${i} из ${count}: сохраняю образ`);
        await invoke('studio.humanAppearanceSave', { name: variant, look });
        if (!bake) continue;
        setStatus(`Вариант ${i} из ${count}: собираю персонажа`);
        let job: HumanBuildStatus = await invoke('studio.humanCharacterBuild', { guid, look, wardrobe: outfit.recipe, name: variant, textureSize: 512, face });
        while (job.running) { await wait(1000); job = await invoke('studio.humanCharacterBuildStatus', {}); }
        if (job.error || !job.path) throw Error(job.error || 'Сборка не удалась');
        setStatus(`Вариант ${i} из ${count}: добавляю в игру`);
        await invoke('studio.humanPlaceInGame', { type: variant.replace(/[^A-Za-z0-9_]/g, '_').replace(/^(\d)/, '_$1'), path: job.path, name: variant });
      }
      onSaved((await invoke('studio.humanAppearanceList', {})).looks);
      setStatus(bake ? `Готово: ${count} персонажей собраны и добавлены в правила` : `Готово: ${count} образов в «Образах»`);
    } catch (e) { setError(String(e).replace(/^Error: /, '')); } finally { setBusy(false); }
  };
  return <HcCard title="Толпа" hint="Варианты одного персонажа: кожа, волосы, глаза, телосложение. Замки из «Случайного персонажа» действуют и здесь">
    {error && <HcNote tone="error">{error}</HcNote>}
    <HcSlider id="hc-crowd" label="Сколько вариантов" min={2} max={12} step={1} value={count} disabled={busy} format={v => String(Math.round(v))} onChange={v => setCount(Math.round(v))}/>
    <StudioCheckbox label="Собрать каждого и добавить в игру как тип юнита (долго)" checked={bake} disabled={busy || !guid} onChange={e => setBake(e.target.checked)}/>
    <StudioButton variant="primary" disabled={busy || !info || !valid} onClick={() => void run()}><Users size={14}/> {busy ? 'Создаю…' : 'Создать варианты'}</StudioButton>
    {!valid && <p className="hc-hint">Имя пресета (поле в «Образах») — латиница, цифры, _ и -.</p>}
    {status && <p className="hc-hint">{status}</p>}
  </HcCard>;
}
