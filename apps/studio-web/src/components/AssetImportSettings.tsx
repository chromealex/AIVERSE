import {useEffect,useState} from 'react';
import {X, FileCog, ClipboardCopy, Lock, LockOpen} from 'lucide-react';
import {invoke} from '../bridge';
import type {StudioAsset,StudioAssetDetail,StudioAssetReference} from '../types';

const OPTIONS={
  compression:['auto','none','fast','high','lossless'],
  metalFormat:['auto','astc','rgba8'],
  vulkanFormat:['auto','astc','bc7','etc2','rgba8'],
  glesFormat:['auto','etc2','rgba8'],
  meshCompression:['off','low','medium','high'],
  audioCompression:['auto','lossless','vorbis','aac']
} as const;
type SelectProps={label:string;value:string;options:readonly string[];
  disabled:boolean;onChange:(value:string)=>void};
function SettingSelect({label,value,options,disabled,onChange}:SelectProps){
  return <label className="studio-asset-setting">
    <span>{label}</span>
    <select value={value} disabled={disabled} onChange={e=>onChange(e.target.value)}>
      {options.map(option=><option value={option} key={option}>{option}</option>)}
    </select>
  </label>;
}
function SettingToggle({label,value,disabled,onChange}:{
  label:string;value:boolean;disabled:boolean;onChange:(next:boolean)=>void
}){
  return <label className="studio-asset-setting">
    <span>{label}</span>
    <input type="checkbox" checked={value} disabled={disabled}
      onChange={e=>onChange(e.target.checked)}/>
  </label>;
}
export default function AssetImportSettings({asset,onClose,onSaved,onError,locked,onToggleLock}:{
  asset:StudioAsset;onClose:()=>void;onSaved:()=>void;onError:(message:string)=>void;
  locked:boolean;onToggleLock:()=>void;
}){
  const [detail,setDetail]=useState<StudioAssetDetail|null>(null);
  const [busy,setBusy]=useState(false);
  const [loading,setLoading]=useState(true);
  const [references,setReferences]=useState<StudioAssetReference[]>([]);
  const [referenceError,setReferenceError]=useState('');
  const guid=asset.guid;
  useEffect(()=>{
    let disposed=false;
    setDetail(null);setLoading(true);
    if(!guid){setLoading(false);return;}
    void invoke('studio.assetByGuid',{guid}).then(result=>{
      if(!disposed)setDetail(result);
    }).catch(e=>{if(!disposed)onError(String(e instanceof Error?e.message:e))})
      .finally(()=>{if(!disposed)setLoading(false)});
    return ()=>{disposed=true};
  },[guid,onError]);
  useEffect(()=>{
    let cancelled=false;
    setReferences([]);setReferenceError('');
    void invoke('studio.assetReferences',{}).then(rows=>{
      if(!cancelled)setReferences(rows.filter(row=>
        (guid && row.guid===guid) || row.path==='content/'+asset.path));
    }).catch(error=>{
      if(!cancelled)setReferenceError(String(error instanceof Error?error.message:error));
    });
    return ()=>{cancelled=true};
  },[guid,asset.path]);
  const save=async(key:string,value:string|number|boolean)=>{
    if(!guid||busy)return;
    setBusy(true);
    try{
      setDetail(await invoke('studio.assetMetaPatch',{guid,key,value}));
      onSaved();
    }catch(e){onError(String(e instanceof Error?e.message:e))}
    finally{setBusy(false)}
  };
  const settings=detail?.settings??{};
  const str=(key:string,defaultValue='auto'):string=>
    typeof settings[key]==='string'?String(settings[key]):defaultValue;
  const yes=(key:string):boolean=>settings[key]===true;
  const platforms=settings.platformFormats&&typeof settings.platformFormats==='object'?
    settings.platformFormats as Record<string,unknown>:{};
  const platform=(key:string):string=>
    typeof platforms[key]==='string'?String(platforms[key]):'auto';
  return <aside className="studio-asset-import-settings" aria-label="Asset import settings">
    <header><FileCog size={16}/><strong>Import Settings</strong>
      <button type="button" onClick={onToggleLock} aria-pressed={locked}
        title={locked ? 'Unlock asset import settings from Assets selection' : 'Lock asset import settings to this asset'}
        aria-label={locked ? 'Unlock import settings' : 'Lock import settings'}>
        {locked ? <Lock size={16}/> : <LockOpen size={16}/>}
      </button>
      <button type="button" title="Close asset import settings" aria-label="Close import settings"
        onClick={onClose}><X size={16}/></button></header>
    {loading?<div className="studio-asset-settings-note">Loading metadata…</div>:
      !detail?<div className="studio-asset-settings-note">No metadata available. Refresh Assets.</div>:
      <div className="studio-asset-settings-body">
        <div className="studio-asset-setting-path" title={detail.path}>{detail.path}</div>
        <div className="studio-asset-setting-kind">{detail.importer}</div>
        <div className="studio-asset-guid">
          <span>GUID</span><code title={detail.guid}>{detail.guid}</code>
          <button type="button" title="Copy GUID" aria-label="Copy asset GUID"
            onClick={()=>{void navigator.clipboard?.writeText(detail.guid)}}><ClipboardCopy size={14}/></button>
        </div>
        {detail.importer==='texture'&&<>
          <SettingSelect label="Compression" value={str('compression')}
            options={OPTIONS.compression} disabled={busy}
            onChange={v=>void save('compression',v)}/>
          <SettingSelect label="Max size" value={String(settings.maxSize??4096)}
            options={['128','256','512','1024','2048','4096','8192','16384']} disabled={busy}
            onChange={v=>void save('maxSize',Number(v))}/>
          <SettingToggle label="Mipmaps" value={yes('mipmaps')} disabled={busy}
            onChange={v=>void save('mipmaps',v)}/>
          <SettingToggle label="sRGB" value={yes('sRGB')} disabled={busy}
            onChange={v=>void save('sRGB',v)}/>
          <strong className="studio-asset-setting-section">Platform format hints</strong>
          <SettingSelect label="Metal" value={platform('metal')} disabled={busy}
            options={OPTIONS.metalFormat} onChange={v=>void save('metalFormat',v)}/>
          <SettingSelect label="Vulkan" value={platform('vulkan')} disabled={busy}
            options={OPTIONS.vulkanFormat} onChange={v=>void save('vulkanFormat',v)}/>
          <SettingSelect label="GLES" value={platform('gles')} disabled={busy}
            options={OPTIONS.glesFormat} onChange={v=>void save('glesFormat',v)}/>
        </>}
        {detail.importer==='model'&&<>
          <SettingSelect label="Mesh compression" value={str('meshCompression','medium')}
            options={OPTIONS.meshCompression} disabled={busy}
            onChange={v=>void save('meshCompression',v)}/>
          <SettingToggle label="Generate LODs" value={yes('generateLODs')} disabled={busy}
            onChange={v=>void save('generateLODs',v)}/>
        </>}
        {detail.importer==='audio'&&<>
          <SettingSelect label="Compression" value={str('compression')}
            options={OPTIONS.audioCompression} disabled={busy}
            onChange={v=>void save('compression',v)}/>
          <SettingSelect label="Quality" value={String(settings.quality??0.8)}
            options={['0','0.25','0.5','0.75','0.8','1']} disabled={busy}
            onChange={v=>void save('quality',Number(v))}/>
        </>}
        {detail.importer==='data'&&<p className="studio-asset-settings-note">
          Data assets have no compression settings yet.</p>}
        <p className="studio-asset-settings-note">
          Settings are stored in the adjacent .meta file. Actual GPU
          conversion and platform packaging remain separate build steps.
        </p>
      </div>}
    <section className="studio-asset-references" aria-label="Asset usages">
      <strong>Used by ({references.length})</strong>
      {referenceError && <p role="alert">{referenceError}</p>}
      {!referenceError && !references.length && <p>No indexed references in materials or model views.</p>}
      {references.map((ref,index)=><div key={ref.document+ref.owner+ref.field+index}
        className="studio-asset-usage">
        <span>{ref.document} · {ref.owner}</span>
        <small>{ref.field} · {ref.writable?'Material reference':'Read-only model reference'}</small>
      </div>)}
    </section>
  </aside>;
}
