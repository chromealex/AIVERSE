import {useMemo,useState} from 'react';
import {StudioAlert,StudioSection} from '../ui';
import GraphCanvas from './GraphCanvas';
import {buildingGraphOwners,parseBuildingGeometry,projectBuildingGeometry} from './buildingGeometryGraph';
export function BuildingGeometryGraphView({geometry,solids}:{geometry:unknown;solids:number}){
 const [selected,setSelected]=useState('');
 const result=useMemo(()=>{try{const report=parseBuildingGeometry(geometry,solids);return {report,view:projectBuildingGeometry(report),error:''}}catch(e){return {report:null,view:null,error:String(e)}}},[geometry,solids]);
 if(!result.report||!result.view)return <StudioAlert tone="warning">{result.error}</StudioAlert>;
 const owner=buildingGraphOwners.find(o=>o.id===selected);
 return <StudioSection title="Geometry, collision and graph owners">
  <small>{solids} authored solids · {result.report.previewBoxes} preview collision boxes · {result.report.movableParts} movable parts</small>
  <div style={{height:360,display:'flex',flexDirection:'column',minWidth:0}}>
   <GraphCanvas view={result.view} label="Building geometry and independent runtime graph owners" selected={selected} readOnly onSelect={setSelected} onMove={()=>{}}/>
  </div>
  {owner&&<StudioAlert>{owner.detail} Source-level integration inventory; live registration unknown.</StudioAlert>}
  {selected==='boxes'&&<StudioAlert>Generated preview AABBs are not active PHYSIS bodies. Runtime scene ownership and actual contacts are not inspected.</StudioAlert>}
  {selected==='movable'&&<StudioAlert>Door leaves, roof panels and elevator platforms have separate runtime ownership; preview may include initial poses.</StudioAlert>}
  <details><summary>Native solid categories</summary>{Object.entries(result.report.parts).map(([kind,n])=><div key={kind}>{kind}: {n}</div>)}</details>
  <StudioAlert>Top lane: native generation report. Lower lane: architecture of committed runtime invalidation, not a preview execution trace. No runtime registration, graph rebuild, path or physical traversal is certified.</StudioAlert>
 </StudioSection>;
}
