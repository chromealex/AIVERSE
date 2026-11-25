// Data-driven descriptions for the editor's HTML5 Lighting Inspector.
import type { FieldDescriptor } from '../propertyLogic';
export type LightingScalarKey = 'time' | 'dayLength' | 'nightLevel' | 'clouds' | 'storm';
export interface LightEntry {
  entityId?: string;
  pattern?: string; rate?: number; flicker?: number;
  angle?: number; pitch?: number; yaw?: number; sweep?: number; sweepSpeed?: number;
  x?: number; z?: number; height?: number; radius?: number; intensity?: number;
  name?: string; type?: string; nightOnly?: boolean; electric?: boolean; bake?: string;
}
export interface LightingState {
  lighting: Partial<Record<LightingScalarKey, number>> | null;
  lights: LightEntry[];
}
export const LIGHTING_KEYS: readonly LightingScalarKey[] = ['time', 'dayLength', 'nightLevel', 'clouds', 'storm'];
export const LIGHT_EDITABLE_KEYS = ['name','type','x','z','height','radius','intensity','flicker','pattern','rate','angle','pitch','yaw','sweep','sweepSpeed','nightOnly','electric','bake'] as const;
export type LightEditableKey = typeof LIGHT_EDITABLE_KEYS[number];

export function lightingProperties(state: LightingState): FieldDescriptor[] {
  const values = state.lighting ?? {};
  return [
    { key: 'time', label: 'Time of day', type: 'number', value: values.time ?? 0.5, min: 0, max: 1, step: .01,
      description: '0 midnight · 0.25 sunrise · 0.5 noon · 0.75 sunset' },
    { key: 'dayLength', label: 'Day length (s)', type: 'number', value: values.dayLength ?? 1200, min: 0, max: 100000000, step: 1 },
    { key: 'nightLevel', label: 'Night light', type: 'number', value: values.nightLevel ?? .18, min: 0, max: 1, step: .01 },
    { key: 'clouds', label: 'Clouds', type: 'number', value: values.clouds ?? 0, min: 0, max: 1, step: .01 },
    { key: 'storm', label: 'Storm', type: 'number', value: values.storm ?? 0, min: 0, max: 1, step: .01 }
  ];
}
export function lightProperties(light: LightEntry): FieldDescriptor[] {
  const spot = light.type === 'spot' || light.angle !== undefined;
  const pattern = light.pattern ?? 'steady';
  const fields: FieldDescriptor[] = [
    {key:'name',label:'Name',type:'text',value:light.name ?? ''},
    {key:'type',label:'Type',type:'text',value:light.type ?? (spot ? 'spot' : 'point'),options:[{label:'Point',value:'point'},{label:'Spot',value:'spot'}]},
    {key:'x',label:'Position X',type:'number',value:light.x ?? 0,min:-1000000,max:1000000,step:.1},
    {key:'z',label:'Position Z',type:'number',value:light.z ?? 0,min:-1000000,max:1000000,step:.1},
    {key:'height',label:'Height',type:'number',value:light.height ?? (spot ? 1.5 : 1),min:-1000,max:10000,step:.1},
    {key:'radius',label:'Radius',type:'number',value:light.radius ?? (spot ? 9 : 4),min:.001,max:100000,step:.1},
    {key:'intensity',label:'Intensity',type:'number',value:light.intensity ?? (spot ? 2.2 : 1.5),min:0,max:10000,step:.1},
    {key:'flicker',label:'Flicker',type:'number',value:light.flicker ?? 0,min:0,max:1,step:.01},
    {key:'pattern',label:'Pattern',type:'text',value:pattern,options:[
      {label:'Steady',value:'steady'},{label:'Pulse',value:'pulse'},
      {label:'Strobe',value:'strobe'},{label:'Beacon',value:'beacon'}]}
  ];
  if (pattern !== 'steady') fields.push(
    {key:'rate',label:'Frequency (Hz)',type:'number',value:light.rate ?? (pattern==='strobe' ? 1.5 : pattern==='beacon' ? .5 : .6),min:.01,max:30,step:.01});
  if (spot) {
    fields.push(
      {key:'angle',label:'Cone angle (°)',type:'number',value:light.angle ?? 40,min:2,max:170,step:.5},
      {key:'pitch',label:'Downward tilt (°)',type:'number',value:light.pitch ?? 35,min:-10,max:90,step:.5},
      {key:'yaw',label:'Direction (°)',type:'number',value:light.yaw ?? 0,min:-360,max:360,step:1},
      {key:'sweep',label:'Sweep amplitude (°)',type:'number',value:light.sweep ?? 0,min:0,max:180,step:.5});
    if ((light.sweep ?? 0)>0) fields.push(
      {key:'sweepSpeed',label:'Sweep speed (°/s)',type:'number',value:light.sweepSpeed ?? 25,min:1,max:180,step:.5});
  }
  fields.push(
    {key:'nightOnly',label:'Night only',type:'boolean',value:light.nightOnly ?? false},
    {key:'electric',label:'Electric',type:'boolean',value:light.electric ?? spot},
    {key:'bake',label:'Bake mode',type:'text',value:light.bake ?? 'realtime',options:[
      {label:'Realtime',value:'realtime'},{label:'Mixed',value:'mixed'},{label:'Baked',value:'baked'}]});
  return fields;
}
