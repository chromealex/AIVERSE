import type {GraphView,NodeView,Link} from './graphFramework';
export const solidKinds=['floor','wall','trim','door-leaf','roof','roof-panel','window-pane','partition','stair-tread','stair-landing','elevator-platform','elevator-guide'] as const;
type SolidKind=typeof solidKinds[number];
export interface BuildingGeometryReport {version:1;parts:Record<SolidKind,number>;previewBoxes:number;movableParts:number;staticParts:number}
const record=(v:unknown):Record<string,unknown>=>{if(!v||typeof v!=='object'||Array.isArray(v))throw Error('Invalid geometry object');return v as Record<string,unknown>};
const count=(v:unknown):number=>{if(typeof v!=='number'||!Number.isSafeInteger(v)||v<0||v>1000000)throw Error('Invalid geometry count');return v};
export function parseBuildingGeometry(value:unknown,solids:number):BuildingGeometryReport {
 const r=record(value),p=record(r.parts);
 if(r.version!==1)throw Error('Unsupported geometry version');
 const parts=Object.fromEntries(solidKinds.map(k=>[k,count(p[k])])) as Record<SolidKind,number>;
 const staticParts=count(r.staticParts),movableParts=count(r.movableParts),previewBoxes=count(r.previewBoxes);
 if(Object.values(parts).reduce((a,b)=>a+b,0)!==solids||staticParts+movableParts!==solids||
 movableParts!==parts['door-leaf']+parts['roof-panel']+parts['elevator-platform'])throw Error('Geometry counts do not match native report');
 return {version:1,parts,staticParts,movableParts,previewBoxes};
}
// Source-level owner inventory, deliberately not a runtime registration report.
export const buildingGraphOwners=[
 {id:'navigation',label:'NavMesh / dynamic source',detail:'BuildingNavSource, BuildingDynamicNavSource; actual mesh rebuild owned by navigation.'},
 {id:'spatial',label:'Room / spatial graph',detail:'BuildingRoomGraphCache; committed traversal callback required.'},
 {id:'hierarchy',label:'Grid A* / HPA* / flow',detail:'BuildingGridGraphAdapter; flat MapGrid projection and cache invalidation, not upper-floor routing.'},
 {id:'transport',label:'Road / rail',detail:'BuildingRoadGraphView and rail invalidation callback; owner provides network and access policy.'},
 {id:'gameplay',label:'Zone Graph / abilities',detail:'BuildingZoneGraphView; owner supplies directed access policy.'},
 {id:'presentation',label:'Render / material / VFX',detail:'Presentation domain reserved by router; registration not reported.'},
 {id:'audio',label:'Audio / acoustics',detail:'Audio domain reserved by router; registration not reported.'},
 {id:'simulation',label:'Utilities / simulation',detail:'Simulation domain reserved by router; registration not reported.'}
] as const;
export function projectBuildingGeometry(report:BuildingGeometryReport):GraphView {
 const nodes:NodeView[]=[],links:Link[]=[];
 const add=(id:string,label:string,x:number,y:number)=>nodes.push({id,label,x,y,inputs:[{id:'in',type:'dependency',multiple:true}],outputs:[{id:'out',type:'dependency',multiple:true}]});
 const link=(from:string,to:string)=>links.push({id:JSON.stringify([from,to]),from,to,output:'out',input:'in'});
 add('structure','StructureGraph',30,30);
 add('mesh','Generated mesh',230,30);link('structure','mesh');
 add('boxes','Preview AABBs: '+report.previewBoxes,230,130);link('structure','boxes');
 add('static','Static parts: '+report.staticParts,230,230);link('structure','static');
 add('movable','Movable parts: '+report.movableParts,230,330);link('structure','movable');
 // Separate architectural lane: no link from preview to a committed-state router.
 add('physics','PHYSIS / SpatialWorld owner',30,400);
 add('commit','Runtime committed state',30,500);link('physics','commit');
 add('router','Owner invalidation router',230,500);link('commit','router');
 buildingGraphOwners.forEach((o,i)=>{add(o.id,o.label,450,500+i*95);link('router',o.id)});
 return {nodes,links};
}
