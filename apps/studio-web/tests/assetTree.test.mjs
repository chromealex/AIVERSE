import test from 'node:test';
import assert from 'node:assert/strict';
import {safeAssetPath,assetKind,assetFolders,childFolders,visibleAssetFiles,assetBreadcrumbs} from '../src/assetTree.ts';

const assets=[
  {path:'Content/Environment/Ruins/Arch.fbx',bytes:1024},
  {path:'Content/Environment/Ruins/Moss.png',bytes:200},
  {path:'Content/Characters/Player.glb',bytes:20},
  {path:'game.json',bytes:56}
];
test('folder tree comes only from actual native file paths',()=>{
  const folders=assetFolders(assets);
  assert.deepEqual(folders.map(f=>f.path),[
    'Content','Content/Characters','Content/Environment','Content/Environment/Ruins']);
  assert.equal(folders.find(f=>f.path==='Content')?.count,3);
  assert.deepEqual(childFolders(folders,'Content').map(f=>f.name),['Characters','Environment']);
});
test('folder navigation shows immediate files and children, global search is explicit',()=>{
  assert.deepEqual(visibleAssetFiles(assets,'','').map(f=>f.path),['game.json']);
  assert.deepEqual(visibleAssetFiles(assets,'Content/Environment/Ruins','').map(f=>f.path),
    ['Content/Environment/Ruins/Arch.fbx','Content/Environment/Ruins/Moss.png']);
  assert.equal(visibleAssetFiles(assets,'','moss').length,1);
  assert.equal(visibleAssetFiles(assets,'','missing').length,0);
});
test('renderer receives only normalized relative paths and blocks traversal',()=>{
  for(const input of ['/etc/passwd','../secret','Content/../foo','C:/Users/secret',
     'Content//bad','Content/./bad','', 'a\0b'])
    assert.equal(safeAssetPath(input),null);
  assert.equal(safeAssetPath('Content\\Art\\Steel.png'),'Content/Art/Steel.png');
});
test('file categories are stable and do not fake thumbnails',()=>{
  assert.equal(assetKind('Moss.png'),'image');
  assert.equal(assetKind('tree.glb'),'model');
  assert.equal(assetKind('sound.ogg'),'audio');
  assert.equal(assetKind('game.json'),'code');
  assert.equal(assetKind('unknown.xyz'),'document');
});
test('breadcrumbs are derived without new filesystem access',()=>{
  assert.deepEqual(assetBreadcrumbs('Content/Environment/Ruins'),[
    {name:'content',path:''},{name:'Content',path:'Content'},
    {name:'Environment',path:'Content/Environment'},
    {name:'Ruins',path:'Content/Environment/Ruins'}
  ]);
});

test('Assets breadcrumb root represents actual lower-case content directory',()=>{
  assert.deepEqual(assetBreadcrumbs(''),[{name:'content',path:''}]);
  assert.deepEqual(assetBreadcrumbs('Models/Characters'),[
    {name:'content',path:''},
    {name:'Models',path:'Models'},
    {name:'Characters',path:'Models/Characters'}
  ]);
});
test('content-relative root shows top-level imported assets without a content/ prefix',()=>{
  const listed=[
    {path:'textures/rock.png',bytes:10},
    {path:'rock.glb',bytes:99},
    {path:'models/tank.fbx',bytes:400}
  ];
  assert.deepEqual(visibleAssetFiles(listed,'','').map(x=>x.path),['rock.glb']);
  assert.deepEqual(assetFolders(listed).map(x=>x.path),['models','textures']);
  assert.deepEqual(visibleAssetFiles(listed,'textures','').map(x=>x.path),['textures/rock.png']);
  assert.equal(visibleAssetFiles(listed,'','tank').length,1);
});
