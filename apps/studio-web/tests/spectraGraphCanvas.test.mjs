import test from 'node:test';
import assert from 'node:assert/strict';
import { nodePoint, moveGraphNode, visualEdges, connectionParams } from '../src/panels/spectraGraphCanvasModel.ts';

const graph=()=>({
 version:2,instanceParameters:{},dataInterfaces:{},
 nodes:[
   {id:'spawn',type:'Spawn',position:[20,30],properties:{burst:8}},
   {id:'init',type:'Initialize',position:[300,30],properties:{life:1}},
   {id:'wind',type:'DataInterface',position:[40,250],properties:{name:'wind_lift'}}
 ],
 links:[
   {from:'spawn',to:'init'},
   {from:'wind',to:'init',toPort:'speed'}
 ],
 outputs:[]
});
test('node positions and graph scene fallback stay bounded',()=>{
  const original=graph();
  assert.deepEqual(nodePoint(original.nodes[0],0),{x:20,y:30});
  assert.deepEqual(nodePoint({id:'unplaced',type:'Update'},5),{x:250,y:155});
  assert.deepEqual(nodePoint({id:'bad',type:'Update',position:[NaN,0]},2),{x:480,y:30});
});
test('node dragging does not mutate original authored stage or its data',()=>{
  const original=graph();
  const updated=moveGraphNode(original,'spawn',{x:111.7,y:77.2});
  assert.equal(updated.error,'');
  assert.deepEqual(updated.graph.nodes[0].position,[112,77]);
  assert.deepEqual(original.nodes[0].position,[20,30]);
  assert.equal(original.nodes[0].properties.burst,8);
  assert.equal(updated.graph.nodes[1],original.nodes[1]);
  assert.equal(moveGraphNode(original,'missing',{x:1,y:2}).graph,original);
  assert.equal(moveGraphNode(original,'spawn',{x:Infinity,y:2}).graph,original);
  assert.equal(moveGraphNode(original,'spawn',{x:1e8,y:2}).graph,original);
});
test('visual links distinguish typed data from execution',()=>{
  const original=graph();
  const links=visualEdges(original);
  assert.equal(links.length,2);
  assert.equal(links[0].toPort,undefined);
  assert.equal(links[1].toPort,'speed');
  assert.match(links[0].path,/M190 56 C/);
  assert.deepEqual(visualEdges({...original,links:[{from:'removed',to:'init'}]}),[]);
});
test('connection requests are native Workspace-only, with validation guards',()=>{
  const original=graph();
  assert.deepEqual(connectionParams('connect',original,'spawn','init'),{
    action:'connect',graph:original,from:'spawn',to:'init'
  });
  assert.deepEqual(connectionParams('connect',original,'wind','init','speed'),{
    action:'connect',graph:original,from:'wind',to:'init',toPort:'speed'
  });
  assert.equal(connectionParams('disconnect',original,'spawn','spawn'),null);
  assert.equal(connectionParams('connect',original,'missing','init'),null);
});
