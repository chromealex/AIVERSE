import { useEffect, useRef } from 'react';
import { invoke, nativeConnected } from '../bridge';
import { dockDragActive, forgetViewportRect, visibleViewport,
  viewportRectChanged, watchDockDrag, watchOverlays } from '../nativeOverlay';
import { fxViewportRect } from './spectraViewportModel';
import './SpectraDockViewport.css';

/** The real native gfx::Device's Scene CAMetalLayer/Vulkan surface is
 * REPOSITIONED over this HTML rectangle. It renders an isolated particle
 * stage using StudioScene's existing renderer and GraphRuntime; this DOM
 * contains neither a fake canvas nor a serialized particle bitmap.
 */
export default function SpectraDockViewport(){
  const ref=useRef<HTMLDivElement>(null);
  const native=nativeConnected();
  useEffect(()=>{
    if(!native)return;
    const element=ref.current;
    if(!element)return;
    let disposed=false,frame=0;
    const publish=()=>{
      if(frame)cancelAnimationFrame(frame);
      frame=requestAnimationFrame(()=>{
        frame=0;
        if(disposed)return;
        const viewport=element.getBoundingClientRect();
        const scroller=element.closest('.spectra-di__editor');
        const clipped=fxViewportRect(viewport,
          scroller?.getBoundingClientRect()??viewport,
          window.devicePixelRatio||1,
          document.visibilityState==='visible'&&!dockDragActive());
        const fit=visibleViewport({
          left:clipped.x,top:clipped.y,
          right:clipped.x+clipped.width,bottom:clipped.y+clipped.height
        });
        const rect={...clipped,x:fit.x,y:fit.y,
          width:fit.width,height:fit.height,visible:clipped.visible&&fit.visible};
        if(!viewportRectChanged('fx',rect))return;
        void invoke('studio.viewportRect',{id:'fx',...rect}).catch(error=>{
          forgetViewportRect('fx');
          console.warn('AIVERSE SpectraFX native viewport:',error);
        });
      });
    };
    const ro=new ResizeObserver(publish);
    ro.observe(element);
    const unsubscribeOverlay=watchOverlays(publish);
    const unsubscribeDrag=watchDockDrag(publish);
    window.addEventListener('resize',publish);
    window.addEventListener('scroll',publish,true);
    document.addEventListener('visibilitychange',publish);
    publish();
    return()=>{
      disposed=true;
      if(frame)cancelAnimationFrame(frame);
      ro.disconnect();unsubscribeOverlay();unsubscribeDrag();
      window.removeEventListener('resize',publish);
      window.removeEventListener('scroll',publish,true);
      document.removeEventListener('visibilitychange',publish);
      forgetViewportRect('fx');
      void invoke('studio.viewportRect',{id:'fx',x:0,y:0,
        width:0,height:0,dpr:1,visible:false})
        .catch(()=>{});
      // Scene may be visible in a different dock at the same time. It must
      // re-report its last DOM rect after the FX surface is released.
      window.dispatchEvent(new Event('studio:viewport-reflow'));
    };
  },[native]);
  return <div ref={ref} className="spectra-native__surface"
    role="img" aria-label="Native GPU particle preview">
    {!native&&<div className="spectra-native__surface-hint">
      Native Metal/Vulkan preview is available in AIVERSE Studio, not the browser demo.
    </div>}
  </div>;
}
