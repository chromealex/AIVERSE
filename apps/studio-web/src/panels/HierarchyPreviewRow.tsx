import { ChevronDown, ChevronRight, Eye } from 'lucide-react';
import type { StudioEntity } from '../types';

interface Props {
  node:StudioEntity;
  depth:number;
  selected?:boolean;
  childCount?:number;
  collapsed:boolean;
  toggle:()=>void;
}

export function HierarchyPreviewRow({node,depth,selected,childCount,collapsed,toggle}:Props){
  const details=node.preview!;
  return <div className="hier-preview" data-selected={selected || undefined} style={{paddingInlineStart:6+depth*12}} data-preview-id={node.id} data-nested-leaf={childCount===0 || undefined}
    onKeyDown={event=>{
      // A focused preview must not dispatch map deletion or duplication shortcuts.
      if(['Delete','Backspace'].includes(event.key) || ((event.metaKey||event.ctrlKey) && event.key.toLowerCase()==='d')){
        event.preventDefault();event.stopPropagation();
      }
      if(['Enter',' ','ArrowUp','ArrowDown'].includes(event.key))event.stopPropagation();
    }}>
    {!!childCount && <button type="button" className="hier__disclosure" aria-expanded={!collapsed}
      aria-label={(collapsed?'Expand ':'Collapse ')+node.name} onClick={toggle}>
      {collapsed?<ChevronRight size={13}/>:<ChevronDown size={13}/>}
    </button>}
    <details open={selected || undefined}>
      <summary><Eye size={12} aria-hidden="true"/><span>{node.name}</span><small>Preview</small></summary>
      <dl>
        <dt>Type</dt><dd>{node.kind}</dd>
        <dt>Owner</dt><dd>{node.owner}</dd>
        {details.health!==undefined && <><dt>Health</dt><dd>{details.health}</dd></>}
        <dt>Components</dt><dd>{details.components.join(', ') || 'None'}</dd>
      </dl>
    </details>
  </div>;
}
