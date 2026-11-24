import type { StudioProjectHistory, StudioParams } from '../types';
export type HistoryScope = 'map' | 'project';
export type HistoryDirection = 'undo' | 'redo';

/** Only owner-advertised single-step navigation; never derive a project jump from an ordinal. */
export function historyStepParams(history: StudioProjectHistory | null, direction: HistoryDirection): StudioParams['studio.projectHistoryStep'] | null {
  if (!history || history.scope !== 'project' || !history.version || history.strokeActive || history.blockedReason) return null;
  if (direction === 'undo' ? !history.canUndo : !history.canRedo) return null;
  return { direction, expectedVersion: history.version };
}
