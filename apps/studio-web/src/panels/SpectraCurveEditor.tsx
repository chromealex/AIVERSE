import { useEffect, useMemo, useState } from 'react';
import { Plus, Trash2 } from 'lucide-react';
import { StudioButton, StudioField, StudioInput, StudioSection, StudioSelect } from '../ui';
import type { GraphEdit, SpectraGraphV2 } from './spectraDataInterfaceModel';
import {
  addCurveKey, addGradientKey, colorAndAlpha, editCurveKey, editGradientKey,
  gradientCss, readCurve, readGradient, removeCurveKey, removeGradientKey,
  rgbaWithAlpha, type CurveKey, type GradientKey
} from './spectraCurveModel';
import './SpectraCurveEditor.css';

interface Props {
  graph:SpectraGraphV2;
  disabled?:boolean;
  onEdit:(next:SpectraGraphV2)=>void;
  onError:(error:string)=>void;
}
const fmt=(n:number)=>Number(n.toFixed(4));
export default function SpectraCurveEditor({graph,disabled=false,onEdit,onError}:Props) {
  const nodes=graph.nodes.filter(n=>n.type==='Curve'||n.type==='Gradient');
  const [selected,setSelected]=useState('');
  const [keyIndex,setKeyIndex]=useState(0);
  const node=nodes.find(n=>n.id===selected)??nodes[0];
  const curve=useMemo(()=>node?.type==='Curve'?readCurve(node):null,[node]);
  const gradient=useMemo(()=>node?.type==='Gradient'?readGradient(node):null,[node]);
  const current=curve?.[keyIndex]??gradient?.[keyIndex];
  const isCurve=!!curve;
  const isGradient=!!gradient;

  useEffect(()=>{
    if(!node){setSelected('');setKeyIndex(0);return;}
    if(!selected || !nodes.some(n=>n.id===selected)){setSelected(node.id);setKeyIndex(0);}
  },[selected,node?.id,graph.nodes.length]);

  const apply=(result:GraphEdit)=>{
    if(result.error){onError(result.error);return;}
    onEdit(result.graph);
  };
  const editTime=(time:number)=>{
    if(!node || !current || !Number.isFinite(time))return;
    // Stop a key from jumping over its neighbors during pointer dragging.
    const keys=curve??gradient??[];
    const left=keyIndex>0?keys[keyIndex-1].t+0.0002:0;
    const right=keyIndex<keys.length-1?keys[keyIndex+1].t-0.0002:1;
    if(left>right)return;
    const next=Math.max(left,Math.min(right,time));
    if(isCurve)apply(editCurveKey(graph,node.id,keyIndex,{t:fmt(next),value:(current as CurveKey).value}));
    else if(isGradient)apply(editGradientKey(graph,node.id,keyIndex,{t:fmt(next),color:(current as GradientKey).color}));
  };
  const editNumber=(value:number)=>{
    if(!node||!curve||!Number.isFinite(value))return;
    apply(editCurveKey(graph,node.id,keyIndex,{t:curve[keyIndex].t,value}));
  };
  const editColor=(hex:string,alpha:number)=>{
    if(!node||!gradient)return;
    const color=rgbaWithAlpha(hex,alpha);
    if(!color){onError('Invalid RGBA color.');return;}
    apply(editGradientKey(graph,node.id,keyIndex,{t:gradient[keyIndex].t,color}));
  };
  const add=()=>{
    if(!node)return;
    apply(isCurve?addCurveKey(graph,node.id):addGradientKey(graph,node.id));
  };
  const remove=()=>{
    if(!node||!current)return;
    apply(isCurve?removeCurveKey(graph,node.id,keyIndex):removeGradientKey(graph,node.id,keyIndex));
    setKeyIndex(i=>Math.max(0,i-1));
  };
  const curvePoints=curve?.map(k=>`${(k.t*400).toFixed(2)},${(110-(Math.max(-100,Math.min(100,k.value))*0.48+50)).toFixed(2)}`).join(' ')??'';
  const selectedGradient=gradient&&current?colorAndAlpha((current as GradientKey).color):null;
  return <StudioSection title="Curves & Gradients">
    {nodes.length===0?<p className="spectra-curve__hint">
      Add a Curve or Gradient value node from the node canvas to edit its keys here.
    </p>:<>
      <div className="spectra-curve__head">
        <StudioField label="Value node" htmlFor="spectra-curve-node">
          <StudioSelect id="spectra-curve-node" value={node?.id??''}
            onChange={e=>{setSelected(e.target.value);setKeyIndex(0);}}>
            {nodes.map(n=><option key={n.id} value={n.id}>{n.id} · {n.type}</option>)}
          </StudioSelect>
        </StudioField>
        <StudioButton size="sm" disabled={disabled||!node||!current} onClick={add}>
          <Plus size={14}/> Add key
        </StudioButton>
        <StudioButton size="sm" variant="danger" disabled={disabled||!node||!current||(curve??gradient)?.length===1}
          onClick={remove}><Trash2 size={14}/> Remove key</StudioButton>
      </div>
      {node && !curve && !gradient?
        <p className="spectra-curve__hint">This node's values are not a valid bounded Curve/Gradient array. Correct its JSON before using the visual editor.</p>:<>
        {curve&&<div className="spectra-curve__plot">
          <svg viewBox="0 0 400 125" preserveAspectRatio="none" role="img" aria-label="Curve editor plot">
            <path d="M0 110 H400 M0 10 V110" stroke="var(--au-line-strong)" fill="none"/>
            <polyline points={curvePoints} stroke="var(--au-accent)" strokeWidth="2" fill="none"/>
            {curve.map((k,i)=><circle key={i} cx={k.t*400}
              cy={110-(Math.max(-100,Math.min(100,k.value))*0.48+50)}
              r={i===keyIndex?6:4} stroke="var(--au-text)" fill={i===keyIndex?'var(--au-accent-secondary)':'var(--au-accent)'}
              onClick={()=>setKeyIndex(i)}/>)}
          </svg>
          <small>Preview scaled to ±100 for readability. Exact values are in the key editor.</small>
        </div>}
        {gradient&&<div className="spectra-curve__gradient">
          <div className="spectra-curve__gradient-bar" style={{background:gradientCss(gradient)}} aria-label="Color gradient preview"/>
          <div className="spectra-curve__gradient-keys">
            {gradient.map((k,i)=><button key={i} type="button" disabled={disabled}
              title={`Time ${fmt(k.t)} · ${k.color}`} aria-label={`Select gradient stop ${i+1}`}
              className={'spectra-curve__swatch'+(i===keyIndex?' is-selected':'')}
              onClick={()=>setKeyIndex(i)}
              style={{background:k.color}}/>)}
          </div>
        </div>}
        {current&&<div className="spectra-curve__controls">
          <StudioField label="Selected key" htmlFor="spectra-key-index">
            <StudioSelect id="spectra-key-index" value={keyIndex}
              onChange={e=>setKeyIndex(Number(e.target.value))}>
              {(curve??gradient??[]).map((k,i)=><option key={i} value={i}>#{i+1} · t={fmt(k.t)}</option>)}
            </StudioSelect>
          </StudioField>
          <StudioField label="Time (0–1)" htmlFor="spectra-key-time">
            <StudioInput id="spectra-key-time" type="number" step="0.01" min={0} max={1}
              disabled={disabled} value={fmt(current.t)}
              onChange={e=>editTime(Number(e.target.value))}/>
          </StudioField>
          <StudioField label="Move key" htmlFor="spectra-key-slider">
            <StudioInput id="spectra-key-slider" type="range" min={0} max={1} step={0.001}
              disabled={disabled} value={current.t}
              onChange={e=>editTime(Number(e.target.value))}/>
          </StudioField>
          {curve&&<StudioField label="Scalar value" htmlFor="spectra-key-value">
            <StudioInput id="spectra-key-value" type="number" step="any"
              disabled={disabled} value={curve[keyIndex]?.value??0}
              onChange={e=>editNumber(Number(e.target.value))}/>
          </StudioField>}
          {selectedGradient&&<>
            <StudioField label="Color" htmlFor="spectra-key-color">
              <StudioInput id="spectra-key-color" type="color" disabled={disabled}
                value={selectedGradient.hex}
                onChange={e=>editColor(e.target.value,selectedGradient.alpha)}/>
            </StudioField>
            <StudioField label="Alpha" htmlFor="spectra-key-alpha">
              <StudioInput id="spectra-key-alpha" type="number" step="0.05" min={0} max={1}
                disabled={disabled} value={fmt(selectedGradient.alpha)}
                onChange={e=>editColor(selectedGradient.hex,Number(e.target.value))}/>
            </StudioField>
          </>}
        </div>}
      </>}
      <p className="spectra-curve__hint">
        These controls edit graph authoring data only. Validate and Publish with the native SpectraFX compiler; this is not a GPU particle preview.
      </p>
    </>}
  </StudioSection>;
}
