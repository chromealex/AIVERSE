// Reference photo card (Face tab): load an image from disk, then match the
// face to it with the blend circle or the sculpt brush.
import { useEffect, useState } from 'react';
import { ImagePlus } from 'lucide-react';
import { StudioButton } from '../ui';
import { HcCard, HcSlider, HcSwitch } from './HumanCreatorUi';
import { onReferenceView, referenceView, setReferenceView } from './referenceStore';

export default function HumanReferenceControls() {
  const [r, setR] = useState(referenceView());
  useEffect(() => onReferenceView(setR), []);
  const load = (file: File | undefined) => {
    if (!file || !file.type.startsWith('image/') || file.size > 20 * 1024 * 1024) return;
    const reader = new FileReader();
    reader.onload = () => setReferenceView({ src: String(reader.result), visible: true });
    reader.readAsDataURL(file);
  };
  return <HcCard title="Референс" hint="Фото поверх вьюпорта: подгоняйте лицо кругом смешивания или кистью">
    <label className="hc-file"><ImagePlus size={14}/> {r.src ? 'Заменить фото' : 'Загрузить фото'}
      <input type="file" accept="image/*" onChange={e => load(e.target.files?.[0])}/></label>
    {r.src && <>
      <HcSwitch label="Показывать" checked={r.visible} onChange={v => setReferenceView({ visible: v })}/>
      <HcSlider id="ref-op" label="Прозрачность" min={.05} max={1} step={.01} value={r.opacity} format={v => Math.round(v * 100) + '%'} onChange={v => setReferenceView({ opacity: v })}/>
      <HcSlider id="ref-sc" label="Размер" min={.1} max={2} step={.01} value={r.scale} format={v => Math.round(v * 100) + '%'} onChange={v => setReferenceView({ scale: v })}/>
      <HcSlider id="ref-x" label="Сдвиг по X" min={-1} max={1} step={.005} value={r.x} format={v => v.toFixed(2)} onChange={v => setReferenceView({ x: v })}/>
      <HcSlider id="ref-y" label="Сдвиг по Y" min={-1} max={1} step={.005} value={r.y} format={v => v.toFixed(2)} onChange={v => setReferenceView({ y: v })}/>
      <HcSwitch label="Отразить" checked={r.mirror} onChange={v => setReferenceView({ mirror: v })}/>
      <StudioButton size="sm" onClick={() => setReferenceView({ src: '' })}>Убрать фото</StudioButton>
    </>}
  </HcCard>;
}
