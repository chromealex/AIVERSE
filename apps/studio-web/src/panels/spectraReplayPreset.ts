import type { StudioSpectraCameraSnapshot } from '../types';

export interface SpectraReplayPreset {
  format:'aiverse.spectrafx.replay';version:1;
  graphSignature:string;projectPath:string;
  camera:StudioSpectraCameraSnapshot;tick:number;speed:number;loop:boolean;
  background:'dark'|'neutral'|'light'|'black';grid:boolean;axes:boolean;
  parameters:Record<string,number>;
  seed?:number; // CPU backend only
}
const record=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
const bounded=(v:unknown,min:number,max:number):v is number=>
  typeof v==='number'&&Number.isFinite(v)&&v>=min&&v<=max;
export function parseSpectraReplayPreset(text:string):SpectraReplayPreset {
  if(text.length>65536)throw new Error('Replay preset exceeds 64 KiB');
  const v:unknown=JSON.parse(text);
  if(!record(v)||v.format!=='aiverse.spectrafx.replay'||v.version!==1)
    throw new Error('Unsupported replay preset format');
  if(typeof v.projectPath!=='string'||v.projectPath.length>4096||
     typeof v.graphSignature!=='string'||v.graphSignature.length>65536)
    throw new Error('Invalid replay preset identity');
  if(!bounded(v.tick,0,3600)||!Number.isInteger(v.tick)||
     ![0.25,0.5,1,2,4].includes(v.speed as number)||
     typeof v.loop!=='boolean'||typeof v.grid!=='boolean'||typeof v.axes!=='boolean'||
     !['dark','neutral','light','black'].includes(v.background as string))
    throw new Error('Invalid replay playback or stage settings');
  const c=v.camera;
  if(!record(c)||!bounded(c.yaw,-1000,1000)||!bounded(c.pitch,-1.56,1.56)||
     !bounded(c.distance,0.01,100000)||!bounded(c.targetX,-1000000,1000000)||
     !bounded(c.targetY,-1000000,1000000)||!bounded(c.targetZ,-1000000,1000000)||
     typeof c.ortho!=='boolean')throw new Error('Invalid replay camera');
  if(v.seed!==undefined && (!bounded(v.seed,0,2147483647)||!Number.isInteger(v.seed)))
    throw new Error('Invalid CPU replay seed');
  if(!record(v.parameters)||Object.keys(v.parameters).length>8)
    throw new Error('Invalid replay parameter table');
  for(const [key,value] of Object.entries(v.parameters)){
    if(!/^[A-Za-z_][A-Za-z0-9_]{0,47}$/.test(key)||!bounded(value,-1000000,1000000))
      throw new Error('Invalid replay scalar parameter');
  }
  return v as unknown as SpectraReplayPreset;
}
