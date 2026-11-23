import type { StudioMethod, StudioParams, StudioResult, StudioSnapshot } from './types';

let snapshot: StudioSnapshot = {
  project: { name: 'AIVERSE Demo', path: 'DEMO / no native project', dirty: false },
  scene: {
    name: 'Sample scene',
    entities: [
      { id: 'root', name: 'Environment', kind: 'Group', visible: true },
      { id: 'sun', name: 'Directional Light', kind: 'Light', parentId: 'root', visible: true },
      { id: 'terrain', name: 'Terrain', kind: 'Mesh', parentId: 'root', visible: true },
      { id: 'camera', name: 'Main Camera', kind: 'Camera', visible: true },
      { id: 'player', name: 'Player', kind: 'Entity', visible: true }
    ]
  },
  selectedId: 'camera',
  properties: [
    { key: 'name', label: 'Name', type: 'text', value: 'Main Camera' },
    { key: 'x', label: 'Position X', type: 'number', value: 0 },
    { key: 'y', label: 'Position Y', type: 'number', value: 3 },
    { key: 'z', label: 'Position Z', type: 'number', value: -6 },
    { key: 'enabled', label: 'Enabled', type: 'boolean', value: true }
  ],
  logs: [{ level: 'info', text: 'HTML5 Studio preview loaded — native engine is not connected.', time: 'preview' }],
  mode: 'edit',
  history: { canUndo: false, canRedo: false }
};
const entityFields = new Map<string, StudioSnapshot['properties']>();
const fieldsFor = (id: string): StudioSnapshot['properties'] => {
  const cached = entityFields.get(id);
  if (cached) return cached;
  const entity = snapshot.scene.entities.find(item => item.id === id);
  if (!entity) throw Error('Unknown entity: ' + id);
  const fields: StudioSnapshot['properties'] = [
    { key: 'name', label: 'Name', type: 'text', value: entity.name },
    { key: 'x', label: 'Position X', type: 'number', value: 0 },
    { key: 'y', label: 'Position Y', type: 'number', value: 0 },
    { key: 'z', label: 'Position Z', type: 'number', value: 0 },
    { key: 'enabled', label: 'Enabled', type: 'boolean', value: true }
  ];
  entityFields.set(id, fields);
  return fields;
};
// DEMO inspector: the demo has no document components, only these fields.
const demoInspector = (id: string): NonNullable<StudioSnapshot['inspector']> => {
  const entity = snapshot.scene.entities.find(item => item.id === id);
  if (!entity) throw Error('Unknown entity: ' + id);
  const fields = structuredClone(fieldsFor(id)).filter(field => field.key !== 'name');
  return { id, name: entity.name, kind: entity.kind, nameEditable: true,
    components: [{ id: 'Transform', title: 'Transform', note: 'Browser DEMO data, not a project document.', properties: fields }] };
};
const undoStack: StudioSnapshot[] = [];
const redoStack: StudioSnapshot[] = [];
const clone = (): StudioSnapshot => ({ ...structuredClone(snapshot), history: { canUndo: undoStack.length > 0, canRedo: redoStack.length > 0 } });

// Public overload preserves method -> parameter -> result correlation.
// Implementation uses the union of valid method results, since TypeScript
 // cannot narrow generic M through a runtime method comparison.
export function mockInvoke<M extends StudioMethod>(method: M, params: StudioParams[M]): Promise<StudioResult[M]>;
export async function mockInvoke(method: StudioMethod, params: StudioParams[StudioMethod]): Promise<StudioResult[StudioMethod]> {
  if (method === 'studio.worldProcessAreaPreview' ||
      method === 'studio.worldProcessGroupPivot' ||
      method === 'studio.worldProcessAreaBatchResize')
    throw Error('EVOLVE group authoring requires the native AIVERSE host');
  if (method === 'studio.gameEntities') return {active:false,epoch:'0',tick:0,rows:[],total:0};
  if (method === 'studio.gameInspect') throw Error('Runtime inspection requires native gameplay');
  if (method === 'studio.gameDevices') return [];
  if (method === 'studio.gameState') return {device:{id:'none',landscape:true,safeArea:true,width:0,height:0,touch:false},paused:false,canControl:false,canStep:false,active:false,loading:false,progress:0,inMatch:false,tick:0,error:''};
  if (method === 'studio.gameStart' || method === 'studio.gameStop' || method === 'studio.gameControl' || method === 'studio.gameDevice') throw Error('Play requires the native AIVERSE host');
  if (method === 'studio.project') return {maps: [], currentMap: '', canCreateMap: false, maxMapSize: 0};
  if (method === 'studio.newMap' || method === 'studio.openMap' || method === 'studio.saveAs')
    throw Error('Document operations require a native project');
  if (method === 'studio.entityCatalog') return {types: [], mapEntities: false};
  if (method === 'studio.entityCreate' || method === 'studio.entityDelete' || method === 'studio.entityDuplicate')
    throw Error('Entity authoring requires a native project');
  if (method === 'studio.facialRigList')return {rigs: []};
  if (method === 'studio.facialRigGet' || method === 'studio.facialRigSave')
    throw Error('Facial Rig project files require the native AIVERSE host');
  if (method === 'studio.facialRigValidate') {
    // Browser demo never claims native compile/model/morph verification.
    throw Error('Facial Rig native validation requires the AIVERSE host');
  }
  if (method === 'studio.materials') {
    return { library: {} };
  }
  if (method === 'studio.shaderGraphCatalog') return {nodes:[]};
  if (method === 'studio.shaderGraphs') return {graphs:[]};
  if (method === 'studio.shaderGraphGet' || method === 'studio.shaderGraphValidate' ||
      method === 'studio.shaderGraphSave')
    throw Error('Shader Graph documents require a native project');
  if (method === 'studio.materialPreview') {
    throw Error('GPU material preview requires the native AIVERSE host');
  }
  if (method === 'studio.materialPatch' || method === 'studio.materialCreate') {
    throw Error('Editing materials requires the native AIVERSE host');
  }
  if(method==='studio.wind'){
    const curve=(f:(t:number)=>number)=>Array.from({length:32},(_,i)=>Math.round(f(i/31)*1000)/1000);
    return {settings:{enabled:true,speed:6,direction:0,gust:.35,scale:.035,debug:'off'},
      library:{masks:[{name:'trunk',index:2,samples:curve(t=>t<.25?t*.12:t<.6?.03+(t-.25)*.77:.3+(t-.6)*1.75)},
        {name:'branch',index:3,samples:curve(t=>t*t)},{name:'leaf',index:5,samples:curve(t=>Math.min(1,.5+t*.5))}],
        profiles:[],uniqueMasks:8,warnings:[]},scene:{}} as StudioResult['studio.wind'];
  }
  if(method==='studio.windSet')throw Error('Wind preview requires the native host');
  if(method==='studio.nereidFlow')return {surfaces:[],computed:[]};
  if(method==='studio.nereidFlowSet')throw Error('NEREID flow editing requires the native host');
  if (method === 'studio.renderLighting') return { version: 1, indirect: 'off', direct: 'legacy', quality: 'high', probes: 8, layers: 4, spacing: 4, samples: 32, distance: 32, cpuBudgetMs: 2 } as StudioResult['studio.renderLighting'];
  if (method === 'studio.renderLightingSet') throw new Error('Open the native Studio to edit lighting');
  if (method === 'studio.lighting') {
    return { lighting: null, lights: [] };
  }
  if (method === 'studio.lightingSet' || method === 'studio.lightAdd' ||
      method === 'studio.lightUpdate' || method === 'studio.lightDelete') {
    throw Error('Editing map lights requires the native AIVERSE host');
  }
  if (method === 'studio.assetByGuid' || method === 'studio.assetMetaPatch' ||
      method === 'studio.assetFolderCreate' ||
      method === 'studio.assetMovePreview' || method === 'studio.assetImportPreview' ||
      method === 'studio.assetTransferCommit') {
    throw Error('Project asset operations require the native AIVERSE host');
  }
  if (method === 'studio.assetReferences') {
    // Demo cannot inspect an actual project's materials/rules.
    return [];
  }
  if (method === 'studio.assets') {
    // Empty DEMO catalog: never imply that mock file names exist in the user project.
    return { files: [], folders: [], root: 'content' };
  }
  if (method === 'studio.viewportRect' || method === 'studio.viewportInput') {
    return { ok: true }; // Browser preview has no native surface.
  }
  if (method === 'studio.rules') return { types: { soldier: { name: 'Soldier', health: { max: 60 }, move: { speed: 2.5 }, sight: 7, cost: { gold: 50, time: 10 }, weapons: [{ id: 'sword', damage: 6, range: 1, cooldown: 1 }] } },
    resolved: { soldier: { name: 'Soldier', health: { max: 60 }, move: { speed: 2.5, radius: 0.4 }, sight: 7, cost: { gold: 50, time: 10 }, weapons: [{ id: 'sword', damage: 6, range: 1, cooldown: 1 }] } },
    resources: ['gold'], summary: [], dirty: false } as StudioResult['studio.rules'];
  if (method === 'studio.rulesSet') throw new Error('Open the native Studio to edit rules');
  // The browser preview has a single window: panels stay docked.
  if (method === 'studio.windowOpen') return { opened: false } as StudioResult['studio.windowOpen'];
  if (method === 'studio.windowClose') return { closed: false } as StudioResult['studio.windowClose'];
  if (method === 'studio.windowBroadcast') return { ok: true } as StudioResult['studio.windowBroadcast'];
  if (method === 'studio.windowTitle') return { ok: true } as StudioResult['studio.windowTitle'];
  if (method.startsWith('studio.build')) throw new Error('Builds run in the native Studio');
  if (method === 'studio.assetThumbnail') return { none: true } as StudioResult['studio.assetThumbnail'];
  if (method === 'studio.assetUsage') return { unused: [], mentions: [] } as StudioResult['studio.assetUsage'];
  if (method === 'studio.hub') return { recent: [], templates: [{ id: 'empty', name: 'Empty', description: '', genre: '', maps: 1 }], defaultLocation: '~/AiverseProjects' } as StudioResult['studio.hub'];
  if (method === 'studio.hubTemplates') return { templates: [
    { id: 'pkg:skirmish', name: 'Iron and Gold', description: 'Classic base building in the style of Warcraft and Red Alert: base, army, towers.', genre: 'game', maps: 4, source: 'catalog', official: true, version: '1.0.0', size: 1400000 },
    { id: 'pkg:community-arena', name: 'Community Arena', description: 'A template from the catalog community.', genre: 'game', maps: 1, source: 'catalog', official: false, version: '0.2.0', size: 2600000 },
  ] } as StudioResult['studio.hubTemplates'];
  if (method === 'studio.hubOpen' || method === 'studio.hubCreate') throw new Error('Open the native Studio');
  if (method === 'studio.ops') return { panel: String((params as { panel?: string })?.panel ?? ''), ops: [] } as StudioResult['studio.ops'];
  if (method === 'studio.op' && String((params as { op?: string })?.op ?? '').startsWith('package_')) return mockPackageOp(params as StudioParams['studio.op']) as StudioResult['studio.op'];
  if (method === 'studio.op' || method === 'studio.inputGet' || method === 'studio.inputSet' || method === 'studio.assetAudit' ||
    method === 'studio.agentInfo' || method === 'studio.sceneBrush' || method === 'studio.splineEdit' || method === 'studio.stats' || method === 'studio.gpuStats' || method === 'studio.keepPlayChanges' || method === 'studio.extensions' || method === 'studio.extensionAction' || method === 'studio.extensionCreate' || method === 'studio.attachmentsGet' || method === 'studio.attachmentsSet' || method === 'studio.agentStart' || method === 'studio.agentStop' || method === 'studio.agentReset')
    throw new Error('Open the native Studio');
  if (method === 'studio.history') return { steps: [], position: 0 } as StudioResult['studio.history'];
  if (method === 'studio.historyJump' || method === 'studio.entitiesAlign') throw new Error('Open the native Studio');
  if (method === 'studio.componentCatalog') return [
    { id: 'MeshRenderer', title: 'Mesh Renderer', group: 'Rendering', hint: 'Model and material slots', action: 'native', available: false, reason: 'Already on this entity' },
    { id: 'Light', title: 'Point Light', group: 'Rendering', hint: 'Adds a light entity at this position', action: 'light', available: true },
  ] as StudioResult['studio.componentCatalog'];
  if (method === 'studio.componentAdd' || method === 'studio.componentRemove' || method === 'studio.setProperties')
    throw new Error('Open the native Studio to change components');
  if (method === 'studio.inspect') {
    return demoInspector((params as StudioParams['studio.inspect']).entityId);
  }
  if (method === 'studio.propertiesFor') {
    const id = (params as StudioParams['studio.propertiesFor']).entityId;
    return structuredClone(fieldsFor(id));
  }
  if (method === 'studio.select') {
    const select = params as StudioParams['studio.select'];
    const id = select.ids?.length ? select.ids[select.ids.length - 1] : select.id ?? null;
    if (id !== null && !snapshot.scene.entities.some(entity => entity.id === id)) throw Error('Unknown entity: ' + id);
    snapshot.selectedIds = id === null ? [] : select.ids?.length ? [...select.ids] : [id];
    if (snapshot.selectedId && !entityFields.has(snapshot.selectedId)) {
      entityFields.set(snapshot.selectedId, structuredClone(snapshot.properties));
    }
    snapshot.selectedId = id;
    snapshot.properties = id === null ? [] : structuredClone(fieldsFor(id));
  } else if (method === 'studio.openProject') {
    throw Error('Opening projects requires the native AIVERSE host');
  } else if (method === 'studio.save') {
    throw Error('Saving a project requires the native AIVERSE host');
  } else if (method === 'studio.undo') {
    if (!undoStack.length) throw Error('Nothing to undo');
    redoStack.push(clone());
    snapshot = undoStack.pop()!;
  } else if (method === 'studio.redo') {
    if (!redoStack.length) throw Error('Nothing to redo');
    undoStack.push(clone());
    snapshot = redoStack.pop()!;
  } else if (method === 'studio.setProperty') {
    const { entityId, key, value } = params as StudioParams['studio.setProperty'];
    const target = fieldsFor(entityId);
    const property = target.find(p => p.key === key);
    if (!property) throw Error('Unknown property: ' + key);
    if (typeof value !== property.type.replace('text', 'string').replace('number', 'number').replace('boolean', 'boolean')) {
      throw Error('Property type mismatch');
    }
    undoStack.push(clone());
    redoStack.length = 0;
    property.value = value;
    if (snapshot.selectedId === entityId) snapshot.properties = structuredClone(target);
    if (key === 'name') {
      const entity = snapshot.scene.entities.find(e => e.id === entityId);
      if (entity) entity.name = String(value);
    }
    snapshot.project.dirty = true;
  } else if (method === 'studio.entityMove') {
    const { entityId, targetId, placement } = params as StudioParams['studio.entityMove'];
    const list = snapshot.scene.entities;
    const from = list.findIndex(e => e.id === entityId);
    if (from < 0) throw Error('Unknown entity: ' + entityId);
    undoStack.push(clone()); redoStack.length = 0;
    const [moved] = list.splice(from, 1);
    let to = targetId ? list.findIndex(e => e.id === targetId) : list.length;
    if (to < 0) to = list.length; else if (targetId && placement === 'after') to += 1;
    list.splice(to, 0, moved);
    snapshot.selectedId = entityId;
  } else if (method !== 'studio.snapshot') {
    throw Error('Unsupported operation');
  }
  const result = clone();
  if (result.selectedId) result.inspector = demoInspector(result.selectedId);
  return result;
}

/** Browser demo of the Catalog panel: two catalog packages, one installed (the native Studio runs aiverse_pkg.py). */
function mockPackageOp({ op, params }: StudioParams['studio.op']): StudioResult['studio.op'] {
  const packs = [
    { id: 'terraweave-trees', kind: 'content', category: 'models', title: 'TerraWeave trees', latest: '1.0.0', size: 8504568, cached: false,
      summary: 'Ten tree species from the TerraWeave Arbor generator, three LODs each.', license: 'MIT', minEngine: '3.1.0',
      install: { mount: 'models/terraweave/trees' }, dependencies: { 'vegetation-pbr': '^1.0' }, versions: { '1.0.0': { size: 8504568 } } },
    { id: 'terrain-pbr', kind: 'content', category: 'materials', title: 'Terrain PBR materials', latest: '1.0.0', size: 82512081, cached: true,
      summary: '14 seamless 1024² terrain PBR materials.', license: 'MIT', minEngine: '2.2.0',
      install: { mount: 'materials/terrain' }, versions: { '1.0.0': { size: 82512081 } } },
  ];
  let result: unknown;
  if (op === 'package_search') result = packs.filter(p => !params.category || p.category === params.category);
  else if (op === 'package_info') result = packs.find(p => p.id === params.id);
  else if (op === 'package_status') result = { packages: [{ id: 'terrain-pbr', wanted: '^1.0.0', locked: '1.0.0', files: 130, missing: 0, changed: 0 }], mismatch: 0 };
  else throw new Error('Open the native Studio to install packages');
  return { op, result, changed: false };
}
