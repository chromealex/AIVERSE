import test from 'node:test';
import assert from 'node:assert/strict';
import {solidKinds,parseBuildingGeometry,projectBuildingGeometry,buildingGraphOwners} from '../src/graphs/buildingGeometryGraph.ts';
const fixture=()=>({version:1,parts:{...Object.fromEntries(solidKinds.map(k=>[k,0])),wall:5,'door-leaf':2,'elevator-platform':1},staticParts:5,movableParts:3,previewBoxes:8});
test('native solid partition and preview counts',()=>{
 const r=parseBuildingGeometry(fixture(),8);assert.equal(r.movableParts,3);assert.equal(r.previewBoxes,8);
 assert.throws(()=>parseBuildingGeometry({...fixture(),movableParts:4},8));
 assert.throws(()=>parseBuildingGeometry(fixture(),9));
});
test('all eight domains are independent of authored preview',()=>{
 const view=projectBuildingGeometry(parseBuildingGeometry(fixture(),8));
 assert.equal(buildingGraphOwners.length,8);
 assert.equal(view.links.filter(l=>l.from==='router').length,8);
 assert.ok(!view.links.some(l=>l.from==='boxes'&&l.to==='physics'));
 assert.ok(view.nodes.some(n=>n.id==='hierarchy'));
 assert.ok(view.nodes.some(n=>n.id==='transport'));
});
