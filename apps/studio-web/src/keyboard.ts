/**
 * Layout-independent shortcut key: letters and digits come from the physical
 * key (event.code), so W/A/S/D, F, ⌘S, ⌘Z… work on Russian or any other layout.
 * Other keys (arrows, Home, Delete, Escape…) use the lowercase event.key.
 */
export function shortcutKey(event: { code?: string; key: string }): string {
  const code = event.code ?? '';
  if (/^Key[A-Z]$/.test(code)) return code.slice(3).toLowerCase();
  if (/^Digit[0-9]$/.test(code)) return code.slice(5);
  return event.key.toLowerCase();
}

/** Display the same primary modifier used by the desktop shortcut handlers. */
export function menuShortcut(key: string, shift = false, platform = globalThis.navigator?.platform ?? ''): string {
  return /Mac|iPhone|iPad|iPod/i.test(platform)
    ? (shift ? '⇧⌘' : '⌘') + key
    : (shift ? 'Ctrl+Shift+' : 'Ctrl+') + key;
}
