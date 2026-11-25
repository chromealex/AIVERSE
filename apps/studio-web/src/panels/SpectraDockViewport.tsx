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
export default function SpectraDockViewport({id='fx',label='Native GPU particle preview'}:{
  id?:'fx'|'shader'|'material';label?:string
}={}){
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
        const scroller=element.closest('.spectra-di__editor,.graph-inspector,.materials-editor');
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
        if(!viewportRectChanged(id,rect))return;
        void invoke('studio.viewportRect',{id,...rect}).catch(error=>{
          forgetViewportRect(id);
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
      forgetViewportRect(id);
      void invoke('studio.viewportRect',{id,x:0,y:0,
        width:0,height:0,dpr:1,visible:false})
        .catch(()=>{});
      // Scene uses its own independent native surface and remains untouched
      // when this FX panel closes or moves to another window.
    };
  },[native,id]);
  return <div ref={ref} className={id==='fx'?'spectra-native__surface':'asset-native-surface'}
    role="img" aria-label={label}>
    {!native&&<div className="spectra-native__surface-hint">
      Native Metal/Vulkan preview is available in AIVERSE Studio, not the browser demo.
    </div>}
  </div>;
}
