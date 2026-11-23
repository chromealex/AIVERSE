import type {GraphView,NodeView,Link} from './graphFramework';

export interface StrataAnalysis {
 eligiblePrimitives:number; clusterNodes:number; meshlets:number; auxiliaryIndexBytes:number;
 seamCertifiedPrimitives:number; seamRejectedParents:number; sharedBoundarySegments:number;
 sourceSeamsCertified:boolean; skipped: {mesh:number;primitive:number;reason:string}[]; note:string;
}
const object=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
const count=(v:unknown):number=>{
 if(typeof v!=='number'||!Number.isSafeInteger(v)||v<0)throw new Error('Invalid STRATA count');
 return v;
};
export function parseStrataAnalysis(value:unknown):StrataAnalysis {
 if(!object(value)||value.ok!==true||typeof value.sourceSeamsCertified!=='boolean'||!Array.isArray(value.skipped)||typeof value.note!=='string')throw new Error('Invalid native STRATA report');
 return {eligiblePrimitives:count(value.eligiblePrimitives),clusterNodes:count(value.clusterNodes),
  meshlets:count(value.meshlets),auxiliaryIndexBytes:count(value.auxiliaryIndexBytes),
  seamCertifiedPrimitives:count(value.seamCertifiedPrimitives),seamRejectedParents:count(value.seamRejectedParents),
  sharedBoundarySegments:count(value.sharedBoundarySegments),sourceSeamsCertified:value.sourceSeamsCertified,
  skipped:value.skipped.map(s=>{if(!object(s)||typeof s.reason!=='string')throw new Error('Invalid skipped primitive');return {mesh:count(s.mesh),primitive:count(s.primitive),reason:s.reason};}),note:value.note};
}
export function strataReportGraph(report:StrataAnalysis):GraphView {
 const make=(id:string,label:string,x:number,y:number,input:boolean,output:boolean):NodeView=>({id,label,x,y,inputs:input?[{id:'in',type:'analysis'}]:[],outputs:output?[{id:'out',type:'analysis',multiple:true}]:[]});
 const nodes=[make('import',report.eligiblePrimitives+' eligible primitives',30,100,false,true),
 make('clusters',report.clusterNodes+' cluster nodes',250,100,true,true),
 make('meshlets',report.meshlets+' meshlets',470,30,true,false),
 make('indices',report.auxiliaryIndexBytes+' CPU bytes',470,130,true,false),
 make('seams',report.sourceSeamsCertified?'Source seams certified':'Source seams not certified',470,230,true,false),
 make('skipped',report.skipped.length+' skipped primitives',250,230,true,false)];
 const pairs=[['import','clusters'],['import','skipped'],['clusters','meshlets'],['clusters','indices'],['clusters','seams']];
 const links:Link[]=pairs.map(([from,to])=>({id:from+'>'+to,from,to,output:'out',input:'in'}));
 return {nodes,links};
}
