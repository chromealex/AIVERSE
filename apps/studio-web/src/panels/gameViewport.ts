// Fit logical device points into the available panel without distorting input.
export function gameViewportSize(width:number,height:number,deviceWidth:number,deviceHeight:number) {
  if (![width,height,deviceWidth,deviceHeight].every(Number.isFinite) || width<=0 || height<=0)
    return {width:0,height:0};
  if (deviceWidth<=0 || deviceHeight<=0)return {width,height};
  const scale=Math.min(width/deviceWidth,height/deviceHeight);
  return {width:deviceWidth*scale,height:deviceHeight*scale};
}
