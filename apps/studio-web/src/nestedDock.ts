/** Persistable nested docking geometry; no React, renderer or genre dependencies. */
export const STUDIO_PANELS = ['hierarchy','assets','scene','game','materials','animation','facial_rig','inspector','console','lighting','shader_graph','project','runtime','nereid_flow','rules','human_appearance','build','human_creator','wind','history','terrain','water','roads','tiles','fog','generate','postfx','effects','lightbake','models','scripts','input','overlays','branding','asset_audit','agent','fields','physis','actor','raid','balance','fx_graph','fx_functions','modeling','anim_assets','anim_graph','anim_rig','model_anim','project_import','stats','snapshot_diff','frame_capture','texture_resources','extensions','attachments','terraweave_generator','catalog','feature_readiness'] as const;
export type StudioPanel = typeof STUDIO_PANELS[number];
/** Single metadata source for the UI shell and AI-authored panel integrations. */
export const STUDIO_PANEL_TITLES: Record<StudioPanel,string> = {
  hierarchy: 'Hierarchy', assets: 'Assets', scene: 'Scene', game: 'Game',
  materials: 'Materials', animation: 'Animation', facial_rig: 'Facial Rig', inspector: 'Inspector', console: 'Console', lighting: 'Lighting', shader_graph: 'Shader Graph', project: 'Project',
  runtime: "Runtime",
  nereid_flow: "NEREID Water",
  rules: "Rules",
  human_appearance: "VIVID Appearance",
  build: "Build",
  wind: "Ветер",
  history: "History",
  terrain: "Terrain",
  water: "Water",
  roads: "Roads",
  tiles: "Tiles",
  fog: "Fog of War",
  generate: "Generate",
  postfx: "Post FX",
  effects: "Effects",
  lightbake: "Light Bake",
  models: "Models",
  scripts: "Scripts",
  input: "Input",
  overlays: "Overlays",
  branding: "Branding",
  asset_audit: "Asset Audit",
  agent: "Agent",
  stats: "Statistics",
  snapshot_diff: "Snapshot Diff",
  frame_capture: "Frame Capture",
  texture_resources: "Texture Resources",
  extensions: "Extensions",
  attachments: "Attachments",
  fields: "Fields",
  physis: "PHYSIS",
  actor: "Actors & Cameras",
  raid: "Raid",
  balance: "Balance",
  fx_graph: "SpectraFX Graph",
  fx_functions: "SpectraFX Functions",
  modeling: "Modeling",
  anim_assets: "Animation Clips",
  anim_graph: "Animation Graph",
  anim_rig: "Rig",
  model_anim: "Model Animation",
  project_import: "Import Project",
  human_creator: "Character Creator",
  terraweave_generator: "TerraWeave Generator",
  catalog: "Catalog",
  feature_readiness: "Feature Readiness"
};
export const STUDIO_PANEL_HOME: Record<StudioPanel,string> = {
  hierarchy: 'dock-left', assets: 'dock-bottom', scene: 'dock-center', game: 'dock-center',
  materials: 'dock-center', animation: 'dock-center', facial_rig: 'dock-center', inspector: 'dock-right', console: 'dock-bottom', lighting: 'dock-right', shader_graph: 'dock-center', project: 'dock-left',
  runtime: "dock-right",
  nereid_flow: "dock-right",
  rules: "dock-center",
  human_appearance: "dock-right",
  build: "dock-center",
  wind: "dock-right",
  history: "dock-right",
  terrain: "dock-right",
  water: "dock-right",
  roads: "dock-right",
  tiles: "dock-right",
  fog: "dock-right",
  generate: "dock-right",
  postfx: "dock-right",
  effects: "dock-right",
  lightbake: "dock-right",
  models: "dock-right",
  scripts: "dock-center",
  input: "dock-right",
  overlays: "dock-right",
  branding: "dock-right",
  asset_audit: "dock-right",
  agent: "dock-bottom",
  stats: "dock-right",
  snapshot_diff: "dock-center",
  frame_capture: "dock-bottom",
  texture_resources: "dock-bottom",
  extensions: "dock-right",
  attachments: "dock-right",
  fields: "dock-right",
  physis: "dock-right",
  actor: "dock-right",
  raid: "dock-right",
  balance: "dock-right",
  fx_graph: "dock-right",
  fx_functions: "dock-right",
  modeling: "dock-right",
  anim_assets: "dock-right",
  anim_graph: "dock-right",
  anim_rig: "dock-right",
  model_anim: "dock-right",
  project_import: "dock-right",
  human_creator: "dock-right",
  terraweave_generator: "dock-right",
  catalog: "dock-right",
  feature_readiness: "dock-center"
};

export type DockAxis = 'horizontal' | 'vertical';
export type DockEdge = 'left' | 'right' | 'top' | 'bottom';
export interface DockStack { kind: 'stack'; id: string; tabs: StudioPanel[]; active: StudioPanel | null }
export interface DockSplit { kind: 'split'; id: string; axis: DockAxis; ratio: number; first: DockNode; second: DockNode }
export type DockNode = DockStack | DockSplit;
export interface DockTree { version: 3; root: DockNode }
export interface PreviousDockLayout { areas: Record<string, unknown>; active?: Record<string, unknown>; leftSize?: number; rightSize?: number; bottomSize?: number }
const ALL = new Set<string>(STUDIO_PANELS);
const STORAGE_KEY = 'aiverse.studio.dock.v3';
const PREVIOUS_KEY = 'aiverse.studio.dock.v2';
const MIN_RATIO = .12, MAX_RATIO = .88;
const MAX_DEPTH = 8, MAX_NODES = 48;
function known(value: unknown): value is StudioPanel { return typeof value === 'string' && ALL.has(value); }
function object(value: unknown): Record<string, unknown> | null {
  return value !== null && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : null;
}
function size(value: unknown, fallback: number, min: number, max: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? Math.min(max,Math.max(min,value)) : fallback;
}
function stack(id: string, tabs: StudioPanel[], active: StudioPanel | null = tabs[0] ?? null): DockStack {
  return {kind:'stack',id,tabs,active};
}
function split(id: string, axis: DockAxis, ratio: number, first: DockNode, second: DockNode): DockSplit {
  return {kind:'split',id,axis,ratio,first,second};
}
export function defaultDockTree(): DockTree {
  return { version:3, root:split('root','vertical',.76,
    split('split-body-left','horizontal',.19,
      stack('dock-left',['hierarchy','project'],'hierarchy'),
      split('split-body-right','horizontal',.72,
        stack('dock-center',['scene','game','materials','animation','facial_rig','shader_graph','rules','build','scripts','snapshot_diff','feature_readiness'],'scene'),
        stack('dock-right',['inspector','human_creator','lighting','runtime','nereid_flow','human_appearance','wind','history','terrain','water','roads','tiles','fog','generate','postfx','effects','lightbake','models','input','overlays','branding','asset_audit','fields','physis','actor','raid','balance','fx_graph','fx_functions','modeling','anim_assets','anim_graph','anim_rig','model_anim','project_import','stats','extensions','attachments','terraweave_generator','catalog'],'inspector'))),
    stack('dock-bottom',['assets','console','agent','frame_capture','texture_resources'],'assets')) };
}
function traverse(node: DockNode, visitor: (n: DockNode)=>void): void {
  visitor(node);
  if(node.kind==='split') { traverse(node.first,visitor); traverse(node.second,visitor); }
}
/** Collapse empty source docks so the adjacent panel immediately fills the space.
 * Preserve one empty center stack when all windows are closed.
 */
function compactDockNode(node: DockNode): DockNode | null {
  if(node.kind==='stack')return node.tabs.length>0?node:null;
  const a=compactDockNode(node.first),b=compactDockNode(node.second);
  if(!a)return b;
  if(!b)return a;
  return {...node,first:a,second:b};
}
function compactDockTree(tree: DockTree): DockTree {
  return {version:3,root:compactDockNode(tree.root)??stack('dock-center',[],null)};
}
export function dockStacks(tree: DockTree): DockStack[] {
  const results: DockStack[]=[];
  traverse(tree.root,node=>{ if(node.kind==='stack')results.push(node); });
  return results;
}
export function findDockStack(tree: DockTree, id: string): DockStack | null {
  return dockStacks(tree).find(s=>s.id===id) ?? null;
}
export function panelStack(tree: DockTree, panel: StudioPanel): string | null {
  return dockStacks(tree).find(s=>s.tabs.includes(panel))?.id ?? null;
}
export function normalizeDockTree(input: unknown): DockTree {
  const source=object(input);
  if(!source || !object(source.root))return defaultDockTree();
  let count=0;
  const seenIds=new Set<string>(), seenPanels=new Set<StudioPanel>();
  function visit(inputNode: unknown, depth: number): DockNode | null {
    const value=object(inputNode);
    if(!value || depth>MAX_DEPTH || ++count>MAX_NODES) return null;
    const rawId=typeof value.id==='string' && /^[a-z0-9_-]{1,60}$/i.test(value.id) ? value.id : "node-"+count;
    const id=seenIds.has(rawId)?"node-"+count+"-"+depth:rawId;
    seenIds.add(id);
    if(value.kind==='stack') {
      const tabs: StudioPanel[]=[];
      if(Array.isArray(value.tabs))for(const tab of value.tabs) {
        if(known(tab) && !seenPanels.has(tab)) { tabs.push(tab);seenPanels.add(tab); }
      }
      return stack(id,tabs,known(value.active) && tabs.includes(value.active) ? value.active : tabs[0] ?? null);
    }
    if(value.kind==='split') {
      const axis: DockAxis=value.axis==='vertical'?'vertical':'horizontal';
      const first=visit(value.first,depth+1),second=visit(value.second,depth+1);
      if(!first || !second)return null;
      return split(id,axis,size(value.ratio,.5,MIN_RATIO,MAX_RATIO),first,second);
    }
    return null;
  }
  const root=visit(source.root,0);
  return root ? compactDockTree({version:3,root}) : defaultDockTree();
}
function update(node: DockNode, id: string, change: (node: DockNode)=>DockNode): DockNode {
  if(node.id===id)return change(node);
  return node.kind==='split'?{...node,first:update(node.first,id,change),second:update(node.second,id,change)}:node;
}
function remove(node: DockNode, panel: StudioPanel): DockNode {
  if(node.kind==='split')return {...node,first:remove(node.first,panel),second:remove(node.second,panel)};
  if(!node.tabs.includes(panel))return node;
  const tabs=node.tabs.filter(x=>x!==panel);
  return {...node,tabs,active:node.active===panel?tabs[0]??null:node.active};
}
function newId(tree: DockTree, prefix: string): string {
  const ids=new Set<string>();traverse(tree.root,n=>ids.add(n.id));
  for(let i=1;i<=MAX_NODES+1;i++)if(!ids.has(prefix+"-"+i))return prefix+"-"+i;
  return prefix+"-"+(MAX_NODES+2);
}
export function moveTab(tree: DockTree, panel: StudioPanel, stackId: string, before?: StudioPanel): DockTree {
  const t=normalizeDockTree(tree);
  if(!known(panel) || !findDockStack(t,stackId))return t;
  const without=remove(t.root,panel);
  const root=update(without,stackId,n=>{
    if(n.kind!=='stack')return n;
    const tabs=[...n.tabs];
    const at=before?tabs.indexOf(before):-1;
    if(at<0)tabs.push(panel);else tabs.splice(at,0,panel);
    return {...n,tabs,active:panel};
  });
  return compactDockTree({version:3,root});
}
export function closeTab(tree: DockTree, panel: StudioPanel): DockTree {
  const t=normalizeDockTree(tree);
  return compactDockTree({version:3,root:remove(t.root,panel)});
}
export function activateTab(tree: DockTree, stackId: string, panel: StudioPanel): DockTree {
  const t=normalizeDockTree(tree);
  return {version:3,root:update(t.root,stackId,n=>n.kind==='stack' && n.tabs.includes(panel)?{...n,active:panel}:n)};
}
/** Reconstruct preferred regions pruned when their last tab moved away. */
export function reopenTab(tree: DockTree, panel: StudioPanel, preferredStackId=STUDIO_PANEL_HOME[panel]): DockTree {
  const t=normalizeDockTree(tree);
  if(!known(panel))return t;
  const present=panelStack(t,panel);
  if(present)return activateTab(t,present,panel);
  const existing=findDockStack(t,preferredStackId);
  if(existing)return moveTab(t,panel,existing.id);

  const panes=dockStacks(t);
  const anchor=findDockStack(t,'dock-center')??panes[0];
  if(!anchor)return defaultDockTree();
  if(panes.length>=24)return moveTab(t,panel,anchor.id);
  const fresh=stack(preferredStackId,[panel],panel);
  const splitId=newId(t,'split');
  if(preferredStackId==='dock-bottom')
    return normalizeDockTree({version:3,root:split(splitId,'vertical',.76,t.root,fresh)});
  const early=preferredStackId==='dock-left';
  const ratio=early?.19:.72;
  return normalizeDockTree({version:3,root:update(t.root,anchor.id,n=>
    split(splitId,'horizontal',ratio,early?fresh:n,early?n:fresh))});
}
export function resizeDockSplit(tree: DockTree, id: string, ratio: number): DockTree {
  const t=normalizeDockTree(tree);
  if(!Number.isFinite(ratio))return t;
  return {version:3,root:update(t.root,id,n=>n.kind==='split'?{...n,ratio:size(ratio,.5,MIN_RATIO,MAX_RATIO)}:n)};
}
/** Dock a panel against the whole workspace, so a bottom strip can become a full-height column. */
export function splitDockRoot(tree: DockTree, panel: StudioPanel, side: DockEdge): DockTree {
  const t=normalizeDockTree(tree);
  if(!known(panel) || !panelStack(t,panel))return t;
  if(t.root.kind==='stack' && t.root.tabs.length===1 && t.root.tabs[0]===panel)return t;
  if(dockStacks(t).length>=24)return t;
  const splitId=newId(t,'split'), stackIdNew=newId(t,'dock');
  const next=compactDockTree({version:3,root:remove(t.root,panel)});
  const axis:DockAxis=side==='left'||side==='right'?'horizontal':'vertical';
  const early=side==='left'||side==='top';
  const portion=axis==='horizontal'?.22:.28;
  const fresh=stack(stackIdNew,[panel],panel);
  return normalizeDockTree({version:3,root:split(splitId,axis,early?portion:1-portion,early?fresh:next.root,early?next.root:fresh)});
}
export function splitDockStack(tree: DockTree, panel: StudioPanel, stackId: string, side: DockEdge): DockTree {
  const t=normalizeDockTree(tree),target=findDockStack(t,stackId);
  if(!target || !known(panel))return t;
  const from=panelStack(t,panel);
  if(from===stackId && target.tabs.length===1)return t;
  if(dockStacks(t).length>=24)return t;
  const next=remove(t.root,panel);
  const splitId=newId(t,'split'), stackIdNew=newId(t,'dock');
  const axis:DockAxis=side==='left'||side==='right'?'horizontal':'vertical';
  const early=side==='left'||side==='top';
  const root=update(next,stackId,n=>{
    if(n.kind!=='stack')return n;
    const fresh=stack(stackIdNew,[panel],panel);
    return split(splitId,axis,.5,early?fresh:n,early?n:fresh);
  });
  return normalizeDockTree({version:3,root});
}
export function migrateDockV2(input: unknown): DockTree {
  const v=object(input),areas=object(v?.areas),active=object(v?.active);
  if(!areas)return defaultDockTree();
  const baseline=defaultDockTree();
  const byArea:Record<string,string>={left:'dock-left',center:'dock-center',right:'dock-right',bottom:'dock-bottom'};
  let root=baseline.root;
  const taken=new Set<StudioPanel>();
  for(const [area,stackId] of Object.entries(byArea)) {
    const raw=areas[area]; const tabs:StudioPanel[]=[];
    if(Array.isArray(raw))for(const x of raw)if(known(x)&&!taken.has(x)) {tabs.push(x);taken.add(x);}
    root=update(root,stackId,n=>n.kind==='stack'?{...n,tabs,active:known(active?.[area]) && tabs.includes(active[area] as StudioPanel)?active[area] as StudioPanel:tabs[0]??null}:n);
  }
  const left=size(v?.leftSize,260,180,600),right=size(v?.rightSize,300,180,600),bottom=size(v?.bottomSize,180,110,480);
  const top=update(root,'root',n=>n.kind==='split'?{...n,ratio:.12+(.88-.12)*Math.max(0,1-bottom/800)}:n);
  const withLeft=update(top,'split-body-left',n=>n.kind==='split'?{...n,ratio:size(left/1200,.21,.12,.88)}:n);
  const withRight=update(withLeft,'split-body-right',n=>n.kind==='split'?{...n,ratio:size(1-right/1000,.77,.12,.88)}:n);
  return normalizeDockTree({version:3,root:withRight});
}
// MetaHuman-style character studio: the editor wide on the left, the Scene
// viewport as the main stage, a narrow right column, no bottom dock.
export const DOCK_PRESET_EVENT='aiverse:dock-preset';
export function characterDockTree(): DockTree {
  return { version:3, root:split('root','horizontal',.36,
    stack('dock-left',['human_creator','hierarchy','project'],'human_creator'),
    split('split-body-right','horizontal',.8,
      stack('dock-center',['scene','game','materials','animation','facial_rig','shader_graph'],'scene'),
      stack('dock-right',['inspector','lighting','runtime','nereid_flow','human_appearance'],'lighting'))) };
}
// The character editor previews in the Scene viewport, which is not drawn
// while another tab covers it: never keep the editor in the Scene stack.
export function moveCharacterEditorBesideScene(tree: DockTree): DockTree {
  return panelStack(tree,'human_creator')==='dock-center' && findDockStack(tree,'dock-right')
    ? moveTab(tree,'human_creator','dock-right') : tree;
}
export function readNestedDock(): DockTree {
  if(typeof window==='undefined')return defaultDockTree();
  try{
    const stored=window.localStorage.getItem(STORAGE_KEY);
    if(stored!==null)return moveCharacterEditorBesideScene(normalizeDockTree(JSON.parse(stored) as unknown));
    const previous=window.localStorage.getItem(PREVIOUS_KEY);
    return previous!==null?migrateDockV2(JSON.parse(previous) as unknown):defaultDockTree();
  }catch{return defaultDockTree();}
}
export function saveNestedDock(tree: DockTree): void {
  if(typeof window==='undefined')return;
  try{window.localStorage.setItem(STORAGE_KEY,JSON.stringify(normalizeDockTree(tree)));}catch{}
}
