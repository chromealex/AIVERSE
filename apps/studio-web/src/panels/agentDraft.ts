export interface AgentDraft { text: string; brief: boolean }
export const emptyDraft: AgentDraft = { text: '', brief: false };

export function readAgentDraft(key: string): AgentDraft {
  try {
    const raw = localStorage.getItem(key);
    if (raw) {
      const draft = JSON.parse(raw);
      if (typeof draft?.text === 'string' && draft.text)
        return { text: draft.text, brief: draft.brief === true };
    }
  } catch { /* An unreadable draft still allows the composer to open. */ }
  return emptyDraft;
}
