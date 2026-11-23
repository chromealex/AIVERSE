import { useState } from 'react';
import { invoke } from '../bridge';

type Props = { ids: string[]; onApplied: () => Promise<void>; onError: (message: string) => void };
const integer = (value: string): number | null => {
  if (!/^-?\d+$/.test(value.trim())) return null;
  const n = Number(value);
  return Number.isSafeInteger(n) ? n : null;
};

/** Exact typed authoring; native StudioSession validates all selected areas. */
export default function EvolveGroupNumeric({ ids, onApplied, onError }: Props) {
  const [open, setOpen] = useState(false);
  const [scaleX, setScaleX] = useState('100');
  const [scaleZ, setScaleZ] = useState('100');
  const [pivot, setPivot] = useState<'shared' | 'individual' | 'custom'>('shared');
  const [pivotX, setPivotX] = useState('0');
  const [pivotZ, setPivotZ] = useState('0');
  const [pending, setPending] = useState(false);
  if (ids.length < 2) return null;
  const submit = async () => {
    const x = integer(scaleX), z = integer(scaleZ);
    const px = integer(pivotX), pz = integer(pivotZ);
    if (x === null || z === null || x < 1 || z < 1 || x > 1000000 || z > 1000000 ||
        (pivot === 'custom' && (px === null || pz === null))) {
      onError('EVOLVE: enter positive integer percentages and integer pivot coordinates (cm)');
      return;
    }
    setPending(true);
    try {
      await invoke('studio.worldProcessAreaBatchResize', {
        entityIds: ids, axis: 'both', pivot,
        scaleNumerator: x, scaleDenominator: 100,
        scaleNumeratorZ: z, scaleDenominatorZ: 100,
        ...(pivot === 'custom' ? { pivotXcm: px, pivotZcm: pz } : {})
      });
      await onApplied();
    } catch (error) {
      onError(error instanceof Error ? error.message : String(error));
    } finally { setPending(false); }
  };
  return <section className="evolve-numeric" data-native-overlay aria-label="EVOLVE group scale">
    <button type="button" onClick={() => setOpen(v => !v)} aria-expanded={open}>
      EVOLVE · Scale {ids.length} regions
    </button>
    {open && <div className="evolve-numeric__fields">
      <label>Scale X (%)<input type="number" min="1" max="1000000" step="1" value={scaleX} onChange={e => setScaleX(e.target.value)}/></label>
      <label>Scale Z (%)<input type="number" min="1" max="1000000" step="1" value={scaleZ} onChange={e => setScaleZ(e.target.value)}/></label>
      <label>Pivot<select value={pivot} onChange={e => setPivot(e.target.value as typeof pivot)}>
        <option value="shared">Group centroid</option><option value="individual">Each center</option>
        <option value="custom">Custom world position</option>
      </select></label>
      {pivot === 'custom' && <>
        <label>Pivot X (cm)<input type="number" step="1" value={pivotX} onChange={e => setPivotX(e.target.value)}/></label>
        <label>Pivot Z (cm)<input type="number" step="1" value={pivotZ} onChange={e => setPivotZ(e.target.value)}/></label>
      </>}
      <button type="button" disabled={pending} onClick={() => void submit()}>
        {pending ? 'Applying…' : 'Apply group scale'}
      </button>
    </div>}
  </section>;
}
