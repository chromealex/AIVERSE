/**
 * Which workspace tabs stay in the title bar. The rest belong in the overflow menu.
 * `available` is the width the bar may use. `widths` are the tab buttons, in order.
 * The active tab stays in the bar when it fits next to the overflow button.
 */
export function fitWorkspaceTabs(widths: number[], available: number, activeIndex: number, moreWidth = 36, gap = 2, chrome = 8): number[] {
  const n = widths.length;
  if (n === 0 || available <= 0) return [];
  const sum = widths.reduce((total, width) => total + width, 0) + gap * (n - 1) + chrome;
  if (sum <= available + 0.5) return widths.map((_, index) => index);
  let budget = available - chrome - moreWidth;
  const keep: number[] = [];
  const take = (index: number) => {
    if (index < 0 || index >= n) return false;
    const need = widths[index] + (keep.length > 0 ? gap : 0);
    if (need > budget) return false;
    keep.push(index);
    budget -= need;
    return true;
  };
  take(activeIndex);
  for (let index = 0; index < n; index++) {
    if (index === activeIndex) continue;
    if (!take(index)) break;
  }
  keep.sort((a, b) => a - b);
  return keep;
}

/** Width the workspace switcher can use: the header side, minus the controls that must stay visible. */
export function workspaceTabBudget(slot: HTMLElement): number {
  const side = slot.parentElement;
  if (!side) return slot.clientWidth;
  const gap = Number.parseFloat(getComputedStyle(side).columnGap) || 0;
  let fixed = 0;
  let count = 0;
  for (const child of side.children) {
    count += 1;
    if (child === slot) continue;
    const element = child as HTMLElement;
    if (getComputedStyle(element).position === 'absolute') continue;
    if (element.classList.contains('header-drag-space')) { fixed += 12; continue; }
    fixed += element.offsetWidth;
  }
  return Math.max(0, side.clientWidth - fixed - gap * Math.max(0, count - 1));
}
