import { WORKSPACES, WORKSPACE_INFO, type StudioWorkspace } from '../workspaces';

/** Workspace switcher (⌘1…⌘6) in the title bar. */
export default function WorkspaceTabs({ value, onChange }: { value: StudioWorkspace; onChange: (id: StudioWorkspace) => void }) {
  return <div className="workspace-tabs" role="tablist" aria-label="Workspaces">
    {WORKSPACES.map(id => <button key={id} type="button" role="tab" aria-selected={value === id}
      className={'workspace-tabs__tab' + (value === id ? ' is-active' : '')}
      title={WORKSPACE_INFO[id].purpose + ' (⌘' + WORKSPACE_INFO[id].key + ')'}
      onClick={() => onChange(id)}>{WORKSPACE_INFO[id].title}</button>)}
  </div>;
}
