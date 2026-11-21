// Explicit offline repair for smooth facial surfaces with malformed exported
// morph normals. Preserve positions, indices, skinning, UVs and base normals.
function repairMorphNormals(doc, mesh, selectMaterial, targetNames) {
 const groups=new Map(), entries=[];
 for(const p of mesh.listPrimitives()) {
  if(!selectMaterial(p.getMaterial()))continue;
  const pos=p.getAttribute('POSITION'), normal=p.getAttribute('NORMAL');
  if(!pos||!normal)throw Error('Morph normal repair requires positions and normals');
  const a=pos.getArray(), n=normal.getArray(), ids=[];
  for(let i=0;i<pos.getCount();i++) {
   // Weld only coincident smooth vertices; retain authored hard-normal seams.
   const key=[...a.slice(i*3,i*3+3),...n.slice(i*3,i*3+3)].map(v=>Math.round(v*1e5)).join(',');
   if(!groups.has(key))groups.set(key,groups.size);ids.push(groups.get(key));
  }
  entries.push({p,a,n,ids});
 }
 const unit=v=>{const l=Math.hypot(...v);return l>1e-12?v.map(x=>x/l):[0,0,0];};
 const sums=target=> {
  const result=Array.from({length:groups.size},()=>[0,0,0]);
  for(const {p,a,ids} of entries) {
   const d=target<0?null:p.listTargets()[target]?.getAttribute('POSITION')?.getArray();
   const ix=p.getIndices()?.getArray()||Array.from({length:ids.length},(_,i)=>i);
   for(let k=0;k+2<ix.length;k+=3) {
    const points=[ix[k],ix[k+1],ix[k+2]].map(i=>[0,1,2].map(c=>a[i*3+c]+(d?.[i*3+c]||0)));
    const u=points[1].map((x,c)=>x-points[0][c]),v=points[2].map((x,c)=>x-points[0][c]);
    const cross=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];
    for(let j=0;j<3;j++)for(let c=0;c<3;c++)result[ids[ix[k+j]]][c]+=cross[c];
   }
  }
  return result.map(unit);
 };
 const base=sums(-1), names=mesh.getExtras().targetNames||[];let repaired=0;
 for(let t=0;t<names.length;t++) {
  if(!targetNames.includes(names[t]))continue;
  const deformed=sums(t);
  for(const {p,n,ids} of entries) {
   const target=p.listTargets()[t];if(!target)continue;
   const delta=new Float32Array(n.length);
   for(let i=0;i<ids.length;i++) {
    // Apply geometry-derived normal change to authored smooth normal.
    const a=base[ids[i]],b=deformed[ids[i]],old=[n[i*3],n[i*3+1],n[i*3+2]];
    const next=unit(old.map((x,c)=>x+b[c]-a[c]));
    for(let c=0;c<3;c++)delta[i*3+c]=next[c]-old[c];
   }
   const accessor=doc.createAccessor().setType('VEC3').setArray(delta).setBuffer(p.getAttribute('POSITION').getBuffer());
   target.setAttribute('NORMAL',accessor);repaired++;
  }
 }
 return {repaired,vertices:groups.size};
}
module.exports={repairMorphNormals};
