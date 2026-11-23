import test from 'node:test';
import assert from 'node:assert/strict';
import {
  readCurve, readGradient, gradientCss, validRgbaHex,
  rgba8, colorAndAlpha, rgbaWithAlpha,
  editCurveKey, editGradientKey, addCurveKey, addGradientKey,
  removeCurveKey, removeGradientKey
} from '../src/panels/spectraCurveModel.ts';

function graph(){
  return {
    version:2,
    nodes:[
      {id:'curve',type:'Curve',properties:{value:[[0,-1],[1,4]],extra:'preserved'},position:[12,34]},
      {id:'gradient',type:'Gradient',properties:{value:[[0,'#ff0000ff'],[1,'#0000ff00']]}},
      {id:'unrelated',type:'Spawn',properties:{burst:9}}
    ],
    links:[],outputs:[],
    metadata:{untouched:true}
  };
}

test('Curve and Gradient keys are normalized, sorted and source stays immutable',()=>{
  const g=graph();
  assert.deepEqual(readCurve(g.nodes[0]),[{t:0,value:-1},{t:1,value:4}]);
  assert.deepEqual(readGradient(g.nodes[1]),[
    {t:0,color:'#ff0000ff'},{t:1,color:'#0000ff00'}
  ]);
  assert.equal(readCurve(g.nodes[1]),null);
  assert.equal(readGradient(g.nodes[0]),null);
  const change=editCurveKey(g,'curve',1,{t:1,value:10});
  assert.equal(change.error,'');
  assert.equal(change.graph.nodes[0].properties.extra,'preserved');
  assert.equal(change.graph.nodes[0].position,g.nodes[0].position);
  assert.equal(change.graph.nodes[2],g.nodes[2]);
  assert.deepEqual(g.nodes[0].properties.value,[[0,-1],[1,4]]);
  assert.deepEqual(change.graph.nodes[0].properties.value,[[0,-1],[1,10]]);
});

test('visual curves add at widest gap and reject invalid coordinates atomically',()=>{
  const g=graph();
  const inserted=addCurveKey(g,'curve');
  assert.equal(inserted.error,'');
  assert.deepEqual(inserted.graph.nodes[0].properties.value.map(([t])=>t),[0,0.5,1]);
  const same=editCurveKey(g,'curve',0,{t:1,value:5});
  assert.ok(same.error);
  assert.equal(same.graph,g);
  for(const invalid of [NaN,Infinity,1e8]){
    const result=editCurveKey(g,'curve',0,{t:0,value:invalid});
    assert.ok(result.error);assert.equal(result.graph,g);
  }
  assert.equal(editCurveKey(g,'missing',0,{t:0.5,value:1}).graph,g);
  const removed=removeCurveKey(inserted.graph,'curve',1);
  assert.equal(removed.error,'');
  assert.deepEqual(removed.graph.nodes[0].properties.value,[[0,-1],[1,4]]);
  let one=removeCurveKey(removed.graph,'curve',1).graph;
  assert.ok(removeCurveKey(one,'curve',0).error);
});

test('rgba color editing retains alpha, normalizes case and blocks injection',()=>{
  assert.equal(validRgbaHex('#ABCDEF'),true);
  assert.equal(validRgbaHex('#aabbccdd'),true);
  assert.equal(validRgbaHex('red'),false);
  assert.equal(validRgbaHex('#12345z'),false);
  assert.equal(validRgbaHex('var(--au-accent)'),false);
  assert.equal(rgba8('#AA00EE'),'#aa00eeff');
  assert.deepEqual(colorAndAlpha('#11223380'),{hex:'#112233',alpha:128/255});
  assert.equal(rgbaWithAlpha('#123456',0.5),'#12345680');
  assert.equal(rgbaWithAlpha('#123456',2),null);
  assert.equal(rgbaWithAlpha('javascript:alert(1)',0.5),null);
});

test('gradient key edit/add/delete and CSS preview',()=>{
  const g=graph();
  assert.match(gradientCss(readGradient(g.nodes[1])),/^linear-gradient\(90deg,/);
  const edit=editGradientKey(g,'gradient',1,{t:1,color:'#00ff00'});
  assert.equal(edit.error,'');
  assert.equal(edit.graph.nodes[1].properties.value[1][1],'#00ff00ff');
  assert.equal(g.nodes[1].properties.value[1][1],'#0000ff00');
  const added=addGradientKey(g,'gradient');
  assert.equal(added.error,'');
  assert.deepEqual(added.graph.nodes[1].properties.value.map(([t])=>t),[0,0.5,1]);
  const bad=editGradientKey(g,'gradient',1,{t:1,color:'url(evil)'});
  assert.ok(bad.error);assert.equal(bad.graph,g);
  const removed=removeGradientKey(added.graph,'gradient',1);
  assert.equal(removed.error,'');
  assert.deepEqual(removed.graph.nodes[1].properties.value,g.nodes[1].properties.value);
  assert.ok(removeGradientKey(removeGradientKey(g,'gradient',0).graph,'gradient',0).error);
});

test('invalid raw key arrays are not reinterpreted or lost',()=>{
  const g=graph();
  g.nodes[0].properties.value=[[0,0],['wrong',2]];
  assert.equal(readCurve(g.nodes[0]),null);
  const changed=addCurveKey(g,'curve');
  assert.ok(changed.error);assert.equal(changed.graph,g);
  g.nodes[1].properties.value=[[0,'#badcolor'],[1,'#123456ff']];
  assert.equal(readGradient(g.nodes[1]),null);
  assert.equal(addGradientKey(g,'gradient').graph,g);
});
