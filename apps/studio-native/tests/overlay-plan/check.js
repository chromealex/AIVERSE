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
      !document.querySelector('canvas[aria-label="Ground overlays on the open map"]'), 'off clears ghost and overlays');
    log('LINK_OFF');
    await wait(() => document.getElementById('overlay-structure').value==='outpost', 'linked selection restored');
    const before = (await rpc('studio.snapshot')).graphDocument;
    checkbox('sensor_radius').click();
    const canvas = await wait(() => document.querySelector('canvas[aria-label="Ground overlays on the open map"]'), 'ground plan');
    checkbox('build_site').click();

    function hasColor(r,g,b) {
      const pixels = canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;
      for (let i=0; i<pixels.length; i+=4) {
        if (pixels[i]===r && pixels[i+1]===g && pixels[i+2]===b) return true;
      }
      return false;
    }
    await wait(() => hasColor(246,191,74) && hasColor(22,234,175), 'gold ghost and turquoise rings');
    log('SHOT_plan');
    await sleep(900);
    const rect = canvas.getBoundingClientRect();
    const scale = Math.min((rect.width-16)/24, (rect.height-16)/16);
    canvas.dispatchEvent(new MouseEvent('click',{bubbles:true,
      clientX:rect.left+(rect.width-24*scale)/2+4.1*scale, clientY:rect.top+(rect.height-16*scale)/2+4.1*scale}));
    await wait(() => hasColor(219,56,84), 'red ghost at water');
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
    log('VERIFIED map isolation and stale command rejection; project colors, forced rings, concurrent overlays, water click, deselection and Hierarchy selection');
    log('DONE_1');
  } catch (error) { log('FAIL '+error.message); }
})();
