import type { StudioProjectHistory, StudioAssetHistory, StudioParams } from '../types';
export type HistoryScope = 'map' | 'project' | 'assets';
export type HistoryDirection = 'undo' | 'redo';

/** Only owner-advertised single-step navigation; never derive a project jump from an ordinal. */
export function historyStepParams(history: StudioProjectHistory | StudioAssetHistory | null, direction: HistoryDirection, scope: 'project' | 'assets' = 'project'): StudioParams['studio.projectHistoryStep'] | null {
  if (!history || history.scope !== scope || !history.version || history.strokeActive || history.blockedReason) return null;
  if (direction === 'undo' ? !history.canUndo : !history.canRedo) return null;
  return { direction, expectedVersion: history.version };
}
