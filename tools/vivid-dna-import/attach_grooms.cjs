// Offline bind-preserving attachment for imported rigid/morphed groom cards.
// Existing skins must use the skeletal attachment path instead of reparenting
// just their mesh node. No animation tracks, meshes or morph data are duplicated.
function attachRigidGroom(doc,node,head) {
 if(node===head||node.getSkin())throw Error('Rigid groom attachment requires an unskinned child mesh');
 const up=[];for(let n=head;n;n=n.getParentNode()){if(n===node)throw Error('Attachment cycle');up.push(n);}
 for(let n=node.getParentNode();n;n=n.getParentNode())if(n===head)return {node:node.getName(),head:head.getName(),alreadyAttached:true};
 const old=[];for(let n=node.getParentNode();n;n=n.getParentNode())old.push(n);
 const common=up.find(n=>old.includes(n))||null;
 const inverse=common?up.slice(0,up.indexOf(common)):up;
 const forward=(common?old.slice(0,old.indexOf(common)):old).reverse();
 for(const n of [...inverse,...forward]) {
  if(![...n.getTranslation(),...n.getRotation(),...n.getScale()].every(Number.isFinite))throw Error('Nonfinite attachment frame');
  if(inverse.includes(n)&&n.getScale().some(v=>Math.abs(v)<1e-8))throw Error('Singular attachment frame');
 }
 let parent=head,serial=0;
 const add=(suffix,t,r,s)=>{const n=doc.createNode('groom-binding/'+node.getName()+'/'+serial+++'-'+suffix);if(t)n.setTranslation(t);if(r)n.setRotation(r);if(s)n.setScale(s);parent.addChild(n);parent=n;};
 // Inverse(T R S) = inverse(S) inverse(R) inverse(T), kept as separate
 // nodes so rotated nonuniform scales never get decomposed with shear loss.
 for(const n of inverse){const t=n.getTranslation(),q=n.getRotation();add('scale',null,null,n.getScale().map(v=>1/v));add('rotation',null,[-q[0],-q[1],-q[2],q[3]]);add('translation',t.map(v=>-v));}
 for(const n of forward)add('source-parent',n.getTranslation(),n.getRotation(),n.getScale());
 parent.addChild(node); // glTF Transform removes the previous parent atomically.
 return {node:node.getName(),head:head.getName(),compensationNodes:serial};
}
module.exports={attachRigidGroom};
