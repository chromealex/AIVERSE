export type NatureOpenRequest={epoch:string;source:'map'|'game'};
export type NatureOpenRoute={request:NatureOpenRequest;consume:()=>void};
type Ring={kind:string;x:number;z:number;radius:number};
export type StudioPhysisGrid={requestedMeters:number;shoreSand:number;humidity:number;mapAvailable:boolean;meters:number;width:number;height:number;baseHumidity:number};
export type StudioPhysisFrame={active:true;runtimeEpoch?:string;canShowNature?:boolean;tick:number;width:number;height:number;stride:number;originX:number;originZ:number;available:number;values:(number|null)[];colors:number[];rings:Ring[];presentation?:{fieldOverlay?:boolean;overlayRadius?:number;overlayStride?:number;overlayAlpha?:number;overlayVolumes?:boolean;quality:number;volume:number;igniteOnClick:boolean;forest?:boolean;forestMask?:string;treeGeometry?:string;hierarchical?:boolean;horizon?:boolean;lodScale?:number;grass:boolean;flames:boolean;sound:boolean;tick?:number;patches?:number;particles?:number;voices?:number};previewGrid?:StudioPhysisGrid;previewWind?:{speed:number;direction:number};cell?:StudioPhysisCell};
export type StudioPhysisCell={active:boolean;available:boolean;tick:number;wind:number[];gravity:number[]};
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
export interface StudioInspectorComponent { scope?: StudioGraphDocumentScope; id: string; title: string; note?: string; properties: StudioProperty[] }
/** Entity header + its document components (Transform, Mesh Renderer, Light…). */
/** Add Component entry: `native` adds via studio.componentAdd; `light`/`water` run an editor action. */
export interface StudioComponentCatalogItem { id: string; title: string; group: string; hint: string; action: 'native' | 'light' | 'water'; available: boolean; reason?: string }
/** Unit types of rules.json: as written (`types`) and with "extends" applied (`resolved`). */
export interface StudioRules { bodies?:Record<string,{radius:number;mass:number}>; acquireOrder: 'nearest' | 'rowMajor'; triggers?:unknown[]; armorTypes?:string[]; damageTable?:Record<string,Record<string,number>>; types: Record<string, unknown>; resolved: Record<string, unknown>; resources: string[]; summary: unknown[]; dirty: boolean }
export interface StudioInspector { id: string; name: string; kind: string; nameEditable?: boolean; components: StudioInspectorComponent[] }
export interface StudioEntity { preview?:{components:string[];health?:number}; id: string; name: string; kind: string; parentId?: string; visible?: boolean; locked?: boolean; owner?: number }
export interface StudioLog { level: 'info' | 'warning' | 'error'; text: string; time: string; source?: string }
export interface AnimationAuthoringRequest {project:string;name:string;tab:'clips'|'graph'}
export interface AnimationAuthoringRoute {request?:AnimationAuthoringRequest;consume?:()=>void}
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
export type StudioTrashAction = 'restore' | 'purge';
export interface StudioTrashEntry {
  id:string; status:'retained'|'invalid'; path?:string; guid?:string;
  kind?:'deleted'|'created'|'replacement'; role?:string; directory?:boolean; bytes?:number; slotBytes?:number; historyProtected?:boolean; error?:string;
}
export interface StudioRecoveryStorage {trashBytes:number;checkpointBytes:number;journalBytes:number;quotaMiB:number;overQuota:boolean;warning:boolean;availableBytes:number}
export interface StudioTrashPage {items:StudioTrashEntry[];total:number;truncated:boolean;recoveryPending:boolean;recoveryReason?:string;historyWarning?:string;storage?:StudioRecoveryStorage;storageError?:string}
export interface StudioAssetHistoryResetReview {token:string;version:string;reason:string;checkpointBytes:number;legacyMarker:boolean}
export interface StudioTrashReview extends StudioTrashEntry {action:StudioTrashAction;token:string;version:string}
export interface StudioTrashBatchReview {items:StudioTrashEntry[];bytes:number;token:string;version:string}
export interface StudioTrashBatchResult {ok:boolean;removed:string[];failedId?:string;error?:string}
export interface StudioAssetTransferItem {
  id: number; name: string; from: string; to: string; directory: boolean;
  conflict: string; guid: string | null; replacementAvailable?: boolean;
}
export interface StudioAssetTransferPlan {
  token: string; source: 'internal' | 'external'; items: StudioAssetTransferItem[];
  references?: Array<{ id: number; document: string; owner: string; field: string;
    from: string; to: string; writable: boolean; validateOnApply?:boolean; preservesDefault?:boolean; repairCandidate?:boolean }>;
}
export type StudioAssetConflictAction =
  'import' | 'skip' | 'keep-both' | 'replace' | 'new-guid';
export interface StudioAssetTransferResult {
  historyRecorded?: boolean;
  files: string[]; issues: Array<{name: string; message: string}>;
  repairedMaterialReferences?: number;
  repairedResourceDocuments?: number;
}
export interface StudioDependencyCoverage {gltf:number;glb:number;obj:number;dae:number;opaqueGlb:number;unsupportedModels:number;symlinks:number;unsupportedUris:number;missingLibraries:number;embedded:number;complete:boolean;note:string}
export interface StudioDependencyPage {items:StudioAssetReference[];token:string;total:number;incoming:number;outgoing:number;unresolved:number;coverage:StudioDependencyCoverage;depth:number;path:string;guid:string}
export interface StudioAssetReference {
  document: string; owner: string; field: string; path: string;
  guid: string; writable: boolean;
  direction?: 'incoming' | 'outgoing'; depth?: number; cycle?: boolean;
}
export interface StudioAssetDetail extends StudioAsset {
  importer: string; guid: string; directory: boolean; settings: Record<string,unknown>;
}
export type StudioPreviewWeather = {time:number;clouds:number;storm:number};
export type StudioLightingState = import('./panels/lightingSchema').LightingState;
export type StudioMaterialLibrary = import('./panels/materialSchema').MaterialLibrary;
export interface StudioShaderGraphList { graphs: string[] }
export interface StudioShaderGraphOp { id:string; inputs:number; domains:string[] }
export interface StudioShaderGraphCatalog { nodes:StudioShaderGraphOp[] }
export interface StudioShaderGraphDocument {
  originalJson?: string;
  graph: { version?: number; domain?: string; minQuality?:string; nodes?: Array<{id:string;op:string;inputs?:string[];position?:number[];value?:number|number[];name?:string;mask?:string;texture?:string;srgb?:boolean}>; parameters?:Record<string,number|number[]>; outputs?:Record<string,string> };
  nodes: number; samples: number;
}
export interface StudioGraphDocumentScope { projectPath:string; mapPath:string; epoch:string; revision:string }
export type StudioCaveGraphCapture =
  | {scope:StudioGraphDocumentScope;status:'unavailable'|'unsupported'|'absent'}
  | {scope:StudioGraphDocumentScope;status:'ready';report:unknown};
export type PlayerStartPosition = [number,number] | {body:string;face:number;x:number;y:number};
export interface MapPlayer {
  name:string; team:number; waves:boolean; faction:string; start?:PlayerStartPosition; resources:Record<string,number>;
}
export type PlayerChanges = Partial<Omit<MapPlayer,'start'>> & {start?:PlayerStartPosition|null};
export interface StudioMapRegions {
  regions:Array<{letter:string;name:string;cells:number}>; coverage:string[]; stride:number; width:number; height:number;
}
export interface StudioMapProperties {
  worldSpace?:{spawns:number;colliders:number};
  areas:Record<string,unknown>;
  surfaces:{id:string;size:number}[]; roster:MapPlayer[]; factions:{id:string;name:string}[]; resources:{id:string;name:string;initial:number}[];
  scope:StudioGraphDocumentScope; name:string; width:number; height:number; grid:boolean; maxSize:number;
  entities:number; players:number; roadsOnly:boolean; victory:Record<string,unknown>|null;
  issues?:{error:boolean;text:string;entityId?:string}[];
}
export interface StudioMapChanges {name?:string;size?:[number,number];roadsOnly?:boolean;victory?:Record<string,unknown>|null}
export interface StudioSnapshot {
  graphDocument?: StudioGraphDocumentScope;
  project: { name: string; path: string; dirty: boolean; actorSession?:boolean; historyVersion?: string; assetHistoryVersion?: string };
  scene: { name: string; mapProperties?:boolean; worldSpace?:boolean; mapRegions?:boolean; groupings?:string[]; entities: StudioEntity[] };
  selectedId: string | null;
  /** Whole selection, primary (selectedId) last. */
  selectedIds?: string[];
  selectedPreviewIds?: string[];
  previewInspector?: Pick<StudioRuntimeEntity,'components'|'stats'> & {row:{ref:number;name:string}; rules?:unknown;data?:unknown};
  selectedRoad?: boolean;
  canScaleEvolveRegions?: boolean;
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
export type ProjectRuleGroup = 'types'|'resources'|'techs'|'factions';
export interface StudioProjectState { groups?:Record<ProjectRuleGroup,{id:string;name:string}[]>; rulesError?:string; maps: Array<{file: string; title: string}>; currentMap: string; canCreateMap: boolean; maxMapSize: number }
export interface StudioRuntimeOwnership {state:'untracked'|'invalid-tag'|'ledger-unavailable'|'missing-record'|'retired'|'live-record'; stableId?:string; cell?:[number,number,number];revision?:string;dormantMarker:boolean}
export type StudioHierarchyPaths = Record<string,(string|string[])[]>;
export interface StudioRuntimeRow {ref:string; name:string; kind:string; owner:number; parent:string; depth?:number; children?:number; health:number; components:string[];ownership?:StudioRuntimeOwnership}
export interface StudioRuntimePage {active:boolean; epoch:string; tick:number; rows:StudioRuntimeRow[]; total:number; groups?:{name:string; count:number}[]; groupCount?:number}
export interface StudioRuntimeEditField {id:'hp'|'energy';title:string;raw:string;value:number;min:number;max:number}
export interface StudioRuntimeEntity {ref:string; epoch:string; tick:number; components:Record<string,unknown>; stats?:Record<string,unknown>;ownership?:StudioRuntimeOwnership;componentSchemas?:Record<string,unknown>;runtimeEditAvailable?:boolean;runtimeFields?:StudioRuntimeEditField[];runtimeDebugEdited?:boolean}
export interface StudioGameDeviceChrome { corner:number; cutout?:{x:number;y:number;width:number;height:number;radius:number}; insets:{left:number;top:number;right:number;bottom:number} }
export interface StudioGameDevice { id:string; landscape:boolean; safeArea:boolean; width:number; height:number; touch:boolean; chrome?:StudioGameDeviceChrome }
export interface StudioGameDeviceOption { id:string; name:string; group:string; width:number; height:number; scale:number }
export interface EvolveLiveRegion { id:string; minXcm:number; minZcm:number; maxXcm:number; maxZcm:number; fertility:number; pollution:number; succession:number; durability:number }
export interface PlanetLiveProcessSummary {sampleTick:number;truncated:boolean;rejected:number;cells:unknown[]}
export interface PlanetEvolveLiveRegion {id:string;body:string;fertility:number;pollution:number;succession:number;durability:number;chartClipped:boolean;edges:(number[]|null)[][]}
export interface PlanetEvolveLiveState {sampleTick:number;regionCount:number;truncated:boolean;rejected:number;clipped:number;regions:PlanetEvolveLiveRegion[]}
export interface PlanetGravityLiveBody {body:string;minMagnitude:number;maxMagnitude:number;zero:number;inward:number;outward:number;tangent:number;samples:{face:number;x:number;y:number;position:number[];up:number[];acceleration:number[];magnitude:number;radial:number;kind:'zero'|'inward'|'outward'|'tangent'}[]}
export interface PlanetGravityLiveState {available:boolean;fieldCount:number;usesDefaultSettings:boolean;sampleTick:number;truncated:boolean;rejected:number;bodies:PlanetGravityLiveBody[]}
export type AetherLayer = 'off'|'smoke'|'toxin'|'oxygen';
export interface PlanetAetherLiveCell {id:string;body:string;position:number[];mass:number[];sourceRate:number[]}
export interface PlanetAetherLiveDuct {id:string;body:string;from:string;to:string;a:number[];b:number[];oneWay:boolean;cap:number;potentialAvailable:boolean;head?:number[];bias?:number[];potential?:number[]}
export interface PlanetAetherLiveState {available:boolean;sampleTick:number;policy:'unknown'|'diffusion'|'stratified';gravityCapped:boolean;cellCount:number;ductCount:number;fluxCount:number;truncated:boolean;rejected:number;crossBody:number;omittedDucts:number;cells:PlanetAetherLiveCell[];ducts:PlanetAetherLiveDuct[]}
export interface PlanetFractureLiveMember {id:string;body:string;position:number[];anchored:boolean;broken:boolean;detached:boolean;collapse?:{delivery:string;key:string;tick:number};natureFracture?:{delivery:string;key:string;climateTick:string};stress?:{climateTick:string;windLoadN:number;bendingMomentNm:number;normalizedLoad:number;accumulatedDamage:number;fractured:boolean}}
export interface PlanetFractureLiveBond {id:string;body:string;first:string;second:string;a:number[];b:number[];strength:number;damage:number;enabled:boolean;activeSupportEdge:boolean}
export interface PlanetFractureLiveState {available:boolean;sampleTick:number;memberCount:number;bondCount:number;anchored:number;broken:number;detached:number;pending:number;truncated:boolean;rejected:number;crossBody:number;omittedBonds:number;members:PlanetFractureLiveMember[];bonds:PlanetFractureLiveBond[]}
export interface PlanetMorphLiveNode {id:string;enabled:boolean;anchored:boolean;current:number[];rest:number[];displacement:number[];velocity:number[];configuredForce:number[];projected:boolean;body?:string;owner?:string;unitsPerMetre?:number;position?:number[];restPosition?:number[];binding?:{owner:string;unitsPerMetre:number;impulseMilli:number;relative:boolean;enabled:boolean};currentGravityLoad?:number[];pendingImpact?:{tick:number;impulse:number[]}}
export interface PlanetMorphLiveSpring {id:string;first:string;second:string;enabled:boolean;stiffness:number;residual:number[];projected:boolean;body?:string;a?:number[];b?:number[]}
export interface PlanetMorphLiveState {available:boolean;sampleTick:number;nodeCount:number;springCount:number;forceCount:number;gravityInstalled:boolean;gravityCapped:boolean;truncated:boolean;rejected:number;unprojected:number;omittedSprings:number;frameMismatch:number;crossBody:number;nodes:PlanetMorphLiveNode[];springs:PlanetMorphLiveSpring[]}
export interface PlanetEchoLiveLocation {id:string;placed:boolean;body?:string;position?:number[]}
export interface PlanetEchoLiveZone extends PlanetEchoLiveLocation {enabled:boolean}
export interface PlanetEchoLiveEmitter extends PlanetEchoLiveLocation {zone:string;intensity:number;enabled:boolean;active:boolean}
export interface PlanetEchoLiveReceiver extends PlanetEchoLiveLocation {zone:string;threshold:number;enabled:boolean;zoneActive:boolean;hearingAvailable:boolean;valid?:boolean;audible?:boolean;intensity?:number;source?:string;sourcePosition?:number[]}
export interface PlanetEchoLivePortal {id:string;from:string;to:string;enabled:boolean;bidirectional:boolean;loss:number;active:boolean;placed:boolean;body?:string;a?:number[];b?:number[]}
export interface PlanetEchoLiveState {available:boolean;sampleTick:number;zoneCount:number;portalCount:number;emitterCount:number;receiverCount:number;truncated:boolean;rejected:number;unplaced:number;omittedPortals:number;crossBody:number;staleHearing:number;audible:number;zones:PlanetEchoLiveZone[];portals:PlanetEchoLivePortal[];emitters:PlanetEchoLiveEmitter[];receivers:PlanetEchoLiveReceiver[]}
export interface PlanetCrowdLocation {id:string;enabled:boolean;placed:boolean;body?:string;position?:number[];support?:{status:'supported'|'airborne'|'weak'|'outward'|'steep';acceleration:number[];normal:number[];clearance:number;inward:number}}
export interface PlanetCrowdAgent extends PlanetCrowdLocation {eligible?:boolean;allocated?:string;reservationValid?:boolean;reservationPosition?:number[];preferences:string[];orderCount?:number;moving?:boolean;flow?:boolean;velocity?:number[];pathCursor?:number;pathCount?:number;goal?:number[];routeTruncated?:boolean;route?:(number[]|null)[]}
export interface PlanetCrowdSlot extends PlanetCrowdLocation {occupancy:number}
export interface PlanetCrowdLiveState {available:boolean;sampleTick:number;agentCount:number;slotCount:number;preferenceCount:number;gravityEnabled:boolean;gravityCapped:boolean;truncated:boolean;rejected:number;stale:number;unplaced:number;crossBody:number;invalidReservation:number;routePoints:number;agents:PlanetCrowdAgent[];slots:PlanetCrowdSlot[]}
export interface PlanetCircuitLocation {id:string;placed:boolean;body?:string;position?:number[];enabled:boolean}
export interface PlanetCircuitNode extends PlanetCircuitLocation {supply:number;demand:number;drive:boolean;outputAvailable:boolean;powered?:boolean;signal?:boolean}
export interface PlanetCircuitWire {id:string;from:string;to:string;enabled:boolean;bidirectional:boolean;active:boolean;outputsAvailable:boolean;bothPowered?:boolean;placed:boolean;body?:string;a?:number[];b?:number[]}
export interface PlanetCircuitGate extends PlanetCircuitLocation {operation:string;inputA:string;inputB?:string;output:string;inputsValid:boolean;outputAvailable:boolean;outputSignal?:boolean}
export interface PlanetCircuitSwitch extends PlanetCircuitLocation {node:string;closed:boolean;nodeValid:boolean}
export interface PlanetCircuitDevice extends PlanetCircuitLocation {node:string;kind:string;requireSignal:boolean;outputAvailable:boolean;valid?:boolean;active?:boolean;transition?:number;changedTick?:number}
export interface PlanetCircuitLiveState {available:boolean;sampleTick:number;nodeCount:number;wireCount:number;gateCount:number;switchCount:number;deviceCount:number;powered:number;high:number;activeDevices:number;truncated:boolean;rejected:number;unplaced:number;crossBody:number;omittedWires:number;staleOutputs:number;nodes:PlanetCircuitNode[];wires:PlanetCircuitWire[];gates:PlanetCircuitGate[];switches:PlanetCircuitSwitch[];devices:PlanetCircuitDevice[]}
export interface PlanetSocietyLocation {id:string;placed:boolean;body?:string;position?:number[];enabled:boolean}
export interface PlanetSocietyPerson extends PlanetSocietyLocation {credibility:number;acceptance:number;knowledgeAvailable:boolean;knowledge?:string;topicCount?:number}
export interface PlanetSocietyContact {id:string;source:string;target:string;enabled:boolean;trust:number;score:number;threshold:number;eligibleNow:boolean;sourceOnlyTopics?:string;placed:boolean;body?:string;a?:number[];b?:number[]}
export interface PlanetSocietyMessage extends PlanetSocietyLocation {source:string;topics:string;sourceValid:boolean;personPosition?:number[]}
export interface PlanetSocietyTrigger extends PlanetSocietyLocation {person:string;topics:string;actionId:number;mode:string;once:boolean;outputAvailable:boolean;active?:boolean;fired?:boolean;transition?:number;changedTick?:number;personPosition?:number[]}
export interface PlanetSocietyLiveState {available:boolean;sampleTick:number;personCount:number;contactCount:number;messageCount:number;triggerCount:number;eligibleContacts:number;activeTriggers:number;firedTriggers:number;truncated:boolean;rejected:number;unplaced:number;crossBody:number;omittedContacts:number;staleOutputs:number;people:PlanetSocietyPerson[];contacts:PlanetSocietyContact[];messages:PlanetSocietyMessage[];triggers:PlanetSocietyTrigger[]}
export interface EvolveLiveState {forestTerrain?:{active:boolean;tick?:number;grid?:{body:string;faceSize:number;radius:number;step:number;side:number;corners:number[]}};forest?:StudioPlanetForestLive;diagnosticBudget?:{payloadLimitBytes:number;lineLimit:number;sampleLayers:number;trimmedLayers:string[];omittedLayers:string[]};society?:PlanetSocietyLiveState;circuit?:PlanetCircuitLiveState;crowd?:PlanetCrowdLiveState;echo?:PlanetEchoLiveState;morph?:PlanetMorphLiveState;fracture?:PlanetFractureLiveState;aether?:PlanetAetherLiveState;gravity?:PlanetGravityLiveState;evolve?:PlanetEvolveLiveState;topology?:'planet';epoch?:string;imprint?:PlanetLiveProcessSummary;granular?:PlanetLiveProcessSummary & {available:boolean;cellSizeCm:number;fieldCount:number;tiles:number};logs?:{x:number;z:number;ratio:number}[];soil?:{x:number;z:number;units:string}[];absorbedUnits?:string;respiredUnits?:string;active:boolean;tick?:number;regions:EvolveLiveRegion[];plantCount?:number;plants?:{id:string;x:number;z:number;fallen:boolean;dead?:boolean;recruited?:boolean;mature?:boolean;ageTicks?:string;matureTicks?:string;shadeStress?:string;floodStress?:string;droughtStress?:string}[];weather?:{x:number;z:number;size:number;rain:number;temperature:number;wetness:number}[]}
export type WorldProcessDiagnosticLane = {available:false;error:string} | {
  available:true;tick:number;hot:number;warm:number;cold:number;executed:number;deferred:number;
  budget:number;selected:number;pending:number;urgent:number;urgentSelected:number;queued:number;removed:number;
  oldestPendingTicks:number;mode:'serial'|'parallel';
  samples:{id:string;requestedTick:number;sampleTick:number}[];
  waiting:{id:string;requestedTick:number;urgent:boolean}[];
};
export type WorldProcessDiagnostics = {active:false} | {
  active:true;epoch:string;tick:number;observerAvailable:boolean;cellSizeCm:number;
  topology?:'planet';focusSurface?:{body:string;face:number;x:number;y:number;gravityLocal:[number,number,number]}|null;
  config:{enabled:boolean;hotCells:number;warmCells:number;warmTicks:number;coldTicks:number};
  lanes:Record<'evolve'|'crowd'|'society',WorldProcessDiagnosticLane>;
};
export type EvolveLayer = 'combined' | 'fertility' | 'pollution' | 'succession' | 'durability' | 'off';
export interface StudioPlayReview {token:string;tick:number;moved:number;amounts:number;removed:number;skipped:number}
export interface StudioScriptMessage { tick:number; text:string; error:boolean; source?:'simulation'|'view' }
export interface StaticGeometryReport {
  error?:string;
  enabled:boolean; instances:number; autoStrataSources:number; autoLodSources:number;
  cookedSources:number; cookStaleSources:number; select:string[];
}
export interface StudioGameState { geometrySource?:"Scene"; geometry?:StaticGeometryReport|null; roleLabel?:string; simulationSpeed?:number; selectedLiveRef?:string; selectedLiveRefs?:string[]; scriptErrors?: StudioScriptMessage[]; device:StudioGameDevice; epoch?:string;canReviewPlayChanges?:boolean;runtimeDebugEdited?:boolean; paused: boolean; canControl: boolean; canStep: boolean; active: boolean; loading: boolean; progress: number; inMatch: boolean; tick: number; error: string }
export interface RuntimeAnimationRow {
  entityId:string;entity:number;climbing:boolean;attached:boolean;stanceFeet:number;steppingFeet:number;
  footError:number;penetration:number;bodyPosition:number[];surfaceNormal:number[];error:string;
}
export type StudioMethod = 'studio.panelInventory' | 'studio.animationRuntime' | 'studio.animationEditorState' | 'studio.importedClipPreview' | 'studio.sourceModelPreview' | 'studio.projectDirectory' | 'studio.placementGhost' | 'studio.gpuEmitterPreview' | 'studio.effectFilePreview' | 'studio.lightBakeJob' | 'studio.lightBakeDisplay' | 'studio.lightBakeAtlas' | 'studio.lightingPreview' | 'studio.cavernsPreview' | 'studio.buildingRecipes' | 'studio.buildingReadRecipe' | 'studio.buildingRegenerate' | 'studio.buildingStaleMaterials' | 'studio.buildingSurfacePreview' | 'studio.buildingPublishMaterial' | 'studio.buildingCatalog' | 'studio.buildingPreview' | 'studio.buildingPreviewView' | 'studio.buildingPreviewStop' | 'studio.buildingTextureProvenance' | 'studio.buildingGraphOverlay' | 'studio.buildingInspect' | 'studio.buildingGenerate' | 'studio.cavernsGraphInspect' | 'studio.planetSceneBody' | 'studio.planetForestMove' | 'studio.planetForestArm' | 'studio.planetForestPlant' | 'studio.planetForest' | 'studio.planetForestSet' | 'studio.physisNature' | 'studio.physisRemoteFields' | 'studio.physisFieldEdit' | 'studio.physisLive' | 'studio.planetForestLive' | 'studio.worldProcessDiagnostics' | 'studio.evolveState' | 'studio.evolveLayer' | 'studio.worldProcessAreaPreview' | 'studio.worldProcessGroupPivot' | 'studio.worldProcessAreaBatchResize' | 'studio.renderLighting' | 'studio.renderLightingSet' | 'studio.nereidFlow' | 'studio.nereidFlowSet' | 'studio.gameEntities' | 'studio.gameSelectEntity' | 'studio.gameInspect' | 'studio.gameDevices' | 'studio.gameDevice' | 'studio.gameControl' | 'studio.legacyCameraBookmarks' | 'studio.standalonePlay' | 'studio.networkPlay' | 'studio.gameStart' | 'studio.gameStop' | 'studio.scriptWatch' | 'studio.gameScriptLog' | 'studio.gameState' | 'studio.projectRule' | 'studio.project' | 'studio.newMap' | 'studio.openMap' | 'studio.saveAs' | 'studio.playerStartPick' | 'studio.areaPick' | 'studio.roadPick' | 'studio.mapRegions' | 'studio.selectedRoadSet' | 'studio.mapRegionName' | 'studio.mapArea' | 'studio.mapPlayer' | 'studio.mapProperties' | 'studio.mapPropertiesSet' | 'studio.hierarchyGroups' | 'studio.entityCatalog' | 'studio.entityCreate' | 'studio.entityDelete' | 'studio.entityDuplicate' | 'studio.snapshot' | 'studio.propertiesFor' | 'studio.inspect' | 'studio.componentCatalog' | 'studio.componentAdd' | 'studio.componentRemove' | 'studio.setProperties' | 'studio.entitiesDelete' | 'studio.entitiesDuplicate' | 'studio.entityFlags' | 'studio.assetPlace' | 'studio.rules' | 'studio.rulesSet' | 'studio.windowOpen' | 'studio.windowClose' | 'studio.windowBroadcast' | 'studio.windowTitle' | 'studio.buildInfo' | 'studio.buildToolchain' | 'studio.buildStart' | 'studio.buildLog' | 'studio.buildEstimate' | 'studio.buildReveal' | 'studio.entitiesAlign' | 'studio.history' | 'studio.historyJump' | 'studio.ops' | 'studio.op' | 'studio.agentInfo' | 'studio.sceneBrush' | 'studio.splineEdit' | 'studio.hub' | 'studio.hubTemplates' | 'studio.wizardDictation' | 'studio.wizardFetchArt' | 'studio.wizardArt' | 'studio.wizardCreate' | 'studio.wizardOptions' | 'studio.wizardPlan' | 'studio.hubForget' | 'studio.projectPort' | 'studio.portFolder' | 'studio.openImported' | 'studio.hubOpen' | 'studio.hubCreate' | 'studio.stats' | 'studio.gpuStats' | 'studio.keepPlayChanges' | 'studio.extensions' | 'studio.extensionAction' | 'studio.extensionCreate' | 'studio.lensPreview' | 'studio.attachmentsPreview' | 'studio.attachmentsGet' | 'studio.attachmentsSet' | 'studio.agentStart' | 'studio.agentStop' | 'studio.agentReset' | 'studio.inputGet' | 'studio.inputSet' | 'studio.assetAudit' | 'studio.tileAtlas' | 'studio.assetThumbnail' | 'studio.assetUsage' | 'studio.entityMove' | 'studio.select' | 'studio.setProperty' | 'studio.save' | 'studio.undo' | 'studio.redo' | 'studio.viewportRect' | 'studio.viewportInput' | 'studio.openProject' | 'studio.assets' | 'studio.assetByGuid' | 'studio.assetReferences' | 'studio.assetMetaPatch' | 'studio.assetFolderCreate' | 'studio.assetMovePreview' | 'studio.assetImportPreview' | 'studio.assetTransferCommit' | 'studio.lighting' | 'studio.lightingSet' | 'studio.lightAdd' | 'studio.lightUpdate' | 'studio.lightDelete' | 'studio.materials' | 'studio.materialPatch' | 'studio.materialCreate' | 'studio.materialPreview' | 'studio.shaderGraphs' | 'studio.shaderGraphGet' | 'studio.shaderGraphValidate' | 'studio.shaderGraphSave' | 'studio.shaderGraphCatalog' | 'studio.facialRigList' | 'studio.facialRigGet' | 'studio.facialRigValidate' | 'studio.facialRigSave' | 'studio.facialPreviewBind' | 'studio.facialPreviewControls' | 'studio.facialPreviewInfo' | 'studio.humanAppearanceInfo' | 'studio.humanAppearanceApply' | 'studio.humanAppearanceSave' | 'studio.humanAppearanceLoad' | 'studio.humanAppearanceList' | 'studio.humanAppearanceFromAsset' | 'studio.humanWardrobeFromAsset' | 'studio.humanPreviewAnimation' | 'studio.humanStudioLighting' | 'studio.humanFaceFitScan' | 'studio.humanHeadBodyImport' | 'studio.humanWardrobeInfo' | 'studio.humanCharacterBuild' | 'studio.humanCharacterBuildStatus' | 'studio.wind' | 'studio.windSet' | 'studio.humanAppearanceThumbnails' | 'studio.humanLookCapture' | 'studio.humanCamera' | 'studio.humanCompare' | 'studio.humanRuler' | 'studio.humanClipList' | 'studio.humanClipAdd' | 'studio.humanClipRemove' | 'studio.humanMakeupAlign' | 'studio.humanTryOn' | 'studio.humanPlaceInGame' | 'studio.humanPackageList' | 'studio.humanPackageExport' | 'studio.humanPackageImport' | 'studio.humanFacePick' | 'studio.humanSculpt' | 'studio.humanSculptStroke' | 'studio.humanSculptUndo' | 'studio.humanCreatorInfo' | 'studio.humanCreatorSet' | 'studio.humanCreatorPreset' | 'studio.humanCreatorUndo' | 'studio.humanCreatorRedo' | 'studio.humanCreatorRoute' | 'studio.humanCreatorRig' | 'studio.humanCreatorSkin' | 'studio.humanCreatorNew' | 'studio.humanCreatorSave' | 'studio.humanCreatorLoad' | 'studio.humanCreatorList' | 'studio.humanCreatorPreview' | 'studio.humanMetaHuman' | 'studio.gameProfile' | 'studio.gameRegions' | 'studio.gameEdit' | 'studio.projectHistory' | 'studio.projectHistoryStep' | 'studio.assetHistory' | 'studio.assetHistoryStep' | 'studio.agentHistoryStep' | 'studio.agentUndoAll' | 'studio.agentBatchPreview' | 'studio.agentBatchApply' | 'studio.previewPlayChanges' | 'studio.assetDependencyPage' | 'studio.assetDelete' | 'studio.assetRecoveryQuota' | 'studio.assetTrashBatchReview' | 'studio.assetTrashBatchApply' | 'studio.assetHistoryResetReview' | 'studio.assetHistoryResetApply' | 'studio.assetTrashList' | 'studio.assetTrashReview' | 'studio.assetTrashApply' | 'studio.spectraPackageChoose' | 'studio.spectraAssetCheck' | 'studio.spectraPreview' | 'studio.rulesDocument' | 'studio.rulesDocumentSave' | 'studio.materialsToFiles' | 'studio.shaderGraphPreview' | 'studio.materialPublicationRollbackPreview' | 'studio.materialPublicationRollbackEmpty' | 'studio.materialPublicationQuarantine' | 'studio.materialPublicationRestore' | 'studio.materialPublicationRetry' | 'studio.materialPublicationFinalize' | 'studio.materialPublicationRepair';
export interface StudioSceneBrush { armed: boolean; kind: 'terrain' | 'sparse' | 'paint' | 'region' | 'tiles' | 'field' | 'objects' | 'road' | 'river' | 'caverns';
  sparseMode:'create'|'raise'|'lower'; chunkMetres:number; sparseChunks:number; depthCm:number;
  planet:boolean; layerNames?:string[]; terrainGrid:'auto'|'base'|'detail'; detailScale:number; targetHeight:number; pickHeight:boolean;
  op: 'set' | 'raise' | 'lower' | 'smooth' | 'flatten' | 'noise' | 'erode' | 'stamp'; stamp:'crater'|'mesa'|'dunes'|'rocks'; angle:number; hardness:number; seed:number; radius: number; strength: number; layer: number; tile: string; fixedTile: number; regionLetter: string; regionBorders: boolean; field: string;
  fieldPlayer: number; fieldMode: 'set' | 'add' | 'max' | 'erase'; value: number; type: string; owner: number; count: number; spread: number; roadType: string; width: number; caveMode?: 'carve'|'fill'|'shaft'; caveY?: number; caveDepth?:number; caveSurface?: boolean }
export interface StudioStats { map: string; width: number; height: number; entities: number; players: number; roads: number; ruleTypes: number;
  undoSteps: number; dirty: boolean; sceneFrameMs: number; sceneFrames: number;
  types: Array<{ type: string; count: number }>; owners: Array<{ owner: number; name: string; count: number }> }
export interface StudioGpuTexture { id: number; format: string; width: number; height: number; depth: number; layers: number; mips: number; samples: number; renderTarget: boolean; bytes: number }
export interface StudioCavernsBudgets { perSourceBytes:number; perShardBytes:number; deviceBytes:number; perSourceUploadBytes:number; perShardUploadBytes:number; deviceUploadBytes:number }
export interface StudioCavernsPreview {
  budgets:StudioCavernsBudgets;
  planetaryChart?:boolean; previewResolution?:string; previewSpacingCm?:string;
  globeBody?:string; globeReady?:boolean; globeError?:string;
  chartSlots?:string; exactChartSlots?:string; chartLodFallback?:boolean;
  terrainCutEntrances?:string; terrainRejectedEntrances?:string; terrainTriangles?:string;
  terrainCutDiagnostic?:string; terrainCutSupported?:boolean;
  sparseTerrainResident?:string; sparseTerrainAuthored?:string; sparseTerrainDiagnostic?:string;
  sparseCertifiedEdges?:string; sparsePlannedOpenings?:string;
  rimGroupReady?:boolean; rimGroupPending?:boolean; rimDiagnostic?:string;
  enabled:boolean; isolate:boolean; paging:boolean; visible:boolean; visualOnly:true; error:string;
  sourceRevision?:string; tracked?:string; ready?:string; pendingCpu?:string; retirePending?:string;
  activeShards?:string; stagedShards?:string; retiredScenes?:string;
  activeSourceBytes?:string; stagedSourceBytes?:string; retiredSourceBytes?:string;
  activeStrataArenaBytes?:string; stagedStrataArenaBytes?:string; retiredStrataArenaBytes?:string;
  pageTransfers?:string; pageDeferrals?:string;
}
export interface PrismaViewReport {
  id:string; sequence:string; quality:string; width:number; height:number; hdr:boolean; sceneReady:boolean;
  compositorPasses:number; shaderGraphTextures:number; shaderGraphFallback:boolean;
  autoExposureTextureSlot:number; autoExposureHighlightRejection:number; autoExposureFrozen:boolean;
  autoExposureRegionMetered:boolean; autoExposureBoxMetered:boolean; manualExposureEncoded:boolean; manualExposureEV:number;
  reactiveInputSupplied:boolean; reactiveInputAccepted:boolean; objectVelocityInputAccepted:boolean;
  reactiveWithoutObjectVelocity:boolean; motionBlurReactiveEdgesEnabled:boolean;
  effects:{type:string;requested:boolean;pipelinePrepared:boolean;encoded:boolean;degraded:boolean;reason:string}[];
}
export interface StudioGpuStats { prisma?:Record<string,PrismaViewReport[]>; history: Array<[number, number]>; frames: number; drawCalls?: number; error?: string; gpu?: { textures: StudioGpuTexture[]; totalBytes: number } }
export interface StudioExtensionWidget { kind: string; id?: string; label?: string; value?: string | number | boolean; min?: number; max?: number; step?: number; children?: StudioExtensionWidget[] }
export interface StudioExtensions { path: string; exists: boolean; error?: string; widgets?: StudioExtensionWidget[]; gizmos?: unknown[] }
export interface StudioAttachmentItem { model: string; socket: string; enabled?: boolean; id?: string; animated?: boolean; animationGraph?: string; local?: number[]; childWorld?: number[] }
export interface StudioAttachmentDoc { parent: string; attachments: StudioAttachmentItem[] }
export interface StudioAttachmentsResult { path: string; exists: boolean; error?: string; document: StudioAttachmentDoc }
/** A project template: an installed sample game (catalog package), or a catalog package ("pkg:<id>", source "catalog"). */
export interface StudioTemplate { id: string; name: string; description: string; genre: string; maps: number;
  source?: 'engine' | 'catalog'; official?: boolean; version?: string; size?: number; preview?: string }
export interface WizardPlanInput {
  artDir: string; verifyLevels: boolean;
  studio: string; version: string; languages: string[]; platforms: string[]; seed: number;
  mapSize: string; difficultyCurve: string; bossEvery: number; keepTemplateMaps: boolean; startResourcesPercent: number;
  tutorialForced: boolean; tutorialSkippable: boolean; tutorialDim: boolean; hints: boolean;
  metaIdea: string; currencies: string[]; upgrades: boolean; shop: boolean; daily: boolean; profileScreen: boolean;
  xpPerLevel: number; ads: boolean; interstitialEvery: number; purchases: boolean; stores: string[];
  paletteFromArt: boolean; brandingFromArt: boolean;
  title: string; idea: string; genre: string; features: string[]; orientation: string; tutorial: string; mapPreset: string; levels: number;
}
export interface StudioHub { recent: Array<{ dir: string; name: string; lastOpened: number; exists: boolean }>;
  templates: StudioTemplate[]; defaultLocation: string }
export interface StudioAgentChange { op: string; args: string; ok: boolean; undoable: boolean; position: number; error?: string;
  scope?:'map'|'project'|'assets'|'untracked'; historyId?:string; historyVersion?:string; applied?:boolean; canUndo?:boolean; canRedo?:boolean }
export interface StudioAgentBatch {direction:'undo'|'redo';available:boolean;reason?:string;token?:string;steps?:Array<{scope:'map'|'project'|'assets';historyId:string}>}
export interface StudioAgentInfo { hasWizardBrief?: boolean; wizardBrief?: string; hasWizardReport?: boolean; wizardReport?: string; lines: string[]; total: number; running: boolean; port: number; clients: number; changes: StudioAgentChange[];
  startPosition: number; historyVersion?:string; batchUndo?:StudioAgentBatch; batchRedo?:StudioAgentBatch; cli: string; codexCli?: string; runClient?: string; mcp: boolean; conversation: boolean; result?: string }
// World maintenance uses decimal strings for uint64 values, never JS numbers.
export interface StudioWorldRegion {cell:[number,number,number];live:number;materialized:number}
export interface StudioWorldStatus {
  mode:'offline';tick:number;sequence:string;generation:string;revision:string;
  historyFloor:string;retiredFloor:string;nextIdentity:string;
  live:number;tombstones:number;capacity:number;materialized:number;
  ledgerArchiveBytes:string;checkpointBytes:string;regionCount:number;
  regionsTruncated:boolean;regions:StudioWorldRegion[];error?:string;
}
/** Read-only snapshot of the hosted Game ECS, never a finalized disk save. */
export type StudioGameProfile =
  {available:false;epoch:string;reason:string} |
  {available:true;epoch:string;active:boolean;paused:boolean;target:number;tick:number;tickRate:number;
    historyMs:number[];totalMs:number;systemsMs:number;systemCount:number;
    rows:Array<{index:number;name:string;totalMs:number}>};
export type StudioSimulationLodDiagnostics =
  {available:false} | {available:true;tick:number;hot:number;warm:number;cold:number;executed:number;deferred:number};
export type StudioLiveWorldRegions =
  {active:boolean;available:false;epoch:string;tick:number;reason:string} |
  (Pick<StudioWorldStatus,'revision'|'historyFloor'|'retiredFloor'|'nextIdentity'|'live'|'tombstones'|'capacity'|'materialized'|'ledgerArchiveBytes'|'regionCount'|'regionsTruncated'|'regions'> &
  {active:true;available:true;epoch:string;tick:number;durabilityKnown:false;simulationLod:StudioSimulationLodDiagnostics});
export interface StudioWorldBackup {
  generation:string;active:boolean;status:string;sequence?:string;revision?:string;
}
export interface StudioWorldRetention {
  valid:boolean;overBudget:boolean;retainedBytes:string;removeGenerations:string[];
}
export interface StudioWorldCleanup extends StudioWorldRetention {
  ok:boolean;removed:string[];failedGeneration:string;
}
export interface StudioWorldPage {
  revision:string;historyFloor:string;bootstrap:boolean;resetRequired:boolean;more:boolean;next:string;
  records:Array<{id:string;revision:string;cell:[number,number,number];live:boolean}>;
}
export interface StudioWorldJob {
  state:'idle'|'running'|'succeeded'|'failed'|'cancelled';
  id?:string;project?:string;operation?:string;phase?:string;
  cancelRequested?:boolean;cancelAllowed?:boolean;error?:string;result?:unknown;
  progress?:{checkpointBytes?:string;checkpointTotal?:string;walBytes?:string;frames?:string;sequence?:string;assetsRead?:string;assetsTotal?:string};
}
export interface StudioSimulationLodConfig {
  enabled:boolean;hotCells:number;warmCells:number;warmTicks:number;coldTicks:number;
}
export interface StudioSimulationLodSettings extends StudioSimulationLodConfig {
  authored:boolean;requiresRestart:boolean;
}
export interface StudioWorldOps {
  world_save_list:{params:Record<string,never>;result:{worlds:string[]}};
  world_save_create:{params:{world:string;seed:string};result:StudioWorldStatus};
  world_save_status:{params:{world:string;seed:string};result:StudioWorldStatus};
  world_save_checkpoint:{params:{world:string;seed:string;retainRevisions:string};result:StudioWorldStatus};
  world_save_backups:{params:{world:string;seed:string;verify:boolean};result:{complete:boolean;backups:StudioWorldBackup[]}};
  world_save_retention_preview:{params:{world:string;seed:string;keepPrevious:string;maxBytes:string};result:StudioWorldRetention};
  world_save_retention_apply:{params:{world:string;seed:string;keepPrevious:string;maxBytes:string;expectedGenerations:string[]};result:StudioWorldCleanup};
  world_save_fork:{params:{world:string;seed:string;generation:string;newWorld:string};result:{ok:boolean;world:string}};
  world_save_page:{params:{world:string;seed:string;after:string;limit:string;bootstrap:boolean;expectedRevision:string};result:StudioWorldPage};
}

export interface StudioToolOp { name: string; description: string; schema: unknown; readOnly: boolean }
export interface StudioInputControl { device: string; code: string | number; deviceIndex?: number }
export interface StudioInputAsset {
  version: 1;
  actions: Array<{ name: string; type: 'bool' | 'axis1D' | 'axis2D' | 'axis3D'; modifiers?: unknown[]; triggers?: unknown[] }>;
  contexts: Array<{ name: string; priority?: number; active?: boolean;
    mappings: Array<{ id?: number; action: string; control: StudioInputControl; modifiers?: unknown[]; triggers?: unknown[]; consume?: boolean }> }>;
}
export interface StudioInputInfo { asset: StudioInputAsset; exists: boolean; path: string; error?: string; keys: string[]; padButtons: string[]; padAxes: string[]; devices: string[] }
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
export interface StudioBuildingFunction { id: string; name: string; width: number; height: number; depth: number }
export type StudioBuildingRoofProfile = 'flat'|'gable'|'shed'|'barrel'|'hip'|'dome'|'terrace'|'pagoda';
export interface StudioBuildingStyle { id: string; name: string; setting: string; functions: StudioBuildingFunction[]; materials?: string[]; recommendedRoof?:StudioBuildingRoofProfile; surfaces?: Record<string,{pattern:string;seed:number;resolution?:number}> }
export interface StudioBuildingCatalog { styles: StudioBuildingStyle[] }
export interface StudioBuildingSurfacePreview { style:string; slot:string; resolution:number; maps:Array<{channel:string;label:string;url:string}> }
export type StudioBuildingFeatureKind = 'large-gate' | 'small-gate' | 'personnel-door' | 'sliding-roof' | 'window' | 'stairs' | 'elevator';
export type StudioBuildingFeatureSide = 'front' | 'back' | 'left' | 'right';
export interface StudioBuildingFeature {
  kind: StudioBuildingFeatureKind;
  side: StudioBuildingFeatureSide;
  wallLine?:number; // -1 outer envelope; otherwise zero-based facade grid line
  balconyDepth?:number;
  balconySupport?:'cantilever'|'columns';
  count: number;
  width: number;
  height: number;
  depth: number;
  open: number;
  position: number; // <0 auto; wall axis or roof X in metres
  positionZ: number; // roof Z; <0 auto
  spacing: number; // minimum clear gap between grouped openings, metres
  floor: number; // zero-indexed storey for windows
  sill: number; // window sill above its storey's floor
}
export type StudioBuildingRoomUse='lounge'|'bedroom'|'kitchen'|'office'|'meeting'|'workshop'|'storage'|'retail'|'circulation'|'empty';
export interface StudioBuildingDamageZone {kind:'wall'|'window'|'roof';minimum:[number,number,number];maximum:[number,number,number]}
export interface StudioBuildingRequest {
  setting: string; style: string; function: string;
  width: number; height: number; depth: number; seed: number;
  floors?: number; roomsX?: number; roomsZ?: number;
  footprint?:string[];
  floorFootprints?:string[][];
  walkableSetbacks?:boolean;
  cornerChamfers?:[number,number,number,number];
  damageZones?:StudioBuildingDamageZone[];
  interiorProfile?:'none'|'residential'|'office'|'industrial'|'commercial'|'mixed';
  furnitureDensity?:number;
  interiorLighting?:boolean; interiorDecor?:boolean;
  roomUses?:Array<{floor:number;x:number;z:number;use?:StudioBuildingRoomUse;furnitureDensity?:number;lighting?:boolean;decor?:boolean}>;
  mode?: 'features' | 'preset';
  features?: StudioBuildingFeature[];
  materialBindings?: Record<string,string>; // authored style slot -> project materials.json name
  textureResolution?:128|256|512;
  uniqueAtlas?:boolean;
  generateVisualLods?:boolean;
  roofProfile?:StudioBuildingRoofProfile; roofRise?:number;
}
export interface StudioBuildingPreview {
  seed: number; triangles: number; vertices: number; materials: number;
  width: number; height: number; depth: number;
  solids?:number; connections?:number; floors?:number; roomsX?:number; roomsZ?:number; footprint?:string[]; floorFootprints?:string[][];
  footprintPolygon?:Array<[number,number]>;
  removedDamage?:Array<StudioBuildingDamageZone & {zone:number}>;
  visualLods?:Array<{detailLevel:number;maxPixels:number;triangles:number;vertices:number}>;
  visualGroups?:Array<{id:string;triangles:number}>;
  roomUses?:Array<{roomId:string;use:StudioBuildingRoomUse}>;
  interiorLights?:Array<{id:string;roomId:string;floor:number;x:number;y?:number;z:number;intensity:number;range:number}>;
  interiorDecor?:Array<{id:string;roomId:string;floor:number;x:number;z:number}>;
  furnitureSockets?:Array<{id:string;furnitureId:string;roomId:string;floor:number;action:string;x:number;y:number;z:number}>;
  furniture?:Array<{id:string;roomId:string;floor:number;kind:string;x0:number;y0?:number;z0:number;x1:number;y1?:number;z1:number}>;
  roofSections?:Array<{x0:number;z0:number;x1:number;z1:number;base:number;rise:number;ridgeAlongX:boolean}>;
  terraces?:Array<{floor:number;x:number;z:number;height:number}>;
  terraceAccess?:Array<{socketId:string;roomId:string;floor:number;x:number;z:number}>;
  balconies?:Array<{socketId:string;floor:number;x:number;z:number;x0:number;z0:number;x1:number;z1:number;height:number;support:'cantilever'|'columns'}>;
  geometry?:import('./graphs/buildingGeometryGraph').BuildingGeometryReport;
  route?:import('./graphs/buildingRoomTopology').BuildingRouteResult;
  topology?: {exteriorApertures?:import('./graphs/buildingRoomTopology').TopologyExteriorAperture[];version:1;rooms:import('./graphs/buildingRoomTopology').TopologyRoom[];connections:import('./graphs/buildingRoomTopology').TopologyConnection[];components:number;withoutElevatorsComponents:number};
  sockets?:Array<{socketId:string;kind:StudioBuildingFeatureKind;
    side:string;featureIndex:number;width:number;height:number;floor?:number;
    open:number;outside:[number,number,number]}>;
}
export interface StudioBuildingGenerated { path: string; recipePath?: string; guid: string; bytes: number; triangles: number }
export interface StudioSpectraCameraSnapshot {
  yaw:number;pitch:number;distance:number;targetX:number;targetY:number;targetZ:number;ortho:boolean;
}
export interface StudioSpectraPreviewState {
  dispatch?:{sequence:number;action:string;elapsedMs:number;failed:number};
  ready:boolean;playing:boolean;gpu:boolean;backend:string;
  tick:number;targetTick:number;timeSeconds:number;
  generation:number;alive:number;error:string;
  gpuOwnedAllocationRequested?:boolean;gpuOwnedAllocation?:boolean;aliveEstimated?:boolean;
  gpuAllocatedCapacity?:number;gpuBytes?:number;gpuLiveCompactions?:number;gpuAllocationBudgetDenied?:number;
  volume?:boolean;hybrid?:boolean;volumeRendered?:boolean;volumeDimension?:number;volumeBytes?:number;volumeImpulse?:boolean;
  speed:number;loop:boolean;
  bursts:number;queuedBursts:number;clearing:boolean;
  checkpoints?:Array<{id:number;tick:number}>;
  checkpointPending?:boolean;
  transferId?:number;
  fileChunk?:string;fileSize?:number;
  checkpointWaitFrames?:number;
  gpuSeedPending?:boolean;
  gpuSeedFailed?:boolean;
  gpuSeedRetries?:number;
  replayReady?:boolean;
  replayPending?:boolean;
  replayAttemptGeneration?:number;
  replayOutcome?:'pending'|'committed'|'failed'|'cancelled'|'';
  replayError?:string;
  capture?:{
    pending:boolean;ready:boolean;captureId?:number;cancelled?:boolean;
    width?:number;height?:number;mime?:'image/jpeg';
    image?:string;error?:string;
    provenance?:{tick?:number;generation?:number;backend?:string;camera?:StudioSpectraCameraSnapshot;background?:string;gpu?:boolean;sampling?:string;replayReady?:boolean;
      frameSerialAtRequest?:number;completedFrameSerialAtRequest?:number;
      submissionFenceAtRequest?:number;completedFenceAtRequest?:number;frameExact?:boolean};
  };
  background:'dark'|'neutral'|'light'|'black';
  grid:boolean;axes:boolean;
  camera?:StudioSpectraCameraSnapshot;
}
export interface StudioParams {
  'studio.animationRuntime': {project:string;panel:'graph'|'rig'};
  'studio.animationEditorState': {project:string;panel:'graph'|'rig';graph:object|null;dirty:boolean;token:number;error:string};
  'studio.buildingSurfacePreview': {style:string;slot:string;resolution?:128|256|512};
  'studio.buildingPublishMaterial': {style:string;slot:string;name:string;resolution:128|256|512};
  'studio.buildingCatalog': Record<string, never>;
  'studio.buildingStaleMaterials': Record<string, never>;
  'studio.buildingRegenerate': {recipe:string};
  'studio.buildingRecipes': Record<string,never>;
  'studio.buildingReadRecipe': {recipe:string};
  'studio.buildingTextureProvenance': {guid:string};
  'studio.buildingGraphOverlay': {sceneId:string;layers?:number};
  'studio.buildingInspect': StudioBuildingRequest & {routeQuery?:import('./graphs/buildingRoomTopology').BuildingRouteQuery};
  'studio.buildingPreview': StudioBuildingRequest;
  'studio.buildingPreviewView': {token:string;yaw:number;pitch:number;zoom:number;panX:number;panY:number};
  'studio.buildingPreviewStop': {token:string};
  'studio.buildingGenerate': StudioBuildingRequest;
  'studio.spectraPreview': {
    expectedProjectPath?:string;
    expectedGeneration?:number;
    action:'status'|'load'|'play'|'pause'|'restart'|'step'|'seek'|'parameter'|'stop'|'cameraReset'|'cameraOrbit'|'cameraZoom'|'stage'|'cameraPreset'|'cameraRestore'|'replayPrepare'|'replayRestore'|'replayCancel'|'checkpointSave'|'checkpointRestore'|'checkpointCancel'|'checkpointFileBegin'|'checkpointFileAppend'|'checkpointFileExport'|'checkpointFileRead'|'checkpointFileImport'|'checkpointFileCancel'|'playback'|'burst'|'clear'|'capture'|'captureStatus'|'captureCancel';
    graph?:Record<string,unknown>;
    tick?:number;
    name?:string;
    value?:number;
    play?:boolean;
    dx?:number;dy?:number;amount?:number;
    background?:'dark'|'neutral'|'light'|'black';
    grid?:boolean;axes?:boolean;
    preset?:'perspective'|'front'|'side'|'top';
    camera?:StudioSpectraCameraSnapshot;
    parameters?:Record<string,number>;
    seed?:number;
    gpuShaderSeed?:number;
    checkpointId?:number;
    offset?:number;chunk?:string;transferId?:number;
    captureId?:number;
    gpuBirthRandom?:boolean;
    gpuOwnedAllocation?:boolean;
    requireReplayReady?:boolean;
    gpuAllocationSeed?:number;
    speed?:number;loop?:boolean;count?:number;maxSide?:number;
  };
  'studio.worldProcessAreaPreview': {owner:string;revision:number;selectionKey:string;clear?:boolean;scaleNumerator?:number;scaleNumeratorZ?:number;pivot?:'shared'|'individual'|'custom';pivotXcm?:number|null;pivotZcm?:number|null};
  'studio.worldProcessGroupPivot': Record<string,never>;
  'studio.worldProcessAreaBatchResize': {scope?:StudioGraphDocumentScope;entityIds:string[];axis:'x'|'z'|'both';pivot:'shared'|'individual'|'custom';scaleNumerator:number;scaleDenominator:number;scaleNumeratorZ?:number;scaleDenominatorZ?:number;pivotXcm?:number|null;pivotZcm?:number|null};
  'studio.wind':Record<string,never>;
  'studio.windSet':Partial<WindSettings>;
  'studio.nereidFlow':Record<string,never>;
  'studio.nereidFlowSet':NereidFlowInput;
  'studio.legacyCameraBookmarks': {project:string};
  'studio.standalonePlay': {project:string};
  'studio.networkPlay': {project:string;mode:'listen'|'connect';address?:string};
  'studio.gameStart': {entry: 'map' | 'menu'; fromScene?: boolean;localSeat?:number;scope?:StudioGraphDocumentScope};
  'studio.gameStop': Record<string,never>;
  'studio.scriptWatch': {restart?:boolean};
  'studio.gameScriptLog': Record<string,never>;
  'studio.gameState': { scriptErrors?: boolean; geometryProject?: string };
  'studio.gameProfile': {expectedProject:string;action:'status'|'start'|'stop';steps?:number};
  'studio.gameRegions': {expectedProject:string};
  'studio.gameEntities': {offset:number;limit:number;query:string;expectedProject?:string;group?:number;path?:string[];collapsed?:string[]};
  'studio.gameSelectEntity': {epoch:string;ref:string;expectedProject:string};
  'studio.gameInspect': {epoch:string;ref:string;expectedProject?:string};
  'studio.gameEdit': {expectedProject:string;epoch:string;ref:string;expectedTick:number;field:StudioRuntimeEditField['id'];expectedRaw:string;value:string};
  'studio.gameDevices': Record<string,never>;
  'studio.gameDevice': Pick<StudioGameDevice,'id'|'landscape'|'safeArea'>;
  'studio.gameControl': {action:'pause'|'resume'|'step'|'restart'|'checkpointSave'|'checkpointRestore'|'speed1'|'speed2'|'speed5'|'speed10'|'speed25'|'speed50'|'speed100'} | {action:'setSpeed';speed:number};
  'studio.projectRule': {project:string;group:ProjectRuleGroup;id:string;original?:string|null;value?:unknown};
  'studio.project': Record<string, never>;
  'studio.cavernsGraphInspect': {scope:StudioGraphDocumentScope};
  'studio.newMap': {width: number; height: number; name: string};
  'studio.openMap': {file: string};
  'studio.saveAs': Record<string, never>;
  'studio.mapProperties': {scope:StudioGraphDocumentScope;validate?:boolean};
  'studio.playerStartPick': {token:string;armed:boolean};
  'studio.roadPick': {token:string;armed:boolean;scope?:StudioGraphDocumentScope};
  'studio.areaPick': {token:string;armed:boolean};
  'studio.mapRegions': {scope:StudioGraphDocumentScope;letter:string};
  'studio.selectedRoadSet': {scope:StudioGraphDocumentScope;ids:string[];linked:boolean};
  'studio.mapRegionName': {scope:StudioGraphDocumentScope;letter:string;name:string};
  'studio.mapArea': {scope:StudioGraphDocumentScope;action:'create'|'edit'|'remove';name:string;newName?:string;rect?:number[]};
  'studio.mapPlayer': {scope:StudioGraphDocumentScope;slot:number;action:'edit'|'add'|'remove';changes?:PlayerChanges};
  'studio.mapPropertiesSet': {scope:StudioGraphDocumentScope;changes:StudioMapChanges};
  'studio.hierarchyGroups': {expectedProject:string;scope:StudioGraphDocumentScope;group?:number;refresh?:boolean};
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
  'studio.humanMetaHuman': { guid:string };
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
  'studio.setProperties': { scope?: StudioGraphDocumentScope; entityId?: string; entityIds?: string[]; componentId?: string; values: Record<string, string | number | boolean> };
  'studio.entitiesDelete': { entityIds: string[] };
  'studio.entitiesDuplicate': { entityIds: string[] };
  'studio.entityFlags': { entityIds: string[]; hidden?: boolean; locked?: boolean };
  'studio.assetPlace': { asset: string; x: number; z: number };
  'studio.rulesDocument': {section:'global'|'document'|'actorSimulation'|'actorMovement'|'actorLoadout'};
  'studio.rulesDocumentSave': {section:'global'|'document'|'actorSimulation'|'actorMovement'|'actorLoadout';projectPath:string;original:string;value:unknown};
  'studio.planetSceneBody': {body?:string;mapPath?:string;expectedRevision?:string};
  'studio.planetForestArm': {armed:boolean;mapPath?:string;expectedRevision?:string;species?:number;key?:number;habit?:'tree'|'shrub';action?:'plant'|'pick'|'move'};
  'studio.planetForestMove': {mapPath:string;expectedRevision:string;species:number;key:number;at:{body:string;face:number;x:number;y:number}};
  'studio.planetForestPlant': {mapPath:string;expectedRevision:string;species:number;habit:'tree'|'shrub';at:{body:string;face:number;x:number;y:number}};
  'studio.planetForest': Record<string, never>;
  'studio.planetForestSet': {mapPath:string;expectedRevision:string;forest:unknown;catalog:unknown};
  'studio.physisNature': Omit<StudioParams['studio.physisLive'],'action'> & {action:'view'|'reset'|'step'|'step60'|'ignite'|'wind'|'playback'|'grid'|'presentation';quality?:number;volume?:number;igniteOnClick?:boolean;forest?:boolean;forestMask?:string;treeGeometry?:string;hierarchical?:boolean;horizon?:boolean;lodScale?:number;grass?:boolean;flames?:boolean;sound?:boolean;humidity?:number;cellMeters?:number;elapsed?:number;speed?:number;windSpeed?:number;windDirection?:number;expectedDocumentEpoch:string};
  'studio.physisRemoteFields': {action:'status'|'apply'|'refresh'|'retry';expectedProject:string;expectedDocumentEpoch:string;connectionVersion?:string;remoteRevision?:string;mapRevision?:string;requestId?:string};
  'studio.physisFieldEdit': {action:'status'|'apply';expectedProject:string;expectedDocumentEpoch:string;runtimeEpoch?:string;mapRevision?:string;fieldRevision?:string};
  'studio.physisLive': {action?:'view'|'presentation';expectedDocumentEpoch?:string;runtimeEpoch?:string;fieldOverlay?:boolean;overlayRadius?:number;overlayStride?:number;overlayAlpha?:number;overlayVolumes?:boolean;quality?:number;volume?:number;grass?:boolean;flames?:boolean;sound?:boolean;expectedProject:string;layer:number;palette:number;size:number;stride:number;centerX:number;centerZ:number;cellX?:number;cellZ?:number};
  'studio.planetForestLive': Record<string, never>;
  'studio.rules': Record<string, never>;
  'studio.rulesSet': { path: string; value: unknown };
  /** Open (or focus) floating window `window` holding `panels`; ifOutside opens only when the mouse is outside every Studio window. */
  'studio.windowOpen': { window: string; panels?: string[]; title?: string; ifOutside?: boolean; x?: number; y?: number; width?: number; height?: number };
  'studio.windowClose': { window: string };
  /** A floating window names itself (its active tab). */
  'studio.windowTitle': { title: string };
  'studio.buildInfo': Record<string,never>;
  'studio.entitiesAlign': { entityIds: string[]; mode: StudioAlignMode };
  'studio.history': Record<string,never>;
  'studio.projectHistory': Record<string,never>;
  'studio.projectHistoryStep': { direction: 'undo' | 'redo'; expectedVersion: string };
  'studio.assetHistory': Record<string,never>;
  'studio.assetHistoryStep': { direction: 'undo' | 'redo'; expectedVersion: string };
  'studio.tileAtlas': {project:string; tileset:string};
  'studio.assetThumbnail': { path: string };
  'studio.assetUsage': { path?: string };
  'studio.historyJump': { position: number; expectedVersion?:string };
  /** Tool panel ops (Terrain, Roads, PostFX, ...) with their JSON schemas. */
  'studio.ops': { panel: string };
  /** Run one Workspace op of a tool panel (validated, undoable like an agent edit). */
  'studio.op': { op: string; params: Record<string, unknown>; expectedDocumentRevision?:string; expectedProject?: string; expectedMap?: string; expectedDocumentEpoch?: string };
  'studio.inputGet': Record<string,never>;
  'studio.inputSet': { asset: StudioInputAsset };
  'studio.assetAudit': Record<string,never>;
  /** Agent panel: transcript lines from `from`, change list, link status. */
  'studio.agentInfo': { from?: number; briefForProject?: string; reportForProject?: string };
  'studio.worldProcessDiagnostics': {budget:number;focus?:string};
  'studio.evolveState': Record<string, never>;
  'studio.evolveLayer': {layer?: EvolveLayer;live?:boolean;granularCellCm?:number;liveGravity?:boolean;aetherLayer?:AetherLayer;liveFracture?:boolean;liveMorph?:boolean;liveEcho?:boolean;liveCrowd?:boolean;liveCircuit?:boolean;liveSociety?:boolean;liveForest?:boolean};
  'studio.agentHistoryStep': { scope:'map'|'project'|'assets';historyId:string;expectedVersion:string;direction:'undo'|'redo' };
  'studio.agentUndoAll': {expectedVersion:string};
  'studio.agentBatchPreview': {direction:'undo'|'redo'};
  'studio.agentBatchApply': {direction:'undo'|'redo';token:string;confirmed:boolean};
  'studio.sceneBrush': Partial<StudioSceneBrush> & { scope?:StudioGraphDocumentScope; expectedProject?: string; expectedMap?: string };
  'studio.splineEdit': { kind: '' | 'road' | 'river' };
  'studio.hub': Record<string,never>;
  'studio.hubTemplates': { all?: boolean; query?: string; refresh?: boolean };
  'studio.wizardDictation': { action: 'available' | 'start' | 'poll' | 'stop' | 'cancel'; owner?: string; language?: string };
  'studio.wizardFetchArt': { url: string };
  'studio.wizardArt': Record<string, never>;
  'studio.wizardCreate': WizardPlanInput;
  'studio.wizardOptions': Record<string, never>;
  'studio.wizardPlan': WizardPlanInput;
  'studio.hubForget': { dir: string };
  'studio.projectPort': {action:'scan'|'import';path:string;out?:string;mode?:string;scale?:number};
  'studio.portFolder': Record<string,never>;
  'studio.openImported': {dir:string};
  'studio.hubOpen': { dir: string };
  'studio.hubCreate': { template: string; name: string };
  'studio.stats': Record<string,never>;
  'studio.cavernsPreview': { expectedProject:string; expectedMap:string; enabled?:boolean; isolate?:boolean; paging?:boolean; budgets?:StudioCavernsBudgets; globeBody?:string };
  'studio.gpuStats': { textures?: boolean; prisma?: boolean; project?: string };
  'studio.previewPlayChanges': {expectedProject:string;positions:boolean;amounts:boolean;removeDead:boolean};
  'studio.keepPlayChanges': {expectedProject:string;token:string};
  'studio.extensions': Record<string,never>;
  'studio.extensionAction': { id: string; value?: string | number | boolean };
  'studio.extensionCreate': Record<string,never>;
  'studio.lensPreview': {project:string;close?:boolean;copyScene?:boolean;useInScene?:boolean;capture?:boolean;document?:unknown;id?:number};
  'studio.importedClipPreview': {project:string;model?:string;clip?:number;playing?:boolean;time?:number;status?:boolean;close?:boolean};
  'studio.sourceModelPreview': {project:string;model?:string;type?:string;owner?:number;playing?:boolean;yaw?:number;pitch?:number;close?:boolean};
  'studio.attachmentsPreview': {project:string;close?:boolean;status?:boolean;document?:StudioAttachmentDoc;playing?:boolean;time?:number;yaw?:number;pitch?:number};
  'studio.attachmentsGet': { path?: string; project?:string };
  'studio.attachmentsSet': { path?: string; project?:string; expected?:StudioAttachmentDoc|null; document: StudioAttachmentDoc };
  'studio.agentStart': { useWizardBrief?: boolean; prompt?: string; from?: number; client?: 'claude-code'|'codex'; expectedProject?: string };
  'studio.agentStop': { from?: number };
  'studio.agentReset': { from?: number };
  'studio.buildToolchain': { target: string };
  'studio.buildStart': { target: string; config: 'Release' | 'Debug'; installMissing?: boolean; estimate?: boolean };
  'studio.buildLog': { from: number; run?: number; project?: string };
  'studio.buildEstimate': { target: string };
  /** Show a build folder (or the last build log) in Finder. */
  'studio.buildReveal': { target?: string; log?: boolean; logFolder?:boolean };
  /** UI-only event (name starts with "ui.") delivered to every Studio window. */
  'studio.panelInventory': {windows:{id:string;title:string;group:string;open:boolean}[]};
  'studio.windowBroadcast': { event: string; payload: unknown };
  'studio.save': Record<string, never>;
  'studio.undo': Record<string, never>;
  'studio.redo': Record<string, never>;
  'studio.openProject': Record<string, never>;
  'studio.spectraAssetCheck': {guid:string};
  'studio.projectDirectory': {expectedProject:string; folder:string};
  'studio.assets': Record<string, never>;
  'studio.assetByGuid': { guid: string };
  'studio.assetReferences': { path?: string; depth?: number };
  'studio.assetDependencyPage': {expectedProject:string;path:string;depth:number;direction:'all'|'incoming'|'outgoing';offset:number;limit:number;expectedToken?:string};
  'studio.assetMetaPatch': {guid:string;key:string;value:string|number|boolean;expectedProject?:string;expectedDocumentEpoch?:string};
  'studio.assetFolderCreate': { parent: string; name: string };
  'studio.assetDelete': { path:string; guid:string; expectedProject:string; expectedVersion:string; confirmed:boolean };
  'studio.assetRecoveryQuota': { expectedProject:string;expectedMiB:number;quotaMiB:number };
  'studio.assetHistoryResetReview': {expectedProject:string};
  'studio.assetHistoryResetApply': {expectedProject:string;token:string;expectedVersion:string;confirmed:boolean};
  'studio.assetTrashList': { expectedProject:string;offset:number;limit:number };
  'studio.assetTrashBatchReview': {expectedProject:string;ids:string[]};
  'studio.assetTrashBatchApply': {expectedProject:string;ids:string[];token:string;expectedVersion:string;confirmed:boolean};
  'studio.assetTrashReview': { expectedProject:string;id:string;action:StudioTrashAction };
  'studio.assetTrashApply': { expectedProject:string;id:string;action:StudioTrashAction;token:string;expectedVersion:string;confirmed:boolean };
  'studio.assetMovePreview': { paths: string[]; destination: string; newName?: string };
  'studio.spectraPackageChoose': Record<string,never>;
  'studio.assetImportPreview': { destination: string };
  'studio.assetTransferCommit': { token: string; decisions: Array<{id:number;action:StudioAssetConflictAction}> };
  'studio.materialsToFiles': Record<string, never>;
  'studio.materialPublicationRollbackPreview': {name:string;expectedProject:string;expectedDocumentEpoch:string};
  'studio.materialPublicationRollbackEmpty': {name:string;expectedProject:string;expectedDocumentEpoch:string};
  'studio.materialPublicationQuarantine': {id:string;expectedProject:string;expectedDocumentEpoch:string};
  'studio.materialPublicationRestore': {id:string;expectedProject:string;expectedDocumentEpoch:string};
  'studio.materialPublicationRetry': {name:string;expectedProject:string;expectedDocumentEpoch:string};
  'studio.materialPublicationFinalize': {name:string;expectedProject:string;expectedDocumentEpoch:string};
  'studio.materialPublicationRepair': {name:string;filename:string;expectedProject:string;expectedDocumentEpoch:string};
  'studio.materials': Record<string, never>;
  'studio.materialPatch': { name: string; patch: import('./panels/materialSchema').MaterialPatch };
  'studio.materialCreate': { name: string; preset: import('./panels/materialSchema').MaterialPreset; colorGuid?:string; normalGuid?:string; repeatMetres?:number };
  'studio.shaderGraphPreview': {name:string;projectPath:string;graph:StudioShaderGraphDocument['graph']};
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
  'studio.shaderGraphSave': { name: string; graph: StudioShaderGraphDocument['graph']; expected?: string | StudioShaderGraphDocument['graph']; expectedProject?: string; expectedDocumentEpoch?:string };
  'studio.placementGhost': {placeType:string;x?:number;z?:number;expectedDocumentEpoch:string;expectedDocumentRevision:string};
  'studio.gpuEmitterPreview': {project:string;source?:string;restart?:boolean;paused?:boolean;step?:boolean;orbitX?:number;orbitY?:number;zoom?:number;resetCamera?:boolean;close?:boolean};
  'studio.effectFilePreview': {project:string;source?:string;speed?:number;loop?:boolean;moving?:boolean;quality?:number;orbitX?:number;orbitY?:number;zoom?:number;resetCamera?:boolean;close?:boolean};
  'studio.lightingPreview': {key:'weather';value:StudioPreviewWeather;expectedDocumentEpoch:string};
  'studio.renderLighting': Record<string, never>;
  'studio.renderLightingSet': { patch: Partial<RenderLightingSettings> };
  'studio.lighting': Record<string, never>;
  'studio.lightingSet': ({ key: import('./panels/lightingSchema').LightingScalarKey; value: number } | { key:'preset'; value:string } | { key:'remove'; value:true } | { key:'weather'; value:StudioPreviewWeather }) & {expectedDocumentEpoch?:string};
  'studio.lightBakeJob': {action:'start'|'cancel'|'status'; expectedDocumentEpoch:string; force?:boolean};
  'studio.lightBakeDisplay': {enabled?:boolean};
  'studio.lightBakeAtlas': { expectedDocumentEpoch:string; hash:string; page:number; channel:'lightmap'|'mask' };
  'studio.lightAdd': { x?: number; z?: number; atSceneTarget?: boolean; expectedDocumentEpoch?: string };
  'studio.lightUpdate': { index: number; entityId?: string } & ({key: import('./panels/lightingSchema').LightEditableKey; value: string | number | boolean} | {key:'color'; value:number[]});
  'studio.lightDelete': { index: number; entityId?: string };
  'studio.select': { id?: string | null; ids?: string[]; mode?: 'replace' | 'add' | 'toggle' } | {previewRef:string;scope:StudioGraphDocumentScope;historyVersion:string};
  'studio.setProperty': { entityId: string; componentId?: string; key: string; value: string | number | boolean };
  'studio.viewportRect': { id: 'importedClip' | 'sourceModel' | 'scene' | 'game' | 'fx' | 'material' | 'shader' | 'facialRig' | 'character' | 'appearance' | 'animationGraph' | 'animationRig' | 'attachments' | 'lensCamera' | 'lighting' | 'effectFile' | 'gpuEmitter' | 'nature'; liveNature?:boolean; x: number; y: number; width: number; height: number; dpr: number; visible: boolean };
  'studio.viewportInput': ({kind:'erosionHighlight';index?:number} & StudioGraphDocumentScope) | {kind:'focusLight'; entityId:string; expectedDocumentEpoch:string} | {surface:'character'|'appearance';kind:'brushCursor';x:number;y:number;radius:number} | {surface:'character'|'appearance';kind:'reference';image?:string;opacity:number;scale:number;x:number;y:number;mirror:boolean;visible:boolean} | {surface:'character'|'appearance';kind:'pick';x:number;y:number} | {surface:'character'|'appearance';kind:'orbit';dx:number;dy:number} | {surface:'character'|'appearance';kind:'zoom';amount:number} | { kind: 'orbit' | 'pan' | 'look'; dx: number; dy: number } | { kind: 'move'; right: number; up: number; forward: number } | { kind: 'focus' | 'reset' | 'frameAll' } | { kind: 'camera'; set?: { x: number; y: number; z: number; distance: number; yaw: number; pitch: number; ortho?: boolean } } | { kind: 'zoom'; amount: number } | { kind: 'view'; yaw: number; pitch: number } | { kind: 'projection'; ortho: boolean } | { kind: 'hover'; id: string } | { kind: 'tool'; tool?: 'view' | 'move' | 'rotate' | 'scale'; local?: boolean; snap?: boolean; snapMove?: number; snapRotate?: number; snapScale?: number; gizmos?: boolean; natureRadius?:number; natureSpacing?:number; natureWind?:boolean; natureFire?:boolean; worldAxes?:boolean; objectBounds?:boolean; selectionBox?:boolean; surface?: boolean } | { kind:'areaGesture'; token:string; phase:'down'|'drag'|'up'; x:number; y:number; width:number; height:number } | { kind: 'handle'; phase: 'down' | 'drag' | 'up'; x: number; y: number; width: number; height: number; invertSnap?: boolean } | { kind: 'pick'; x: number; y: number; width: number; height: number; roadToken?:string;chain?:boolean };
}
export interface StudioResult {
  'studio.animationRuntime': RuntimeAnimationRow[];
  'studio.animationEditorState': Record<string, never>;
  'studio.buildingSurfacePreview': StudioBuildingSurfacePreview;
  'studio.buildingPublishMaterial': {name:string;path:string;assets:Array<{path:string;guid:string;channel:string}>};
  'studio.buildingCatalog': StudioBuildingCatalog;
  'studio.buildingStaleMaterials': {stale:Array<{recipe:string;changes:Array<{guid:string;reason:string}>}>};
  'studio.buildingRegenerate': StudioBuildingGenerated & {sourceRecipe:string;createdNewAsset:boolean};
  'studio.buildingRecipes': {recipes:string[]};
  'studio.buildingReadRecipe': {recipe:string;request:StudioBuildingRequest};
  'studio.buildingTextureProvenance': {guid:string;path:string;manifestFound:boolean;manifestPath?:string;output?:string;graph?:unknown;overrides?:unknown};
  'studio.buildingGraphOverlay': {active:boolean};
  'studio.buildingInspect': StudioBuildingPreview;
  'studio.buildingPreview': {active:boolean;token:string;url:string};
  'studio.buildingPreviewView': {active:boolean;token:string;url:string};
  'studio.buildingPreviewStop': {active:boolean};
  'studio.buildingGenerate': StudioBuildingGenerated;
  'studio.spectraPreview': StudioSpectraPreviewState;
  'studio.worldProcessAreaPreview': {ok?:boolean;stale?:boolean};
  'studio.worldProcessGroupPivot': {pivotXcm:number;pivotZcm:number;custom:boolean};
  'studio.worldProcessAreaBatchResize': number;
  'studio.wind':WindState;
  'studio.windSet':WindState;
  'studio.nereidFlow':NereidFlowState;
  'studio.nereidFlowSet':NereidFlowState;
  'studio.legacyCameraBookmarks': import('./components/sceneBookmarks').CameraBookmark[];
  'studio.standalonePlay': Record<string,never>;
  'studio.networkPlay': Record<string,never>;
  'studio.gameStart': StudioGameState;
  'studio.gameStop': StudioGameState;
  'studio.scriptWatch': {restart:boolean};
  'studio.gameScriptLog': StudioScriptMessage[];
  'studio.gameState': StudioGameState;
  'studio.gameProfile': StudioGameProfile;
  'studio.gameRegions': StudioLiveWorldRegions;
  'studio.gameEntities': StudioRuntimePage;
  'studio.gameSelectEntity': StudioGameState;
  'studio.gameInspect': StudioRuntimeEntity;
  'studio.gameEdit': {field:StudioRuntimeEditField;runtimeDebugEdited:true};
  'studio.gameDevices': StudioGameDeviceOption[];
  'studio.gameDevice': StudioGameState;
  'studio.gameControl': StudioGameState;
  'studio.projectRule': {text:string};
  'studio.project': StudioProjectState;
  'studio.cavernsGraphInspect': StudioCaveGraphCapture;
  'studio.newMap': StudioSnapshot;
  'studio.openMap': StudioSnapshot;
  'studio.saveAs': StudioSnapshot;
  'studio.mapProperties': StudioMapProperties;
  'studio.playerStartPick': Record<string,never>;
  'studio.roadPick': Record<string,never>;
  'studio.areaPick': Record<string,never>;
  'studio.mapRegions': StudioMapRegions;
  'studio.selectedRoadSet': StudioSnapshot;
  'studio.mapRegionName': StudioSnapshot;
  'studio.mapArea': StudioSnapshot;
  'studio.mapPlayer': StudioSnapshot;
  'studio.mapPropertiesSet': StudioSnapshot;
  'studio.hierarchyGroups': {pending:boolean;preview?:StudioEntity[];paths?:StudioHierarchyPaths;parents?:Record<string,string>};
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
  'studio.humanMetaHuman': MetaHumanReport;
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
  'studio.rulesDocument': {text:string};
  'studio.rulesDocumentSave': {text:string};
  'studio.planetSceneBody': {override:string;activeBody:string};
  'studio.planetForestArm': {armed:boolean};
  'studio.planetForestPlant': StudioPlanetForestDocument;
  'studio.planetForestMove': StudioPlanetForestDocument;
  'studio.planetForest': StudioPlanetForestDocument;
  'studio.planetForestSet': StudioPlanetForestDocument;
  'studio.physisNature': StudioPhysisFrame|{active:false};
  'studio.physisRemoteFields': {available:boolean;status:string;message:string;canRefresh:boolean;canRetry:boolean;connectionVersion:string;mapRevision:string;remoteRevision:string;requestId:string;receipt?:string};
  'studio.physisFieldEdit': {available:boolean;status:string;message:string;runtimeEpoch?:string;mapRevision?:string;fieldRevision?:string;tick?:number;scheduledTick?:number};
  'studio.physisLive': StudioPhysisFrame|{active:false};
  'studio.planetForestLive': StudioPlanetForestLive;
  'studio.rules': StudioRules;
  'studio.rulesSet': StudioRules;
  'studio.windowOpen': { opened: boolean };
  'studio.windowClose': { closed: boolean };
  'studio.panelInventory': {ok:boolean};
  'studio.windowBroadcast': { ok: boolean };
  'studio.windowTitle': { ok: boolean };
  'studio.buildInfo': StudioBuildInfo;
  'studio.entitiesAlign': StudioSnapshot;
  'studio.history': StudioHistory;
  'studio.projectHistory': StudioProjectHistory;
  'studio.projectHistoryStep': StudioProjectHistory & { snapshot: StudioSnapshot };
  'studio.assetHistory': StudioAssetHistory;
  'studio.assetHistoryStep': StudioAssetHistory & { snapshot: StudioSnapshot };
  'studio.tileAtlas': {width:number; height:number; tileWidth:number; tileHeight:number; columns:number; url:string};
  'studio.assetThumbnail': { url?: string; pending?: boolean; none?: boolean };
  'studio.assetUsage': { path?: string; mentions?: Array<{ file: string; line: number; text: string }>; unused?: string[]; scannedFiles?: number };
  'studio.historyJump': StudioHistory & { snapshot: StudioSnapshot };
  'studio.ops': { panel: string; ops: StudioToolOp[] };
  'studio.op': { op: string; result: unknown; changed: boolean };
  'studio.inputGet': StudioInputInfo;
  'studio.inputSet': StudioInputInfo;
  'studio.agentInfo': StudioAgentInfo;
  'studio.worldProcessDiagnostics': WorldProcessDiagnostics;
  'studio.evolveState': EvolveLiveState;
  'studio.evolveLayer': {layer: EvolveLayer;live:boolean;granularCellCm?:number;liveGravity?:boolean;aetherLayer?:AetherLayer;liveFracture?:boolean;liveMorph?:boolean;liveEcho?:boolean;liveCrowd?:boolean;liveCircuit?:boolean;liveSociety?:boolean;liveForest?:boolean};
  'studio.agentHistoryStep': StudioSnapshot;
  'studio.agentUndoAll': StudioSnapshot;
  'studio.agentBatchPreview': StudioAgentBatch;
  'studio.agentBatchApply': StudioSnapshot;
  'studio.sceneBrush': StudioSceneBrush;
  'studio.hub': StudioHub;
  'studio.hubTemplates': { templates: StudioTemplate[]; error?: string };
  'studio.wizardDictation': { supported: boolean; phase: 'idle' | 'starting' | 'listening' | 'finishing' | 'done'; text?: string; error?: string };
  'studio.wizardFetchArt': { dir: string };
  'studio.wizardArt': { dir: string };
  'studio.wizardCreate': { snapshot?: StudioSnapshot; cancelled?: boolean };
  'studio.wizardOptions': { agentClients?: ('codex'|'claude-code')[]; templates: { id: string; name: string }[]; features: { id: string; description: string }[]; tutorialModes: string[]; mapPresets: string[]; mapSizes: string[]; languages: string[]; platforms: string[]; stores: string[]; defaults: WizardPlanInput };
  'studio.wizardPlan': { template: string; templateName: string; reason: string; levelPreset: string; orientation: string; tutorialArchetype: string; levels: number; meta: string[] };
  'studio.hubForget': Record<string, never>;
  'studio.projectPort': {ok?:boolean;engine?:string;version?:string;counts?:Record<string,number>;summary?:Record<string,number>;errors?:string[];error?:string;destination?:string};
  'studio.portFolder': string;
  'studio.openImported': {snapshot?:StudioSnapshot|null;cancelled?:boolean};
  'studio.hubOpen': { snapshot?: StudioSnapshot | null; cancelled?: boolean };
  'studio.hubCreate': { snapshot?: StudioSnapshot | null; cancelled?: boolean };
  'studio.splineEdit': { kind: string; handles: Array<{ id: string; x: number; z: number }>; segments: number[][] };
  'studio.stats': StudioStats;
  'studio.cavernsPreview': StudioCavernsPreview;
  'studio.gpuStats': StudioGpuStats;
  'studio.extensions': StudioExtensions;
  'studio.extensionAction': { command: Record<string, unknown> | null };
  'studio.extensionCreate': StudioExtensions;
  'studio.lensPreview': {position?:number[];rotation?:number[];camera?:{projection:string;fov:number;near:number;far:number;orthographicHalfHeight:number}};
  'studio.importedClipPreview': {clips?:{name:string;duration:number}[];time?:number};
  'studio.sourceModelPreview': {duration?:number;time?:number};
  'studio.attachmentsPreview': {time?:number;children?:number;duration?:number;sockets?:string[]};
  'studio.attachmentsGet': StudioAttachmentsResult;
  'studio.attachmentsSet': StudioAttachmentsResult;
  'studio.previewPlayChanges': StudioPlayReview;
  'studio.keepPlayChanges': { moved: number; amounts: number; removed: number; skipped: number; snapshot: StudioSnapshot };
  'studio.agentStart': StudioAgentInfo;
  'studio.agentStop': StudioAgentInfo;
  'studio.agentReset': StudioAgentInfo;
  'studio.assetAudit': { missing: Array<{ source: string; reference: string }>; scanned: number; truncated: boolean };
  'studio.buildToolchain': StudioToolchain;
  'studio.buildStart': { started: boolean };
  'studio.buildLog': StudioBuildLog;
  'studio.buildEstimate': StudioBuildEstimate;
  'studio.buildReveal': { ok: boolean };
  'studio.save': StudioSnapshot;
  'studio.undo': StudioSnapshot;
  'studio.redo': StudioSnapshot;
  'studio.openProject': { cancelled: boolean; snapshot: StudioSnapshot | null };
  'studio.spectraAssetCheck': {guid:string;ok:boolean;detail:string;code:string;gpuVerified:false};
  'studio.projectDirectory': {absolute:string; entries:{name:string; directory:boolean}[]};
  'studio.assets': StudioAssetList;
  'studio.assetByGuid': StudioAssetDetail;
  'studio.assetReferences': StudioAssetReference[];
  'studio.assetDependencyPage': StudioDependencyPage;
  'studio.assetMetaPatch': StudioAssetDetail;
  'studio.assetFolderCreate': { path: string };
  'studio.assetDelete': { path:string; backup:string; historyRecorded:boolean };
  'studio.assetRecoveryQuota': StudioRecoveryStorage;
  'studio.assetHistoryResetReview': StudioAssetHistoryResetReview;
  'studio.assetHistoryResetApply': {ok:boolean};
  'studio.assetTrashList': StudioTrashPage;
  'studio.assetTrashBatchReview': StudioTrashBatchReview;
  'studio.assetTrashBatchApply': StudioTrashBatchResult;
  'studio.assetTrashReview': StudioTrashReview;
  'studio.assetTrashApply': {ok:boolean};
  'studio.assetMovePreview': StudioAssetTransferPlan;
  'studio.spectraPackageChoose': {name:string;projectPath:string;path:string}|{cancelled:true};
  'studio.assetImportPreview': StudioAssetTransferPlan | { cancelled: true };
  'studio.assetTransferCommit': StudioAssetTransferResult;
  'studio.materialsToFiles': StudioMaterialLibrary;
  'studio.materialPublicationRollbackPreview': {name:string;blockers:{kind:string;owner:string}[];journalOnlyRollbackAllowed:boolean;contentRollbackAllowed:boolean;coverage:{visited:number;scanned:number;skipped:number;truncated:boolean;dynamicReferencesVerified:boolean}};
  'studio.materialPublicationRollbackEmpty': {rolledBack:boolean;pendingPublications:import('./panels/materialSchema').MaterialPublication[]};
  'studio.materialPublicationQuarantine': {ok:boolean};
  'studio.materialPublicationRestore': {ok:boolean};
  'studio.materialPublicationRetry': {name:string;path:string;assets:Array<{path:string;guid:string;channel:string}>};
  'studio.materialPublicationFinalize': {finalized:boolean;pendingPublications:import('./panels/materialSchema').MaterialPublication[]};
  'studio.materialPublicationRepair': {restored:boolean;pendingPublications:import('./panels/materialSchema').MaterialPublication[]};
  'studio.materials': StudioMaterialLibrary;
  'studio.materialPatch': StudioMaterialLibrary;
  'studio.materialCreate': StudioMaterialLibrary;
  'studio.shaderGraphPreview': {name:string};
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
  'studio.placementGhost': {url:string};
  'studio.gpuEmitterPreview': {paused:boolean;time:number;error:string;gpu?:number;cpu?:number;yaw:number;pitch:number;distance:number};
  'studio.effectFilePreview': {particles:number;drawCalls:number;time:number;yaw:number;pitch:number;distance:number};
  'studio.lightingPreview': Record<string,never>;
  'studio.renderLighting': RenderLightingSettings;
  'studio.renderLightingSet': RenderLightingSettings;
  'studio.lighting': StudioLightingState;
  'studio.lightingSet': StudioLightingState;
  'studio.lightBakeJob': {running:boolean; stage:string; fraction:number; message:string};
  'studio.lightBakeDisplay': {enabled:boolean; loaded?:boolean; lightmapped?:number; probed?:number; message?:string};
  'studio.lightBakeAtlas': {png:string};
  'studio.lightAdd': StudioLightingState;
  'studio.lightUpdate': StudioLightingState;
  'studio.lightDelete': StudioLightingState;
  'studio.select': StudioSnapshot;
  'studio.setProperty': StudioSnapshot;
  'studio.viewportRect': { ok: boolean };
  'studio.viewportInput': { ok?: boolean; hit?: boolean; zones?:string[]; highlight?:number };
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
export interface StudioBuildLog { run:number; from: number; next: number; text: string; size: number; running: boolean; label: string; exitCode: number; runs: number; logFile: string }
export interface StudioBuildEstimate { target: string; pack: Record<string, unknown>; atlas: Record<string, unknown>; split?: Record<string, unknown>;
  heaviest: Array<{ path?: string; file?: string; bytes?: number; storedBytes?: number }> }

export interface MetaHumanReport { pending?:boolean; detected:boolean; score:number; headBone:string; faceMeshes:string[]; hiddenMaterials:string[]; materialRoles:Record<string,string>;
  channelsFound:number; channelsMissing:string[]; facialJoints:number; correctiveJoints:number; skins:number; lodLevels:number; notes:string[]; look:HumanLook; face:HumanFaceShape;
  rig?: { halfJoints:number; twistJoints:number; jawJoint:boolean; eyeJoints:boolean } }
export type StudioAlignMode = 'minX' | 'centerX' | 'maxX' | 'minZ' | 'centerZ' | 'maxZ' | 'distributeX' | 'distributeZ' | 'ground';
/** Undo history: applied steps first (oldest → newest), then the redo branch; position = applied count. */
export interface StudioHistory { steps: Array<{ label: string; done: boolean }>; position: number; version?:string; scope?:'map'; strokeActive?:boolean; persistent?:boolean; persistenceWarning?:string }
export interface StudioProjectHistory { scope:'project'; steps: StudioHistory['steps']; position:number; version:string; strokeActive:boolean; canUndo:boolean; canRedo:boolean; bytes:number; maxBytes:number; maxSteps:number; blockedReason?:string; persistent?:boolean; persistenceWarning?:string }

export type StudioAssetHistory = Omit<StudioProjectHistory,'scope'> & {scope:'assets'};

export interface StudioPlanetForestMarker {id:string;type:string;placement:{body:string;face:number;x:number;y:number}}
export interface StudioPlanetForestDocument {available:boolean;mapPath:string;revision:string;moduleEnabled:boolean;physicsRequested:boolean;override:boolean;forest:unknown;catalog:unknown;markers:StudioPlanetForestMarker[];presets?:string[];bodies?:string[]}
export interface StudioPlanetForestPlant {viewPriority?:number;species:number;key:string;body:string;cell:number;ageTicks:string;supportPermille:number;dead:boolean;fallen:boolean;harvested:boolean;spent:boolean;physical:boolean;remainingUnits:string|null;initialUnits:string|null;pending?:boolean;motionTick?:number|null;awake?:boolean;position?:number[];up?:number[];rootRadiusCm?:number;rootDepthCm?:number;east?:number[];habit?:'tree'|'shrub'|'grass';sizePermille?:number;foliagePermille?:number;exposedCm?:number;visibleRoots?:boolean;windLocal?:number[];stormWindLocal?:number[];mappedWeather?:boolean;seasonPermille?:number;climateTick?:string;capsule?:{center:number[];axis:number[];right?:number[];radius:number;halfHeight:number}}
export interface StudioPlanetForestLive {active:boolean;focusBody?:string;focusPlantCount?:number;cameraPrioritized?:boolean;viewCandidates?:number;tick?:number;plantCount?:number;deadCount?:number;fallenCount?:number;spentCount?:number;physicalCount?:number;pendingCount?:number;rejected?:number;truncated?:boolean;windZonesCapped?:boolean;stormsCapped?:boolean;plants?:StudioPlanetForestPlant[]}
