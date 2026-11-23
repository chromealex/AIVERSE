import test from 'node:test';
import assert from 'node:assert/strict';
import {
  graphV2, safeGraphName, scalarParameters, validateInterfaces,
  putParameter, putInterface, dropParameter, dropInterface,
  putNode, dropNode, providerNames, putSpriteTexture
} from '../src/panels/spectraDataInterfaceModel.ts';

const graph=()=>({
  version:2,
  instanceParameters:{wind_lift:{slot:0,default:2,min:0,max:25}},
  dataInterfaces:{},
  nodes:[{id:'spawn',type:'Spawn',properties:{burst:4}}],
  links:[],outputs:[], metadata:{keep:'opaque'}
});
const wind={provider:'weather',channel:'vertical_acceleration',scale:1.5,offset:0.5,fallback:2,required:true};

test('graph V2 guards reject malformed data without rewriting author metadata',()=>{
  assert.equal(graphV2({version:1,nodes:[],links:[],outputs:[]}),null);
  assert.equal(graphV2({version:2,nodes:'bad',links:[],outputs:[]}),null);
  const original=graph();
  const v2=graphV2(original);assert.ok(v2);
  const changed=putInterface(v2,'wind_lift',wind);
  assert.equal(changed.error,'');
  assert.equal(changed.graph.metadata,original.metadata);
  assert.deepEqual(original.dataInterfaces,{});
  assert.deepEqual(changed.graph.dataInterfaces.wind_lift,wind);
});
test('strict native-compatible names, ranges and bounded slots',()=>{
  assert.equal(safeGraphName('fire_fx1'),true);
  for(const x of ['../evil','Big','', 'x'.repeat(49),'fx'])assert.equal(safeGraphName(x),false);
  const base=graphV2(graph());
  assert.ok(base);
  assert.equal(scalarParameters(base).length,1);
  const invalid=putParameter(base,'extra',{slot:0,default:1,min:0,max:3});
  assert.match(invalid.error,/duplicate/i);assert.equal(invalid.graph,base);
  const wrong=putParameter(base,'unsafe/foo',{slot:1,default:1,min:0,max:3});
  assert.ok(wrong.error);assert.equal(wrong.graph,base);
  const valid=putParameter(base,'new_param',{slot:1,default:2,min:-20,max:20});
  assert.equal(valid.error,'');assert.equal(Object.keys(valid.graph.instanceParameters).length,2);
  const removed=dropParameter(valid.graph,'new_param');
  assert.equal(removed.error,'');assert.equal(Object.keys(removed.graph.instanceParameters).length,1);
});
test('reject invalid bindings atomically with existing valid source untouched',()=>{
  const base=graphV2(graph());assert.ok(base);
  for(const value of [
    {...wind,provider:'../weather'},
    {...wind,channel:''},
    {...wind,scale:1001},
    {...wind,fallback:26},
    {...wind,required:'yes'}
  ]){
    const result=putInterface(base,'wind_lift',value);
    assert.ok(result.error);assert.equal(result.graph,base);
  }
  const missing=putInterface(base,'missing',wind);
  assert.ok(missing.error);assert.equal(missing.graph,base);
  const value=putInterface(base,'wind_lift',wind);
  assert.equal(validateInterfaces(value.graph),null);
  assert.deepEqual(providerNames(value.graph,'WEA'),['weather']);
});
test('binding nodes do not permit removal of required data inputs',()=>{
  const base=graphV2(graph());assert.ok(base);
  const bound=putInterface(base,'wind_lift',wind).graph;
  const created=putNode(bound,'wind_lift');
  assert.equal(created.error,'');
  assert.equal(created.graph.nodes.length,2);
  assert.match(dropInterface(created.graph,'wind_lift').error,/node/i);
  assert.match(dropParameter(created.graph,'wind_lift').error,/binding/i);
  const repeat=putNode(created.graph,'wind_lift');
  assert.ok(repeat.error);
  const erased=dropNode(created.graph,created.graph.nodes[1].id);
  assert.equal(erased.error,'');
  const unbound=dropInterface(erased.graph,'wind_lift');
  assert.equal(unbound.error,'');
  assert.equal(dropParameter(unbound.graph,'wind_lift').error,'');
});
test('provider search is distinct from assets and only returns bound identifiers',()=>{
  const base=graphV2(graph());assert.ok(base);
  const first=putInterface(base,'wind_lift',wind).graph;
  assert.deepEqual(providerNames(first,'wea'),['weather']);
  assert.deepEqual(providerNames(first,'snow'),[]);
  assert.equal(validateInterfaces({...first,nodes:[...first.nodes,{
    id:'di1',type:'DataInterface',properties:{name:'missing'}}]}).includes('matching'),true);
});

test('real Assets catalog is authoritative for SpriteOutput texture references',()=>{
  const graphWithOutput={...graph(),nodes:[
    {id:'spawn',type:'Spawn',properties:{burst:4}},
    {id:'sprite',type:'SpriteOutput',properties:{texture:'builtin:soft',blend:'alpha'}}
  ]};
  const base=graphV2(graphWithOutput);assert.ok(base);
  const images=['Textures/smoke.png','Actors/Portrait.JPG','Textures/ship.tga'];
  for(const bad of ['Textures/unknown.png','../secrets.png','Textures/smoke.webp',
    '/root/a.png','Textures\\smoke.png']){
    const result=putSpriteTexture(base,'sprite',bad,images);
    assert.ok(result.error);assert.equal(result.graph,base);
  }
  assert.ok(putSpriteTexture(base,'absent','Textures/smoke.png',images).error);
  const next=putSpriteTexture(base,'sprite','Textures/smoke.png',images);
  assert.equal(next.error,'');
  assert.equal(next.graph.nodes[1].properties.texture,'content/Textures/smoke.png');
  assert.equal(next.graph.nodes[1].properties.blend,'alpha');
  assert.equal(base.nodes[1].properties.texture,'builtin:soft');
  assert.equal(next.graph.nodes[0],base.nodes[0]);
});
