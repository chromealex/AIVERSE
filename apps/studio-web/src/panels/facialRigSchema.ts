// MotionWeave Facial Control Rig v1 — Studio authoring DTO.
// Same shape as the genre-neutral runtime's FacialControlRig JSON; this
// browser validation does NOT prove channels exist in an imported Model.
export type FacialBlend = 'override' | 'additive';
export interface FacialMorphBinding {
  parameter: string;
  node: string;
  target: string;
  gain?: number;
  bias?: number;
  halfLife?: number;
  blend?: FacialBlend;
}
export interface FacialIkUnit {
  op: 'aim' | 'angularLimit' | 'limit' | 'transform' | 'copy'
      | 'twoBoneIK' | 'fullBodyIK' | 'surfaceLocomotion';
  bone: string;
  target?: string;
  pole?: string;
  knee?: string;
  end?: string;
  source?: string;
  axis?: number[];
  min?: number[];
  max?: number[];
  weight?: number;
  weightParameter?: string;
  [key: string]: unknown;
}
export interface FacialRigDocument {
  version: 1;
  morphs: FacialMorphBinding[];
  rig?: FacialIkUnit[];
}
export interface FacialRigList { rigs: string[] }
export interface FacialRigResult { name: string; rig: FacialRigDocument }
export interface FacialRigValidation {
  valid: boolean;
  morphs: number;
  ikUnits: number;
  runtimeVerified: false;
}
export const ARKIT52 = [
  'browDownLeft','browDownRight','browInnerUp','browOuterUpLeft','browOuterUpRight',
  'cheekPuff','cheekSquintLeft','cheekSquintRight',
  'eyeBlinkLeft','eyeBlinkRight','eyeLookDownLeft','eyeLookDownRight',
  'eyeLookInLeft','eyeLookInRight','eyeLookOutLeft','eyeLookOutRight',
  'eyeLookUpLeft','eyeLookUpRight','eyeSquintLeft','eyeSquintRight',
  'eyeWideLeft','eyeWideRight',
  'jawForward','jawLeft','jawOpen','jawRight',
  'mouthClose','mouthDimpleLeft','mouthDimpleRight','mouthFrownLeft','mouthFrownRight',
  'mouthFunnel','mouthLeft','mouthLowerDownLeft','mouthLowerDownRight',
  'mouthPressLeft','mouthPressRight','mouthPucker','mouthRight',
  'mouthRollLower','mouthRollUpper','mouthShrugLower','mouthShrugUpper',
  'mouthSmileLeft','mouthSmileRight','mouthStretchLeft','mouthStretchRight',
  'mouthUpperUpLeft','mouthUpperUpRight',
  'noseSneerLeft','noseSneerRight','tongueOut'
] as const;
export function newFacialRig(): FacialRigDocument { return {version:1,morphs:[],rig:[]}; }
export function arkitFaceMapping(faceNode='Face'): FacialRigDocument {
  return {version:1,morphs:ARKIT52.map((name):FacialMorphBinding=>({
    parameter:name,node:faceNode,target:name,gain:1,bias:0,halfLife:0.06,blend:'override'
  })),rig:[]};
}
const validName=(x:string)=>x.length>0 && x.length<=128 && !/[\x00-\x1f\x7f]/.test(x);
const bounded=(n:number|undefined,min:number,max:number)=>n===undefined ||
  (typeof n==='number' && Number.isFinite(n) && n>=min && n<=max);
export const validRigFileName=(name:string)=>/^[A-Za-z0-9_-]{1,64}$/.test(name);
export function validateFacialRigDraft(rig: FacialRigDocument): string[] {
  const errors:string[]=[];
  if(rig.version!==1 || !Array.isArray(rig.morphs) || rig.morphs.length>256)
    return ['Invalid facial rig version or morph array'];
  if(rig.rig!==undefined && (!Array.isArray(rig.rig) || rig.rig.length>64))
    return ['Invalid facial IK array'];
  if(!rig.morphs.length && !(rig.rig?.length))errors.push('Add a morph mapping or IK unit');
  const seen=new Set<string>();
  rig.morphs.forEach((m,i)=>{
    if(!validName(m.parameter)||!validName(m.node)||!validName(m.target))
      errors.push('Morph '+(i+1)+': parameter, node and target are required');
    const id=JSON.stringify([m.node,m.target]);
    if(seen.has(id))errors.push('Morph '+(i+1)+': duplicate node/target destination');
    seen.add(id);
    if(m.blend!==undefined && m.blend!=='override' && m.blend!=='additive')
      errors.push('Morph '+(i+1)+': invalid blend mode');
    if(!bounded(m.gain,0,4)||!bounded(m.bias,-1,1)||!bounded(m.halfLife,0,10))
      errors.push('Morph '+(i+1)+': invalid numeric control');
  });
  rig.rig?.forEach((ik,i)=>{
    if(!validName(ik.bone) || !validName(ik.op))errors.push('IK '+(i+1)+': missing bone/op');
    if((ik.op==='aim'||ik.op==='twoBoneIK') && !validName(ik.target??''))
      errors.push('IK '+(i+1)+': missing target parameter');
    if(ik.op==='twoBoneIK' && (![ik.pole,ik.knee,ik.end].every(v=>validName(v??''))))
      errors.push('IK '+(i+1)+': missing pole/knee/end');
    if(ik.op==='copy' && !validName(ik.source??''))
      errors.push('IK '+(i+1)+': missing source bone');
    if(!bounded(ik.weight,0,1))errors.push('IK '+(i+1)+': invalid weight');
    for(const vector of ['axis','min','max'] as const) {
      const v=ik[vector];
      if(v!==undefined && (!Array.isArray(v)||v.length!==3||
          v.some(n=>typeof n!=='number'||!Number.isFinite(n)||Math.abs(n)>1e6)))
        errors.push('IK '+(i+1)+': invalid '+vector+' vector');
    }
  });
  if(JSON.stringify(rig).length>128*1024)errors.push('Facial rig exceeds 128 KiB');
  return errors;
}
