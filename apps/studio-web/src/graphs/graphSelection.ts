export interface Point {x:number;y:number}
export interface Rect extends Point {width:number;height:number}
export interface Positioned extends Point {id:string}
export function selectionRect(a:Point,b:Point):Rect {
 return {x:Math.min(a.x,b.x),y:Math.min(a.y,b.y),width:Math.abs(a.x-b.x),height:Math.abs(a.y-b.y)};
}
export function selectInRect(nodes:readonly (Positioned & {width:number;height:number})[],r:Rect):string[] {
 return nodes.filter(n=>n.x<=r.x+r.width&&n.x+n.width>=r.x&&n.y<=r.y+r.height&&n.y+n.height>=r.y).map(n=>n.id);
}
export function selectNode(ids:readonly string[],id:string,extend:boolean):string[] {
 if(!extend)return ids.includes(id)?[...ids]:[id];
 return ids.includes(id)?ids.filter(x=>x!==id):[...ids,id];
}
/** Rigid group movement; clamp once rather than distorting relative positions. */
export function moveGroup(nodes:readonly Positioned[],dx:number,dy:number,max=2000):Positioned[] {
 if(!nodes.length||![dx,dy,max].every(Number.isFinite)||max<0)return [];
 const x=Math.max(-Math.min(...nodes.map(n=>n.x)),Math.min(max-Math.max(...nodes.map(n=>n.x)),dx));
 const y=Math.max(-Math.min(...nodes.map(n=>n.y)),Math.min(max-Math.max(...nodes.map(n=>n.y)),dy));
 return nodes.map(n=>({id:n.id,x:n.x+x,y:n.y+y}));
}
