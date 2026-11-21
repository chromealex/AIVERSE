// Hairstyle library: models whose path mentions hair/hairstyle become cards;
// choosing one puts it in the outfit's "hair" slot (shared skeleton, original
// PBR) and hides the model's own hair meshes. Live try-on shows it at once.
import { useEffect, useState } from 'react';
import { Scissors } from 'lucide-react';
import { invoke } from '../bridge';
import { assetKind } from '../assetTree';
import AssetReferencePicker from '../components/AssetReferencePicker';
import type { StudioAsset } from '../types';
import { HcCard, HcNote } from './HumanCreatorUi';
import type { Outfit } from './useOutfit';

const HAIR = /hair|hairstyle|haircut|groom|причес|причёс|волос/i;
const label = (path: string) => path.split('/').pop()!.replace(/\.(gltf|glb|fbx)$/i, '').replace(/([a-z])([A-Z])/g, '$1 $2');
export default function HumanHairstyleControls({ guid, outfit }: { guid: string; outfit: Outfit }) {
  const [styles, setStyles] = useState<StudioAsset[]>([]);
  const [ownHair, setOwnHair] = useState<string[]>([]);
  const [bones, setBones] = useState<string[]>([]);
  const [error, setError] = useState('');
  useEffect(() => {
    let live = true;
    void invoke('studio.assets', {}).then(r => { if (live) setStyles(r.files.filter(f => f.guid && assetKind(f.path) === 'model' && HAIR.test(f.path))); }).catch(() => {});
    if (guid) void invoke('studio.humanWardrobeInfo', { guid }).then(i => { if (live) { setOwnHair(i.meshes.filter(m => /hair|scalp|groom/i.test(m) && !/brow|lash/i.test(m))); setBones(i.bones); } }).catch(() => {});
    return () => { live = false; };
  }, [guid]);
  const current = outfit.recipe.items.find(i => i.slot === 'hair');
  const choose = (asset: string | null) => void (async () => {
    setError('');
    if (!guid) { setError('Сначала выберите персонажа в «Пресетах»'); return; }
    const items = outfit.recipe.items.filter(i => i.slot !== 'hair');
    const hidden = outfit.recipe.hiddenMeshes.filter(m => !ownHair.includes(m));
    if (!asset) { outfit.setRecipe({ ...outfit.recipe, items, hiddenMeshes: hidden }); return; }
    // Same skeleton (all hair bones exist on the character): skin to it.
    // Otherwise (static or foreign rig) attach to the head bone, keeping the
    // authored placement.
    let shared = false;
    try { const hb = (await invoke('studio.humanWardrobeInfo', { guid: asset })).bones; shared = hb.length > 0 && hb.every(b => bones.includes(b)); } catch { /* static mesh */ }
    const head = bones.find(b => /^(head|Head)$/.test(b)) ?? bones.find(b => /head/i.test(b) && !/end|top|nub/i.test(b)) ?? '';
    if (!shared && !head) { setError('У персонажа не найдена кость головы — настройте крепление во вкладке «Одежда»'); }
    outfit.setRecipe({ ...outfit.recipe, hiddenMeshes: [...hidden, ...ownHair], items: [...items, {
      id: 'hairstyle', asset, slot: 'hair', enabled: true, bone: shared ? '' : head, translation: [0, 0, 0], rotation: [0, 0, 0], scale: [1, 1, 1],
      finish: 'source', hideMeshes: [], masks: [], ...(shared ? {} : { independentRig: true, attachmentRule: 'keep_world' as const }) }] });
  })();
  return <HcCard title="Причёска" hint={outfit.trying ? 'Обновляю примерку…' : 'Причёски из проекта: модели с hair / hairstyle в пути'}>
    {(error || outfit.error) && <HcNote tone="error">{error || outfit.error}</HcNote>}
    <div className="hc-cards">
      <button type="button" className="hc-card" aria-pressed={!current} onClick={() => choose(null)}><Scissors size={22}/><span>Своя причёска модели</span></button>
      {styles.map(s => <button key={s.guid} type="button" className="hc-card" aria-pressed={current?.asset === s.guid}
        onClick={() => choose(s.guid!)}><Scissors size={22}/><span>{label(s.path)}</span></button>)}
    </div>
    {!styles.length && <p className="hc-hint">В проекте нет моделей причёсок. Положите их в content (например, Hair/Bob.glb) с той же костью головы, что у персонажа, или выберите любую модель ниже.</p>}
    <AssetReferencePicker value={current?.asset ?? ''} kinds={['model']} label="Другая модель причёски" onChange={a => choose(a || null)}/>
    {!outfit.tryOn && current && <HcNote tone="warn">Примерка выключена во вкладке «Одежда» — причёска появится после сборки.</HcNote>}
  </HcCard>;
}
