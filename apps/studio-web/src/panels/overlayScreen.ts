type Paint = { fill?:number; line?:number; thickness:number };
type Shape =
  | {shape:'text'; text:string; size:number; color:number; shadow?:number; background?:number; padX:number; padY:number}
  | {shape:'bar'; w:number; h:number; fraction:number; border:number; background?:number; color:number}
  | ({shape:'circle'; radius:number; segments:number} & Paint)
  | ({shape:'rect'; w:number; h:number} & Paint);

export type ScreenOverlay = Shape & {hud:boolean; x:number; z:number; dx:number; dy:number; rounding:number};

function color(packed:number) {
  return `rgba(${packed&255},${(packed>>>8)&255},${(packed>>>16)&255},${(packed>>>24)/255})`;
}

export function drawScreenOverlays(ctx:CanvasRenderingContext2D, items:ScreenOverlay[], width:number, height:number,
  bounds:{scale:number;x:number;z:number}) {
  for (const item of items) {
    const x = item.dx + (item.hud ? item.x + (item.x<0 ? width : 0) : bounds.x + item.x*bounds.scale);
    const y = item.dy + (item.hud ? item.z + (item.z<0 ? height : 0) : bounds.z + item.z*bounds.scale);
    if (item.shape==='text') {
      const size = 13*item.size;
      ctx.font = `${size}px system-ui`;
      ctx.textBaseline = 'top';
      const lines = item.text.split('\n');
      const w = lines.reduce((max,line)=>Math.max(max,ctx.measureText(line).width),0);
      const h = lines.length*size;
      const left = item.hud ? x+(item.x<0 ? -w-item.padX : item.padX) : x-w/2;
      const top = item.hud ? y+(item.z<0 ? -h-item.padY : item.padY) : y-h/2;
      if (item.background!==undefined) {
        ctx.beginPath();
        ctx.roundRect(left-item.padX,top-item.padY,w+2*item.padX,h+2*item.padY,Math.max(0,item.rounding));
        ctx.fillStyle=color(item.background);
        ctx.fill();
      }
      lines.forEach((line,i)=>{
        if (item.shadow!==undefined) {
          ctx.fillStyle=color(item.shadow);
          ctx.fillText(line,left+1,top+i*size+1);
        }
        ctx.fillStyle=color(item.color);
        ctx.fillText(line,left,top+i*size);
      });
    } else if (item.shape==='bar') {
      const left=x-item.w/2;
      if (item.background!==undefined) {
        ctx.fillStyle=color(item.background);
        ctx.fillRect(left-item.border,y-item.border,item.w+2*item.border,item.h+2*item.border);
      }
      ctx.fillStyle=color(item.color);
      ctx.fillRect(left,y,item.w*item.fraction,item.h);
    } else {
      ctx.beginPath();
      if (item.shape==='rect') {
        ctx.roundRect(x-item.w/2,y-item.h/2,item.w,item.h,Math.max(0,item.rounding));
      } else if (item.segments>=3) {
        for (let i=0;i<item.segments;i++) {
          const angle=2*Math.PI*i/item.segments;
          const px=x+item.radius*Math.cos(angle), py=y+item.radius*Math.sin(angle);
          if (i===0) ctx.moveTo(px,py);
          else ctx.lineTo(px,py);
        }
        ctx.closePath();
      } else {
        ctx.arc(x,y,item.radius,0,2*Math.PI);
      }
      if (item.fill!==undefined) {
        ctx.fillStyle=color(item.fill);
        ctx.fill();
      }
      if (item.line!==undefined) {
        ctx.strokeStyle=color(item.line);
        ctx.lineWidth=item.thickness;
        ctx.stroke();
      }
    }
  }
}
