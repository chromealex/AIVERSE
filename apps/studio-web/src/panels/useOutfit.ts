// Outfit recipe shared by the Clothing and Hair tabs, with live try-on: every
// change re-assembles the preview (256 px textures, nothing written) while on.
import { useEffect, useState } from 'react';
import { invoke } from '../bridge';
import type { HumanWardrobe } from '../types';

export const emptyOutfit = (): HumanWardrobe => ({ version: 1, hiddenMeshes: [], items: [] });
export function useOutfit(guid: string) {
  const [recipe, setRecipe] = useState<HumanWardrobe>(emptyOutfit);
  const [tryOn, setTryOn] = useState(true), [trying, setTrying] = useState(false), [error, setError] = useState('');
  useEffect(() => { setRecipe(emptyOutfit()); }, [guid]);
  useEffect(() => {
    if (!guid || !recipe.items.length) return;
    const timer = setTimeout(() => {
      setTrying(true); setError('');
      void invoke('studio.humanTryOn', { wardrobe: tryOn ? recipe : null })
        .catch(e => setError(String(e).replace(/^Error: /, ''))).finally(() => setTrying(false));
    }, 350);
    return () => clearTimeout(timer);
  }, [recipe, tryOn, guid]);
  useEffect(() => () => { void invoke('studio.humanTryOn', { wardrobe: null }).catch(() => {}); }, []);
  return { recipe, setRecipe, tryOn, setTryOn, trying, error };
}
export type Outfit = ReturnType<typeof useOutfit>;
