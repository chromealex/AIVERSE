import {useEffect,useState} from 'react';
import {invoke,nativeConnected,onNativeEvent} from '../bridge';
import {StudioSelect} from '../ui';

type Preview = {quality:string;scene:boolean;gameOverride:boolean};

export default function PrismaPreviewControls({project}:{project:string}) {
  const [preview,setPreview]=useState<Preview>();
  const [busy,setBusy]=useState(false);
  const [error,setError]=useState('');
  async function update(params:Partial<Preview>) {
    const reply=await invoke('studio.op',{op:'view_postfx',expectedProject:project,params});
    return reply.result as Preview;
  }
  useEffect(()=>{
    if (!nativeConnected()) return;
    let current=true;
    const unsubscribe=onNativeEvent((event,payload)=>{
      if(event==='postfx.preview')setPreview(payload as Preview);
    });
    void update({}).then(value=>{if(current)setPreview(value);}).catch(e=>{if(current)setError(String(e));});
    return ()=>{current=false;unsubscribe();};
  },[project]);
  async function change(params:Partial<Preview>) {
    setBusy(true);
    try {setPreview(await update(params));setError('');}
    catch(e){setError(String(e));}
    finally {setBusy(false);}
  }
  if(!preview&&!error)return null;
  return <fieldset className="pfx-preview" disabled={busy}>
    <legend>Viewport preview</legend>
    {preview&&<>
      <label>Quality <StudioSelect name="previewQuality" value={preview.gameOverride?preview.quality:'default'}
        onChange={e=>void change({quality:e.target.value})}>
        {!preview.gameOverride&&<option value="default" disabled>Scene high · Game automatic</option>}
        {['off','low','medium','high'].map(tier=><option key={tier} value={tier}>{tier[0].toUpperCase()+tier.slice(1)}</option>)}
      </StudioSelect></label>
      <label className="ptp__check"><input type="checkbox" checked={preview.scene}
        onChange={e=>void change({scene:e.target.checked})}/>Show effects in Scene</label>
    </>}
    {error&&<span role="alert">{error}</span>}
  </fieldset>;
}
