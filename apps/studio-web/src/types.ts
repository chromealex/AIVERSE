export interface NereidFlowSurface {id:number;kind?:string;shape?:string;flow?:number[];grade?:{automaticCurrent?:boolean;slopeDegrees?:number;downhillHeadingDegrees?:number;responseSeconds?:number;maxSpeed?:number;origin?:number[]}}
export type WindDebugView = 'off'|'parts'|'depth'|'weight'|'skeleton';
export interface WindSettings {enabled:boolean;speed:number;direction:number;gust:number;scale:number;debug:WindDebugView}
export interface WindMask {name:string;index:number;samples:number[]}
export interface WindProfileLevel {mask:string;stiffness:number;flex:number}
export interface WindState {settings:WindSettings;library:{masks:WindMask[];profiles:Array<{name:string;levels:WindProfileLevel[]}>;uniqueMasks:number;warnings:string[]};scene:{riggedInstances?:number;riggedParts?:number;models?:number}}
export interface NereidFlowState {surfaces:NereidFlowSurface[];computed?:Array<{id:number;flow:number[]}>}
export interface NereidFlowInput {id:number;mode:'automatic'|'manual';slopeDegrees:number;downhillHeadingDegrees:number;responseSeconds:number;maxSpeed:number;flow:number[]}
export type { PropertyType } from './propertyLogic';
export type { FacialRigDocument, FacialRigList, FacialRigResult, FacialRigValidation } from './panels/facialRigSchema';

// Native asset GUID and staged facial inputs; NOT a GPU preview result.
export interface StudioFacialPreviewBinding {
  bound: boolean;
  clips?: string[]; // animation clips of the shown model
  baseHeight?: number; // bind-pose height of the shown model, metres
  ruler?: boolean;
  compare?: boolean; tryOn?: boolean;
  guid: string;
  path: string; // content-relative path, resolved by native AssetCatalog
  revision: number; // binding epoch, must match control updates
  values: Record<string,number>;
  vectors: Record<string,[number,number,number]>;
  runtimeReady: boolean; // set only after an actual native Metal Renderer3D frame
  runtimeVerified: false; // GPU pixels/backends have not passed native QA
  rendererChannelCapacity: number;
  reason: string;
}

export type StudioProperty = import('./propertyLogic').FieldDescriptor;
export interface StudioInspectorComponent { id: string; title: string; note?: string; properties: StudioProperty[] }
/** Entity header + its document components (Transform, Mesh Renderer, Light…). */
/** Add Component entry: `native` adds via studio.componentAdd; `light`/`water` run an editor action. */
export interface StudioComponentCatalogItem { id: string; title: string; group: string; hint: string; action: 'native' | 'light' | 'water'; available: boolean; reason?: string }
/** Unit types of rules.json: as written (`types`) and with "extends" applied (`resolved`). */
export interface StudioRules { types: Record<string, unknown>; resolved: Record<string, unknown>; resources: string[]; summary: unknown[]; dirty: boolean }
export interface StudioInspector { id: string; name: string; kind: string; nameEditable?: boolean; components: StudioInspectorComponent[] }
export interface StudioEntity { id: string; name: string; kind: string; parentId?: string; visible?: boolean; locked?: boolean; owner?: number }
export interface StudioLog { level: 'info' | 'warning' | 'error'; text: string; time: string }
export interface StudioAsset {
  path: string;
  bytes: number;
  guid?: string;
  importer?: string;
}
export interface StudioAssetList {
  files: StudioAsset[];
  folders?: StudioAsset[];
  root?: 'content';
}
export interface StudioAssetTransferItem {
  id: number; name: string; from: string; to: string; directory: boolean;
  conflict: string; guid: string | null;
}
export interface StudioAssetTransferPlan {
  token: string; source: 'internal' | 'external'; items: StudioAssetTransferItem[];
  references?: Array<{ id: number; document: string; owner: string; field: string;
    from: string; to: string; writable: boolean }>;
}
export type StudioAssetConflictAction =
  'import' | 'skip' | 'keep-both' | 'replace' | 'new-guid';
export interface StudioAssetTransferResult {
  files: string[]; issues: Array<{name: string; message: string}>;
  repairedMaterialReferences?: number;
}
export interface StudioAssetReference {
  document: string; owner: string; field: string; path: string;
  guid: string; writable: boolean;
}
export interface StudioAssetDetail extends StudioAsset {
  importer: string; guid: string; directory: boolean; settings: Record<string,unknown>;
}
export type StudioLightingState = import('./panels/lightingSchema').LightingState;
export type StudioMaterialLibrary = import('./panels/materialSchema').MaterialLibrary;
export interface StudioShaderGraphList { graphs: string[] }
export interface StudioShaderGraphOp { id:string; inputs:number; domains:string[] }
export interface StudioShaderGraphCatalog { nodes:StudioShaderGraphOp[] }
export interface StudioShaderGraphDocument {
  graph: { version?: number; domain?: string; nodes?: Array<{id:string;op:string;inputs?:string[];position?:number[];value?:number[]}>; outputs?:Record<string,string> };
  nodes: number; samples: number;
}
export interface StudioSnapshot {
  project: { name: string; path: string; dirty: boolean };
  scene: { name: string; entities: StudioEntity[] };
  selectedId: string | null;
  /** Whole selection, primary (selectedId) last. */
  selectedIds?: string[];
  properties: StudioProperty[];
  entityProperties?: StudioProperty[];
  components?: Array<{ id: string; title: string; properties: StudioProperty[] }>;
  inspector?: StudioInspector;
  logs: StudioLog[];
  mode: 'edit' | 'play';
  previewMaterial?: string | null;
  history: { canUndo: boolean; canRedo: boolean };
}
export interface RenderLightingSettings { version: 1; indirect: 'off'|'cpu'|'auto'|'gpu'; direct: 'legacy'|'luma'; quality: 'balanced'|'high'|'ultra'; probes: number; layers: number; spacing: number; samples: number; distance: number; cpuBudgetMs: number }
export interface StudioEntityCatalog { types: Array<{id: string; name: string}>; mapEntities: boolean }
export interface StudioProjectState { maps: Array<{file: string; title: string}>; currentMap: string; canCreateMap: boolean; maxMapSize: number }
export interface StudioRuntimeRow {ref:string; name:string; kind:string; owner:number; parent:string; health:number; components:string[]}
export interface StudioRuntimePage {active:boolean; epoch:string; tick:number; rows:StudioRuntimeRow[]; total:number}
export interface StudioRuntimeEntity {ref:string; epoch:string; tick:number; components:Record<string,unknown>; stats?:Record<string,unknown>}
export interface StudioGameDeviceChrome { corner:number; cutout?:{x:number;y:number;width:number;height:number;radius:number}; insets:{left:number;top:number;right:number;bottom:number} }
export interface StudioGameDevice { id:string; landscape:boolean; safeArea:boolean; width:number; height:number; touch:boolean; chrome?:StudioGameDeviceChrome }
export interface StudioGameDeviceOption { id:string; name:string; group:string }
export interface StudioGameState { device:StudioGameDevice; paused: boolean; canControl: boolean; canStep: boolean; active: boolean; loading: boolean; progress: number; inMatch: boolean; tick: number; error: string }
export type StudioMethod = 'studio.renderLighting' | 'studio.renderLightingSet' | 'studio.nereidFlow' | 'studio.nereidFlowSet' | 'studio.gameEntities' | 'studio.gameInspect' | 'studio.gameDevices' | 'studio.gameDevice' | 'studio.gameControl' | 'studio.gameStart' | 'studio.gameStop' | 'studio.gameState' | 'studio.project' | 'studio.newMap' | 'studio.openMap' | 'studio.saveAs' | 'studio.entityCatalog' | 'studio.entityCreate' | 'studio.entityDelete' | 'studio.entityDuplicate' | 'studio.snapshot' | 'studio.propertiesFor' | 'studio.inspect' | 'studio.componentCatalog' | 'studio.componentAdd' | 'studio.componentRemove' | 'studio.setProperties' | 'studio.entitiesDelete' | 'studio.entitiesDuplicate' | 'studio.entityFlags' | 'studio.assetPlace' | 'studio.rules' | 'studio.rulesSet' | 'studio.windowOpen' | 'studio.windowClose' | 'studio.windowBroadcast' | 'studio.windowTitle' | 'studio.buildInfo' | 'studio.buildToolchain' | 'studio.buildStart' | 'studio.buildLog' | 'studio.buildEstimate' | 'studio.buildReveal' | 'studio.entityMove' | 'studio.select' | 'studio.setProperty' | 'studio.save' | 'studio.undo' | 'studio.redo' | 'studio.viewportRect' | 'studio.viewportInput' | 'studio.openProject' | 'studio.assets' | 'studio.assetByGuid' | 'studio.assetReferences' | 'studio.assetMetaPatch' | 'studio.assetFolderCreate' | 'studio.assetMovePreview' | 'studio.assetImportPreview' | 'studio.assetTransferCommit' | 'studio.lighting' | 'studio.lightingSet' | 'studio.lightAdd' | 'studio.lightUpdate' | 'studio.lightDelete' | 'studio.materials' | 'studio.materialPatch' | 'studio.materialCreate' | 'studio.materialPreview' | 'studio.shaderGraphs' | 'studio.shaderGraphGet' | 'studio.shaderGraphValidate' | 'studio.shaderGraphSave' | 'studio.shaderGraphCatalog' | 'studio.facialRigList' | 'studio.facialRigGet' | 'studio.facialRigValidate' | 'studio.facialRigSave' | 'studio.facialPreviewBind' | 'studio.facialPreviewControls' | 'studio.facialPreviewInfo' | 'studio.humanAppearanceInfo' | 'studio.humanAppearanceApply' | 'studio.humanAppearanceSave' | 'studio.humanAppearanceLoad' | 'studio.humanAppearanceList' | 'studio.humanAppearanceFromAsset' | 'studio.humanWardrobeFromAsset' | 'studio.humanPreviewAnimation' | 'studio.humanStudioLighting' | 'studio.humanFaceFitScan' | 'studio.humanHeadBodyImport' | 'studio.humanWardrobeInfo' | 'studio.humanCharacterBuild' | 'studio.humanCharacterBuildStatus' | 'studio.wind' | 'studio.windSet' | 'studio.humanAppearanceThumbnails' | 'studio.humanLookCapture' | 'studio.humanCamera' | 'studio.humanCompare' | 'studio.humanRuler' | 'studio.humanClipList' | 'studio.humanClipAdd' | 'studio.humanClipRemove' | 'studio.humanMakeupAlign' | 'studio.humanTryOn' | 'studio.humanPlaceInGame' | 'studio.humanPackageList' | 'studio.humanPackageExport' | 'studio.humanPackageImport' | 'studio.humanFacePick' | 'studio.humanSculpt' | 'studio.humanSculptStroke' | 'studio.humanSculptUndo' | 'studio.humanCreatorInfo' | 'studio.humanCreatorSet' | 'studio.humanCreatorPreset' | 'studio.humanCreatorUndo' | 'studio.humanCreatorRedo' | 'studio.humanCreatorRoute' | 'studio.humanCreatorRig' | 'studio.humanCreatorSkin' | 'studio.humanCreatorNew' | 'studio.humanCreatorSave' | 'studio.humanCreatorLoad' | 'studio.humanCreatorList' | 'studio.humanCreatorPreview';
export type HumanCreatorFieldKind = 'number' | 'integer' | 'string' | 'color' | 'bone' | 'bones';
export interface HumanCreatorField { key:string; label:string; kind:HumanCreatorFieldKind; min?:number; max?:number; step?:number }
export interface HumanCreatorSection { id:string; title:string; hint:string; fields:HumanCreatorField[] }
export interface HumanCreatorRoute { channel:string; target:string; gain:number }
export interface HumanCreatorDocument { version:1; dna:Record<string,any>; rigCorrections:{version:1;offsets:{node:string;translation:[number,number,number]}[]}; skinWeights:{version:1;edits:{vertex:number;joint:string;weight:number}[]}|null; routes:HumanCreatorRoute[] }
export interface HumanCreatorInfo { revision?:number; schema?:HumanCreatorSection[]; document:HumanCreatorDocument|{dna:Record<string,any>}; presets?:string[];
  canUndo?:boolean; canRedo?:boolean; rigCanUndo?:boolean; rigCanRedo?:boolean; skinCanUndo?:boolean; skinCanRedo?:boolean; dirty?:boolean;
  catalog?:{nodes:string[];targets:string[];channels:string[];vertices:number;error:string};
  preview?:StudioFacialPreviewBinding & {creator?:boolean} }
export interface HumanSculptState { armed:boolean; radius:number; strength:number; mode:'push'|'pull'; symmetry:boolean; strokes:number; preview:StudioFacialPreviewBinding }
export type HumanCreatorUndoTarget = 'dna' | 'rig' | 'skin';
export interface HumanLook { version: 1; values: Record<string,number>; materials: Record<string,string> }
export interface HumanLookField { key:string; label:string; min:number; max:number; initial:number }
export interface HumanPreviewAnimationSettings {pose:string;playing:boolean;faceIdle:boolean;speed:number;automaticLod?:boolean;expression?:string;clip?:string}
export interface HumanStudioLightingSettings {preset:string;rotation:number;gain:number;backdrop:boolean}
export interface HumanStudioLightingInfo {settings:HumanStudioLightingSettings;presets:{id:string;label:string;sources:number}[]}
export interface HumanFaceShape {version:1;meshes:string[];values:Record<string,number>;landmarks:{from:[number,number,number];to:[number,number,number];radius:number;weight:number}[]}
export interface HumanLookInfo {face:HumanFaceShape|null;faceSchema:{key:string;label:string;min:number;max:number}[];schema:HumanLookField[];look:HumanLook;preview:StudioFacialPreviewBinding}
export interface StudioParams {
  'studio.wind':Record<string,never>;
  'studio.windSet':Partial<WindSettings>;
  'studio.nereidFlow':Record<string,never>;
  'studio.nereidFlowSet':NereidFlowInput;
  'studio.gameStart': {entry: 'map' | 'menu'};
  'studio.gameStop': Record<string,never>;
  'studio.gameState': Record<string,never>;
  'studio.gameEntities': {offset:number;limit:number;query:string};
  'studio.gameInspect': {epoch:string;ref:string};
  'studio.gameDevices': Record<string,never>;
  'studio.gameDevice': Pick<StudioGameDevice,'id'|'landscape'|'safeArea'>;
  'studio.gameControl': {action:'pause'|'resume'|'step'|'restart'};
  'studio.project': Record<string, never>;
  'studio.newMap': {width: number; height: number; name: string};
  'studio.openMap': {file: string};
  'studio.saveAs': Record<string, never>;
  'studio.entityCatalog': Record<string, never>;
  'studio.entityCreate': {type: string; x: number; z: number; owner: number};
  'studio.entityDelete': {entityId: string};
  'studio.entityDuplicate': {entityId: string};
  'studio.humanWardrobeFromAsset': {guid:string};
  'studio.humanPreviewAnimation': {settings?:HumanPreviewAnimationSettings};
  'studio.humanStudioLighting': {settings?:HumanStudioLightingSettings};
  'studio.humanFaceFitScan': {guid:string;scanGuid:string;face:HumanFaceShape;distance:number;strength:number};
  'studio.humanHeadBodyImport': {guid:string;headGuid:string;bone:string;hiddenMeshes:string[];name:string};
  'studio.humanWardrobeInfo': {guid:string};
  'studio.humanCharacterBuild': {guid:string;look:HumanLook;wardrobe:HumanWardrobe;name:string;textureSize:number;face?:HumanFaceShape|null};
  'studio.humanCharacterBuildStatus': Record<string,never>;
  'studio.humanAppearanceInfo': Record<string,never>;
  'studio.humanCamera': { view:'face'|'torso'|'full' };
  'studio.humanCompare': { original:boolean };
  'studio.humanRuler': { enabled:boolean };
  'studio.humanMakeupAlign': Record<string,never>;
  'studio.humanClipList': Record<string,never>;
  'studio.humanClipAdd': { file:string; name:string; clip?:string };
  'studio.humanClipRemove': { name:string };
  'studio.humanTryOn': { wardrobe:HumanWardrobe|null };
  'studio.humanPlaceInGame': { type:string; path:string; name?:string };
  'studio.humanPackageList': Record<string,never>;
  'studio.humanPackageExport': { look:string; character?:string };
  'studio.humanPackageImport': { name:string };
  'studio.humanFacePick': { enabled:boolean };
  'studio.humanSculpt': { enabled?:boolean; radius?:number; strength?:number; mode?:'push'|'pull'; symmetry?:boolean };
  'studio.humanSculptStroke': { point:[number,number,number]; normal:[number,number,number] };
  'studio.humanSculptUndo': { all?:boolean };
  'studio.humanCreatorInfo': Record<string,never>;
  'studio.humanCreatorSet': { key:string; value:unknown };
  'studio.humanCreatorPreset': { id:string };
  'studio.humanCreatorUndo': { target:HumanCreatorUndoTarget };
  'studio.humanCreatorRedo': { target:HumanCreatorUndoTarget };
  'studio.humanCreatorRoute': { index:number; channel?:string; target?:string; gain?:number; remove?:boolean };
  'studio.humanCreatorRig': { node:string; translation:[number,number,number] };
  'studio.humanCreatorSkin': { vertex:number; joint?:string; weight?:number; erase?:boolean };
  'studio.humanCreatorNew': Record<string,never>;
  'studio.humanCreatorSave': { name:string };
  'studio.humanCreatorLoad': { name:string };
  'studio.humanCreatorList': Record<string,never>;
  'studio.humanCreatorPreview': { enabled:boolean };
  'studio.humanAppearanceApply': {guid:string;revision:number;look:HumanLook;face?:HumanFaceShape|null};
  'studio.humanAppearanceSave': {name:string;look:HumanLook};
  'studio.humanAppearanceLoad': {name:string};
  'studio.humanAppearanceList': Record<string,never>;
  'studio.humanAppearanceThumbnails': Record<string,never>;
  'studio.humanLookCapture': { name:string };
  'studio.humanAppearanceFromAsset': {guid:string};
  'studio.snapshot': Record<string, never>;
  'studio.propertiesFor': { entityId: string };
  'studio.inspect': { entityId: string };
  'studio.entityMove': { entityId: string; targetId?: string | null; placement?: 'before' | 'after' };
  'studio.componentCatalog': { entityId: string };
  'studio.componentAdd': { entityId: string; componentId: string };
  'studio.componentRemove': { entityId: string; componentId: string };
  'studio.setProperties': { entityId?: string; entityIds?: string[]; componentId?: string; values: Record<string, string | number | boolean> };
  'studio.entitiesDelete': { entityIds: string[] };
  'studio.entitiesDuplicate': { entityIds: string[] };
  'studio.entityFlags': { entityIds: string[]; hidden?: boolean; locked?: boolean };
  'studio.assetPlace': { asset: string; x: number; z: number };
  'studio.rules': Record<string, never>;
  'studio.rulesSet': { path: string; value: unknown };
  /** Open (or focus) floating window `window` holding `panels`; ifOutside opens only when the mouse is outside every Studio window. */
  'studio.windowOpen': { window: string; panels?: string[]; title?: string; ifOutside?: boolean; x?: number; y?: number; width?: number; height?: number };
  'studio.windowClose': { window: string };
  /** A floating window names itself (its active tab). */
  'studio.windowTitle': { title: string };
  'studio.buildInfo': Record<string,never>;
  'studio.buildToolchain': { target: string };
  'studio.buildStart': { target: string; config: 'Release' | 'Debug'; installMissing?: boolean; estimate?: boolean };
  'studio.buildLog': { from: number };
  'studio.buildEstimate': { target: string };
  /** Show a build folder (or the last build log) in Finder. */
  'studio.buildReveal': { target?: string; log?: boolean };
  /** UI-only event (name starts with "ui.") delivered to every Studio window. */
  'studio.windowBroadcast': { event: string; payload: unknown };
  'studio.save': Record<string, never>;
  'studio.undo': Record<string, never>;
  'studio.redo': Record<string, never>;
  'studio.openProject': Record<string, never>;
  'studio.assets': Record<string, never>;
  'studio.assetByGuid': { guid: string };
  'studio.assetReferences': Record<string, never>;
  'studio.assetMetaPatch': {guid:string;key:string;value:string|number|boolean};
  'studio.assetFolderCreate': { parent: string; name: string };
  'studio.assetMovePreview': { paths: string[]; destination: string };
  'studio.assetImportPreview': { destination: string };
  'studio.assetTransferCommit': { token: string; decisions: Array<{id:number;action:StudioAssetConflictAction}> };
  'studio.materials': Record<string, never>;
  'studio.materialPatch': { name: string; patch: import('./panels/materialSchema').MaterialPatch };
  'studio.materialCreate': { name: string; preset: import('./panels/materialSchema').MaterialPreset };
  'studio.materialPreview': { name: string };
  'studio.shaderGraphs': Record<string, never>;
  'studio.shaderGraphGet': { name: string };
  'studio.shaderGraphCatalog': Record<string, never>;
  'studio.facialRigList': Record<string, never>;
  'studio.facialRigGet': { name: string };
  'studio.facialRigValidate': { rig: import('./panels/facialRigSchema').FacialRigDocument };
  'studio.facialRigSave': { name: string; rig: import('./panels/facialRigSchema').FacialRigDocument };
  'studio.facialPreviewBind': { guid: string; rig: import('./panels/facialRigSchema').FacialRigDocument } | { guid: '' };
  'studio.facialPreviewControls': { revision: number; values: Record<string,number>; vectors?: Record<string,[number,number,number]> };
  'studio.facialPreviewInfo': Record<string,never>;
  'studio.shaderGraphValidate': { graph: StudioShaderGraphDocument['graph'] };
  'studio.shaderGraphSave': { name: string; graph: StudioShaderGraphDocument['graph'] };
  'studio.renderLighting': Record<string, never>;
  'studio.renderLightingSet': { patch: Partial<RenderLightingSettings> };
  'studio.lighting': Record<string, never>;
  'studio.lightingSet': { key: import('./panels/lightingSchema').LightingScalarKey; value: number };
  'studio.lightAdd': { x?: number; z?: number };
  'studio.lightUpdate': { index: number; entityId?: string; key: import('./panels/lightingSchema').LightEditableKey; value: string | number | boolean };
  'studio.lightDelete': { index: number; entityId?: string };
  'studio.select': { id?: string | null; ids?: string[]; mode?: 'replace' | 'add' | 'toggle' };
  'studio.setProperty': { entityId: string; componentId?: string; key: string; value: string | number | boolean };
  'studio.viewportRect': { id: 'scene' | 'game'; x: number; y: number; width: number; height: number; dpr: number; visible: boolean };
  'studio.viewportInput': { kind: 'orbit' | 'pan' | 'look'; dx: number; dy: number } | { kind: 'move'; right: number; up: number; forward: number } | { kind: 'focus' | 'reset' } | { kind: 'zoom'; amount: number } | { kind: 'view'; yaw: number; pitch: number } | { kind: 'projection'; ortho: boolean } | { kind: 'hover'; id: string } | { kind: 'tool'; tool?: 'view' | 'move' | 'rotate' | 'scale'; local?: boolean; snap?: boolean; snapMove?: number; snapRotate?: number; snapScale?: number; gizmos?: boolean } | { kind: 'handle'; phase: 'down' | 'drag' | 'up'; x: number; y: number; width: number; height: number; invertSnap?: boolean } | { kind: 'pick'; x: number; y: number; width: number; height: number };
}
export interface StudioResult {
  'studio.wind':WindState;
  'studio.windSet':WindState;
  'studio.nereidFlow':NereidFlowState;
  'studio.nereidFlowSet':NereidFlowState;
  'studio.gameStart': StudioGameState;
  'studio.gameStop': StudioGameState;
  'studio.gameState': StudioGameState;
  'studio.gameEntities': StudioRuntimePage;
  'studio.gameInspect': StudioRuntimeEntity;
  'studio.gameDevices': StudioGameDeviceOption[];
  'studio.gameDevice': StudioGameState;
  'studio.gameControl': StudioGameState;
  'studio.project': StudioProjectState;
  'studio.newMap': StudioSnapshot;
  'studio.openMap': StudioSnapshot;
  'studio.saveAs': StudioSnapshot;
  'studio.entityCatalog': StudioEntityCatalog;
  'studio.entityCreate': StudioSnapshot;
  'studio.entityDelete': StudioSnapshot;
  'studio.entityDuplicate': StudioSnapshot;
  'studio.humanWardrobeFromAsset': HumanWardrobe;
  'studio.humanWardrobeInfo': {meshes:string[];bones:string[];materials:string[]};
  'studio.humanPreviewAnimation': HumanPreviewAnimationSettings;
  'studio.humanStudioLighting': HumanStudioLightingInfo;
  'studio.humanFaceFitScan': HumanBuildStatus;
  'studio.humanHeadBodyImport': HumanBuildStatus;
  'studio.humanCharacterBuild': HumanBuildStatus;
  'studio.humanCharacterBuildStatus': HumanBuildStatus;
  'studio.humanAppearanceInfo': HumanLookInfo;
  'studio.humanCamera': { view:string };
  'studio.humanCompare': StudioFacialPreviewBinding;
  'studio.humanClipList': { external:{name:string;file:string}[] };
  'studio.humanClipAdd': { external:{name:string;file:string}[] };
  'studio.humanClipRemove': { external:{name:string;file:string}[] };
  'studio.humanMakeupAlign': { values: Record<string,number> };
  'studio.humanRuler': { enabled:boolean };
  'studio.humanTryOn': StudioFacialPreviewBinding;
  'studio.humanPlaceInGame': { type:string; model:string; placed:boolean };
  'studio.humanPackageList': { packages:string[] };
  'studio.humanPackageExport': { path:string; bytes:number };
  'studio.humanPackageImport': { look:string; character?:string };
  'studio.humanFacePick': { enabled:boolean };
  'studio.humanSculpt': HumanSculptState;
  'studio.humanSculptStroke': HumanSculptState;
  'studio.humanSculptUndo': HumanSculptState;
  'studio.humanCreatorInfo': Required<HumanCreatorInfo>;
  'studio.humanCreatorSet': Required<HumanCreatorInfo>;
  'studio.humanCreatorPreset': Required<HumanCreatorInfo>;
  'studio.humanCreatorUndo': Required<HumanCreatorInfo>;
  'studio.humanCreatorRedo': Required<HumanCreatorInfo>;
  'studio.humanCreatorRoute': Required<HumanCreatorInfo>;
  'studio.humanCreatorRig': Required<HumanCreatorInfo>;
  'studio.humanCreatorSkin': Required<HumanCreatorInfo>;
  'studio.humanCreatorNew': Required<HumanCreatorInfo>;
  'studio.humanCreatorSave': Required<HumanCreatorInfo>;
  'studio.humanCreatorLoad': Required<HumanCreatorInfo>;
  'studio.humanCreatorList': { humans:string[] };
  'studio.humanCreatorPreview': Required<HumanCreatorInfo>;
  'studio.humanAppearanceApply': HumanLookInfo;
  'studio.humanAppearanceSave': {name:string;look:HumanLook};
  'studio.humanAppearanceLoad': {name:string;look:HumanLook};
  'studio.humanAppearanceList': {looks:string[]};
  'studio.humanAppearanceThumbnails': Record<string,string>;
  'studio.humanLookCapture': {ok:boolean};
  'studio.humanAppearanceFromAsset': {name:string;look:HumanLook};
  'studio.snapshot': StudioSnapshot;
  'studio.propertiesFor': StudioProperty[];
  'studio.inspect': StudioInspector;
  'studio.entityMove': StudioSnapshot;
  'studio.componentCatalog': StudioComponentCatalogItem[];
  'studio.componentAdd': StudioSnapshot;
  'studio.componentRemove': StudioSnapshot;
  'studio.setProperties': StudioSnapshot;
  'studio.entitiesDelete': StudioSnapshot;
  'studio.entitiesDuplicate': StudioSnapshot;
  'studio.entityFlags': StudioSnapshot;
  'studio.assetPlace': StudioSnapshot;
  'studio.rules': StudioRules;
  'studio.rulesSet': StudioRules;
  'studio.windowOpen': { opened: boolean };
  'studio.windowClose': { closed: boolean };
  'studio.windowBroadcast': { ok: boolean };
  'studio.windowTitle': { ok: boolean };
  'studio.buildInfo': StudioBuildInfo;
  'studio.buildToolchain': StudioToolchain;
  'studio.buildStart': { started: boolean };
  'studio.buildLog': StudioBuildLog;
  'studio.buildEstimate': StudioBuildEstimate;
  'studio.buildReveal': { ok: boolean };
  'studio.save': StudioSnapshot;
  'studio.undo': StudioSnapshot;
  'studio.redo': StudioSnapshot;
  'studio.openProject': { cancelled: boolean; snapshot: StudioSnapshot | null };
  'studio.assets': StudioAssetList;
  'studio.assetByGuid': StudioAssetDetail;
  'studio.assetReferences': StudioAssetReference[];
  'studio.assetMetaPatch': StudioAssetDetail;
  'studio.assetFolderCreate': { path: string };
  'studio.assetMovePreview': StudioAssetTransferPlan;
  'studio.assetImportPreview': StudioAssetTransferPlan | { cancelled: true };
  'studio.assetTransferCommit': StudioAssetTransferResult;
  'studio.materials': StudioMaterialLibrary;
  'studio.materialPatch': StudioMaterialLibrary;
  'studio.materialCreate': StudioMaterialLibrary;
  'studio.materialPreview': { active: boolean; name: string };
  'studio.shaderGraphs': StudioShaderGraphList;
  'studio.shaderGraphGet': StudioShaderGraphDocument;
  'studio.shaderGraphCatalog': StudioShaderGraphCatalog;
  'studio.facialRigList': import('./panels/facialRigSchema').FacialRigList;
  'studio.facialRigGet': import('./panels/facialRigSchema').FacialRigResult;
  'studio.facialRigValidate': import('./panels/facialRigSchema').FacialRigValidation;
  'studio.facialRigSave': import('./panels/facialRigSchema').FacialRigResult;
  'studio.facialPreviewBind': StudioFacialPreviewBinding;
  'studio.facialPreviewControls': StudioFacialPreviewBinding;
  'studio.facialPreviewInfo': StudioFacialPreviewBinding;
  'studio.shaderGraphValidate': StudioShaderGraphDocument;
  'studio.shaderGraphSave': StudioShaderGraphDocument;
  'studio.renderLighting': RenderLightingSettings;
  'studio.renderLightingSet': RenderLightingSettings;
  'studio.lighting': StudioLightingState;
  'studio.lightingSet': StudioLightingState;
  'studio.lightAdd': StudioLightingState;
  'studio.lightUpdate': StudioLightingState;
  'studio.lightDelete': StudioLightingState;
  'studio.select': StudioSnapshot;
  'studio.setProperty': StudioSnapshot;
  'studio.viewportRect': { ok: boolean };
  'studio.viewportInput': { ok?: boolean; hit?: boolean };
}

export interface HumanSocket {name:string;bone:string;translation:number[];rotation:number[];scale:number[]}
export interface HumanWardrobeItem {independentRig?:boolean;attachmentRule?:'keep_world'|'snap';socket?:string;id:string;asset:string;slot:string;enabled:boolean;bone:string;translation:number[];rotation:number[];scale:number[];finish:'cloth'|'source';hideMeshes:string[];masks:{material:string;texture:string}[];tint?:number[]}
export interface HumanWardrobe {sockets?:HumanSocket[];version:1;hiddenMeshes:string[];items:HumanWardrobeItem[]}
export interface HumanBuildStatus {face?:HumanFaceShape;running:boolean;error?:string;path?:string;guid?:string;vertices?:number;meshes?:number;materials?:number;bytes?:number;lodTriangles?:number[];textureSize?:number}

/** Publish / Build panel (aiversebuild through the Workspace build runner). */
export interface StudioBuildTarget { id: string; name: string; built: boolean }
export interface StudioBuildInfo {
  targets: StudioBuildTarget[]; toolFound: boolean; outDir: string; running: boolean; label: string; exitCode: number; runs: number;
  dirty: boolean; identity: { name: string; id: string; version: string; build: number; orientation: string };
}
export interface StudioToolchainRequirement { id: string; name: string; required: boolean; found: boolean; autoInstall: boolean;
  path?: string; version?: string; detail?: string; install?: string; size?: string; manual?: string }
export interface StudioToolchain { target: string; host: string; hostSupported: boolean; hostNote?: string; ok: boolean; missing: string[];
  requirements: StudioToolchainRequirement[] }
export interface StudioBuildLog { from: number; next: number; text: string; size: number; running: boolean; label: string; exitCode: number; runs: number; logFile: string }
export interface StudioBuildEstimate { target: string; pack: Record<string, unknown>; atlas: Record<string, unknown>; split?: Record<string, unknown>;
  heaviest: Array<{ path?: string; file?: string; bytes?: number; storedBytes?: number }> }
