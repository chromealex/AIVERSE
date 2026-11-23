/** Compare decoded pixels from two genuine native JPEG captures.
 * This is an 8-bit sRGB visual diagnostic, not HDR/alpha or GPU parity.
 */
export interface SpectraPixelDiff {
  width:number; height:number; changed:number; total:number;
  changedPercent:number; meanAbsoluteError:number; heatmap:string;
}
const decode=(uri:string)=>new Promise<HTMLImageElement>((resolve,reject)=>{
  if(!uri.startsWith('data:image/jpeg;base64,')||uri.length>900000) {
    reject(new Error('Expected bounded native JPEG capture'));return;
  }
  const image=new Image();
  image.onload=()=>resolve(image);
  image.onerror=()=>reject(new Error('Unable to decode native capture'));
  image.src=uri;
});
const pixels=(image:HTMLImageElement,width:number,height:number)=>{
  const canvas=document.createElement('canvas');
  canvas.width=width;canvas.height=height;
  const ctx=canvas.getContext('2d',{willReadFrequently:true});
  if(!ctx)throw new Error('2D canvas unavailable');
  ctx.drawImage(image,0,0);
  return ctx.getImageData(0,0,width,height);
};
export async function compareSpectraCaptures(a:string,b:string,threshold:number):Promise<SpectraPixelDiff>{
  if(!Number.isFinite(threshold)||threshold<0||threshold>255)throw new Error('Invalid threshold');
  const [ia,ib]=await Promise.all([decode(a),decode(b)]);
  if(!ia.naturalWidth||!ia.naturalHeight||ia.naturalWidth!==ib.naturalWidth||
     ia.naturalHeight!==ib.naturalHeight)throw new Error('Capture dimensions differ; use the same snapshot size');
  const width=ia.naturalWidth,height=ia.naturalHeight;
  if(width>768||height>768||width*height>768*768)throw new Error('Capture exceeds comparison limit');
  const pa=pixels(ia,width,height).data,pb=pixels(ib,width,height).data;
  const canvas=document.createElement('canvas');canvas.width=width;canvas.height=height;
  const ctx=canvas.getContext('2d');
  if(!ctx)throw new Error('2D canvas unavailable');
  const output=ctx.createImageData(width,height);
  let changed=0,error=0;
  for(let i=0;i<pa.length;i+=4){
    const r=Math.abs(pa[i]-pb[i]),g=Math.abs(pa[i+1]-pb[i+1]),bl=Math.abs(pa[i+2]-pb[i+2]);
    const magnitude=Math.max(r,g,bl);
    if(magnitude>threshold)changed++;
    error+=r+g+bl;
    output.data[i]=magnitude;
    output.data[i+1]=Math.max(0,150-magnitude);
    output.data[i+2]=0;
    output.data[i+3]=255;
  }
  ctx.putImageData(output,0,0);
  const total=width*height;
  return {width,height,changed,total,changedPercent:100*changed/total,
    meanAbsoluteError:error/(total*3),heatmap:canvas.toDataURL('image/png')};
}
