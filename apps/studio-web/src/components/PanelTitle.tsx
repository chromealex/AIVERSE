import type { ReactNode } from 'react';

export default function PanelTitle({ title, right }: { title: string; right?: ReactNode }) {
  return <div className="panel-heading au-toolbar">
    <span>{title}</span>
    {right && <div className="panel-actions">{right}</div>}
  </div>;
}
