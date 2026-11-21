// Character package: export a look (+ picture, + optional built character)
// to <project>/packages/<look>.aivchar; copy the file into another project's
// packages folder and import it there. Import never overwrites.
import { useEffect, useState } from 'react';
import { Package } from 'lucide-react';
import { invoke } from '../bridge';
import { StudioButton, StudioInput } from '../ui';
import { HcCard, HcNote } from './HumanCreatorUi';

export default function HumanPackageControls({ look, onImported }: { look: string; onImported: () => void }) {
  const [packages, setPackages] = useState<string[]>([]);
  const [character, setCharacter] = useState('');
  const [busy, setBusy] = useState(false), [note, setNote] = useState(''), [error, setError] = useState('');
  const reload = () => void invoke('studio.humanPackageList', {}).then(r => setPackages(r.packages)).catch(() => {});
  useEffect(reload, []);
  const run = async (fn: () => Promise<string>) => { setBusy(true); setError(''); try { setNote(await fn()); reload(); } catch (e) { setError(String(e).replace(/^Error: /, '')); } finally { setBusy(false); } };
  const valid = /^[A-Za-z0-9_-]{1,64}$/.test(look);
  return <HcCard title="Пакет персонажа" hint="Один файл .aivchar в папке packages проекта — перенесите его в другой проект и импортируйте">
    {error && <HcNote tone="error">{error}</HcNote>}
    <div className="hc-row">
      <StudioInput aria-label="Собранный персонаж (необязательно)" placeholder="Собранный персонаж, например character_assembled" value={character} onChange={e => setCharacter(e.target.value)}/>
      <StudioButton disabled={busy || !valid} title={'Образ «' + look + '»'} onClick={() => void run(async () => {
        const r = await invoke('studio.humanPackageExport', { look, character: character.trim() || undefined });
        return 'Сохранён ' + r.path + ' · ' + (r.bytes / 1048576).toFixed(1) + ' МБ';
      })}><Package size={14}/> Экспорт</StudioButton>
    </div>
    {packages.length > 0 && <div className="hc-chips">{packages.map(p => <button key={p} type="button" className="hc-chip" disabled={busy}
      title="Импортировать в этот проект" onClick={() => void run(async () => {
        const r = await invoke('studio.humanPackageImport', { name: p }); onImported();
        return 'Импортирован образ «' + r.look + '»' + (r.character ? ' и персонаж ' + r.character : '');
      })}>⤓ {p}</button>)}</div>}
    {note && <p className="hc-hint">{note}</p>}
  </HcCard>;
}
