import { useEffect, useRef } from 'react';
import { invoke, nativeConnected } from '../bridge';
import { dockDragActive, forgetViewportRect, visibleViewport,
  viewportRectChanged, watchDockDrag, watchOverlays } from '../nativeOverlay';
import { fxViewportRect } from './spectraViewportModel';
import './SpectraDockViewport.css';

/** A dedicated native FX CAMetalLayer/Vulkan swapchain covers this HTML
 * rectangle. The authored Scene has its own independent native drawable,
 * camera and StudioScene. Both are serialized on the render-owner thread.
 * No fake browser pixels, WebGL duplicate or bitmap transport.
 */
export default function SpectraDockViewport({id='fx',label='Native GPU particle preview',onRect}:{
  id?:'sourceModel'|'fx'|'shader'|'material'|'facialRig'|'character'|'appearance'|'animationGraph'|'animationRig'|'attachments'|'lensCamera'|'lighting'|'effectFile'|'gpuEmitter';label?:string;onRect?:(rect:{x:number;y:number;width:number;height:number})=>void
}={}){
  const ref=useRef<HTMLDivElement>(null);
  const native=nativeConnected();
  useEffect(()=>{
    if(!native)return;
    const element=ref.current;
    if(!element)return;
    // These surfaces are presented by their own native host even when WebKit throttles the document.
    const nativeVisibility=id==='gpuEmitter'||id==='attachments'||id==='sourceModel';
    let disposed=false,frame=0;
    const measure=()=>{
      frame=0;
      if(disposed)return;
      const viewport=element.getBoundingClientRect();
      const scroller=element.closest('.spectra-di__editor,.tool__scroll,.graph-inspector,.attachment-body,.lens-layout,.effects-files,.lighting-scroll,.materials-editor,.anim-editor,.au-panel__body');
      const clipped=fxViewportRect(viewport,
        scroller?.getBoundingClientRect()??viewport,
        window.devicePixelRatio||1,
        (nativeVisibility||document.visibilityState==='visible')&&!dockDragActive());
      const fit=visibleViewport({
        left:clipped.x,top:clipped.y,
        right:clipped.x+clipped.width,bottom:clipped.y+clipped.height
      });
      const rect={...clipped,x:fit.x,y:fit.y,
        width:fit.width,height:fit.height,visible:clipped.visible&&fit.visible};
      onRect?.(rect);
      if(!viewportRectChanged(id,rect))return;
      void invoke('studio.viewportRect',{id,...rect}).catch(error=>{
        forgetViewportRect(id);
        console.warn('AIVERSE SpectraFX native viewport:',error);
      });
    };
    const publish=()=>{
      // A floating native window can draw while its WebView throttles animation frames.
      // AppKit and Win32 check the native surface and host window before each draw.
      if(nativeVisibility && document.hidden){
        if(frame)cancelAnimationFrame(frame);
        measure();
      }else if(!frame)frame=requestAnimationFrame(measure);
    };
    const ro=new ResizeObserver(publish);
    ro.observe(element);
    const unsubscribeOverlay=watchOverlays(publish,nativeVisibility);
    const unsubscribeDrag=watchDockDrag(publish);
    window.addEventListener('resize',publish);
    window.addEventListener('scroll',publish,true);
    document.addEventListener('visibilitychange',publish);
    measure();
    return()=>{
      disposed=true;
      if(frame)cancelAnimationFrame(frame);
      ro.disconnect();unsubscribeOverlay();unsubscribeDrag();
      window.removeEventListener('resize',publish);
      window.removeEventListener('scroll',publish,true);
      document.removeEventListener('visibilitychange',publish);
      forgetViewportRect(id);
      void invoke('studio.viewportRect',{id,x:0,y:0,
        width:0,height:0,dpr:1,visible:false})
        .catch(()=>{});
      // Scene uses its own independent native surface and remains untouched
      // when this FX panel closes or moves to another window.
    };
  },[native,id,onRect]);
  return <div ref={ref} className={id==='fx'?'spectra-native__surface':'asset-native-surface'}
    role="img" aria-label={label}>
    {!native&&<div className="spectra-native__surface-hint">
      Native Metal/Vulkan preview is available in AIVERSE Studio, not the browser demo.
    </div>}
  </div>;
}
