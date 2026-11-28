export const modes = ['dark', 'light'] as const;
export const accents = ['blue', 'violet', 'teal', 'orange'] as const;
export type Appearance = {mode: typeof modes[number]; accent: typeof accents[number]};
const preferenceKey = 'aiverse.studio.appearance';

export function readAppearance(): Appearance {
  const fallback: Appearance = {mode:'dark', accent:'blue'};
  try {
    const stored = JSON.parse(localStorage.getItem(preferenceKey) ?? '{}');
    return {mode:modes.find(value => value === stored?.mode) ?? fallback.mode,
      accent:accents.find(value => value === stored?.accent) ?? fallback.accent};
  } catch { return fallback; }
}
export function applyAppearance(theme: Appearance) {
  document.documentElement.dataset.theme = theme.mode;
  document.documentElement.dataset.accent = theme.accent;
}
export function saveAppearance(theme: Appearance) {
  applyAppearance(theme);
  localStorage.setItem(preferenceKey, JSON.stringify(theme));
}
export function observeAppearance(update: (value: Appearance) => void) {
  const changed = (event: StorageEvent) => {
    if (event.key === preferenceKey || event.key === null) update(readAppearance());
  };
  window.addEventListener('storage', changed);
  return () => window.removeEventListener('storage', changed);
}
