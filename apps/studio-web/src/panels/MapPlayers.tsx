import { useEffect, useRef, useState } from 'react';
import { Users } from 'lucide-react';
import { invoke, onNativeEvent } from '../bridge';
import { StudioButton, StudioInput } from '../ui';
import type { PlayerStartPosition, PlayerChanges, StudioMapProperties, StudioSnapshot } from '../types';

export function MapPlayers({data, revision, busy, onApply, requestedPlayer}:{
  requestedPlayer?:{slot:number}|null;
  data:StudioMapProperties; revision:string; busy:boolean; onApply:(snapshot:StudioSnapshot)=>void;
}){
  const [selected,setSelected]=useState(requestedPlayer?.slot??1);
  const [draft,setDraft]=useState<StudioMapProperties>();
  const [removing,setRemoving]=useState(false);
  const [pending,setPending]=useState(false);
  const [error,setError]=useState('');
  const [reset,setReset]=useState(0);
  const base=draft??data;
  const slot=Math.min(selected,base.roster.length);
  const player=base.roster[slot-1];
  const stale=base.scope.revision!==revision;
  const discard=()=>{setDraft(undefined);setRemoving(false);setError('');setReset(n=>n+1);};
  useEffect(()=>{if(requestedPlayer){setSelected(requestedPlayer.slot);discard();}},[requestedPlayer]);
  const send=async(action:'add'|'edit'|'remove',changes?:PlayerChanges)=>{
    setPending(true);setError('');
    try{
      const target=action==='add'?base.roster.length+1:slot;
      onApply(await invoke('studio.mapPlayer',{scope:base.scope,slot:target,action,changes}));
      setSelected(action==='remove'?Math.max(1,slot-1):target);
      discard();
    }catch(e){setError(e instanceof Error?e.message:String(e));}
    finally{setPending(false);}
  };
  const apply=(form:HTMLFormElement)=>{
    if(!player)return;
    const fields=new FormData(form);
    const changes:PlayerChanges={};
    const name=String(fields.get('name')),team=Number(fields.get('team')),faction=String(fields.get('faction'));
    if(name!==player.name)changes.name=name;
    if(team!==player.team)changes.team=team;
    if(faction!==player.faction)changes.faction=faction;
    if(fields.has('waves')!==player.waves)changes.waves=fields.has('waves');
    if(fields.has('hasStart')){
      const x=Number(fields.get('x')),z=Number(fields.get('z'));
      if(base.grid){
        if(!Array.isArray(player.start) || player.start[0]!==x || player.start[1]!==z)changes.start=[x,z];
      }else{
        const body=String(fields.get('body')),face=Number(fields.get('face'));
        const old=Array.isArray(player.start)?undefined:player.start;
        if(!old || old.body!==body || old.face!==face || old.x!==x || old.y!==z)changes.start={...old,body,face,x,y:z};
      }
    }else if(player.start)changes.start=null;
    for(const resource of base.resources){
      const amount=Number(fields.get('resource:'+resource.id));
      if(amount!==(player.resources[resource.id]??resource.initial))(changes.resources??={})[resource.id]=amount;
    }
    if(Object.keys(changes).length)void send('edit',changes);
    else discard();
  };
  return <section className="insp-map" aria-label="Map players"><header><Users size={19}/><h2>Players</h2></header>
    <label>Player slot<select disabled={busy||pending||!!draft||removing} value={slot}
      onChange={e=>{setSelected(Number(e.target.value));discard();}}>
      {base.roster.map((p,i)=><option key={i} value={i+1}>{i+1} · {p.name}</option>)}
    </select></label>
    {player && <form key={[slot,base.scope.revision,reset].join(':')} onChange={()=>setDraft(base)}
      onSubmit={e=>{e.preventDefault();apply(e.currentTarget);}}><fieldset disabled={busy||pending||removing||stale}>
      <label>Player name<StudioInput name="name" defaultValue={player.name}/></label>
      <div className="insp-map__size"><label>Team<StudioInput name="team" type="number" step="1" required defaultValue={player.team}/></label>
      <label>Faction<select name="faction" defaultValue={player.faction}><option value="">Project default</option>
        {player.faction && !base.factions.some(f=>f.id===player.faction) && <option>{player.faction}</option>}
        {base.factions.map(f=><option key={f.id} value={f.id}>{f.name}</option>)}</select></label></div>
      <label className="insp-map__toggle"><input name="waves" type="checkbox" defaultChecked={player.waves}/>Scripted waves</label>
      <PlayerStart initial={player.start} data={base} disabled={busy||pending||removing||stale} onEdit={()=>setDraft(base)}/>
      {base.resources.length>0 && <details><summary>Starting resources</summary>{base.resources.map(r=>
        <label key={r.id}>{r.name}<StudioInput name={'resource:'+r.id} type="number" step="any" required
          min={Math.min(0,player.resources[r.id]??r.initial)} max={Math.max(1e6,player.resources[r.id]??r.initial)} defaultValue={player.resources[r.id]??r.initial}/></label>)}</details>}
      <StudioButton type="submit" variant="primary" disabled={!draft}>Apply player changes</StudioButton>
    </fieldset></form>}
    {error && <p role="alert">{error}</p>}
    {stale && draft && <p role="alert">The map changed. Reload the player list before applying.</p>}
    <div className="insp-map__actions">
      {draft && <StudioButton disabled={pending} onClick={discard}>{stale?'Reload players':'Discard player changes'}</StudioButton>}
      {!draft && !removing && <><StudioButton disabled={busy||pending||stale||base.roster.length>=16} onClick={()=>void send('add')}>Add player</StudioButton>
        {base.roster.length>2 && <StudioButton disabled={busy||pending||stale} onClick={()=>{setDraft(base);setRemoving(true);}}>Remove player</StudioButton>}</>}
      {removing && <><p>Remove {player.name}? Their objects will become neutral.</p>
        <StudioButton disabled={busy||pending||stale} onClick={()=>void send('remove')}>Confirm removal</StudioButton></>}
    </div></section>;
}

function PlayerStart({initial,data,disabled,onEdit}:{initial?:PlayerStartPosition;data:StudioMapProperties;disabled:boolean;onEdit:()=>void}){
  const [token,setToken]=useState('');
  const [picked,setPicked]=useState<{position:PlayerStartPosition;token:string}>();
  const [error,setError]=useState('');
  const editRef=useRef(onEdit);
  editRef.current=onEdit;
  useEffect(()=>{
    if(!token)return;
    if(disabled){setToken('');return;}
    let active=true;
    const off=onNativeEvent((event,payload)=>{
      if(event!=='playerStart.picked')return;
      const result=payload as {token:string;start?:PlayerStartPosition;error?:string};
      if(result.token!==token)return;
      setToken('');
      if(result.error)setError(result.error);
      if(result.start){
        setPicked({position:result.start,token});
        editRef.current();
      }
    });
    const keydown=(e:KeyboardEvent)=>{if(e.key==='Escape'){e.preventDefault();setToken('');}};
    window.addEventListener('keydown',keydown);
    void invoke('studio.playerStartPick',{token,armed:true}).catch(e=>{
      if(active){setError(e instanceof Error?e.message:String(e));setToken('');}
    });
    return ()=>{
      active=false;off();window.removeEventListener('keydown',keydown);
      void invoke('studio.playerStartPick',{token,armed:false}).catch(()=>{});
    };
  },[token,disabled]);
  return <><StartFields key={picked?.token??'original'} initial={picked?.position??initial} data={data}/>
    <div className="insp-map__actions"><StudioButton onClick={()=>{
      setError('');setToken(token?'':Array.from(crypto.getRandomValues(new Uint32Array(4))).join('-'));
    }}>{token?'Cancel pick':'Pick start in Scene'}</StudioButton>
      {token && <small role="status">Click the map surface in Scene.</small>}</div>
    {error && <p role="alert">{error}</p>}</>;
}

function StartFields({initial,data}:{initial?:PlayerStartPosition;data:StudioMapProperties}){
  const planet=Array.isArray(initial)?undefined:initial;
  const xy=Array.isArray(initial)?initial:[planet?.x,planet?.y];
  const [body,setBody]=useState(planet?.body??data.surfaces[0]?.id??'');
  const size=data.surfaces.find(s=>s.id===body)?.size;
  const [enabled,setEnabled]=useState(!!initial);
  return <><label className="insp-map__toggle"><input name="hasStart" type="checkbox" checked={enabled}
    onChange={e=>setEnabled(e.target.checked)}/>Start position</label>
    {!data.grid && enabled && <div className="insp-map__size">
      <label>Body<select name="body" required value={body} onChange={e=>setBody(e.target.value)}>
        {data.surfaces.map(s=><option key={s.id}>{s.id}</option>)}</select></label>
      <label>Face<select name="face" defaultValue={planet?.face??0}>
        {[0,1,2,3,4,5].map(f=><option key={f}>{f}</option>)}</select></label></div>}
    <div className="insp-map__size" hidden={!enabled}>{(['x','z'] as const).map((axis,i)=>
      <label key={axis}>Start {axis==='z' && !data.grid?'Y':axis.toUpperCase()}<StudioInput type="number" name={axis} step="any" disabled={!enabled}
        min={data.grid?undefined:0} max={data.grid?undefined:size} required defaultValue={xy[i]??0}/></label>)}</div></>;
}
