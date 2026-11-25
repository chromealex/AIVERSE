import AnimationAssetsPanel from './AnimationAssetsPanel';
import BrandingPanel from './BrandingPanel';
import type { StudioScriptMessage } from '../types';
// AI-friendly rendering surface: keep App.tsx as the application shell.
// For a new editor panel, register a typed panel ID in nestedDock.ts,
// then insert one render case below. All data mutations still use Studio Bridge.
import { type ReactNode } from 'react';
import {
  Activity,
  Focus, RotateCcw
} from 'lucide-react';
import type { StudioEntity, StudioInspector, StudioSnapshot } from '../types';
import { invoke } from '../bridge';
import type { StudioPanel } from '../nestedDock';
import ViewportPlaceholder from '../components/ViewportPlaceholder';
import SceneToolbar from '../components/SceneToolbar';
import RulesPanel from './RulesPanel';
import AssetsPanel from '../components/AssetsPanel';
import LightingPanel from './LightingPanel';
import MaterialsPanel from './MaterialsPanel';
import ShaderGraphPanel from './ShaderGraphPanel';
import AnimationGraphPanel from './AnimationGraphPanel';
import FacialRigPanel from './FacialRigPanel';
import { studioSceneMode } from './sceneState';
import HierarchyPanel from './HierarchyPanel';
import InspectorPanel from './InspectorPanel';
import GamePanel from './GamePanel';

import ProjectPanel from './generated/ProjectPanel';

import RuntimePanel from './generated/RuntimePanel';
import BuildPanel from './BuildPanel';
import ConsolePanel from './ConsolePanel';
import StaticGeometryPanel from './StaticGeometryPanel';
import LensCameraPanel from './LensCameraPanel';
import ToolPanel from './ToolPanel';
import SpectraDataInterfacesPanel from './SpectraDataInterfacesPanel';
import SpectraLibraryPanel from './SpectraLibraryPanel';
import AgentPanel from './AgentPanel';
import AnimationTimeline from './AnimationTimeline';
import PostFxPanel from './PostFxPanel';
import ProjectHub from '../components/ProjectHub';
import ExtensionsPanel from './ExtensionsPanel';
import AttachmentsPanel from './AttachmentsPanel';
import { FrameCapturePanel, SnapshotDiffPanel, StatsPanel, TextureResourcesPanel } from './InsightPanels';
import { AuditPanel, FogPanel, InputPanel, ScriptsPanel } from './ProjectToolPanels';
import HistoryPanel from './HistoryPanel';
import { LiveHierarchy, LiveInspector, LiveSwitch } from './LivePanels';
import { useLivePlay } from './livePlay';

import NereidFlowPanel from './generated/NereidFlowPanel';

import WindPanel from './generated/WindPanel';
import HumanAppearancePanel from './generated/HumanAppearancePanel';

import HumanCreatorPanel from './generated/HumanCreatorPanel';

import WorldPersistencePanel from './WorldPersistencePanel';
import WorldExplorerPanel from './generated/WorldExplorerPanel';
import SimulationProfilerPanel from './generated/SimulationProfilerPanel';
import TerraweaveGeneratorPanel from './generated/TerraweaveGeneratorPanel';
import BuildingGeneratorPanel from './generated/BuildingGeneratorPanel';
import CavernsPanel from './generated/CavernsPanel';

import CatalogPanel from './generated/CatalogPanel';
import FeatureReadinessPanel from './FeatureReadinessPanel';

export interface StudioPanelContext {
  snapshot: StudioSnapshot | null;
  busy: boolean;
  setBusy: (value: boolean) => void;
  updateSnapshot: (value: StudioSnapshot) => void;
  connected: boolean;
  openProject: () => Promise<void>;
  search: string;
  setSearch: (value: string) => void;
  rows: Array<{node: StudioEntity; depth: number}>;
  selected: StudioEntity | undefined;
  inspector: StudioInspector | null;
  inspectorPinned: boolean;
  toggleInspectorPin: () => void;
  select: (id: string | null, mode?: 'replace' | 'add' | 'toggle') => Promise<void>;
  selectIds: (ids: string[], mode?: 'replace' | 'add' | 'toggle') => Promise<void>;
  /** Scene object under the cursor (Scene or Hierarchy hover). */
  hoverId: string;
  changeProperty: (key: string, value: string | number | boolean, componentId?: string) => Promise<void>;
  /** Apply a snapshot from an Inspector action without dropping the Inspector pin. */
  commitSnapshot: (value: StudioSnapshot) => void;
  /** A game runs in the Game panel: Hierarchy / Inspector offer the live view. */
  scriptLocation:{path:string;line:number} | null;
  openScript:(path:string,line:number)=>void;
  consumeScriptLocation:()=>void;
  scriptErrors?: StudioScriptMessage[];
  liveSelection?: import('./livePlay').LiveRef | null;
  onLiveSelection?: (state:import('../types').StudioGameState)=>void;
  playing?: boolean;
  showPanel: (panel: StudioPanel) => void;
  reportError: (message: string) => void;
}

function PlayHierarchy({ ctx }: { ctx: StudioPanelContext }) {
  const { showLive } = useLivePlay();
  return <LiveSwitch>{showLive ? <LiveHierarchy groupings={ctx.snapshot?.scene.groupings} selection={ctx.liveSelection} onSelected={ctx.onLiveSelection} key={ctx.snapshot?.project.path} project={ctx.snapshot?.project.path}/> : <HierarchyPanel {...ctx}/>}</LiveSwitch>;
}
function PlayInspector({ ctx }: { ctx: StudioPanelContext }) {
  const { showLive } = useLivePlay();
  return <LiveSwitch>{showLive ? <LiveInspector selection={ctx.liveSelection} key={ctx.snapshot?.project.path} project={ctx.snapshot?.project.path}/> : <InspectorPanel {...ctx}/>}</LiveSwitch>;
}

export function renderStudioPanel(panel: StudioPanel, ctx: StudioPanelContext): ReactNode {
  const {snapshot,connected,openProject,selected,select}=ctx;
  const sceneMode=studioSceneMode(connected,snapshot?.project.path);
    switch (panel) {
      case 'hierarchy':
        return ctx.playing ? <PlayHierarchy ctx={ctx}/> : <HierarchyPanel {...ctx}/>;
      case 'assets':
        return <AssetsPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'inspector':
        return ctx.playing ? <PlayInspector ctx={ctx}/> : <InspectorPanel {...ctx}/>;
      case 'postfx': return <PostFxPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'terrain': case 'water': case 'roads': case 'tiles': case 'generate':
      case 'effects': case 'lightbake': case 'models': case 'overlays':
      case 'fields': case 'physis': case 'raid': case 'balance': case 'fx_functions': case 'modeling':
      case 'model_anim': case 'project_import':
        return <ToolPanel panel={panel} snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'branding': return <BrandingPanel key={snapshot?.project.path} project={snapshot?.project.path ?? ''} advanced={<ToolPanel panel={panel} snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>}/>;
      case 'anim_assets': return <AnimationAssetsPanel key={snapshot?.project.path} project={snapshot?.project.path??''} tools={<ToolPanel panel={panel} snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>}/>;
      case 'actor': return <LensCameraPanel openScene={()=>ctx.showPanel('scene')} key={snapshot?.project.path} project={snapshot?.project.path??''} operations={<ToolPanel panel={panel} snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>}/>;
      case 'anim_graph': case 'anim_rig': return <AnimationGraphPanel key={panel+snapshot?.project.path} rigMode={panel==='anim_rig'} projectPath={snapshot?.project.path??''} tools={<ToolPanel panel={panel} snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>}/>;
      case 'fx_library': return <SpectraLibraryPanel key={snapshot?.project.path ?? ''} projectPath={snapshot?.project.path ?? ''} showPanel={ctx.showPanel}/>;
      case 'fx_graph': return <SpectraDataInterfacesPanel projectPath={snapshot?.project.path ?? ''}
        onSaved={()=>void invoke('studio.snapshot',{}).then(ctx.commitSnapshot).catch(()=>{})}/>;
      case 'fog': return <FogPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'scripts': return <ScriptsPanel key={snapshot?.project.path} location={ctx.scriptLocation} consumeLocation={ctx.consumeScriptLocation} playing={!!ctx.playing} snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'input': return <InputPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'stats': return <StatsPanel snapshot={snapshot}/>;
      case 'attachments': return <AttachmentsPanel snapshot={snapshot}/>;
      case 'extensions': return <ExtensionsPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot} showPanel={ctx.showPanel}/>;
      case 'frame_capture': return <FrameCapturePanel snapshot={snapshot}/>;
      case 'texture_resources': return <TextureResourcesPanel snapshot={snapshot}/>;
      case 'snapshot_diff': return <SnapshotDiffPanel snapshot={snapshot}/>;
      case 'agent': return <AgentPanel key={snapshot?.project.path} snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'asset_audit': return <AuditPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'history':
        return <HistoryPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'build':
        return <BuildPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'rules':
        return <RulesPanel key={snapshot?.project.path} projectPath={snapshot?.project.path ?? ''} historyVersion={snapshot?.project.historyVersion} onChanged={() => void invoke('studio.snapshot', {}).then(ctx.commitSnapshot).catch(() => {})}/>;
      case 'scene':
        return <>
          <div className="viewport-toolbar">
            <div className="viewport-tools">
              <SceneToolbar enabled={connected && sceneMode !== 'welcome'} projectPath={snapshot?.project.path ?? ''}/>
              <span className="toolbar-divider"/>
              <button type="button" className="viewport-control" title="Focus selected object (F)"
                aria-label="Focus selected Scene object" disabled={!connected || !selected}
                onClick={()=>void invoke('studio.viewportInput',{kind:'focus'}).catch(console.warn)}>
                <Focus size={15}/></button>
              <button type="button" className="viewport-control" title="Reset Scene camera (Home)"
                aria-label="Reset Scene camera" disabled={!connected}
                onClick={()=>void invoke('studio.viewportInput',{kind:'reset'}).catch(console.warn)}>
                <RotateCcw size={15}/></button>
            </div>
            <span className="render-badge"><Activity size={13}/>
              {sceneMode === 'welcome' ? 'Open project to render' : 'Native Renderer3D (experimental)'}
            </span>
          </div>
          {sceneMode === 'welcome' ?
            <ProjectHub onOpened={(next, openAgent, warning) => { ctx.commitSnapshot(next); if (openAgent) ctx.showPanel('agent'); if (warning) ctx.reportError(warning); }} openOther={() => void openProject()}/> :
            <ViewportPlaceholder title="Scene viewport" active/>}
        </>;
      case 'console':
        return <ConsolePanel openScript={ctx.openScript} key={snapshot?.project.path} project={snapshot?.project.path ?? ''} logs={snapshot?.logs ?? []} scriptErrors={ctx.scriptErrors} select={id => select(id)}/>;
      case 'lighting':
        return <LightingPanel projectPath={snapshot?.project.path ?? ''} selectedId={snapshot?.selectedId ?? null} onSelect={select}/>;
      case 'materials':
        return <MaterialsPanel projectPath={snapshot?.project.path ?? ''}
          previewMaterial={snapshot?.previewMaterial} openTextureGraph={()=>ctx.showPanel('texture_graph')}/>;
      case 'texture_graph':
        return <MaterialsPanel textureOnly projectPath={snapshot?.project.path ?? ''} previewMaterial={snapshot?.previewMaterial}/>;
      case 'shader_graph':
        return <ShaderGraphPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'project': return <ProjectPanel key={snapshot?.project.path} {...ctx}/>;
      case 'facial_rig':
        return <FacialRigPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'runtime': return <RuntimePanel snapshot={snapshot}/>;
      case 'nereid_flow': return <NereidFlowPanel snapshot={snapshot}/>;
      case 'wind': return <WindPanel snapshot={snapshot}/>;
      case 'human_appearance': return <HumanAppearancePanel snapshot={snapshot}/>;
      case 'human_creator': return <HumanCreatorPanel snapshot={snapshot}/>;
      case 'caverns': return <CavernsPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'terraweave_generator': return <TerraweaveGeneratorPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'building_generator': return <BuildingGeneratorPanel snapshot={snapshot} showPanel={ctx.showPanel} commitSnapshot={ctx.commitSnapshot}/>;
      case 'catalog': return <CatalogPanel snapshot={snapshot}/>;
      case 'static_geometry': return <StaticGeometryPanel key={snapshot?.project.path} projectPath={snapshot?.project.path ?? ''}/>;
      case 'feature_readiness': return <FeatureReadinessPanel connected={connected}/>;
      case 'world_explorer': return <WorldExplorerPanel snapshot={snapshot}/>;
      case 'simulation_profiler': return <SimulationProfilerPanel snapshot={snapshot}/>;
      case 'world_persistence': return <WorldPersistencePanel projectPath={snapshot?.project.path ?? ''}/>;
      // AI_PANEL_CASES: add new panel renderers above this line.
      case 'game': return <GamePanel {...ctx}/>;
      case 'animation': return <AnimationTimeline snapshot={snapshot} showPanel={ctx.showPanel}/>;
    }
}
