import { useCallback, useEffect, useState } from 'react';
import { Mountain, RefreshCw, Trees, Wand2 } from 'lucide-react';
import { invoke } from '../../bridge';
import type { StudioSnapshot } from '../../types';
import {
  StudioPanel, StudioSection, StudioBadge, StudioAlert, StudioEmptyState, StudioButton,
  StudioIconButton, StudioField, StudioInput, StudioSelect, StudioCheckbox
} from '../../ui';
import {
  DEFAULT_FOREST, DEFAULT_TERRAIN, forestParams, previewStats, terrainParams, validateForest, validateTerrain,
  type ForestForm, type GenerateMode, type TerrainForm
} from './terraweaveGeneratorLogic';
import GraphCanvas from '../../graphs/GraphCanvas';
import {CaveInspectionImport} from '../../graphs/CaveStructureGraphView';
import {terrainRecipeProjection} from '../../graphs/generatorProjections';
import './TerraweaveGeneratorPanel.css';

const message = (e: unknown) => e instanceof Error ? e.message : String(e);
const op = async (name: string, params: Record<string, unknown> = {}) =>
  (await invoke('studio.op', { op: name, params })).result as Record<string, any>;

/** TerraWeave landscape + forest generator. Every write goes through the allowlisted
 *  Workspace ops terraweave_generate / terraweave_forest_set (one undo step each). */
export default function TerraweaveGeneratorPanel({ snapshot, commitSnapshot }:
  { snapshot: StudioSnapshot | null; commitSnapshot?: (s: StudioSnapshot) => void }) {
  const projectLoaded = Boolean(snapshot?.project.path);
  const [terrain, setTerrain] = useState<TerrainForm>(DEFAULT_TERRAIN);
  const [forest, setForest] = useState<ForestForm>(DEFAULT_FOREST);
  const [mode, setMode] = useState<GenerateMode>('new');
  const [preview, setPreview] = useState<number[][] | null>(null);
  const [status, setStatus] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [showGraph,setShowGraph]=useState(false);
  const [showCaverns,setShowCaverns]=useState(false);

  const loadForest = useCallback(async () => {
    if (!projectLoaded) return;
    try {
      const f = await op('terraweave_forest_get');
      setForest({ enabled: Boolean(f.enabled), spacingCm: Number(f.spacingCm), densityPermille: Number(f.densityPermille),
        maxSlopePermille: Number(f.maxSlopePermille), seedOffset: Number(f.seedOffset) });
      const recipe = f.terrain as Record<string, number> | undefined;
      if (recipe && typeof recipe.seed === 'number') setTerrain(t => ({ ...t,
        seed: recipe.seed, reliefCm: recipe.reliefCm ?? t.reliefCm, basePeriodCm: recipe.basePeriodCm ?? t.basePeriodCm,
        octaves: recipe.octaves ?? t.octaves, baseHeightCm: recipe.baseHeightCm ?? t.baseHeightCm,
        cellSizeCm: recipe.cellSizeCm ?? t.cellSizeCm }));
    } catch (e) { setError(message(e)); }
  }, [projectLoaded]);
  useEffect(() => { void loadForest(); }, [loadForest, snapshot?.project.path]);

  const run = async (what: string, action: () => Promise<void>) => {
    setBusy(true); setError(''); setStatus('');
    try { await action(); setStatus(what); }
    catch (e) { setError(message(e)); }
    finally { setBusy(false); }
  };
  const refresh = () => commitSnapshot && void invoke('studio.snapshot', {}).then(commitSnapshot).catch(() => {});
  const terrainError = validateTerrain(terrain);
  const forestError = validateForest(forest);

  const doPreview = () => run('Preview updated (9x9 samples, nothing changed).', async () => {
    const r = await op('terraweave_preview', terrainParams(terrain, 'new'));
    setPreview(Array.isArray(r.samplesCm) ? r.samplesCm as number[][] : null);
  });
  const doGenerate = () => run('Landscape generated. Undo restores the previous terrain.', async () => {
    await op('terraweave_generate', terrainParams(terrain, mode)); refresh();
  });
  const doForest = () => run('Forest settings saved into the map.', async () => {
    await op('terraweave_forest_set', forestParams(forest)); refresh();
  });
  const stats = preview ? previewStats(preview) : null;
  const num = (set: (v: number) => void) => (e: { target: { value: string } }) => set(Number(e.target.value));

  if (!projectLoaded) return <StudioPanel className="studio-panel-terraweave_generator" title="TerraWeave Generator"
    eyebrow="WORLD / TERRAWEAVE" subtitle="No project open">
    <StudioEmptyState icon={<Mountain size={22}/>} title="Open a project"
      description="The generator writes the open map's heightfield and forest recipe."/>
  </StudioPanel>;

  return <StudioPanel className="studio-panel-terraweave_generator" title="TerraWeave Generator"
    eyebrow="WORLD / TERRAWEAVE" subtitle={snapshot?.project.name ?? ''}
    actions={<StudioIconButton aria-label="Reload forest settings" icon={<RefreshCw size={15}/>} onClick={() => void loadForest()}/>}
    footer={<StudioBadge tone={error ? 'danger' : status ? 'success' : 'neutral'}>{error ? 'Error' : busy ? 'Working' : 'Ready'}</StudioBadge>}>
    <div className="au-stack">
      {error && <StudioAlert tone="danger" title="TerraWeave">{error}</StudioAlert>}
      {status && !error && <StudioAlert tone="success" title="Done">{status}</StudioAlert>}
      <StudioSection title="Recipe dependencies" actions={<StudioButton size="sm" onClick={()=>setShowGraph(v=>!v)}>{showGraph?'Hide graph':'Show graph'}</StudioButton>}>
        {showGraph&&<div style={{height:220,display:'flex',flexDirection:'column'}}>
          <GraphCanvas view={terrainRecipeProjection({...terrain},{...forest})} selected="" label="TerraWeave recipe dependency view" disabled onSelect={()=>{}} onMove={()=>{}}/>
        </div>}
        <small>Read-only projection. Landscape and forest edits below use their native operations; this is not a new graph evaluator.</small>
      </StudioSection>
      <StudioSection title="Caverns dependencies" actions={<StudioButton size="sm" onClick={()=>setShowCaverns(v=>!v)}>{showCaverns?'Hide inspection':'Inspect report'}</StudioButton>}>
        {showCaverns&&<CaveInspectionImport key={JSON.stringify([snapshot?.project.path,snapshot?.scene.name])}/>}
        <small>Imported owner report only until the Caverns owner operation is integrated.</small>
      </StudioSection>
      <StudioSection title="Landscape">
        <div className="twg__grid">
          <StudioField label="Seed" htmlFor="twg-seed"><StudioInput id="twg-seed" type="number" min={0} value={terrain.seed} onChange={num(v => setTerrain({ ...terrain, seed: v }))}/></StudioField>
          <StudioField label="Relief, m" htmlFor="twg-relief"><StudioInput id="twg-relief" type="number" min={0} step={1} value={terrain.reliefCm / 100} onChange={num(v => setTerrain({ ...terrain, reliefCm: Math.round(v * 100) }))}/></StudioField>
          <StudioField label="Feature size, m" htmlFor="twg-period"><StudioInput id="twg-period" type="number" min={1} step={1} value={terrain.basePeriodCm / 100} onChange={num(v => setTerrain({ ...terrain, basePeriodCm: Math.round(v * 100) }))}/></StudioField>
          <StudioField label="Octaves" htmlFor="twg-oct"><StudioInput id="twg-oct" type="number" min={1} max={12} value={terrain.octaves} onChange={num(v => setTerrain({ ...terrain, octaves: v }))}/></StudioField>
          <StudioField label="Base height, m" htmlFor="twg-base"><StudioInput id="twg-base" type="number" step={1} value={terrain.baseHeightCm / 100} onChange={num(v => setTerrain({ ...terrain, baseHeightCm: Math.round(v * 100) }))}/></StudioField>
          <StudioField label="Cell size, cm" htmlFor="twg-cell" hint="World distance between map corners"><StudioInput id="twg-cell" type="number" min={1} value={terrain.cellSizeCm} onChange={num(v => setTerrain({ ...terrain, cellSizeCm: v }))}/></StudioField>
        </div>
        <StudioField label="Existing terrain" htmlFor="twg-mode" hint="Keep edits moves sculpt/detail onto the new base (needs a previous TerraWeave recipe).">
          <StudioSelect id="twg-mode" value={mode} onChange={e => setMode(e.target.value as GenerateMode)}>
            <option value="new">Refuse if the map has sculpted terrain</option>
            <option value="overwrite">Replace authored terrain</option>
            <option value="preserveEdits">Keep my edits, new base</option>
          </StudioSelect>
        </StudioField>
        {terrainError && <StudioAlert tone="warning" title="Check parameters">{terrainError}</StudioAlert>}
        <div className="twg__actions">
          <StudioButton disabled={busy || Boolean(terrainError)} onClick={() => void doPreview()}>Preview</StudioButton>
          <StudioButton variant="primary" disabled={busy || Boolean(terrainError)} loading={busy} onClick={() => void doGenerate()}><Wand2 size={14}/> Generate landscape</StudioButton>
        </div>
        {preview && stats && <div className="twg__preview" aria-label="Height preview">
          <div className="twg__cells" style={{ gridTemplateColumns: `repeat(${preview[0]?.length ?? 1}, 1fr)` }}>
            {preview.flatMap((row, z) => row.map((h, x) => <span key={`${x}-${z}`} title={`${(h / 100).toFixed(1)} m`}
              style={{ opacity: 0.25 + 0.75 * stats.normalized(h) }}/>))}
          </div>
          <small>{(stats.min / 100).toFixed(1)} .. {(stats.max / 100).toFixed(1)} m</small>
        </div>}
      </StudioSection>
      <StudioSection title="Forest (Arbor)" actions={<Trees size={15}/>}>
        <StudioCheckbox label="Grow TerraWeave forest on this map" checked={forest.enabled} onChange={e => setForest({ ...forest, enabled: e.target.checked })}/>
        <div className="twg__grid">
          <StudioField label="Tree spacing, m" htmlFor="twg-sp"><StudioInput id="twg-sp" type="number" min={3} max={50} step={0.5} value={forest.spacingCm / 100} onChange={num(v => setForest({ ...forest, spacingCm: Math.round(v * 100) }))}/></StudioField>
          <StudioField label="Density, %" htmlFor="twg-den"><StudioInput id="twg-den" type="number" min={0} max={100} value={forest.densityPermille / 10} onChange={num(v => setForest({ ...forest, densityPermille: Math.round(v * 10) }))}/></StudioField>
          <StudioField label="Max slope, %" htmlFor="twg-slope"><StudioInput id="twg-slope" type="number" min={0} max={500} value={forest.maxSlopePermille / 10} onChange={num(v => setForest({ ...forest, maxSlopePermille: Math.round(v * 10) }))}/></StudioField>
          <StudioField label="Forest seed offset" htmlFor="twg-fseed"><StudioInput id="twg-fseed" type="number" min={0} value={forest.seedOffset} onChange={num(v => setForest({ ...forest, seedOffset: v }))}/></StudioField>
        </div>
        {forestError && <StudioAlert tone="warning" title="Check parameters">{forestError}</StudioAlert>}
        <StudioAlert title="Where it shows">The forest recipe is saved in the map (terraweaveForest) and drives the TerraWeave forest
          overlay of the editor viewport and native hosts. Trees are not placed as entities; the Studio Metal viewport does not draw the
          TerraWeave forest overlay yet.</StudioAlert>
        <div className="twg__actions">
          <StudioButton variant="primary" disabled={busy || Boolean(forestError)} onClick={() => void doForest()}>Save forest</StudioButton>
        </div>
      </StudioSection>
    </div>
  </StudioPanel>;
}
