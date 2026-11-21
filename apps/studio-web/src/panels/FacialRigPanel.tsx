import { useEffect, useMemo, useState } from 'react';
import { Eye, FilePlus2, FolderOpen, RotateCcw, Save, SlidersHorizontal, Plus, Trash2, Lock, LockOpen } from 'lucide-react';
import { invoke, nativeConnected } from '../bridge';
import {
  StudioPanel, StudioSection, StudioButton, StudioIconButton,
  StudioField, StudioInput, StudioSelect, StudioTabs, StudioBadge,
  StudioEmptyState, StudioAlert
} from '../ui';
import {
  ARKIT52, arkitFaceMapping, newFacialRig, validateFacialRigDraft, validRigFileName
} from './facialRigSchema';
import type { FacialRigDocument, FacialMorphBinding, FacialIkUnit } from './facialRigSchema';
import AssetReferencePicker from '../components/AssetReferencePicker';
import type { StudioFacialPreviewBinding } from '../types';
import './FacialRigPanel.css';

type Tab='morphs'|'ik';
const clone=(doc:FacialRigDocument):FacialRigDocument=>structuredClone(doc);
const message=(error:unknown)=>error instanceof Error?error.message:String(error);
const defaultMorph=():FacialMorphBinding=>({
  parameter:'expression', node:'Face', target:'expression',
  gain:1, bias:0, halfLife:0.06, blend:'override'
});

export default function FacialRigPanel({projectPath}:{projectPath:string}) {
  const connected=nativeConnected();
  const [names,setNames]=useState<string[]>([]);
  const [selected,setSelected]=useState('');
  const [saved,setSaved]=useState<FacialRigDocument|null>(null);
  const [draft,setDraft]=useState<FacialRigDocument|null>(null);
  const [newName,setNewName]=useState('');
  const [faceNode,setFaceNode]=useState('Face');
  const [query,setQuery]=useState('');
  const [active,setActive]=useState(0);
  const [tab,setTab]=useState<Tab>('morphs');
  const [busy,setBusy]=useState(false);
  const [loading,setLoading]=useState(false);
  const [error,setError]=useState('');
  const [info,setInfo]=useState('');
  const [nonce,setNonce]=useState(0);
  const [validated,setValidated]=useState(false);
  const [modelGuid,setModelGuid]=useState('');
  const [modelLocked,setModelLocked]=useState(false);
  const [binding,setBinding]=useState<StudioFacialPreviewBinding|null>(null);
  const [inputValues,setInputValues]=useState<Record<string,number>>({});
  const [inputVectors,setInputVectors]=useState<Record<string,[number,number,number]>>({});
  const [previewBusy,setPreviewBusy]=useState(false);
  const [previewStatus,setPreviewStatus]=useState('');
  // Poll actual native renderer status, not a client-generated preview image.
  useEffect(()=>{
    if(!connected || !projectPath || !binding?.bound)return;
    let alive=true;
    const refresh=()=>{
      void invoke('studio.facialPreviewInfo',{}).then(next=>{
        if(alive && next.guid===modelGuid &&
           next.revision===binding.revision)setBinding(next);
      }).catch(()=>{});
    };
    refresh();
    const interval=window.setInterval(refresh,700);
    return ()=>{alive=false;window.clearInterval(interval);};
  },[connected,projectPath,modelGuid,binding?.bound,binding?.revision]);

  // Short debounce keeps sliders interactive while streaming only typed,
  // validated scalar/IK values into the device-owner native session.
  useEffect(()=>{
    if(!connected || !projectPath || !binding?.bound)return;
    const handle=window.setTimeout(()=>{
      void invoke('studio.facialPreviewControls',{
        revision:binding.revision,
        values:inputValues,vectors:inputVectors
      }).then(next=>{
        if(next.guid===modelGuid && next.revision===binding.revision)
          setBinding(next);
      }).catch(e=>setError(message(e)));
    },75);
    return ()=>window.clearTimeout(handle);
  },[connected,projectPath,modelGuid,binding?.bound,
     binding?.revision,inputValues,inputVectors]);

  const dirty=!!draft && (!saved || JSON.stringify(draft)!==JSON.stringify(saved));
  const issues=useMemo(()=>draft?validateFacialRigDraft(draft):[],[draft]);
  const editable=connected && !!projectPath && !busy && !loading;
  const selectedMorph=draft?.morphs[active];
  const filtered=draft?.morphs.map((m,i)=>({...m,index:i}))
    .filter(m=>[m.parameter,m.node,m.target].some(value=>value.toLowerCase().includes(query.toLowerCase())))??[];

  useEffect(()=>{
    let cancelled=false;
    setSelected('');setSaved(null);setDraft(null);setNames([]);
    setError('');setInfo('');setValidated(false);setActive(0);
    setModelGuid('');setModelLocked(false);setBinding(null);
    setInputValues({});setInputVectors({});setPreviewStatus('');
    if(connected && !projectPath)return;
    setLoading(true);
    void invoke('studio.facialRigList',{}).then(response=>{
      if(!cancelled)setNames(response.rigs);
    }).catch(e=>{if(!cancelled)setError(message(e));})
      .finally(()=>{if(!cancelled)setLoading(false);});
    return ()=>{cancelled=true};
  },[projectPath,connected,nonce]);

  const load=async(name:string)=>{
    if(dirty){setError('Unsaved changes: save or discard your draft before changing presets.');return;}
    if(!connected||!projectPath)return;
    setBusy(true);setError('');setInfo('');
    try {
      const response=await invoke('studio.facialRigGet',{name});
      setSelected(name);setSaved(clone(response.rig));setDraft(clone(response.rig));
      setActive(0);setValidated(false);setBinding(null);setPreviewStatus('');
    }catch(e){setError(message(e));}finally{setBusy(false);}
  };
  const startNew=(use52:boolean)=>{
    if(dirty){setError('Save or discard changes before creating a new rig.');return;}
    if(!validRigFileName(newName)){setError('Use 1–64 letters, digits, _ or - for the preset filename.');return;}
    if(names.includes(newName)){setError('A rig with this name already exists.');return;}
    const rig=use52?arkitFaceMapping(faceNode.trim()||'Face'):newFacialRig();
    setSelected(newName);setDraft(rig);setSaved(null);
    setActive(0);setTab('morphs');setError('');setInfo('');
    setValidated(false);setNewName('');setBinding(null);setPreviewStatus('');
  };
  const mutate=(fn:(d:FacialRigDocument)=>FacialRigDocument)=>{
    if(!draft)return;
    setDraft(fn(clone(draft)));setError('');setInfo('');setValidated(false);
    setBinding(null);setPreviewStatus('Draft changed; bind again to validate native selection.');
  };
  const changeMorph=(index:number,change:Partial<FacialMorphBinding>)=>
    mutate(d=>{d.morphs[index]={...d.morphs[index],...change};return d;});
  const addMorph=()=>{
    mutate(d=>{d.morphs.push(defaultMorph());return d;});
    setActive(draft?.morphs.length??0);
  };
  const removeMorph=(index:number)=>{
    mutate(d=>{d.morphs.splice(index,1);return d;});
    setActive(Math.max(0,Math.min(active,(draft?.morphs.length??1)-2)));
  };
  const addAim=()=>{
    mutate(d=>{(d.rig??=[]).push({
      op:'aim',bone:'Eye.L',target:'lookAt',axis:[0,0,1],
      weight:1,weightParameter:'lookWeight'
    });return d;});
    setTab('ik');
  };
  const changeIk=(index:number,update:Partial<FacialIkUnit>)=>
    mutate(d=>{if(d.rig)d.rig[index]={...d.rig[index],...update};return d;});

  const validate=async()=>{
    if(!draft||busy)return;
    if(issues.length){setError(issues[0]);return;}
    if(!editable){setError('Native validation needs an open project in the macOS Studio host.');return;}
    setBusy(true);setError('');setInfo('');
    try{
      const result=await invoke('studio.facialRigValidate',{rig:draft});
      setValidated(result.valid);
      setInfo('Document schema valid. Model/mesh morph names and GPU playback are not yet verified.');
    }catch(e){setError(message(e));setValidated(false);}
    finally{setBusy(false);}
  };
  const save=async()=>{
    if(!draft||!selected||busy)return;
    if(issues.length){setError(issues[0]);return;}
    if(!editable)return;
    setBusy(true);setError('');setInfo('');
    try {
      const response=await invoke('studio.facialRigSave',{name:selected,rig:draft});
      setSaved(clone(response.rig));setDraft(clone(response.rig));
      setValidated(false);
      setNames(prev=>[...new Set([...prev,selected])].sort());
      setInfo('Saved in project/facial-rigs/'+selected+'.json. MapDoc Undo does not cover asset files.');
    }catch(e){setError(message(e));}finally{setBusy(false);}
  };
  const discard=()=>{
    if(saved)setDraft(clone(saved));
    else {setDraft(null);setSelected('');}
    setError('');setInfo('Draft discarded.');setValidated(false);setActive(0);
    setBinding(null);setPreviewStatus('');
  };


  // A model is selected by immutable content/.meta GUID through the shared
  // Assets picker. The native host independently resolves and checks its path.
  const chooseModel=(guid:string)=>{
    if(modelLocked)return;
    setModelGuid(guid);setBinding(null);setInputValues({});setInputVectors({});
    setPreviewStatus(guid?'Model selected; bind the draft to inspect native inputs.':'');
  };
  const bindNative=async()=>{
    if(!draft||!modelGuid||issues.length||!editable||previewBusy)return;
    setPreviewBusy(true);setError('');setPreviewStatus('');
    try {
      const result=await invoke('studio.facialPreviewBind',{guid:modelGuid,rig:draft});
      setBinding(result);setInputValues({...result.values});
      setInputVectors({...result.vectors});
      setPreviewStatus('Binding accepted. The native viewport will load the imported model and compile its facial rig.');
    }catch(e){setError(message(e));setBinding(null);}
    finally{setPreviewBusy(false);}
  };
  const sendInputs=async()=>{
    if(!binding||!binding.bound||previewBusy)return;
    setPreviewBusy(true);setError('');setPreviewStatus('');
    try {
      const result=await invoke('studio.facialPreviewControls',{
        revision:binding.revision,values:inputValues,vectors:inputVectors
      });
      setBinding(result);setInputValues({...result.values});
      setInputVectors({...result.vectors});
      setPreviewStatus('Inputs accepted; the next native Metal frame uses these values.');
    }catch(e){setError(message(e));}
    finally{setPreviewBusy(false);}
  };

  return <StudioPanel title="Facial Rig" eyebrow="MOTIONWEAVE / CHARACTER"
    subtitle={selected||'Semantic morph and IK authoring'}
    actions={<StudioIconButton aria-label="Refresh facial rigs"
      icon={<RotateCcw size={15}/>} onClick={()=>setNonce(x=>x+1)}
      disabled={busy||dirty||loading}/>}
    footer={<div className="au-row facial-footer">
      <StudioBadge tone={dirty?'warning':validated?'success':'neutral'}>
        {dirty?'Unsaved draft':validated?'Schema valid':'Authoring'}
      </StudioBadge>
      <StudioButton onClick={discard} size="sm" disabled={!draft||!dirty}>Discard</StudioButton>
      <StudioButton size="sm" onClick={()=>void validate()} disabled={!draft||busy||issues.length>0}>
        Validate
      </StudioButton>
      <StudioButton size="sm" variant="primary" onClick={()=>void save()}
        loading={busy} disabled={!editable||!draft||!dirty||issues.length>0}>
        <Save size={14}/> Save
      </StudioButton>
    </div>}>
    <div className="au-stack facial-editor">
      {!connected&&<StudioAlert tone="warning" title="Browser demonstration">
        Native project files and rendering are unavailable. You can inspect the authoring layout,
        but saving requires the macOS Studio host.
      </StudioAlert>}
      {connected&&!projectPath&&<StudioEmptyState title="Open a project"
        icon={<FolderOpen size={22}/>}
        description="Facial presets belong to your AIVERSE project, not a global library."/>}
      <StudioSection title="Facial model binding"
        actions={<StudioIconButton aria-label={modelLocked?'Unlock facial model':'Lock facial model'}
          icon={modelLocked?<Lock size={15}/>:<LockOpen size={15}/>}
          onClick={()=>setModelLocked(v=>!v)} disabled={!modelGuid}
          title={modelLocked?'Unlock from asset selection':'Lock model reference'}/>}>
        <StudioField label="Model from Assets" htmlFor="facial-model-guid">
          <AssetReferencePicker value={modelGuid} label="facial model asset"
            kinds={['model']} onChange={chooseModel}
            disabled={!editable||modelLocked||previewBusy}/>
        </StudioField>
        {modelLocked&&<p className="facial-footnote">Model reference locked: Assets selection and drag-and-drop cannot replace it.</p>}
        <div className="au-row facial-actions">
          <StudioButton size="sm" onClick={()=>void bindNative()}
            disabled={!editable||!draft||!modelGuid||issues.length>0||previewBusy}>
            <Eye size={14}/> Bind model &amp; controls
          </StudioButton>
          {binding?.bound&&<StudioBadge>{binding.path}</StudioBadge>}
        </div>
        {binding?.bound&&<>
          <p className="facial-footnote">Native binding revision {binding.revision}.
            Input changes are staged until explicitly sent.</p>
          <div className="facial-native-controls">
            {Object.entries(inputValues).map(([name,value])=>
              <StudioField key={name} label={name} htmlFor={'facial-input-'+name}>
                <div className="au-row">
                  <StudioInput id={'facial-input-'+name} type="range"
                    min={0} max={1} step={0.01} value={value}
                    disabled={previewBusy} onChange={e=>setInputValues(current=>({
                      ...current,[name]:Number(e.target.value)
                    }))}/>
                  <span>{value.toFixed(2)}</span>
                </div>
              </StudioField>)}
          </div>
          {Object.entries(inputVectors).map(([name,vector])=>
            <div key={name} className="facial-ik-vector">
              <strong>{name} (IK vector)</strong>
              <div className="au-row facial-vector-coordinates">
                {vector.map((value,axis)=>
                  <StudioField key={axis} label={['X','Y','Z'][axis]}
                    htmlFor={'facial-vector-'+name+'-'+axis}>
                    <StudioInput id={'facial-vector-'+name+'-'+axis}
                      type="number" step={0.1} value={value}
                      disabled={previewBusy}
                      onChange={e=>{
                        const number=Number(e.target.value);
                        if(e.target.value===''||!Number.isFinite(number))return;
                        setInputVectors(old=>{
                          const previous=old[name];
                          if(!previous)return old;
                          const next=[...previous] as [number,number,number];
                          next[axis]=number;
                          return {...old,[name]:next};
                        });
                      }}/>
                  </StudioField>)}
              </div>
            </div>)}
          <StudioButton size="sm" onClick={()=>void sendInputs()}
            disabled={previewBusy}>Send native inputs</StudioButton>
        </>}
        {previewStatus&&<p className="facial-footnote" role="status">{previewStatus}</p>}
        {binding?.runtimeReady?
          <StudioAlert tone="success" title="Native facial frame submitted">
            <span className="facial-preview-note"><Eye size={14}/>
              The actual imported model is rendered through Studio's Metal Renderer3D
              using MotionWeave's facial rig and one Animator pose.
              This is not yet a passed pixel, Vulkan/GLES or performance test.
            </span>
          </StudioAlert>:
          <StudioAlert tone="warning" title="Native facial preview">
            {binding?.reason??'Choose a model, then bind the facial draft.'}
            The renderer reports readiness only after a real Metal frame;
            no synthetic browser character is displayed.
          </StudioAlert>}
      </StudioSection>
      {error&&<StudioAlert tone="danger" title="Cannot complete action">{error}</StudioAlert>}
      {info&&<StudioAlert tone="success">{info}</StudioAlert>}
      {issues.length>0&&draft&&<StudioAlert tone="warning" title="Draft needs attention">
        {issues.slice(0,4).join(' · ')}{issues.length>4?' · and '+(issues.length-4)+' more':''}
      </StudioAlert>}
      <StudioSection title="Preset library">
        <div className="au-row facial-new">
          <StudioField label="Preset file" htmlFor="facial-preset-name">
            <StudioInput id="facial-preset-name" value={newName} maxLength={64}
              onChange={e=>setNewName(e.target.value)} placeholder="hero_face"/>
          </StudioField>
          <StudioField label="Face node" htmlFor="facial-face-node">
            <StudioInput id="facial-face-node" value={faceNode} maxLength={128}
              onChange={e=>setFaceNode(e.target.value)} placeholder="Face"/>
          </StudioField>
        </div>
        <div className="au-row facial-actions">
          <StudioButton size="sm" onClick={()=>startNew(true)}
            disabled={busy||dirty||!validRigFileName(newName)}>
            <FilePlus2 size={14}/> New 52-channel template
          </StudioButton>
          <StudioButton size="sm" variant="ghost" onClick={()=>startNew(false)}
            disabled={busy||dirty||!validRigFileName(newName)}>Blank rig</StudioButton>
        </div>
        <div className="facial-preset-list">
          {loading?<p>Loading presets…</p>:!names.length?<p>No saved facial presets.</p>:
           names.map(name=><StudioButton key={name} variant={name===selected?'primary':'ghost'}
             size="sm" disabled={busy||dirty||!editable} onClick={()=>void load(name)}>
             {name}
           </StudioButton>)}
        </div>
      </StudioSection>

      {draft?<StudioSection title="Expression and IK controls"
        actions={<StudioBadge>{draft.morphs.length} morphs · {draft.rig?.length??0} IK</StudioBadge>}>
        <StudioTabs<Tab> label="Facial tools" items={[
          {id:'morphs',label:'Morph channels'}, {id:'ik',label:'Bone / gaze IK'}
        ]} selected={tab} onChange={setTab}/>
        {tab==='morphs'?<>
          <div className="au-row facial-actions">
            <StudioInput aria-label="Search morph mappings" placeholder="Search morphs…"
              value={query} onChange={e=>setQuery(e.target.value)}/>
            <StudioButton size="sm" onClick={addMorph} disabled={busy||draft.morphs.length>=256}>
              <Plus size={14}/> Add
            </StudioButton>
          </div>
          <div className="facial-morphs">
            <div className="facial-morph-list" aria-label="Morph channel mappings">
              {filtered.map(m=><div key={m.index} className="facial-morph-row">
                <button type="button" className={'facial-morph-item'+(active===m.index?' active':'')}
                  onClick={()=>setActive(m.index)} aria-pressed={active===m.index}>
                  <span>{m.parameter}</span><small>{m.node} / {m.target}</small>
                </button>
                <StudioInput className="facial-gain-slider" type="range"
                  aria-label={'Gain: '+m.parameter} min={0} max={4} step={0.05}
                  value={m.gain??1} onChange={e=>
                    changeMorph(m.index,{gain:Number(e.target.value)})}/>
              </div>)}
              {!filtered.length&&<p>No matching morph channels.</p>}
            </div>
            <div className="facial-morph-detail">
              {selectedMorph?<>
                <div className="au-row facial-actions">
                  <strong>Channel {active+1}</strong>
                  <StudioButton variant="ghost" size="sm" onClick={()=>removeMorph(active)}>
                    <Trash2 size={14}/> Remove
                  </StudioButton>
                </div>
                {(['parameter','node','target'] as const).map(key=>
                  <StudioField key={key} label={key==='parameter'?'Parameter':key==='node'?'Model node':'Imported morph name'}
                    htmlFor={'facial-'+key}>
                    <StudioInput id={'facial-'+key} maxLength={128} value={selectedMorph[key]}
                      onChange={e=>changeMorph(active,{[key]:e.target.value})}/>
                  </StudioField>)}
                <div className="au-row facial-new">
                  {(['gain','bias','halfLife'] as const).map(key=>
                    <StudioField key={key} label={key==='halfLife'?'Smoothing (s)':key==='gain'?'Gain':'Bias'}
                      htmlFor={'facial-'+key}>
                      <StudioInput id={'facial-'+key} type="number" step="0.01"
                        min={key==='bias'?-1:0} max={key==='gain'?4:key==='halfLife'?10:1}
                        value={selectedMorph[key]??(key==='gain'?1:0)}
                        onChange={e=>{
                          const n=Number(e.target.value);
                          if(e.target.value!==''&&Number.isFinite(n))
                            changeMorph(active,{[key]:n});
                        }}/>
                    </StudioField>)}
                </div>
                <StudioField label="Blending" htmlFor="facial-blend">
                  <StudioSelect id="facial-blend" value={selectedMorph.blend??'override'}
                    onChange={e=>changeMorph(active,{blend:e.target.value as 'override'|'additive'})}>
                    <option value="override">Override</option>
                    <option value="additive">Additive</option>
                  </StudioSelect>
                </StudioField>
              </>:<StudioEmptyState title="Choose a morph channel"
                description="Select a semantic mapping or add a new one." icon={<SlidersHorizontal size={22}/>}/>}
            </div>
          </div>
        </>:<>
          <StudioAlert>IK uses the compiled native AnimationRig when MotionWeave is integrated.
            Node/parameter names must match the imported model and runtime control profile.</StudioAlert>
          <StudioButton size="sm" onClick={addAim}><Plus size={14}/> Add eye aim</StudioButton>
          {(draft.rig??[]).map((ik,index)=><div key={index} className="au-card facial-ik-card">
            <div className="au-row facial-actions">
              <strong>{ik.op} · {index+1}</strong>
              <StudioButton size="sm" variant="ghost" onClick={()=>mutate(d=>{
                d.rig?.splice(index,1);return d;
              })}><Trash2 size={14}/> Remove</StudioButton>
            </div>
            <StudioField label="Bone" htmlFor={'facial-ik-bone-'+index}>
              <StudioInput id={'facial-ik-bone-'+index} value={ik.bone} maxLength={128}
                onChange={e=>changeIk(index,{bone:e.target.value})}/>
            </StudioField>
            <StudioField label="Target parameter" htmlFor={'facial-ik-target-'+index}>
              <StudioInput id={'facial-ik-target-'+index} value={ik.target??''} maxLength={128}
                onChange={e=>changeIk(index,{target:e.target.value})}/>
            </StudioField>
            <StudioField label="Weight" htmlFor={'facial-ik-weight-'+index}>
              <StudioInput type="number" min={0} max={1} step={0.05}
                id={'facial-ik-weight-'+index} value={ik.weight??1}
                onChange={e=>{const n=Number(e.target.value);
                  if(e.target.value!==''&&Number.isFinite(n))changeIk(index,{weight:n});}}/>
            </StudioField>
          </div>)}
          {!(draft.rig?.length)&&<StudioEmptyState title="No IK units yet"
            description="Add an eye aim unit or author advanced IK in the JSON preset."/>}
        </>}
      </StudioSection>:<StudioEmptyState title="Choose or create a facial preset"
        description="Facial rigs map named input controls to imported morph targets and optional bone IK."/>}
      <p className="facial-footnote">{ARKIT52.length} ARKit-style names are provided as an authoring template,
        not as a claim that your selected character contains these shapes. Project asset saves
        are independent of map Undo/Redo.</p>
    </div>
  </StudioPanel>;
}
