import type { StudioInputAsset, StudioInputTrigger } from '../types';

export function changeInputAction(asset: StudioInputAsset, index: number, name: string | null): StudioInputAsset {
  const old = asset.actions[index].name;
  if (name === old || (name !== null && (!name.trim() || asset.actions.some(action => action.name === name)))) return asset;
  const updateTriggers = (items?: StudioInputTrigger[]) => items?.flatMap(trigger => {
    if ((trigger.type !== 'chord' && trigger.type !== 'notChord') || trigger.action !== old) return [trigger];
    return name === null ? [] : [{ ...trigger, action: name }];
  });
  return {
    ...asset,
    actions: asset.actions.flatMap((action, i) => i === index && name === null ? [] : [{
      ...action, name: i === index ? name ?? old : action.name, triggers: updateTriggers(action.triggers),
    }]),
    contexts: asset.contexts.map(context => ({
      ...context,
      mappings: context.mappings.flatMap(mapping => mapping.action === old && name === null ? [] : [{
        ...mapping, action: mapping.action === old ? name ?? old : mapping.action, triggers: updateTriggers(mapping.triggers),
      }]),
    })),
  };
}
