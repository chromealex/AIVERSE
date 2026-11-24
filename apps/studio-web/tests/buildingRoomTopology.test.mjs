import test from 'node:test';
import assert from 'node:assert/strict';
import {parseBuildingRoute,parseBuildingRoomTopology,projectBuildingRoomTopology,roomComponentMembers} from '../src/graphs/buildingRoomTopology.ts';
const r=(id,floor,x,component,withoutElevatorsComponent)=>({id,floor,x,z:0,min:[x*4,floor*4,0],max:[x*4+4,floor*4+3,4],component,withoutElevatorsComponent});
const e=(id,from,to,floor,kind,socketId)=>({id,from,to,floor,kind,position:[2,0,2],width:1,height:2,...(socketId?{socketId}:{})});
const fixture=()=>({version:1,components:1,withoutElevatorsComponents:2,rooms:[r('a',0,0,0,0),r('b',0,1,0,0),r('c',1,0,0,1),r('d',1,1,0,1)],
 connections:[e('p','a','b',0,'passage'),e('lift','a','c',0,'elevator','lift/0/0'),e('q','c','d',1,'passage')]});
const parse=t=>parseBuildingRoomTopology(t,2,2,1,3,[{id:'lift/0/0',kind:'elevator'}]);
test('native IDs and component groups preserved without deriving edges',()=>{
 const input=fixture(),before=JSON.stringify(input),t=parse(input),v=projectBuildingRoomTopology(t);
 assert.equal(v.nodes.length,7);assert.equal(v.links.length,6);
 assert.deepEqual(roomComponentMembers(t,'a').map(r=>r.id),['a','b','c','d']);
 assert.deepEqual(roomComponentMembers(t,'a',true).map(r=>r.id),['a','b']);
 assert.equal(JSON.stringify(input),before);
});
test('floor filter keeps real neighboring endpoints and no unrelated edges',()=>{
 const v=projectBuildingRoomTopology(parse(fixture()),0);
 assert.deepEqual(v.nodes.map(n=>n.id),['room:a','room:b','room:c','connection:p','connection:lift']);
 const ids=new Set(v.nodes.map(n=>n.id));assert.ok(v.links.every(l=>ids.has(l.from)&&ids.has(l.to)));
 assert.equal(v.links.length,4);assert.throws(()=>projectBuildingRoomTopology(parse(fixture()),2));
});
test('reject stale counts, duplicate rooms and unknown connection endpoints',()=>{
 const mutations=[t=>{t.version=2},t=>{t.rooms.pop()},t=>{t.connections.pop()},t=>{t.rooms[1].id='a'},t=>{t.rooms[1].x=0},t=>{t.connections[0].to='missing'},t=>{t.connections[0].to='a'},t=>{t.rooms[0].max[0]=0},t=>{t.connections[1].floor=1}];
 for(const mutate of mutations){const t=fixture();mutate(t);assert.throws(()=>parse(t));}
});
test('reject unknown kinds, missing sockets and inconsistent native groups',()=>{
 const mutations=[t=>{t.connections[1].kind='door'},t=>{delete t.connections[1].socketId},t=>{t.connections[1].socketId='unknown'},t=>{t.rooms[1].withoutElevatorsComponent=1},t=>{t.components=2},t=>{t.withoutElevatorsComponents=3}];
 for(const mutate of mutations){const t=fixture();mutate(t);assert.throws(()=>parse(t));}
});
test('isolated native component is reported without inventing an outside path',()=>{
 const raw={version:1,components:2,withoutElevatorsComponents:2,rooms:[r('a',0,0,0,0),r('b',0,1,1,1)],connections:[]};
 const t=parseBuildingRoomTopology(raw,1,2,1,0,[]);
 assert.equal(roomComponentMembers(t,'a').length,1);assert.equal(projectBuildingRoomTopology(t).links.length,0);
});

test('native route validates ordered edge IDs and reverse traversal',()=>{
 const t=parse(fixture()),query={start:'d',goal:'b',allowStairs:true,allowElevators:true};
 const steps=[{edgeId:'q',from:'d',to:'c'},{edgeId:'lift',from:'c',to:'a'},{edgeId:'p',from:'a',to:'b'}];
 assert.deepEqual(parseBuildingRoute({...query,reachable:true,steps},query,t).steps,steps);
 for(const change of [r=>{r.steps[1].edgeId='missing'},r=>{r.steps.reverse()},r=>{r.steps.pop()},r=>{r.goal='a'},r=>{r.reachable=false}]){
  const result=structuredClone({...query,reachable:true,steps});change(result);
  assert.throws(()=>parseBuildingRoute(result,query,t));
 }
 const restricted={...query,allowElevators:false};
 assert.throws(()=>parseBuildingRoute({...restricted,reachable:true,steps},restricted,t));
 assert.equal(parseBuildingRoute({...restricted,reachable:false,steps:[]},restricted,t).reachable,false);
 const same={...query,goal:query.start};
 assert.equal(parseBuildingRoute({...same,reachable:true,steps:[]},same,t).reachable,true);
 assert.throws(()=>parseBuildingRoute({...same,reachable:false,steps:[]},same,t));
});
test('exterior overlaps preserve multiple room endpoints without adding routing edges',()=>{
 const raw=fixture();raw.exteriorApertures=[{socketId:'gate',roomId:'a',start:3,end:4},{socketId:'gate',roomId:'b',start:4,end:5}];
 const read=()=>parseBuildingRoomTopology(raw,2,2,1,3,[{id:'lift/0/0',kind:'elevator'},{id:'gate',kind:'small-gate'}]);
 assert.equal(read().exteriorApertures.length,2);
 assert.equal(read().connections.length,3);
 raw.exteriorApertures[1].roomId='c';assert.throws(read);
 raw.exteriorApertures[1].roomId='a';assert.throws(read);
});
