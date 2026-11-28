import { safeAssetPath } from './assetTree';

// A detached panel has its own document. Keep the request until its owner mounts.
export function revealProjectAsset(project: string, path: string) {
  if (!project || !safeAssetPath(path)) return;
  localStorage.setItem('studio.assetToReveal:' + project, path);
  window.dispatchEvent(new CustomEvent('studio:reveal-asset', { detail: { project, path } }));
}
