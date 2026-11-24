import type {GraphView} from './graphFramework';
type Obj=Record<string,unknown>;
const obj=(x:unknown):x is Obj=>!!x&&typeof x==='object'&&!Array.isArray(x);
const rows=(x:unknown):Obj[]=>Array.isArray(x)?x.filter(obj):[];
// Deterministic dependency layers, with bounded bands for long chains.
export function textureLayout(view:GraphView):Map<string,[number,number]> {
 const ids=new Set(view.nodes.map(n=>n.id)), degree=new Map([...ids].map(id=>[id,0])), rank=new Map([...ids].map(id=>[id,0])), next=new Map([...ids].map(id=>[id,[] as string[]]));
 for(const e of view.links)if(ids.has(e.from)&&ids.has(e.to)){degree.set(e.to,degree.get(e.to)!+1);next.get(e.from)!.push(e.to);}
 const q=[...ids].filter(id=>degree.get(id)===0);
 for(let i=0;i<q.length;i++)for(const dst of next.get(q[i])!){rank.set(dst,Math.max(rank.get(dst)!,rank.get(q[i])!+1));degree.set(dst,degree.get(dst)!-1);if(!degree.get(dst))q.push(dst);}
 // Cyclic/unfinished drafts still have a deterministic, editable layout.
 const groups=new Map<number,string[]>();
 for(const id of ids){const k=rank.get(id)!;const g=groups.get(k)??[];g.push(id);groups.set(k,g);}
 const out=new Map<string,[number,number]>();let col=0;
 for(const [,group] of [...groups].sort((a,b)=>a[0]-b[0])){
  const perColumn=view.nodes.some(n=>n.thumbnail)?3:4;
  const pitch=perColumn===3?174:112,band=perColumn===3?540:480;
  group.forEach((id,i)=>{const c=col+Math.floor(i/perColumn);out.set(id,[24+(c%10)*190,24+Math.floor(c/10)*band+(i%perColumn)*pitch]);});
  col+=Math.ceil(group.length/perColumn);
 }
 return out;
}
export function duplicateTextureNode(graph:Obj,id:string):{graph:Obj;id:string}|null {
 const nodes=rows(graph.nodes),source=nodes.find(n=>n.id===id);
 if(!source||nodes.length>=32||typeof source.op!=='string')return null;
 let i=1;while(nodes.some(n=>n.id===source.op+'_'+i))i++;
 const copyId=source.op+'_'+i;
 const pos=Array.isArray(source.position)?source.position:[30,30];
 const offset=(v:unknown)=>typeof v==='number'&&Number.isFinite(v)?Math.min(2000,Math.max(0,v+32)):62;
 const copy={...JSON.parse(JSON.stringify(source)) as Obj,id:copyId,position:[offset(pos[0]),offset(pos[1])]};
 return {graph:{...graph,nodes:[...nodes,copy]},id:copyId};
}
// Ignore authoring coordinates when deciding whether to recompute the pixels.
export function texturePreviewKey(graph:Obj,overrides:string,output:string,resolution:number,options:readonly boolean[]):string {
 const nodes=rows(graph.nodes).map(n=>{const {position:_,...semantic}=n;return semantic;});
 const {editorGroups:_,...document}=graph;
 return JSON.stringify([{...document,nodes},overrides,output,resolution,options]);
}
