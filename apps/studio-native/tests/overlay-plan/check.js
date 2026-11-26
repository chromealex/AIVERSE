(async () => {
  if (new URLSearchParams(location.search).has('window')) return;
  const native = window.webkit.messageHandlers.aiverse;
  const log = message => native.postMessage({id:0, method:'studio.webDiagnostic', params:{message:'CROSS main ' + message}});
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  async function wait(read, label) {
    for (let i=0; i<300; i++) {
      const result = read();
      if (result) return result;
      await sleep(150);
    }
    throw Error(label + ' | ' + document.body.innerText.slice(-700));
  }
  try {
    const receive = await wait(() => window.__aiverseReceive, 'web bridge');
    const pending = new Map();
    let sequence = 780000000;
    window.__aiverseReceive = message => {
      const waiter = pending.get(message.id);
      if (!waiter) return receive(message);
      pending.delete(message.id);
      if (message.error) waiter.reject(Error(message.error));
      else waiter.resolve(message.result);
    };
    const rpc = (method, params={}) => new Promise((resolve,reject) => {
      const id = ++sequence;
      pending.set(id,{resolve,reject});
      native.postMessage({id,method,params});
    });
    const plan = async params => (await rpc('studio.op',{op:'overlay_plan_preview',params})).result;
    const checkbox = id => [...document.querySelectorAll('label')]
      .find(label => label.textContent.trim()===id)?.querySelector('input');

    await wait(() => document.querySelector('[aria-label="Play"]'), 'shell');
    document.querySelector('[aria-label="Close tour"]')?.click();
    await wait(() => !document.querySelector('[aria-label="Opening project"]'), 'project ready');
    if (checkbox('sensor_radius')) throw Error('Overlays must initially be closed');
    log('LINK_PORT '+(await rpc('studio.agentInfo')).port);
    await wait(() => checkbox('sensor_radius')?.checked && checkbox('build_site')?.checked &&
      document.getElementById('overlay-x')?.value==='4', 'linked preview opens panel and restores both ids');
    checkbox('sensor_radius').click();
    await wait(() => !checkbox('sensor_radius').checked, 'local change applied');
    log('LINK_LOCAL');
    await wait(() => !checkbox('build_site').checked && document.getElementById('overlay-structure').value==='' &&
      !document.querySelector('canvas[aria-label="Project overlays on the open map"]'), 'off clears ghost and overlays');
    log('LINK_OFF');
    await wait(() => document.getElementById('overlay-structure').value==='outpost', 'linked selection restored');
    const before = (await rpc('studio.snapshot')).graphDocument;
    checkbox('sensor_radius').click();
    const canvas = await wait(() => document.querySelector('canvas[aria-label="Project overlays on the open map"]'), 'ground plan');
    checkbox('build_site').click();

    function hasColor(r,g,b) {
      const pixels = canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;
      for (let i=0; i<pixels.length; i+=4) {
        if (pixels[i]===r && pixels[i+1]===g && pixels[i+2]===b) return true;
      }
      return false;
    }
    await wait(() => hasColor(246,191,74) && hasColor(22,234,175), 'gold ghost and turquoise rings');
    const screenIds=['hud_note','vitality','map_marker','map_badge'];
    for (const id of screenIds) {
      checkbox(id).click();
      await wait(()=>checkbox(id).checked, id+' enabled');
    }
    const screenPlan=await plan({ids:screenIds});
    const note=screenPlan.screen.find(item=>item.shape==='text');
    const bars=screenPlan.screen.filter(item=>item.shape==='bar');
    if(screenPlan.primitives.length || note?.text!=='Patrol · 0 selected' || !note.hud ||
       bars.length<2 || bars.some(item=>item.fraction!==1)) throw Error('Screen-only plan lost project values');
    function hudAtRightEdge() {
      const ratio=canvas.width/canvas.clientWidth;
      const pixel=canvas.getContext('2d').getImageData(Math.floor((canvas.clientWidth-16)*ratio),Math.floor(22*ratio),1,1).data;
      return pixel[0]===74 && pixel[1]===49 && pixel[2]===92;
    }
    await wait(()=>hudAtRightEdge() && hasColor(229,105,122) && hasColor(138,201,125) && hasColor(239,160,135),
      'HUD, health bars, screen circle and rounded rectangle');
    canvas.style.width='80%';
    await wait(()=>canvas.width===Math.round(canvas.clientWidth*devicePixelRatio) && hudAtRightEdge(),'HUD follows resized panel');
    canvas.style.width='100%';
    await wait(()=>canvas.width===Math.round(canvas.clientWidth*devicePixelRatio) && hudAtRightEdge(),'HUD follows restored panel');
    const minimapIds=['patrol_signal','patrol_zone','cursor_ping'];
    for (const id of minimapIds) {
      checkbox(id).click();
      await wait(()=>checkbox(id).checked,id+' enabled');
    }
    function miniPixel(x,y,color) {
      const image=document.querySelector('img[alt="Minimap with project overlays"]');
      if(!image?.complete || image.naturalWidth!==192 || image.naturalHeight!==128)return false;
      const sample=document.createElement('canvas');
      sample.width=1; sample.height=1;
      const ctx=sample.getContext('2d');
      ctx.drawImage(image,x,y,1,1,0,0,1,1);
      return [...ctx.getImageData(0,0,1,1).data].slice(0,3).join(',')===color;
    }
    await wait(()=>miniPixel(55,72,'255,189,89') && miniPixel(40,72,'43,72,58') &&
      miniPixel(128,24,'104,207,222') && miniPixel(144,24,'43,72,58') &&
      miniPixel(26,35,'38,76,107'),'minimap ring, disc, painted ground and water');
    await sleep(1800);
    log('SHOT_plan');
    await sleep(1600);
    const rect = canvas.getBoundingClientRect();
    const scale = Math.min((rect.width-16)/24, (rect.height-16)/16);
    canvas.dispatchEvent(new MouseEvent('click',{bubbles:true,
      clientX:rect.left+(rect.width-24*scale)/2+4.1*scale, clientY:rect.top+(rect.height-16*scale)/2+4.1*scale}));
    await wait(() => hasColor(219,56,84), 'red ghost at water');
    await wait(()=>miniPixel(33,33,'232,121,161'),'cursor ping follows map click');
    for(const id of minimapIds) {
      checkbox(id).click();
      await wait(()=>!checkbox(id).checked,id+' disabled');
    }
    await wait(()=>!document.querySelector('img[alt="Minimap with project overlays"]'),'minimap released');
    checkbox('sensor_radius').click();
    await wait(() => !hasColor(22,234,175) && hasColor(219,56,84), 'single overlay remaining');
    let rejected = false;
    try { await plan({ids:['not_real']}); } catch { rejected=true; }
    if (!rejected) throw Error('unknown id accepted');

    const all = await plan({ids:['sensor_radius']});
    const first = (await rpc('studio.snapshot')).scene.entities.find(e => e.id.startsWith('entity:'));
    await rpc('studio.select',{id:first.id});
    const single = await plan({ids:['sensor_radius']});
    if (!(single.primitives.length>0 && single.primitives.length<all.primitives.length)) throw Error('Hierarchy selection was ignored');
    const selectedScreen=(await plan({ids:screenIds})).screen;
    if(selectedScreen.filter(item=>item.shape==='bar').length!==1 ||
       !selectedScreen.some(item=>item.text==='Patrol · 1 selected'))throw Error('Screen anchors ignored selection');
    const after = (await rpc('studio.snapshot')).graphDocument;
    if (before.revision!==after.revision || window.testSceneWasPresent) throw Error('Document or Scene changed');
    const nextMap=await rpc('studio.openMap',{file:'maps/second.json'});
    if(nextMap.graphDocument.epoch===before.epoch)throw Error('Map epoch did not change');
    const reset=(await rpc('studio.op',{op:'overlay_preview',params:{}})).result;
    if(reset.version!==0 || reset.preview.length || 'x' in reset || 'z' in reset)throw Error('Preview leaked across documents');
    let stale=false;
    try { await rpc('studio.op',{op:'overlay_preview',params:{ids:['sensor_radius']},expectedDocumentEpoch:before.epoch}); }
    catch { stale=true; }
    if(!stale)throw Error('Stale UI overlay command accepted');
    log('VERIFIED minimap terrain/background, ring/disc pixels, cursor ping and release; map isolation and stale command rejection; screen HUD resizing, selection text, health bars, circle, rectangle, project colors, forced rings, concurrent overlays, water click, deselection and Hierarchy selection');
    log('DONE_1');
  } catch (error) { log('FAIL '+error.message); }
})();
