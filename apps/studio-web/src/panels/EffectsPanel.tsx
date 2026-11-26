import {useEffect, useState, type ReactNode} from 'react';
import EffectPlayback from './EffectPlayback';
import {invoke} from '../bridge';
import {StudioAlert, StudioButton, StudioSection, StudioSelect, StudioTextarea} from '../ui';

const emptyBindings = '{\n  "events": {}\n}\n';
async function op<T>(project:string, name:string, params:Record<string,unknown>) {
  const reply = await invoke('studio.op', {op:name, expectedProject:project, params});
  return reply.result as T;
}

export default function EffectsPanel({project, tools}:{project:string; tools:ReactNode}) {
  const [name, select] = useState('fx');
  const [files, setFiles] = useState<string[]>([]);
  const [original, setOriginal] = useState<string|null>();
  const [draft, edit] = useState('');
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [busy, setBusy] = useState(false);
  const [revision, reload] = useState(0);
  const dirty = original !== undefined && draft !== (original ?? emptyBindings);

  useEffect(() => {
    if (!project) return;
    let live = true;
    async function read() {
      setBusy(true); setError(''); setNotice('');
      try {
        const [list, file] = await Promise.all([
          op<{effects:{name:string}[]}>(project,'fx_list',{}),
          op<{missing?:boolean; text:string; parseError?:string}>(project,'fx_get',{name,allowMissing:true})
        ]);
        if (!live) return;
        setFiles(list.effects.map(effect => effect.name));
        setOriginal(file.missing ? null : file.text);
        edit(file.missing ? emptyBindings : file.text);
        setError(file.parseError ?? '');
      } catch (e) { if (live) setError(String(e)); }
      finally { if (live) setBusy(false); }
    }
    void read();
    return () => { live = false; };
  }, [project,name,revision]);

  async function save() {
    setBusy(true); setError(''); setNotice('');
    try {
      const result = await op<{ok:boolean; errors?:string[]; warnings?:string[]}>(project,'fx_set',{
        name,text:draft,expectedSource:original
      });
      if (!result.ok) throw Error(result.errors?.join('\n') || 'Validation failed');
      setOriginal(draft); setNotice(['Saved fx/'+name+'.json', ...(result.warnings ?? [])].join('\n'));
    } catch (e) { setError(String(e)); }
    finally { setBusy(false); }
  }
  if (!project) return <StudioAlert>Open a project to edit particle effects.</StudioAlert>;
  return <div className="effects-files">
    <StudioSection title="Particle effects">
      <label>Project file<StudioSelect value={name} disabled={busy || dirty} onChange={e=>{
        setOriginal(undefined); edit(''); select(e.target.value);
      }}>
        <option value="fx">Event bindings · fx/fx.json</option>
        {files.map(id => <option key={id} value={id}>{'fx/'+id+'.json'}</option>)}
      </StudioSelect></label>
      {name!=='fx' && original!==undefined && <EffectPlayback key={name} project={project} source={draft}/>}
      <StudioTextarea aria-label="Effect source" rows={14} spellCheck={false} value={draft}
        disabled={busy || original === undefined} onChange={e=>{edit(e.target.value);setNotice('');}}/>
      <div className="au-row">
        <StudioButton variant="primary" disabled={busy || !dirty} onClick={()=>void save()}>Save file</StudioButton>
        <StudioButton disabled={busy} onClick={()=>reload(revision+1)}>{dirty?'Discard and reload':'Reload'}</StudioButton>
      </div>
      {error && <StudioAlert tone="danger">{error}</StudioAlert>}
      {notice && <StudioAlert role="status">{notice}</StudioAlert>}
    </StudioSection>
    <details><summary>Templates, bindings and operations</summary>{tools}</details>
  </div>;
}
