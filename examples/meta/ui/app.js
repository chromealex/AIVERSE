'use strict';
let token='', state, catalog, dictionaries, tab='shop', language='ru', busy=false, retryRequest=null;
const $=id=>document.getElementById(id);
const tr=key=>dictionaries?.[language]?.[key]||key;
const node=(tag,text,cls)=>{const el=document.createElement(tag);if(text!==undefined)el.textContent=text;if(cls)el.className=cls;return el;};
const amount=b=>Object.entries(b||{}).map(([k,v])=>`${v} ${k}`).join(' · ');
const label=(id,def)=>{const key=def?.metadata?.label;return key&&dictionaries[language][key.slice(1)]?tr(key.slice(1)):id;};
function button(parent,text,fn){const b=node('button',tr(text));b.disabled=busy;b.onclick=fn;parent.append(b);return b;}
function form(parent,fields,submit,labelKey){const f=node('form');const inputs={};for(const [key,title,type='text',value=''] of fields){const l=node('label',tr(title));const i=node('input');i.type=type;i.value=value;i.required=true;l.append(i);f.append(l);inputs[key]=i;}const b=button(f,labelKey,()=>{});b.type='submit';f.onsubmit=e=>{e.preventDefault();submit(Object.fromEntries(Object.entries(inputs).map(([k,v])=>[k,v.type==='number'?Number(v.value):v.value])));};parent.append(f);}
async function rpc(request){const res=await fetch('/rpc',{method:'POST',headers:{'Content-Type':'application/json','Authorization':`Bearer ${token}`},body:JSON.stringify(request)});if(res.status===401)throw new Error(tr('expired'));if(!res.ok)throw new Error(tr('serviceUnavailable'));return res.json();}
async function refresh(){const response=await rpc({service:'meta',action:'view',arguments:{}});if(response.code>1)throw new Error(tr(`code${response.code}`));state=response.data;render();}
async function perform(request){if(busy)return;busy=true;retryRequest=request;$('retry').hidden=true;$('status').textContent=tr('pending');if(state)render();try{const result=await rpc(request);if(result.code>1){retryRequest=null;throw new Error(tr(`code${result.code}`));}retryRequest=null;await refresh();$('status').textContent=tr('saved');}catch(e){$('status').textContent=e.message;$('retry').hidden=!retryRequest;}finally{busy=false;if(state)render();}}
function act(service,action,args={}){return perform({service,action,arguments:args,operation:crypto.randomUUID()});}
function card(grid,title){const c=node('article',undefined,'card');c.append(node('h2',title));grid.append(c);return c;}
function render(){
 $('login').hidden=true;$('dashboard').hidden=false;$('logout').hidden=false;$('logout').textContent=tr('logout');$('identity').textContent=state.player;$('heading').textContent=state.player;
 $('balances').replaceChildren();for(const [id,n] of Object.entries(state.profile.balances||{})){const b=node('div',id,'balance');b.append(node('strong',String(n)));$('balances').append(b);}
 $('tabs').replaceChildren();for(const name of ['shop','progress','boards','chat','social','items','mail','events']){const b=button($('tabs'),name,()=>{tab=name;render();});if(tab===name)b.classList.add('active');}
 const grid=node('div',undefined,'grid');$('content').replaceChildren(grid);const econ=catalog.economy, ext=catalog.extensions;
 if(tab==='shop')for(const id of state.offers){const def=econ.offers[id],c=card(grid,label(id,def));c.append(node('p',`${tr('reward')}: ${amount(def.reward)}`),node('p',`${tr('cost')}: ${def.product||amount(def.cost)}`));if(def.product)c.append(node('p',tr('payment')));else button(c,'buy',()=>act('economy','buy',{id}));}
 if(tab==='progress'){
  for(const [id,def] of Object.entries(econ.tracks||{})){const c=card(grid,label(id,def));c.append(node('p',`${def.metric}: ${state.profile.progress?.[def.metric]||0}`));def.thresholds.forEach((threshold,tier)=>{const row=node('div',undefined,'row');row.append(node('span',`${threshold} → ${amount(def.rewards[tier])}`));button(row,'claim',()=>act('economy','claim',{id,tier}));c.append(row);});}
  for(const [id,def] of Object.entries(econ.daily||{})){const c=card(grid,`${tr('daily')} · ${id}`);c.append(node('p',def.rewards.map(amount).join(' / ')));button(c,'claim',()=>act('economy','daily',{id}));}
  for(const id of Object.keys(econ.energy||{})){const c=card(grid,`${tr('energy')} · ${id}`);button(c,'regenerate',()=>act('economy','energy',{id}));}
 }
 if(tab==='boards')for(const [id,rows] of Object.entries(state.boards)){const c=card(grid,id),table=node('table'),head=node('tr');for(const k of ['rank','player','score'])head.append(node('th',tr(k)));table.append(head);for(const r of rows){const row=node('tr');for(const k of ['rank','player','score'])row.append(node('td',String(r[k])));table.append(row);}c.append(table);}
 if(tab==='chat')for(const [id,rows] of Object.entries(state.chats)){const c=card(grid,id);for(const m of rows){const el=node('div',undefined,'message');el.append(node('strong',m.sender),node('p',m.text));c.append(el);}form(c,[['text','chat']],args=>act('chat','send',{id,...args}),'send');button(c,'refresh',()=>refresh().catch(e=>$('status').textContent=e.message));}
 if(tab==='social'){
  const friends=card(grid,tr('social'));form(friends,[['target','target']],args=>act('friends','request',args),'request');
  for(const target of Object.keys(state.requests||{})){const row=node('div',target,'row');button(row,'accept',()=>act('friends','accept',{target}));button(row,'reject',()=>act('friends','reject',{target}));friends.append(row);}
  for(const target of Object.keys(state.friends||{})){const row=node('div',target,'row');button(row,'remove',()=>act('friends','remove',{target}));button(row,'block',()=>act('friends','block',{target}));friends.append(row);}
  const clan=card(grid,state.clanId||tr('clan'));
  if(!state.clanId){form(clan,[['id','clanid']],args=>act('clan','create',args),'create');for(const id of state.invites||[])button(clan,`${tr('join')} ${id}`,()=>act('clan','join',{id}));}
  else {const id=state.clanId;for(const [p,role] of Object.entries(state.clan.members))clan.append(node('p',`${p} · ${role}`));form(clan,[['target','target']],args=>act('clan','invite',{id,...args}),'invite');form(clan,[['asset','asset','text','gold'],['amount','amount','number','10']],args=>act('clan','donate',{id,...args}),'donate');for(const goal of Object.keys(ext.clans?.goals||{}))button(clan,`${tr('claim')} ${goal}`,()=>act('clan','goal',{id,goal}));button(clan,'leave',()=>act('clan','leave',{id}));}
 }
 if(tab==='items')for(const [type,def] of Object.entries(ext.items||{}))if(def.asset&&(state.profile.balances[def.asset]||0)>0){const c=card(grid,label(type,def));c.append(node('p',amount({[def.asset]:state.profile.balances[def.asset]})));button(c,'claim',()=>act('items','redeem',{id:crypto.randomUUID(),type}));}
 if(tab==='items')for(const [id,item] of Object.entries(state.extras?.items||{})){const def=ext.items[item.type],c=card(grid,label(item.type,def));c.append(node('p',`${id} · ${tr('level')} ${item.level}`));button(c,'upgrade',()=>act('items','upgrade',{id}));button(c,'salvage',()=>act('items','salvage',{id}));for(const slot of def.slots)button(c,`${tr('equip')} ${slot}`,()=>act('items','equip',{id,slot}));button(c,'unequip',()=>act('items','unequip',{id}));if(def.mergeCount)form(c,[['inputs','inputs']],a=>act('items','merge',{id,inputs:a.inputs.split(',').map(s=>s.trim())}),'merge');}
 if(tab==='mail')for(const [id,m] of Object.entries(state.profile.mail||{})){const c=card(grid,id);c.append(node('p',amount(m.reward)));if(!m.claimed)button(c,'claim',()=>act('economy','mail',{id}));}
 if(tab==='events')for(const [id,def] of Object.entries(ext.tournaments||{})){const c=card(grid,id),entry=state.tournaments[id];c.append(node('p',`${tr('until')} ${new Date(def.end*1000).toLocaleDateString(language)}`));if(entry?.result)c.append(node('p',`${tr('rank')}: ${entry.result.rank}`));else button(c,'join',()=>act('tournament','join',{id}));}
 if(!grid.children.length)grid.append(node('p',tr('empty')));
}
function localize(){for(const el of document.querySelectorAll('[data-i18n]'))el.textContent=tr(el.dataset.i18n);document.documentElement.lang=language;if(state)render();}
$('loginForm').onsubmit=async e=>{e.preventDefault();token=$('token').value;$('token').value='';await act('profile','create');};
$('logout').onclick=()=>{token='';state=null;retryRequest=null;$('login').hidden=false;$('dashboard').hidden=true;$('logout').hidden=true;$('identity').textContent='';$('status').textContent='';$('retry').hidden=true;};
$('retry').onclick=()=>{if(retryRequest)perform(retryRequest);};$('language').onchange=e=>{language=e.target.value;localize();};
Promise.all([fetch('/lang.json').then(r=>r.json()),fetch('/catalog').then(r=>r.json())]).then(([l,c])=>{dictionaries=l;catalog=c;localize();}).catch(e=>$('status').textContent=e.message);
