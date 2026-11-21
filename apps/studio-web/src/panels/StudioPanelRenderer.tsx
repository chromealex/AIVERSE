// AI-friendly rendering surface: keep App.tsx as the application shell.
// For a new editor panel, register a typed panel ID in nestedDock.ts,
// then insert one render case below. All data mutations still use Studio Bridge.
import type { ReactNode } from 'react';
import {
  Activity, Box, FolderOpen,
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
import FacialRigPanel from './FacialRigPanel';
import { studioSceneMode } from './sceneState';
import HierarchyPanel from './HierarchyPanel';
import InspectorPanel from './InspectorPanel';
import GamePanel from './GamePanel';

import ProjectPanel from './generated/ProjectPanel';

import RuntimePanel from './generated/RuntimePanel';
import BuildPanel from './BuildPanel';

import NereidFlowPanel from './generated/NereidFlowPanel';

import WindPanel from './generated/WindPanel';
import HumanAppearancePanel from './generated/HumanAppearancePanel';

import HumanCreatorPanel from './generated/HumanCreatorPanel';

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
  showPanel: (panel: StudioPanel) => void;
  reportError: (message: string) => void;
}

export function renderStudioPanel(panel: StudioPanel, ctx: StudioPanelContext): ReactNode {
  const {snapshot,busy,connected,openProject,selected,select}=ctx;
  const sceneMode=studioSceneMode(connected,snapshot?.project.path);
    switch (panel) {
      case 'hierarchy':
        return <HierarchyPanel {...ctx}/>;
      case 'assets':
        return <AssetsPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'inspector':
        return <InspectorPanel {...ctx}/>;
      case 'build':
        return <BuildPanel snapshot={snapshot} commitSnapshot={ctx.commitSnapshot}/>;
      case 'rules':
        return <RulesPanel projectPath={snapshot?.project.path ?? ''} onChanged={() => void invoke('studio.snapshot', {}).then(ctx.commitSnapshot).catch(() => {})}/>;
      case 'scene':
        return <>
          <div className="viewport-toolbar">
            <div className="viewport-tools">
              <SceneToolbar enabled={connected && sceneMode !== 'welcome'}/>
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
            <div className="studio-no-project" role="status">
              <div className="studio-no-project-symbol"><FolderOpen size={32} strokeWidth={1.25}/></div>
              <h2>No project open</h2>
              <p>Open an AIVERSE project to inspect its scene, edit objects,
                and render the world using the native engine.</p>
              <button className="studio-no-project-action"
                disabled={busy}
                onClick={() => void openProject()}><FolderOpen size={15}/> Open Project</button>
            </div> :
            <ViewportPlaceholder title="Scene viewport" active/>}
        </>;
      case 'console':
        return <div className="log-list">
          {snapshot?.logs.map((line, i) =>
            <div className={'log-row log-' + line.level} key={i}>
              <span>{line.time}</span>{line.text}
            </div>)}
        </div>;
      case 'lighting':
        return <LightingPanel projectPath={snapshot?.project.path ?? ''} selectedId={snapshot?.selectedId ?? null} onSelect={select}/>;
      case 'materials':
        return <MaterialsPanel projectPath={snapshot?.project.path ?? ''}
          previewMaterial={snapshot?.previewMaterial}/>;
      case 'shader_graph':
        return <ShaderGraphPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'project': return <ProjectPanel {...ctx}/>;
      case 'facial_rig':
        return <FacialRigPanel projectPath={snapshot?.project.path ?? ''}/>;
      case 'runtime': return <RuntimePanel snapshot={snapshot}/>;
      case 'nereid_flow': return <NereidFlowPanel snapshot={snapshot}/>;
      case 'wind': return <WindPanel snapshot={snapshot}/>;
      case 'human_appearance': return <HumanAppearancePanel snapshot={snapshot}/>;
      case 'human_creator': return <HumanCreatorPanel snapshot={snapshot}/>;
      // AI_PANEL_CASES: add new panel renderers above this line.
      case 'game': return <GamePanel {...ctx}/>;
      case 'animation':
        return <div className="dock-unported" role="status">
          <Box size={28}/>
          <strong>{panel.charAt(0).toUpperCase() + panel.slice(1)} editor</strong>
          <p>HTML5 panel migration is pending. The legacy native editor remains available.</p>
        </div>;
    }
}
