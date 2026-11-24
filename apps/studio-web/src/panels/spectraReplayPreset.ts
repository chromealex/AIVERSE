import type { StudioSpectraCameraSnapshot } from '../types';

export interface SpectraReplayPreset {
  format:'aiverse.spectrafx.replay';version:1;
  graphSignature:string;projectPath:string;
  camera:StudioSpectraCameraSnapshot;tick:number;speed:number;loop:boolean;
  background:'dark'|'neutral'|'light'|'black';grid:boolean;axes:boolean;
  parameters:Record<string,number>;
  dependencyVersion?:1|2;
  dependencies?:Array<{path:string;content:string;guid?:string;meta?:string}>;
  gpuBirthRandom?:boolean;
  seed?:number; // CPU backend only
  gpuShaderSeed?:number;
  gpuAllocationSeed?:number; // Metal slot ordering only
}
const record=(v:unknown):v is Record<string,unknown>=>!!v&&typeof v==='object'&&!Array.isArray(v);
const bounded=(v:unknown,min:number,max:number):v is number=>
  typeof v==='number'&&Number.isFinite(v)&&v>=min&&v<=max;
export function parseSpectraReplayPreset(text:string):SpectraReplayPreset {
  if(new TextEncoder().encode(text).byteLength>65536)throw new Error('Replay preset exceeds 64 KiB');
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
  if(v.gpuBirthRandom!==undefined&&typeof v.gpuBirthRandom!=='boolean')throw new Error('Invalid GPU random mode');
  if(v.seed!==undefined && (!bounded(v.seed,0,2147483647)||!Number.isInteger(v.seed)))
    throw new Error('Invalid CPU replay seed');
  if(v.gpuAllocationSeed!==undefined && (!bounded(v.gpuAllocationSeed,0,2147483647)||!Number.isInteger(v.gpuAllocationSeed)))
    throw new Error('Invalid GPU allocation seed');
  if(v.gpuShaderSeed!==undefined&&(!bounded(v.gpuShaderSeed,0,4294967295)||!Number.isInteger(v.gpuShaderSeed)))throw new Error('Invalid GPU shader seed');
  if(v.gpuShaderSeed!==undefined&&(v.seed!==undefined||v.gpuAllocationSeed!==undefined))throw new Error('GPU shader seed mode is independent');
  if(v.seed!==undefined&&v.gpuAllocationSeed!==undefined)
    throw new Error('CPU and GPU allocation seed modes are mutually exclusive');
  if(!record(v.parameters)||Object.keys(v.parameters).length>8)
    throw new Error('Invalid replay parameter table');
  for(const [key,value] of Object.entries(v.parameters)){
    if(!/^[A-Za-z_][A-Za-z0-9_]{0,47}$/.test(key)||!bounded(value,-1000000,1000000))
      throw new Error('Invalid replay scalar parameter');
  }
  if(v.dependencyVersion!==undefined&&v.dependencyVersion!==1&&v.dependencyVersion!==2)
    throw new Error('Unsupported replay dependency manifest version');
  if(v.dependencies!==undefined&&(!Array.isArray(v.dependencies)||v.dependencies.length>64||
    v.dependencies.some(d=>!record(d)||typeof d.path!=='string'||!d.path.startsWith('content/')||
      typeof d.content!=='string'||!/^\d+:\d+$/.test(d.content)||
      (d.guid!==undefined&&(typeof d.guid!=='string'||!(/^[a-f0-9]{32}$/).test(d.guid)))||
      (d.meta!==undefined&&(typeof d.meta!=='string'||!(/^\d+:\d+$/).test(d.meta))))))
    throw new Error('Invalid replay dependency manifest');
  return v as unknown as SpectraReplayPreset;
}
