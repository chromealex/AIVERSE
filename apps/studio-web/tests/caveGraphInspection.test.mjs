import test from 'node:test';
import assert from 'node:assert/strict';
import {inspectCaveReply,parseCaveSnapshot,projectCavePage,cavePageSize} from '../src/graphs/caveGraphInspection.ts';
const settings={seed:7,cellCm:4800,tunnelRadiusCm:450,chamberRadiusCm:1200,tunnelChance:550,chamberChance:180,minYcm:-200000,maxYcm:-300};
const stamp=(id,mode='carve')=>({id,mode,xCm:-100,yCm:-1000,zCm:50,radiusCm:400});
const entrance=id=>({id,xCm:0,zCm:1,surfaceYcm:0,caveYcm:-1000,radiusCm:300});
const snapshot=(stamps=[],entrances=[])=>({version:1,generatorVersion:1,sourceRevision:9,settings:{...settings},stamps,entrances});
const reply=s=>({ok:true,revision:s.sourceRevision,stampCount:s.stamps.length,snapshot:s});
test('native reply projects exact identifiers, typed provenance and coordinates without mutation',()=>{
 const s=snapshot([stamp(7,'fill'),stamp(2)],[entrance(2)]);
 const before=JSON.stringify(s),data=inspectCaveReply(reply(s)),p=projectCavePage(data);
 assert.deepEqual(p.visible.map(e=>e.kind+':'+e.id),['stamp:2','stamp:7','entrance:2']);
 assert.equal(p.visible[0].xCm,-100);assert.equal(p.view.nodes.length,5);
 assert.equal(p.view.links.length,4);assert.ok(p.view.links.every(l=>l.to==='volume'));
 assert.equal(JSON.stringify(s),before);
});
test('reject failed replies, revision and count mismatch, unsafe identity and malformed data anywhere',()=>{
 const s=snapshot([stamp(1)]);
 for(const r of [{...reply(s),ok:false},{...reply(s),revision:8},{...reply(s),stampCount:0},{...reply(s),error:'failed'}])
  assert.throws(()=>inspectCaveReply(r));
 for(const bad of [snapshot([stamp(1),stamp(1)]),snapshot([stamp(Number.MAX_SAFE_INTEGER+1)]),snapshot([stamp(1,'other')]),
  snapshot([{...stamp(1),xCm:1000000001}]),snapshot([], [entrance(1),entrance(1)]),snapshot([], [{...entrance(1),surfaceYcm:-1001}]),
  {...s,version:2},{...s,sourceRevision:Number.MAX_SAFE_INTEGER+1},{...s,settings:{...settings,tunnelRadiusCm:1601}}])
  assert.throws(()=>parseCaveSnapshot(bad));
});
test('bounded pages cover all entries and filter deterministically',()=>{
 const data=parseCaveSnapshot(snapshot(Array.from({length:100},(_,i)=>stamp(100-i,i%2?'fill':'carve')),[entrance(1)]));
 const first=projectCavePage(data),ids=[];
 assert.equal(first.pages,5);
 for(let i=0;i<first.pages;i++){
  const p=projectCavePage(data,'all',i);
  assert.ok(p.visible.length<=cavePageSize);ids.push(...p.visible.map(e=>e.kind+':'+e.id));
 }
 assert.equal(new Set(ids).size,101);
 assert.equal(projectCavePage(data,'fill').total,50);assert.equal(projectCavePage(data,'entrance').total,1);
 assert.throws(()=>projectCavePage(data,'all',5));
 const empty=projectCavePage(parseCaveSnapshot(snapshot()),'fill');assert.equal(empty.pages,1);assert.equal(empty.view.nodes.length,2);
});
test('native caps enforced even for rows outside first page',()=>{
 assert.throws(()=>parseCaveSnapshot(snapshot(Array.from({length:16385},(_,i)=>stamp(i+1)))));
 const s=snapshot(Array.from({length:100},(_,i)=>stamp(i+1)));s.stamps[99].radiusCm=0;
 assert.throws(()=>parseCaveSnapshot(s));
});
