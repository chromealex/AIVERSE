// Data-driven descriptions for the editor's HTML5 Lighting Inspector.
import type { FieldDescriptor } from '../propertyLogic';
export type LightingScalarKey = 'time' | 'dayLength' | 'nightLevel' | 'clouds' | 'storm';
export interface LightEntry {
  entityId?: string;
  x?: number; z?: number; height?: number; radius?: number; intensity?: number;
  name?: string; type?: string; nightOnly?: boolean; electric?: boolean; bake?: string;
}
export interface LightingState {
  lighting: Partial<Record<LightingScalarKey, number>> | null;
  lights: LightEntry[];
}
export const LIGHTING_KEYS: readonly LightingScalarKey[] = ['time', 'dayLength', 'nightLevel', 'clouds', 'storm'];
export const LIGHT_EDITABLE_KEYS = ['name','type','x','z','height','radius','intensity','nightOnly','electric','bake'] as const;
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
  return [
    {key:'name',label:'Name',type:'text',value:light.name ?? ''},
    {key:'type',label:'Type',type:'text',value:light.type ?? 'point',options:[{label:'Point',value:'point'},{label:'Spot',value:'spot'}]},
    {key:'x',label:'Position X',type:'number',value:light.x ?? 0,min:-1000000,max:1000000,step:.1},
    {key:'z',label:'Position Z',type:'number',value:light.z ?? 0,min:-1000000,max:1000000,step:.1},
    {key:'height',label:'Height',type:'number',value:light.height ?? 1,min:-1000,max:10000,step:.1},
    {key:'radius',label:'Radius',type:'number',value:light.radius ?? 4,min:.001,max:100000,step:.1},
    {key:'intensity',label:'Intensity',type:'number',value:light.intensity ?? 1.5,min:0,max:10000,step:.1},
    {key:'nightOnly',label:'Night only',type:'boolean',value:light.nightOnly ?? false},
    {key:'electric',label:'Electric',type:'boolean',value:light.electric ?? false},
    {key:'bake',label:'Bake mode',type:'text',value:light.bake ?? 'realtime',options:[
      {label:'Realtime',value:'realtime'},{label:'Mixed',value:'mixed'},{label:'Baked',value:'baked'}]}
  ];
}
