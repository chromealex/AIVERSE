import { useEffect, useState } from 'react';
import { FolderOpen, Save } from 'lucide-react';
import { invoke } from '../../bridge';
import type { StudioProjectState, StudioSnapshot } from '../../types';
import type { StudioPanelContext } from '../StudioPanelRenderer';
import {
  StudioPanel, StudioSection, StudioAlert, StudioEmptyState, StudioButton,
  StudioField, StudioInput, StudioListRow
} from '../../ui';
import './ProjectPanel.css';

export default function ProjectPanel({ snapshot, busy, connected, openProject, updateSnapshot, setBusy }: StudioPanelContext) {
  const [project, setProject] = useState<StudioProjectState | null>(null);
  const [error, setError] = useState('');
  const [name, setName] = useState('New map');
  const [width, setWidth] = useState('64'), [height, setHeight] = useState('48');
  const loaded = connected && !!snapshot?.project.path;
  useEffect(() => {
    let active = true;
    if (!loaded) { setProject(null); return; }
    void invoke('studio.project', {}).then(value => { if (active) setProject(value); })
      .catch(e => { if (active) setError(String(e)); });
    return () => { active = false; };
  }, [loaded, snapshot]);
  const run = async (operation: () => Promise<StudioSnapshot>) => {
    if (busy) return;
    setBusy(true); setError('');
    try { updateSnapshot(await operation()); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  return <StudioPanel className="studio-panel-project" title="Project" subtitle={snapshot?.project.name}
    actions={<StudioButton disabled={!connected || busy} onClick={() => void openProject()}><FolderOpen size={15}/> Open</StudioButton>}>
    <div className="au-stack">
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {!loaded ? <StudioEmptyState title="No native project" description="Open a project to manage its maps."/> : <>
        <StudioSection title="Current map">
          <p>{project?.currentMap || 'Unsaved map'}</p>
          <div className="au-row">
            <StudioButton disabled={busy || !snapshot?.project.dirty} onClick={() => void run(() => invoke('studio.save', {}))}><Save size={15}/> Save</StudioButton>
            <StudioButton disabled={busy} onClick={() => void run(() => invoke('studio.saveAs', {}))}>Save As…</StudioButton>
          </div>
        </StudioSection>
        <StudioSection title="Maps">
          {project?.maps.length ? project.maps.map(map => <StudioListRow key={map.file} label={map.title}
            selected={map.file === project.currentMap} disabled={busy} title={map.file}
            onClick={() => void run(() => invoke('studio.openMap', {file: map.file}))}/>) : <StudioEmptyState title="No saved maps" description="Save a map to add it to the project."/>}
        </StudioSection>
        {project?.canCreateMap && <StudioSection title="New map">
          <form className="au-stack" onSubmit={event => {
            event.preventDefault();
            if (![width,height].every(value => value.trim() && Number.isInteger(Number(value)))) { setError('Map dimensions must be whole numbers.'); return; }
            void run(() => invoke('studio.newMap', {name, width: Number(width), height: Number(height)}));
          }}>
            <StudioField label="Name" htmlFor="new-map-name"><StudioInput id="new-map-name" required maxLength={256} value={name} onChange={event => setName(event.target.value)} disabled={busy}/></StudioField>
            <StudioField label="Width" htmlFor="new-map-width"><StudioInput id="new-map-width" type="number" min={8} max={project.maxMapSize} step={1} required value={width} onChange={event => setWidth(event.target.value)} disabled={busy}/></StudioField>
            <StudioField label="Height" htmlFor="new-map-height"><StudioInput id="new-map-height" type="number" min={8} max={project.maxMapSize} step={1} required value={height} onChange={event => setHeight(event.target.value)} disabled={busy}/></StudioField>
            <StudioButton type="submit" disabled={busy}>Create map</StudioButton>
          </form>
        </StudioSection>}
      </>}
    </div>
  </StudioPanel>;
}
