/** Pure geometry for placing the one native Scene surface inside a
 * scrollable SpectraFX Graph HTML5 panel. CSS rects in browser pixels;
 * never sends display bitmaps or untrusted GPU handles over the bridge.
 */
export interface FxBox {left:number;top:number;right:number;bottom:number}
export interface FxRect {
  x:number;y:number;width:number;height:number;dpr:number;visible:boolean
}
const valid=(v:number)=>Number.isFinite(v);
export function fxViewportRect(
  target:FxBox,scrollClip:FxBox,dpr:number,active:boolean
):FxRect {
  if(![target.left,target.top,target.right,target.bottom,
    scrollClip.left,scrollClip.top,scrollClip.right,scrollClip.bottom,
    dpr].every(valid))return {x:0,y:0,width:0,height:0,dpr:1,visible:false};
  const left=Math.max(target.left,scrollClip.left,0);
  const top=Math.max(target.top,scrollClip.top,0);
  const right=Math.min(target.right,scrollClip.right);
  const bottom=Math.min(target.bottom,scrollClip.bottom);
  const width=Math.max(0,right-left),height=Math.max(0,bottom-top);
  const original=Math.max(0,target.right-target.left)*
    Math.max(0,target.bottom-target.top);
  return {x:left,y:top,width,height,dpr:Math.min(4,Math.max(1,dpr)),
    // Hide the native view before its framebuffer can cover tools when
    // scrolled out of the dock; never paint over the inspector controls.
    visible:active && width>=120 && height>=100 &&
      original>0 && width*height>=original*0.6};
}
