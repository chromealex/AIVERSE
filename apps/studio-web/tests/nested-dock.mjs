import test from 'node:test';
import assert from 'node:assert/strict';
import {
  STUDIO_PANELS, STUDIO_PANEL_TITLES, STUDIO_PANEL_HOME, defaultDockTree, dockStacks, findDockStack, panelStack,
  normalizeDockTree, moveTab, closeTab, reopenTab, activateTab,
  resizeDockSplit, splitDockRoot, splitDockStack, migrateDockV2
} from '../src/nestedDock.ts';

test('default tree has 4 nested stacks and exactly one copy of each panel',()=>{
  const t=defaultDockTree();const s=dockStacks(t);
  assert.equal(s.length,4);
  assert.deepEqual(s.flatMap(x=>x.tabs).sort(),[...STUDIO_PANELS].sort());
  assert.equal(panelStack(t,'scene'),'dock-center');
  assert.equal(panelStack(t,'assets'),'dock-bottom');
  assert.deepEqual(findDockStack(t,'dock-left')?.tabs,['hierarchy','project']);
  assert.deepEqual(findDockStack(t,'dock-bottom')?.tabs,['assets','console','agent','frame_capture','texture_resources']);
});
test('move reorders within a tab stack',()=>{
  const t=moveTab(defaultDockTree(),'animation','dock-center','scene');
  assert.deepEqual(findDockStack(t,'dock-center').tabs,['animation','scene','game','materials','facial_rig','shader_graph','rules','build','scripts','snapshot_diff']);
  assert.equal(findDockStack(t,'dock-center').active,'animation');
});
test('dragging panels across stacks retains identity',()=>{
  const t=moveTab(defaultDockTree(),'inspector','dock-bottom');
  assert.equal(panelStack(t,'inspector'),'dock-bottom');
  assert.deepEqual(findDockStack(t,'dock-right').tabs,['human_creator','lighting','runtime','nereid_flow','human_appearance','wind','history','terrain','water','roads','tiles','fog','generate','postfx','effects','lightbake','models','input','overlays','branding','asset_audit','fields','physis','actor','raid','balance','fx_graph','fx_functions','modeling','anim_assets','anim_graph','anim_rig','model_anim','project_import','stats','extensions','attachments']);
});
test('closing and reopening recreates a panel only once',()=>{
  let t=closeTab(defaultDockTree(),'scene');
  assert.equal(panelStack(t,'scene'),null);
  t=reopenTab(t,'scene');t=reopenTab(t,'scene');
  assert.equal(dockStacks(t).flatMap(s=>s.tabs).filter(x=>x==='scene').length,1);
});
test('nested split adds a stack to left edge and preserves panels',()=>{
  const base=defaultDockTree();const t=splitDockStack(base,'inspector','dock-center','left');
  assert.equal(dockStacks(t).length,5);
  assert.deepEqual(dockStacks(t).flatMap(s=>s.tabs).sort(),[...STUDIO_PANELS].sort());
  assert.equal(panelStack(base,'inspector'),'dock-right');
});
test('window edge dock lifts a bottom tab into a full-height column',()=>{
  const t=splitDockRoot(defaultDockTree(),'assets','left');
  assert.equal(t.root.kind,'split');
  assert.equal(t.root.axis,'horizontal');
  assert.equal(t.root.first.kind,'stack');
  assert.deepEqual(t.root.first.tabs,['assets']);
  assert.equal(panelStack(t,'assets'),t.root.first.id);
  assert.equal(panelStack(t,'console'),'dock-bottom');
  assert.equal(dockStacks(t).flatMap(s=>s.tabs).filter(x=>x==='assets').length,1);
  assert.equal(dockStacks(t).flatMap(s=>s.tabs).length,dockStacks(defaultDockTree()).flatMap(s=>s.tabs).length);
});
test('window edge dock refuses the only remaining tab',()=>{
  const only={version:3,root:{kind:'stack',id:'dock-center',tabs:['scene'],active:'scene'}};
  assert.deepEqual(splitDockRoot(only,'scene','right'),normalizeDockTree(only));
});
test('sole tab can still dock onto another stack',()=>{
  const base=closeTab(closeTab(closeTab(closeTab(defaultDockTree(),'assets'),'agent'),'frame_capture'),'texture_resources');
  const t=splitDockStack(base,'console','dock-center','left');
  assert.equal(panelStack(t,'console')!=='dock-bottom',true);
  assert.equal(dockStacks(t).flatMap(s=>s.tabs).filter(x=>x==='console').length,1);
});
test('cannot split away sole tab from its own stack',()=>{
  const base=closeTab(closeTab(closeTab(closeTab(defaultDockTree(),'assets'),'agent'),'frame_capture'),'texture_resources');
  const t=splitDockStack(base,'console','dock-bottom','bottom');
  assert.deepEqual(t,base);
});
test('split every direction and preserve first stack',()=>{
  for(const side of ['left','right','top','bottom']) {
    const t=splitDockStack(defaultDockTree(),'assets','dock-center',side);
    assert.equal(dockStacks(t).length,5);
    assert.equal(dockStacks(t).flatMap(s=>s.tabs).filter(x=>x==='assets').length,1);
  }
});
test('resize ratio clamps and ignores nonfinite',()=>{
  let t=resizeDockSplit(defaultDockTree(),'root',1e3);
  assert.equal(t.root.ratio,.88);
  t=resizeDockSplit(t,'root',NaN);
  assert.equal(t.root.ratio,.88);
});
test('activate tab does not create a ghost panel',()=>{
  let t=activateTab(defaultDockTree(),'dock-left','scene');
  assert.equal(findDockStack(t,'dock-left').active,'hierarchy');
  t=activateTab(t,'dock-left','project');
  assert.equal(findDockStack(t,'dock-left').active,'project');
});
test('normalization guards malformed nodes, dup tabs and invalid ratios',()=>{
  const t=normalizeDockTree({root:{kind:'split',id:'root',axis:'horizontal',ratio:Infinity,
    first:{kind:'stack',id:'same',tabs:['scene','scene','BAD'],active:'BAD'},
    second:{kind:'stack',id:'same',tabs:['scene','assets'],active:'assets'}}});
  assert.equal(t.root.ratio,.5);
  const stacks=dockStacks(t);
  assert.notEqual(stacks[0].id,stacks[1].id);
  assert.deepEqual(stacks[0].tabs,['scene']);
  assert.deepEqual(stacks[1].tabs,['assets']);
});
test('migration from v2 preserves tabs and active',()=>{
  const layout=migrateDockV2({areas:{left:['assets'],center:['scene'],right:['inspector'],bottom:[]},
    active:{left:'assets',center:'scene',right:'inspector'},leftSize:300,rightSize:280,bottomSize:220});
  assert.equal(findDockStack(layout,'dock-left').active,'assets');
  assert.equal(findDockStack(layout,'dock-center').active,'scene');
  assert.equal(findDockStack(layout,'dock-bottom'),null);
});

test('AI panel registry metadata matches every registered panel',()=>{
  assert.deepEqual(Object.keys(STUDIO_PANEL_TITLES).sort(),[...STUDIO_PANELS].sort());
  assert.deepEqual(Object.keys(STUDIO_PANEL_HOME).sort(),[...STUDIO_PANELS].sort());
  assert.equal(STUDIO_PANEL_HOME.scene,'dock-center');
});

test('moving a last tab prunes the source split and expands its neighbor',()=>{
  const t=moveTab(closeTab(defaultDockTree(),'project'),'hierarchy','dock-center');
  assert.equal(findDockStack(t,'dock-left'),null);
  assert.equal(dockStacks(t).length,3);
  assert.equal(t.root.first.id,'split-body-right');
});
test('closing last right panels removes their obsolete split',()=>{
  let t=closeTab(defaultDockTree(),'inspector');
  assert.ok(findDockStack(t,'dock-right'));
  t=closeTab(closeTab(closeTab(t,'lighting'),'runtime'),'nereid_flow');
  assert.ok(findDockStack(t,'dock-right'));
  t=closeTab(closeTab(closeTab(closeTab(t,'human_appearance'),'wind'),'human_creator'),'history');
  for(const tool of ['terrain','water','roads','tiles','fog','generate','postfx','effects','lightbake','models','input','overlays','branding','asset_audit','fields','physis','actor','raid','balance','fx_graph','fx_functions','modeling','anim_assets','anim_graph','anim_rig','model_anim','project_import','stats','extensions','attachments'])t=closeTab(t,tool);
  assert.equal(findDockStack(t,'dock-right'),null);
  assert.equal(dockStacks(t).length,3);
});
test('reopening Inspector restores pruned right-hand dock',()=>{
  let t=closeTab(closeTab(closeTab(closeTab(closeTab(closeTab(closeTab(closeTab(defaultDockTree(),'inspector'),'lighting'),'runtime'),'nereid_flow'),'human_appearance'),'wind'),'human_creator'),'history');
  for(const tool of ['terrain','water','roads','tiles','fog','generate','postfx','effects','lightbake','models','input','overlays','branding','asset_audit','fields','physis','actor','raid','balance','fx_graph','fx_functions','modeling','anim_assets','anim_graph','anim_rig','model_anim','project_import','stats','extensions','attachments'])t=closeTab(t,tool);
  t=reopenTab(t,'inspector','dock-right');
  assert.equal(panelStack(t,'inspector'),'dock-right');
  assert.equal(dockStacks(t).length,4);
});
test('reopening Assets restores pruned bottom dock',()=>{
  let t=closeTab(closeTab(closeTab(defaultDockTree(),'assets'),'console'),'agent');t=closeTab(closeTab(t,'frame_capture'),'texture_resources');
  assert.equal(findDockStack(t,'dock-bottom'),null);
  t=reopenTab(t,'assets','dock-bottom');
  assert.equal(panelStack(t,'assets'),'dock-bottom');
  assert.equal(t.root.axis,'vertical');
  assert.equal(t.root.second.id,'dock-bottom');
});
test('when all tabs closed a single empty center stack allows reopening',()=>{
  let t=defaultDockTree();
  for(const name of STUDIO_PANELS)t=closeTab(t,name);
  assert.equal(dockStacks(t).length,1);
  assert.deepEqual(dockStacks(t)[0].tabs,[]);
  t=reopenTab(t,'scene','dock-center');
  assert.equal(panelStack(t,'scene'),'dock-center');
});
test('old saved empty leaves are compacted upon restoration',()=>{
  const t=normalizeDockTree({root:{kind:'split',id:'outer',axis:'horizontal',ratio:.3,
    first:{kind:'stack',id:'empty',tabs:[],active:null},
    second:{kind:'stack',id:'dock-center',tabs:['scene'],active:'scene'}}});
  assert.equal(t.root.id,'dock-center');
});

test('Add Tab drops a closed panel into the stack that was clicked',()=>{
  const closed=closeTab(defaultDockTree(),'assets');
  assert.equal(panelStack(closed,'assets'),null);
  const added=moveTab(closed,'assets','dock-left');
  assert.equal(panelStack(added,'assets'),'dock-left');
  assert.equal(findDockStack(added,'dock-left')?.active,'assets');
});

test('character editor never stays over the Scene viewport',async()=>{
  const {moveCharacterEditorBesideScene}=await import('../src/nestedDock.ts');
  const covered=moveTab(defaultDockTree(),'human_creator','dock-center');
  assert.equal(panelStack(covered,'human_creator'),'dock-center');
  assert.equal(panelStack(moveCharacterEditorBesideScene(covered),'human_creator'),'dock-right');
  assert.equal(panelStack(defaultDockTree(),'human_creator'),'dock-right');
});

test('character studio preset: editor left, Scene stage, no bottom dock',async()=>{
  const {characterDockTree}=await import('../src/nestedDock.ts');
  const t=normalizeDockTree(characterDockTree());
  assert.equal(panelStack(t,'human_creator'),'dock-left');
  assert.equal(findDockStack(t,'dock-left').active,'human_creator');
  assert.equal(findDockStack(t,'dock-center').active,'scene');
  assert.equal(findDockStack(t,'dock-bottom'),null);
  assert.equal(panelStack(t,'assets'),null);
});
